#include "tenyear.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
#include "yjcm2013.h"
#include "clientplayer.h"
//#include "clientstruct.h"
#include "engine.h"
#include "maneuvering.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "ol.h"
#include "wind.h"
#include "mobile.h"
#include <QMutexLocker>

class Choutao : public TriggerSkill
{
public:
	Choutao(): TriggerSkill("choutao")
	{
		events << TargetSpecified << TargetConfirmed;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;
		if(event==TargetConfirmed){
			if(!use.to.contains(player))
				return false;
		}
		if(!use.from || !player->canDiscard(use.from,"he"))return false;
		if(!player->askForSkillInvoke(this,use.from))return false;
		player->peiyin(this);

		int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodNone);
		room->throwCard(id,objectName(),use.from,player);

		foreach(ServerPlayer*p,use.to)
			use.no_offset_list << p->objectName();
		if(use.from==player)
			use.m_addHistory = false;
		data = QVariant::fromValue(use);
		return false;
	}
};

class Xiangshu : public PhaseChangeSkill
{
public:
	Xiangshu(): PhaseChangeSkill("xiangshu")
	{
		frequency = Limited;
		limit_mark = "@xiangshuMark";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish || player->getMark("@xiangshuMark")<= 0)return false;
		int mark = player->getMark("damage_point_round");
		if(mark <= 0)return false;
		QList<ServerPlayer*> players;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isWounded())
				players << p;
		}
		if(players.isEmpty())return false;
		mark = qMin(mark,5);
		ServerPlayer*t = room->askForPlayerChosen(player,players,objectName(),"@xiangshu-target:" + QString::number(mark),true,true);
		if(!t)return false;
		player->peiyin(this);
		room->doSuperLightbox(player,"xiangshu");
		room->removePlayerMark(player,"@xiangshuMark");
		room->recover(t,RecoverStruct(player,nullptr,qMin(mark,t->getMaxHp()- t->getHp()),"xiangshu"));
		t->drawCards(mark,objectName());
		return false;
	}
};

class TenyearZhanyi : public PhaseChangeSkill
{
public:
	TenyearZhanyi(): PhaseChangeSkill("tenyearzhanyi")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;

		QStringList choices;
		foreach(const Card*c,player->getCards("he")){
			int id = c->getEffectiveId();
			if(c->isKindOf("BasicCard")&& player->canDiscard(player,id)&& !choices.contains("basic"))
				choices << "basic";
			else if(c->isKindOf("TrickCard")&& player->canDiscard(player,id)&& !choices.contains("trick"))
				choices << "trick";
			else if(c->isKindOf("EquipCard")&& player->canDiscard(player,id)&& !choices.contains("equip"))
				choices << "equip";
		}
		if(choices.isEmpty())return false;
		choices << "cancel";

		QString choice = room->askForChoice(player,objectName(),choices.join("+"));
		if(choice=="cancel")return false;

		QStringList choice_list,pattern_list;
		choice_list << "basic" << "trick" << "equip";
		pattern_list << "BasicCard" << "TrickCard" << "EquipCard";

		QString pattern = pattern_list.at(choice_list.indexOf(choice));

		foreach(QString cho,choice_list){
			if(cho==choice)continue;
			room->addPlayerMark(player,"tenyearzhanyi_" + cho + "-Clear");
		}

		DummyCard*dummy = new DummyCard();
		foreach(const Card*c,player->getCards("he")){
			int id = c->getEffectiveId();
			if(c->isKindOf(pattern.toStdString().c_str())&& player->canDiscard(player,id))
				dummy->addSubcard(id);
		}
		if(dummy->subcardsLength()> 0){
			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());

			room->throwCard(dummy,objectName(),player);
		}
		dummy->deleteLater();
		return false;
	}
};

class TenyearZhanyiEffect : public TriggerSkill
{
public:
	TenyearZhanyiEffect(): TriggerSkill("#tenyearzhanyi")
	{
		events << ConfirmDamage << PreHpRecover << CardUsed << EventPhaseProceeding;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		int basic = player->getMark("tenyearzhanyi_basic-Clear");
		int trick = player->getMark("tenyearzhanyi_trick-Clear");
		int equip = player->getMark("tenyearzhanyi_equip-Clear");

		if(event==CardUsed){
			if(player->isDead())return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")&& trick > 0){
				room->sendCompulsoryTriggerLog(player,"tenyearzhanyi",true,true);
				player->drawCards(trick,"tenyearzhanyi");
			} else if(use.card->isKindOf("EquipCard")&& equip > 0){
				for(int i = 0; i < equip; i++){
					if(player->isDead())return false;
					QList<ServerPlayer*> targets;
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						if(player->canDiscard(p,"he"))
							targets << p;
					}
					if(targets.isEmpty())return false;
					ServerPlayer*t = room->askForPlayerChosen(player,targets,"tenyearzhanyi","@tenyearzhanyi-discard",true,true);
					if(!t)break;
					player->peiyin("tenyearzhanyi");
					int id = room->askForCardChosen(player,t,"he","tenyearzhanyi",false,Card::MethodDiscard);
					room->throwCard(id,"tenyearzhanyi",t,player);
				}
			}
		} else if(event==EventPhaseProceeding){
			if(player->isDead()|| player->getPhase()!= Player::Discard || trick <= 0)return false;
			foreach(const Card*c,player->getCards("h")){
				if(c->isKindOf("TrickCard"))
					room->ignoreCards(player,c);
			}
		} else if(event==ConfirmDamage){
			if(basic <= 0)return false;
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("BasicCard"))return false;
			damage.damage += basic;
			LogMessage log;
			log.type = "#NewlonghunDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = "tenyearzhanyi";
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			player->peiyin("tenyearzhanyi");
			room->notifySkillInvoked(player,"tenyearzhanyi");
			data = QVariant::fromValue(damage);
		} else if(event==PreHpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(!recover.card || !recover.card->isKindOf("BasicCard")|| !recover.who)return false;
			basic = recover.who->getMark("tenyearzhanyi_basic-Clear");
			if(basic <= 0)return false;
			int old = recover.recover;
			recover.recover += basic;
			int now = qMin(recover.recover,player->getMaxHp()- player->getHp());
			if(now <= 0)
				return true;
			if(now > old){
				LogMessage log;
				log.type = "#NewlonghunRecover";
				log.from = recover.who;
				log.to << player;
				log.arg = "tenyearzhanyi";
				log.arg2 = QString::number(now);
				room->sendLog(log);
				recover.who->peiyin("tenyearzhanyi");
				room->notifySkillInvoked(recover.who,"tenyearzhanyi");
			}
			recover.recover = now;
			data = QVariant::fromValue(recover);
		}
		return false;
	}
};

class TenyearZhanyiTarget : public TargetModSkill
{
public:
	TenyearZhanyiTarget(): TargetModSkill("#tenyearzhanyi-mod")
	{
		pattern = "BasicCard";
	}

	int getDistanceLimit(const Player*from,const Card*,const Player*)const
	{
		if(from->getMark("tenyearzhanyi_basic-Clear")> 0)
			return 999;
		return 0;
	}
};

ChanniCard::ChanniCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void ChanniCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->giveCard(from,to,this,"channi");
	if(to->isDead()||(to->isKongcheng()&& to->getHandPile().isEmpty()))return;

	int length = subcardsLength();
	room->setPlayerMark(to,"channi_mark-Clear",length);
	room->setPlayerProperty(to,"ChanniSkillFrom",from->objectName());
	room->askForUseCard(to,"@@channi","@channi:" + QString::number(length));
}

class ChanniVS : public ViewAsSkill
{
public:
	ChanniVS(): ViewAsSkill("channi")
	{
		response_or_use = true;
		response_pattern = "@@channi";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUsePattern()== "@@channi"){
			if(to_select->isEquipped()|| selected.length()>= Self->getMark("channi_mark-Clear"))return false;
			Duel*duel = new Duel(Card::SuitToBeDecided,-1);
			duel->setSkillName("_channi");
			duel->addSubcards(selected);
			duel->addSubcard(to_select);
			return !Self->isLocked(duel);
		}
		return Self->getHandcards().contains(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		if(Sanguosha->getCurrentCardUsePattern()== "@@channi"){
			Duel*duel = new Duel(Card::SuitToBeDecided,-1);
			duel->setSkillName("_channi");
			duel->addSubcards(cards);
			return duel;
		}

		ChanniCard*c = new ChanniCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("ChanniCard");
	}
};

class Channi : public TriggerSkill
{
public:
	Channi(): TriggerSkill("channi")
	{
		events  << Damage << Damaged << PreCardUsed;
		view_as_skill = new ChanniVS;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card || !use.card->isKindOf("Duel")|| !use.card->getSkillNames().contains(objectName()))return false;
			QString name = player->property("ChanniSkillFrom").toString();
			if(name.isEmpty())return false;
			room->setPlayerProperty(player,"ChanniSkillFrom","");
			room->setCardFlag(use.card,"channi_use_from_" + player->objectName());
			room->setCardFlag(use.card,"channi_from_" + name);
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Duel")|| !damage.card->getSkillNames().contains(objectName()))return false;
			if(!damage.card->hasFlag("channi_use_from_" + player->objectName()))return false;

			if(event==Damage){
				if(player->isDead())return false;
				int num = damage.card->subcardsLength();
				player->drawCards(num,objectName());
			} else {
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->isDead()|| !damage.card->hasFlag("channi_from_" + p->objectName()))continue;
					p->throwAllHandCards();
				}
			}
		}
		return false;
	}
};

class Nifu : public TriggerSkill
{
public:
	Nifu(): TriggerSkill("nifu")
	{
		events  << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			int num = p->getHandcardNum()- 3;
			if(num < 0){
				room->sendCompulsoryTriggerLog(p,this,2);
				p->drawCards(-num,objectName());
			} else if(num > 0&& p->canDiscard(p,"h")){
				room->sendCompulsoryTriggerLog(p,this,1);
				room->askForDiscard(p,objectName(),num,num);
			}
		}
		return false;
	}
};

XiongmangCard::XiongmangCard()
{
	mute = true;
}

bool XiongmangCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	const Card*slash = Card::Parse(Self->property("xiongmang").toString());
	return slash&& !to_select->hasFlag("xiongmang_target")&& targets.length()< slash->subcardsLength()- 1
		&& Self->canSlash(to_select,slash);
}

void XiongmangCard::onUse(Room*room,CardUseStruct&use)const
{
	foreach(ServerPlayer*p,use.to)
		room->setPlayerFlag(p,"xiongmang_add_target");
}

class XiongmangVS : public ViewAsSkill
{
public:
	XiongmangVS(): ViewAsSkill("xiongmang")
	{
		response_or_use = true;
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUsePattern()== "@@xiongmang")return false;
		if(to_select->isEquipped())return false;
		Slash*slash = new Slash(Card::SuitToBeDecided,-1);
		slash->setSkillName(objectName());
		slash->deleteLater();
		foreach(const Card*c,selected){
			if(c->getSuit()== to_select->getSuit())return false;
			slash->addSubcard(c);
		}
		slash->addSubcard(to_select);
		return !Self->isLocked(to_select,true);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			if(cards.isEmpty())return nullptr;
			Card*slash = new Slash(Card::SuitToBeDecided,-1);
			slash->setSkillName(objectName());
			slash->setFlags(objectName());
			slash->addSubcards(cards);
			return slash;
		} else if(Sanguosha->getCurrentCardUsePattern()== "@@xiongmang"){
			if(!cards.isEmpty())return nullptr;
			return new XiongmangCard;
		}

		return nullptr;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("slash")|| pattern.contains("Slash")|| pattern=="@@xiongmang";
	}
};

class Xiongmang : public TriggerSkill
{
public:
	Xiongmang(): TriggerSkill("xiongmang")
	{
		events << PreChangeSlash << CardFinished;
		view_as_skill = new XiongmangVS;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;
		if(!use.card->getSkillNames().contains(objectName())&& !use.card->hasFlag(objectName()))return false;
		if(event==CardFinished){
			if(use.card->hasFlag("DamageDone"))return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isAlive()&& use.card->hasFlag("xiongmang_" + p->objectName())&& p->getMaxHp()> 1){
					room->sendCompulsoryTriggerLog(p,"xiongmang",true,true);
					room->loseMaxHp(p,1,"xiongmang");
				}
			}
		}else{
			room->setCardFlag(use.card,"xiongmang_" + player->objectName());
			int n = use.card->subcardsLength()- 1;
			if(n <= 0)return false;
			foreach(ServerPlayer*p,use.to)
				room->setPlayerFlag(p,"xiongmang_target");
			room->setPlayerProperty(player,"xiongmang",use.card->toString());
			const Card*c = room->askForUseCard(player,"@@xiongmang","@xiongmang:" + QString::number(n),-1,Card::MethodNone);
			foreach(ServerPlayer*p,use.to)
				room->setPlayerFlag(p,"-xiongmang_target");
			if(!c)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->hasFlag("xiongmang_add_target")){
					room->setPlayerFlag(p,"-xiongmang_add_target");
					use.to << p;
				}
			}
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

TenyearHuoshuiCard::TenyearHuoshuiCard()
{
}

bool TenyearHuoshuiCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*Self)const
{
	int lose = qMax(1,Self->getLostHp());
	return targets.length()< lose;
}

void TenyearHuoshuiCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	effect.to->addMark("tenyearhuoshui");
	room->addPlayerMark(effect.to,"@skill_invalidity");

	foreach(ServerPlayer*p,room->getAllPlayers())
		room->filterCards(p,p->getCards("he"),true);
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,args);
}

class TenyearHuoshuiVS : public ZeroCardViewAsSkill
{
public:
	TenyearHuoshuiVS(): ZeroCardViewAsSkill("tenyearhuoshui")
	{
		response_pattern = "@@tenyearhuoshui";
	}

	const Card*viewAs()const
	{
		return new TenyearHuoshuiCard;
	}
};

class TenyearHuoshui : public PhaseChangeSkill
{
public:
	TenyearHuoshui(): PhaseChangeSkill("tenyearhuoshui")
	{
		view_as_skill = new TenyearHuoshuiVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		int lose = qMax(1,player->getLostHp());
		room->askForUseCard(player,"@@tenyearhuoshui","@tenyearhuoshui:" + QString::number(lose));
		return false;
	}
};

class TenyearHuoshuiClear : public TriggerSkill
{
public:
	TenyearHuoshuiClear(): TriggerSkill("#tenyearhuoshui-clear")
	{
		events << EventPhaseChanging << Death;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*target,QVariant&data)const
	{
		if(triggerEvent==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to != Player::NotActive)
				return false;
		} else if(triggerEvent==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != target || target != room->getCurrent())
				return false;
		}
		foreach(ServerPlayer*player,room->getAllPlayers(true)){
			int n = player->getMark("tenyearhuoshui");
			if(n<1)continue;
			player->setMark("tenyearhuoshui",0);
			room->removePlayerMark(player,"@skill_invalidity",n);

			foreach(ServerPlayer*p,room->getAllPlayers())
				room->filterCards(p,p->getCards("he"),false);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,args);
		}
		return false;
	}
};

TenyearQingchengCard::TenyearQingchengCard()
{
}

bool TenyearQingchengCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select != Self&& to_select->isMale()&& to_select->getHandcardNum()<= Self->getHandcardNum();
}

void TenyearQingchengCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	ServerPlayer*a = effect.from;
	ServerPlayer*b = effect.to;

	int n1 = a->getHandcardNum();
	int n2 = b->getHandcardNum();

		QList<CardsMoveStruct> exchangeMove;
		CardsMoveStruct move1(a->handCards(),b,Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_SWAP,a->objectName(),b->objectName(),"tenyearqingcheng",""));
		CardsMoveStruct move2(b->handCards(),a,Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_SWAP,b->objectName(),a->objectName(),"tenyearqingcheng",""));
		exchangeMove.push_back(move1);
		exchangeMove.push_back(move2);
		room->moveCardsAtomic(exchangeMove,false);

		LogMessage log;
		log.type = "#Dimeng";
		log.from = a;
		log.to << b;
		log.arg = QString::number(n1);
		log.arg2 = QString::number(n2);
		room->sendLog(log);
		room->getThread()->delay();
}

class TenyearQingcheng : public ZeroCardViewAsSkill
{
public:
	TenyearQingcheng(): ZeroCardViewAsSkill("tenyearqingcheng")
	{
	}

	const Card*viewAs()const
	{
		return new TenyearQingchengCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearQingchengCard");
	}
};

class TenyearDaoji : public TriggerSkill
{
public:
	TenyearDaoji(): TriggerSkill("tenyeardaoji")
	{
		events << CardUsed;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Weapon"))return false;
		player->addMark("tenyeardaoji-Keep");
		if(player->getMark("tenyeardaoji-Keep")!= 1)return false;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| !p->hasSkill(objectName())|| !p->askForSkillInvoke(this,player))continue;
			room->broadcastSkillInvoke(this);
			QStringList choices;
			if(room->CardInTable(use.card))
				choices << "obtain=" + use.card->objectName();
			choices << "limit=" + player->objectName();
			if(room->askForChoice(p,objectName(),choices.join("+"),data).startsWith("obtain"))
				room->obtainCard(p,use.card);
			else {
				if(player->isDead())continue;
				room->addPlayerMark(player,"tenyeardaoji_limit-Clear");
			}
		}
		return false;
	}
};

class TenyearDaojiLimit : public CardLimitSkill
{
public:
	TenyearDaojiLimit(): CardLimitSkill("#tenyeardaoji-limit")
	{
	}

	QString limitList(const Player*)const
	{
		return "use,response";
	}

	QString limitPattern(const Player*target)const
	{
		if(target->getMark("tenyeardaoji_limit-Clear")> 0)
			return "Slash";
		return "";
	}
};

class Fuzhong : public TriggerSkill
{
public:
	Fuzhong(): TriggerSkill("fuzhong")
	{
		events << CardsMoveOneTime << DrawNCards << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			if(room->getTag("FirstRound").toBool())return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to != player || player->hasFlag("CurrentPlayer")|| move.to_place != Player::PlaceHand)return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&fzzhong");
		} else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("&fzzhong")< 1)return false;
			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = "fuzhong";
			room->sendLog(log);
			room->broadcastSkillInvoke("fuzhong");
			room->notifySkillInvoked(player,"fuzhong");
			draw.num++;
			data = QVariant::fromValue(draw);
		} else {
			if(player->getPhase()!=Player::Finish||player->getMark("&fzzhong")< 4)return false;
			ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@fuzhong-damage",false,true);
			room->broadcastSkillInvoke(this);
			room->damage(DamageStruct("fuzhong",player,to));
			player->loseMark("&fzzhong",4);
		}
		return false;
	}
};

class FuzhongMax : public MaxCardsSkill
{
public:
	FuzhongMax(): MaxCardsSkill("#fuzhong-max")
	{
	}

	int getExtra(const Player*target)const
	{
		if(target->getMark("&fzzhong")>=3&&target->hasSkill("fuzhong"))
			return 3;
		return 0;
	}
};

class FuzhongDistance : public DistanceSkill
{
public:
	FuzhongDistance(): DistanceSkill("#fuzhong-distance")
	{
	}

	int getCorrect(const Player*from,const Player*)const
	{
		if(from->getMark("&fzzhong")>=2&&from->hasSkill("fuzhong"))
			return -2;
		return 0;
	}
};

XuezhaoCard::XuezhaoCard(const QString&xuezhao): xuezhao(xuezhao)
{
}

bool XuezhaoCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(xuezhao=="xuezhao")
		return targets.length()< Self->getHp()&& to_select != Self;
	else if(xuezhao=="secondxuezhao")
		return targets.length()< Self->getMaxHp()&& to_select != Self;
	return false;
}

void XuezhaoCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	QVariant data = QVariant::fromValue(source);
	foreach(ServerPlayer*p,targets){
		if(source->isDead())return;
		if(p->isDead())continue;
		const Card*card = room->askForCard(p,"..","@" + xuezhao + "-give:" + source->objectName(),data,Card::MethodNone);
		if(card){
			room->giveCard(p,source,card,xuezhao);
			p->drawCards(1,xuezhao);
			if(source->isAlive())
				room->addSlashCishu(source,1);
		} else
			room->addPlayerMark(p,xuezhao + "_no_respond" + source->objectName()+ "-Clear");
	}
}

SecondXuezhaoCard::SecondXuezhaoCard(): XuezhaoCard("secondxuezhao")
{
}

class XuezhaoVS : public OneCardViewAsSkill
{
public:
	XuezhaoVS(const QString&xuezhao): OneCardViewAsSkill(xuezhao),xuezhao(xuezhao)
	{
		filter_pattern = ".|.|.|hand!";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		QString classname = "XuezhaoCard";
		if(xuezhao=="secondxuezhao")
			classname = "SecondXuezhaoCard";
		return !player->hasUsed(classname);
	}

	const Card*viewAs(const Card*originalcard)const
	{
		if(xuezhao=="xuezhao"){
			XuezhaoCard*card = new XuezhaoCard;
			card->addSubcard(originalcard);
			return card;
		} else if(xuezhao=="secondxuezhao"){
			SecondXuezhaoCard*card = new SecondXuezhaoCard;
			card->addSubcard(originalcard);
			return card;
		}
		return nullptr;
	}
private:
	QString xuezhao;
};

class Xuezhao : public TriggerSkill
{
public:
	Xuezhao(const QString&xuezhao): TriggerSkill(xuezhao),xuezhao(xuezhao)
	{
		events << TargetSpecified;
		view_as_skill = new XuezhaoVS(xuezhao);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.from || use.card->isKindOf("SkillCard"))return false;
		QList<ServerPlayer*> no_responds;
		foreach(ServerPlayer*p,use.to){
			if(p->getMark(xuezhao + "_no_respond" + use.from->objectName()+ "-Clear")> 0){
				no_responds << p;
				use.no_respond_list << p->objectName();
			}
		}
		if(no_responds.isEmpty())return false;
		LogMessage log;
		log.type = "#FuqiNoResponse";
		log.from = use.from;
		log.arg = objectName();
		log.card_str = use.card->toString();
		log.to = no_responds;
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(use.from,objectName());
		data = QVariant::fromValue(use);
		return false;
	}
private:
	QString xuezhao;
};

class Koulve : public TriggerSkill
{
public:
	Koulve(const QString&koulve): TriggerSkill(koulve),koulve(koulve)
	{
		events << Damage;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play)return false;
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.to->isDead()|| damage.to==player || damage.to->isKongcheng()|| !player->askForSkillInvoke(this,damage.to))return false;
		room->broadcastSkillInvoke(this);

		int num = 1;
		if(koulve=="secondkoulve")
			num = damage.to->getLostHp();
		if(num <= 0)return false;

		DummyCard*dummy = new DummyCard();
		for(int i = 0; i < num; i++){
			if(player->isDead()|| damage.to->getHandcardNum()<=i)break;
			int id = room->askForCardChosen(player,damage.to,"h",objectName(),false,Card::MethodNone,dummy->getSubcards());
			if(id<0)break;
			dummy->addSubcard(id);
			room->showCard(damage.to,id);
		}

		dummy->deleteLater();
		bool red = false;

		foreach(int id,dummy->getSubcards()){
			const Card*card = Sanguosha->getCard(id);
			if(card->isKindOf("Slash")||(card->isNDTrick()&& card->isDamageCard()))
				dummy->addSubcard(card);
			if(card->isRed())
				red = true;
		}

		if(dummy->subcardsLength()> 0)
			room->obtainCard(player,dummy);
		if(red){
			if(player->isWounded())
				room->loseMaxHp(player,1,objectName());
			else
				room->loseHp(HpLostStruct(player,1,objectName(),player));
			player->drawCards(2,objectName());
		}

		return false;
	}
private:
	QString koulve;
};

class Suirenq : public TriggerSkill
{
public:
	Suirenq(): TriggerSkill("suirenq")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who != player)
			return false;

		QList<int> ids;
		foreach(const Card*card,player->getHandcards()){
			if(card->isKindOf("Slash")||(card->isNDTrick()&& card->isDamageCard()))
				ids << card->getEffectiveId();
		}
		if(ids.isEmpty())return false;

		ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@suirenq-invoke",true,true);
		if(!to)return false;
		room->broadcastSkillInvoke(this);
		room->giveCard(player,to,ids,objectName(),true);

		return false;
	}
};

class Mouni : public PhaseChangeSkill
{
public:
	Mouni(const QString&mouni): PhaseChangeSkill(mouni),mouni(mouni)
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;

		QList<const Card*> slashs;
		foreach(const Card*c,player->getHandcards()){
			if(c->isKindOf("Slash"))
				slashs << c;
		}
		if(slashs.isEmpty())return false;

		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->canSlash(p,false))
				targets << p;
		}

		ServerPlayer* target = room->askForPlayerChosen(player,targets,objectName(),"@" + mouni + "-invoke",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(objectName());

		try {
			int use_time = 0;
			while(!slashs.isEmpty()&&player->isAlive()){
				if(target->hasFlag(mouni + "_dying"))break;
				foreach(const Card*c,slashs){
					if(!c->isKindOf("Slash")|| !player->canSlash(target,c,false))continue;
					room->setPlayerMark(player,mouni + "-Clear",1);
					room->setCardFlag(c,mouni + "_used_slash");
					room->useCard(CardUseStruct(c,player,target));
					use_time++;
					break;
				}
				slashs.clear();
				foreach(const Card*c,player->getHandcards()){
					if(c->isKindOf("Slash")&& player->canSlash(target,c,false))
						slashs << c;
				}
				room->getThread()->delay();
			}
			target->setFlags("-" + mouni + "_dying");

			int damage = room->getTag(mouni + "_damage_slashs").toInt();
			room->removeTag(mouni + "_damage_slashs");
			if(player->isAlive()){
				if(damage < use_time){
					if(!player->isSkipped(Player::Play))
						player->skip(Player::Play);
					if(mouni=="secondmouni"&& !player->isSkipped(Player::Discard))
						player->skip(Player::Discard);
				}
			}
		}
		catch(TriggerEvent triggerEvent){
			if(triggerEvent==TurnBroken || triggerEvent==StageChange){
				target->setFlags("-" + mouni + "_dying");
				room->removeTag(mouni + "_damage_slashs");
			}
			throw triggerEvent;
		}

		return false;
	}
private:
	QString mouni;
};

class MouniDying : public TriggerSkill
{
public:
	MouniDying(const QString&mouni): TriggerSkill("#" + mouni + "-dying"),mouni(mouni)
	{
		events << EnterDying << DamageDone;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EnterDying){
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->getMark(mouni + "-Clear")<= 0)continue;
				player->setFlags(mouni + "_dying");
				break;
			}
		} else {
			DamageStruct d = data.value<DamageStruct>();
			if(!d.card || !d.card->hasFlag(mouni + "_used_slash"))return false;
			room->setCardFlag(d.card,"-" + mouni + "_used_slash");
			int damage = room->getTag(mouni + "_damage_slashs").toInt();
			damage++;
			room->setTag(mouni + "_damage_slashs",damage);
		}
		return false;
	}
private:
	QString mouni;
};

class Zongfan : public TriggerSkill
{
public:
	Zongfan(): TriggerSkill("zongfan")
	{
		events << EventPhaseChanging << EventPhaseSkipped;
		frequency = Wake;
		waked_skills = "zhangu";
	}

	bool triggerable(const ServerPlayer*player)const
	{
		return player&&player->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseSkipped){
			if(player->getPhase()== Player::Play)
				player->addMark("zongfanSkipPlay-Clear");
			return false;
		}
		if(data.value<PhaseChangeStruct>().to!=Player::NotActive||player->getMark("zongfan")>0
			||!player->hasSkill(objectName())||player->getMark("zongfanSkipPlay-Clear")>0)
			return false;
		if(player->getMark("mouni-Clear")>0||player->canWake(objectName())){
		}else return false;
		room->setPlayerMark(player,"zongfan",1);
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->doSuperLightbox(player,"zongfan");

		QList<int> ids = player->handCards();
		int n = room->askForyiji(player,ids,objectName()).length();
		if(player->isDead())return false;

		n = qMin(n,5);
		if(room->changeMaxHpForAwakenSkill(player,n,objectName())){
			room->recover(player,RecoverStruct(player,nullptr,qMin(n,player->getMaxHp()- player->getHp()),"zongfan"));
			if(player->isDead())return false;
			room->handleAcquireDetachSkills(player,"-mouni|zhangu");
		}
		return false;
	}
};

class Zhangu : public PhaseChangeSkill
{
public:
	Zhangu(): PhaseChangeSkill("zhangu")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::RoundStart)return false;
		if(player->getMaxHp()<= 1)return false;
		if(!player->isKongcheng()&& !player->getEquips().isEmpty())return false;

		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->loseMaxHp(player,1,objectName());
		if(player->isDead())return false;

		QList<int> basics,tricks,equips;
		foreach(int id,room->getDrawPile()){
			const Card*c = Sanguosha->getCard(id);
			if(c->isKindOf("BasicCard"))
				basics << id;
			else if(c->isKindOf("TrickCard"))
				tricks << id;
			else if(c->isKindOf("EquipCard"))
				equips << id;
		}
		DummyCard*dummy = new DummyCard;
		dummy->deleteLater();
		if(!basics.isEmpty()){
			int basic = basics.at(qrand()% basics.length());
			dummy->addSubcard(basic);
		}
		if(!tricks.isEmpty()){
			int trick = tricks.at(qrand()% tricks.length());
			dummy->addSubcard(trick);
		}
		if(!equips.isEmpty()){
			int equip = equips.at(qrand()% equips.length());
			dummy->addSubcard(equip);
		}
		if(dummy->subcardsLength()<= 0)return false;
		room->obtainCard(player,dummy,true);
		return false;
	}
};

class TenyearYixiang : public TriggerSkill
{
public:
	TenyearYixiang(): TriggerSkill("tenyearyixiang")
	{
		events << PreCardUsed << DamageCaused << CardUsed;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&& damage.card->hasFlag("tenyearyixiang_first_card")&& damage.to->isAlive()&& damage.to->hasSkill(objectName())){
				if(damage.to->getPhase()== Player::Play)return false;
				room->sendCompulsoryTriggerLog(damage.to,this);
				return damage.to->damageRevises(data,-1);
			}
		} else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isBlack()|| !use.card->hasFlag("tenyearyixiang_second_card"))return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(use.from)){
				if(p->hasSkill(objectName())){
					if(use.to.contains(p))
						room->sendCompulsoryTriggerLog(p,this);
					use.nullified_list << p->objectName();
					data.setValue(use);
				}
			}
		} else {
			if(player->getPhase()!= Player::Play)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			player->addMark("tenyearyixiang_num-PlayClear");
			int mark = player->getMark("tenyearyixiang_num-PlayClear");
			if(mark==1)room->setCardFlag(use.card,"tenyearyixiang_first_card");
			else if(mark==2)room->setCardFlag(use.card,"tenyearyixiang_second_card");
		}
		return false;
	}
};

class TenyearYirang : public PhaseChangeSkill
{
public:
	TenyearYirang(const QString&tenyearyirang): PhaseChangeSkill(tenyearyirang),tenyearyirang(tenyearyirang)
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		DummyCard*dummy = new DummyCard();
		foreach(const Card*c,player->getCards("he")){
			if(!c->isKindOf("BasicCard"))
				dummy->addSubcard(c);
		}
		if(dummy->subcardsLength()> 0){
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getMaxHp()> player->getMaxHp()|| tenyearyirang=="secondtenyearyirang")
					players << p;
			}
			if(!players.isEmpty()){
				ServerPlayer*target = room->askForPlayerChosen(player,players,objectName(),"@yirang-invoke",true,true);
				if(target){
					room->broadcastSkillInvoke(objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE,player->objectName(),target->objectName(),"yirang","");
					room->obtainCard(target,dummy,reason,false);

					if(target->getMaxHp()> player->getMaxHp())
						room->gainMaxHp(player,target->getMaxHp()- player->getMaxHp(),objectName());
					else if(target->getMaxHp()< player->getMaxHp()&& tenyearyirang=="tenyearyirang")
						room->loseMaxHp(player,player->getMaxHp()- target->getMaxHp(),objectName());

					int n = qMin(dummy->subcardsLength(),player->getMaxHp()- player->getHp());
					if(n > 0)
						room->recover(player,RecoverStruct(player,nullptr,n,tenyearyirang));
				}
			}
		}
		delete dummy;
		return false;
	}
private:
	QString tenyearyirang;
};

MinsiCard::MinsiCard()
{
	target_fixed = true;
}

void MinsiCard::use(Room*,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	source->drawCards(2* subcardsLength(),"minsi");
}

class MinsiVS : public ViewAsSkill
{
public:
	MinsiVS(): ViewAsSkill("minsi")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Self->isJilei(to_select))return false;
		int num = 0;
		foreach(const Card*card,selected)
			num += card->getNumber();
		return num + to_select->getNumber()<= 13;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int num = 0;
		foreach(const Card*card,cards)
			num += card->getNumber();
		if(num != 13)return nullptr;

		MinsiCard*c = new MinsiCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("MinsiCard");
	}
};

class Minsi : public TriggerSkill
{
public:
	Minsi(): TriggerSkill("minsi")
	{
		events << CardsMoveOneTime << EventPhaseChanging << EventPhaseProceeding;
		view_as_skill = new MinsiVS;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			if(!player->hasSkill(objectName()))return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName != objectName()|| move.to != player || move.to_place != Player::PlaceHand)return false;
			QVariantList minsi_black = player->property("minsi_black").toList();
			QVariantList minsi_red = player->getTag("minsi_red").toList();
			QList<int> hands = player->handCards();
			foreach(int id,move.card_ids){
				if(!hands.contains(id))continue;
				room->setCardTip(id,objectName());
				const Card*card = Sanguosha->getCard(id);
				if(card->isRed()){
					if(minsi_red.contains(id))continue;
					minsi_red << id;
				} else if(card->isBlack()){
					if(minsi_black.contains(id))continue;
					minsi_black << id;
				}
			}
			room->setPlayerProperty(player,"minsi_black",minsi_black);
			player->setTag("minsi_red", minsi_red);
		}else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			player->removeTag("minsi_red");
			room->setPlayerProperty(player,"minsi_black",QVariantList());
			foreach(int id,player->handCards()+ player->getEquipsId())
				room->setCardTip(id,"-minsi");
		}else{
			if(player->getPhase()!= Player::Discard)return false;
			QVariantList minsi_red = player->getTag("minsi_red").toList();
			if(minsi_red.isEmpty())return false;
			room->ignoreCards(player,ListV2I(minsi_red));
		}
		return false;
	}
};

class MinsiTargetMod : public TargetModSkill
{
public:
	MinsiTargetMod(): TargetModSkill("#minsi-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getDistanceLimit(const Player*from,const Card*card,const Player*)const
	{
		if(card->isBlack()&&(!card->isVirtualCard()|| card->subcardsLength()== 1)
			&&from->property("minsi_black").toList().contains(card->getEffectiveId()))
			return 999;
		if(from->getMark("&jieyingh-SelfClear")>0&&from->hasFlag("CurrentPlayer")
			&&(card->isKindOf("Slash")||card->isNDTrick()))
			return 999;
		if(card->getSkillName()=="dandu")
			return 999;
		return 0;
	}
};

JijingCard::JijingCard()
{
	target_fixed = true;
}

void JijingCard::onUse(Room*,CardUseStruct&)const
{
}

class JijingVS : public ViewAsSkill
{
public:
	JijingVS(): ViewAsSkill("jijing")
	{
		response_pattern = "@@jijing";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Self->isJilei(to_select))return false;
		int judge_num = Self->property("jijing_judge").toInt();
		if(judge_num <= 0)return false;
		int num = 0;
		foreach(const Card*card,selected)
			num += card->getNumber();
		return num + to_select->getNumber()<= judge_num;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int judge_num = Self->property("jijing_judge").toInt();
		if(judge_num <= 0)return nullptr;
		int num = 0;
		foreach(const Card*card,cards)
			num += card->getNumber();
		if(num != judge_num)return nullptr;

		JijingCard*card = new JijingCard;
		card->addSubcards(cards);
		return card;
	}
};

class Jijing : public MasochismSkill
{
public:
	Jijing(): MasochismSkill("jijing")
	{
		view_as_skill = new JijingVS;
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		if(!player->askForSkillInvoke(this))return;
		Room*room = player->getRoom();
		room->broadcastSkillInvoke(objectName());

		JudgeStruct judge;
		judge.reason = objectName();
		judge.who = player;
		judge.pattern = ".";
		judge.play_animation = false;
		room->judge(judge);

		int number = judge.card->getNumber();

		if(number <= 0){
			room->recover(player,RecoverStruct("jijing",player));
			return;
		}

		room->setPlayerProperty(player,"jijing_judge",number);
		const Card*card = room->askForUseCard(player,"@@jijing","@jijing:" + QString::number(number),-1,Card::MethodDiscard);
		if(!card)return;
		CardMoveReason reason(CardMoveReason::S_REASON_THROW,player->objectName(),"jijing","");
		room->throwCard(card,reason,player);
		room->recover(player,RecoverStruct("jijing",player));
	}
};

class Zhuide : public TriggerSkill
{
public:
	Zhuide(): TriggerSkill("zhuide")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->hasSkill(objectName());
	}

	int getDraw(const QString&name,Room*room)const
	{
		QList<int> ids;
		foreach(int id,room->getDrawPile()){
			const Card*card = Sanguosha->getCard(id);
			if((name=="slash"&& card->isKindOf("Slash"))|| card->objectName()== name)
				ids << id;
		}
		if(ids.isEmpty())return -1;
		return ids.at(qrand()% ids.length());
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who != player)
			return false;

		ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@zhuide-invoke",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(objectName());

		QStringList names;
		foreach(int id,room->getDrawPile()){
			const Card*card = Sanguosha->getCard(id);
			if(!card->isKindOf("BasicCard"))continue;
			QString name = card->objectName();
			if(card->isKindOf("Slash"))
				name = "slash";
			if(names.contains(name))continue;
			names << name;
		}
		if(names.isEmpty())return false;

		QList<int> draw_ids,four;
		foreach(QString name,names){
			int id = getDraw(name,room);
			if(id <0)continue;
			draw_ids << id;
		}
		if(draw_ids.isEmpty())return false;

		int num = qMin(4,draw_ids.length());
		for(int i = 0; i < num; i++){
			int id = draw_ids.at(qrand()% draw_ids.length());
			four << id;
			draw_ids.removeOne(id);
		}

		CardsMoveStruct move;
		move.card_ids = four;
		move.from = nullptr;
		move.to = target;
		move.to_place = Player::PlaceHand;
		move.reason = CardMoveReason(CardMoveReason::S_REASON_DRAW,target->objectName(),objectName(),"");
		room->moveCardsAtomic(move,true);

		return false;
	}
};

CixiaoCard::CixiaoCard()
{
	mute = true;
}

bool CixiaoCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*)const
{
	if(targets.length()> 1)return false;
	if(targets.isEmpty())
		return to_select->getMark("&cxyizi")> 0;
	return true;
}

bool CixiaoCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	return targets.length()== 2;
}

void CixiaoCard::onUse(Room*room,CardUseStruct&card_use)const
{
	QVariant data = QVariant::fromValue(card_use);
	room->setTag("CixiaoCard",data);
	SkillCard::onUse(room,card_use);
}

void CixiaoCard::use(Room*room,ServerPlayer*,QList<ServerPlayer*>&)const
{
	CardUseStruct use = room->getTag("CixiaoCard").value<CardUseStruct>();
	ServerPlayer*first = use.to.at(0);
	if(first->isDead()|| first->getMark("&cxyizi")<= 0)return;
	ServerPlayer*second = use.to.at(1);
	if(second->isDead())return;

	first->loseMark("&cxyizi");
	second->gainMark("&cxyizi");
}

class CixiaoVS : public OneCardViewAsSkill
{
public:
	CixiaoVS(): OneCardViewAsSkill("cixiao")
	{
		filter_pattern = ".!";
		response_pattern = "@@cixiao";
	}

	const Card*viewAs(const Card*card)const
	{
		CixiaoCard*c = new CixiaoCard;
		c->addSubcard(card);
		return c;
	}
};

class Cixiao : public PhaseChangeSkill
{
public:
	Cixiao(): PhaseChangeSkill("cixiao")
	{
		view_as_skill = new CixiaoVS;
		waked_skills = "panshi,#cixiao-skill";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		bool yizi = false;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getMark("&cxyizi")<= 0)continue;
			yizi = true;
			break;
		}
		if(yizi&& player->canDiscard(player,"he"))
			room->askForUseCard(player,"@@cixiao","@cixiao",-1,Card::MethodDiscard);
		else if(!yizi){
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),"cixiao","@cixiao-give",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke(objectName());
			target->gainMark("&cxyizi");
		}
		return false;
	}
};

class CixiaoSkill : public TriggerSkill
{
public:
	CixiaoSkill(): TriggerSkill("#cixiao-skill")
	{
		events << MarkChanged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		MarkStruct mark = data.value<MarkStruct>();
		if(mark.name != "&cxyizi")return false;
		if(player->getMark("&cxyizi")<= 0)
			room->detachSkillFromPlayer(player,"panshi");
		else
			room->acquireSkill(player,"panshi");
		return false;
	}
};

class Xianshuai : public TriggerSkill
{
public:
	Xianshuai(): TriggerSkill("xianshuai")
	{
		events << Damage;
		frequency = Compulsory;
		waked_skills = "#xianshuai";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(room->getTag("XianshuaiFirstDamage").toInt()> 1)return false;
		DamageStruct damage = data.value<DamageStruct>();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			room->sendCompulsoryTriggerLog(p,objectName(),true,true);
			p->drawCards(1,objectName());
			if(p->isAlive()&& p==damage.from)
				room->damage(DamageStruct("xianshuai",p,damage.to));
		}
		return false;
	}
};

class XianshuaiRecord : public TriggerSkill
{
public:
	XianshuaiRecord(): TriggerSkill("#xianshuai")
	{
		events << Damage << RoundStart;
		global = true;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&)const
	{
		if(event==Damage){
			int n = 1+room->getTag("XianshuaiFirstDamage").toInt();
			room->setTag("XianshuaiFirstDamage",n);
		} else
			room->removeTag("XianshuaiFirstDamage");
		return false;
	}
};

class Panshi : public TriggerSkill
{
public:
	Panshi(): TriggerSkill("panshi")
	{
		events << EventPhaseStart << DamageCaused << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Start)return false;
			if(player->isKongcheng())return false;
			//QList<ServerPlayer*> dingyuans = room->findPlayersBySkillName("cixiao");
			QList<ServerPlayer*> dingyuans;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->hasSkill("cixiao",true)&& p != player){
					dingyuans << p;
					room->setPlayerFlag(p,"dingyuan");
				}
			}
			if(dingyuans.isEmpty())return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);

			try {
				while(!player->isKongcheng()){
					if(player->isDead())break;
					QList<int> cards = player->handCards();
					ServerPlayer*dingyuan = room->askForYiji(player,cards,objectName(),false,false,false,1,
											dingyuans,CardMoveReason(),"@panshi-give",false);
					if(!dingyuan){
						dingyuan = dingyuans.at(qrand()% dingyuans.length());
						const Card*card = player->getRandomHandCard();
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE,player->objectName(),dingyuan->objectName(),"panshi","");
						room->obtainCard(dingyuan,card,reason,false);
					}
					dingyuans.removeOne(dingyuan);
					room->setPlayerFlag(dingyuan,"-dingyuan");
					foreach(ServerPlayer*p,dingyuans){
						if(!p->hasSkill("cixiao",true)){
							dingyuans.removeOne(p);
							room->setPlayerFlag(p,"-dingyuan");
						}
					}

					if(dingyuans.isEmpty())break;
				}
			}
			catch(TriggerEvent triggerEvent){
				if(triggerEvent==TurnBroken || triggerEvent==StageChange){
					foreach(ServerPlayer*p,room->getAlivePlayers()){
						if(p->hasFlag("dingyuan"))
							room->setPlayerFlag(p,"-dingyuan");
					}
				}
				throw triggerEvent;
			}

			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->hasFlag("dingyuan"))
					room->setPlayerFlag(p,"-dingyuan");
			}
		} else {
			if(player->getPhase()!= Player::Play)return false;
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash"))return false;
			if(!damage.to->hasSkill("cixiao",true))return false;
			if(event==DamageCaused){
				if(damage.to->isDead())return false;
				LogMessage log;
				log.type = "#PanshiDamage";
				log.from = player;
				log.to << damage.to;
				log.arg = objectName();
				log.arg2 = QString::number(++damage.damage);
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player,objectName());
				data = QVariant::fromValue(damage);
			} else {
				player->endPlayPhase();
			}
		}
		return false;
	}
};

JieyinghCard::JieyinghCard()
{
	mute = true;
}

bool JieyinghCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.length()< Self->getMark("&jieyingh-SelfClear")&& to_select->hasFlag("jieyingh_canchoose");
}

void JieyinghCard::onUse(Room*room,CardUseStruct&card_use)const
{
	foreach(ServerPlayer*p,card_use.to)
		room->setPlayerFlag(p,"jieyingh_extratarget");
}

class JieyinghVS : public ZeroCardViewAsSkill
{
public:
	JieyinghVS(): ZeroCardViewAsSkill("jieyingh")
	{
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("@@jieyingh");
	}

	const Card*viewAs()const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@jieyingh1")
			return new ExtraCollateralCard;
		return new JieyinghCard;
	}
};

class Jieyingh : public TriggerSkill
{
public:
	Jieyingh(): TriggerSkill("jieyingh")
	{
		events << PreCardUsed << Damage << EventPhaseStart;
		view_as_skill = new JieyinghVS;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damage){
			if(player->getMark("&jieyingh-SelfClear")<= 0 || !player->hasFlag("CurrentPlayer"))return false;
			LogMessage log;
			log.type = "#JieyinghEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->setPlayerCardLimitation(player,"use",".",true);
		} else if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish || !player->hasSkill(objectName()))return false;
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),"jieyingh","@jieyingh-invoke",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke("jieyingh");
			room->addPlayerMark(target,"&jieyingh-SelfClear");
		} else {
			if(player->getMark("&jieyingh-SelfClear")<= 0)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.to.isEmpty()||!player->hasFlag("CurrentPlayer"))return false;
			if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;

			if(use.card->isKindOf("Collateral")){
				for(int i = 1; i <= player->getMark("&jieyingh-SelfClear"); i++){
					bool canextra = false;
					foreach(ServerPlayer*p,room->getAlivePlayers()){
						if(use.to.contains(p))continue;
						if(player->canUse(use.card,p)){
							canextra = true;
							break;
						}
					}
					if(!canextra)break;
					QStringList tos;
					tos << use.card->toString();
					foreach(ServerPlayer*t,use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player,"extra_collateral",tos.join("+"));
					room->askForUseCard(player,"@@jieyingh1","@jieyingh:" + use.card->objectName());
					ServerPlayer*p = player->getTag("ExtraCollateralTarget").value<ServerPlayer*>();
					player->removeTag("ExtraCollateralTarget");
					if(p){
						use.to.append(p);
						room->sortByActionOrder(use.to);
						data = QVariant::fromValue(use);
					}
				}
			} else {
				bool canextra = false;
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(use.to.contains(p))continue;
					if(player->canUse(use.card,p)){
						room->setPlayerFlag(p,"jieyingh_canchoose");
						canextra = true;
					}
				}
				if(!canextra)return false;
				player->setTag("jieyinghData", data);
				if(!room->askForUseCard(player,"@@jieyingh","@jieyingh:" + use.card->objectName()))return false;
				LogMessage log;
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					room->setPlayerFlag(p,"-jieyingh_canchoose");
					if(p->hasFlag("jieyingh_extratarget")){
						room->setPlayerFlag(p,"-jieyingh_extratarget");
						log.to << p;
					}
				}
				if(log.to.isEmpty())return false;
				use.to << log.to;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "jieyingh";
				room->sendLog(log);
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class Weipo : public TriggerSkill
{
public:
	Weipo(): TriggerSkill("weipo")
	{
		events << CardFinished << TargetSpecified << EventPhaseStart;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			player->setTag("Weipo_Wuxiao", false);
		} else if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
			foreach(ServerPlayer*p,use.to){
				if(p==use.from)continue;
				if(p->isDead()|| !p->hasSkill(objectName())|| p->getTag("Weipo_Wuxiao").toBool())continue;
				if(p->getHandcardNum()>= p->getMaxHp())continue;
				room->sendCompulsoryTriggerLog(p,objectName(),true,true);
				p->drawCards(p->getMaxHp()- p->getHandcardNum(),objectName());
				p->setTag("weipo_" + use.card->toString(), p->getHandcardNum()+ 1);
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
			foreach(ServerPlayer*p,room->getAllPlayers(true)){
				int n = p->getTag("weipo_" + use.card->toString()).toInt()- 1;
				if(n >= 0){
					p->removeTag("weipo_" + use.card->toString());
					if(p->isDead()|| p->getHandcardNum()>= n)continue;
					p->setTag("Weipo_Wuxiao", true);
					if(use.from->isAlive()&& !p->isKongcheng()){
						const Card*c = room->askForExchange(p,"weipo",1,1,false,"@weipo-give:" + use.from->objectName());
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE,p->objectName(),use.from->objectName(),"weipo","");
						room->obtainCard(use.from,c,reason,false);
					}
				}
			}
		}
		return false;
	}
};

class TenyearFuqi : public TriggerSkill
{
public:
	TenyearFuqi(): TriggerSkill("tenyearfuqi")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;

		LogMessage log;
		foreach(ServerPlayer*p,room->getOtherPlayers(use.from)){
			if(use.from->distanceTo(p)!= 1)continue;
			use.no_respond_list << p->objectName();
			log.to << p;
		}
		if(log.to.isEmpty())return false;

		log.type = "#FuqiNoResponse";
		log.from = use.from;
		log.arg = objectName();
		log.card_str = use.card->toString();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(use.from,objectName());

		data = QVariant::fromValue(use);
		return false;
	}
};

PingjianDialog*PingjianDialog::getInstance()
{
	static PingjianDialog*instance;
	if(instance==nullptr)
		instance = new PingjianDialog();
	return instance;
}

PingjianDialog::PingjianDialog()
{
	setObjectName("pingjian");
	setWindowTitle(Sanguosha->translate("pingjian"));
	group = new QButtonGroup(this);
	button_layout = new QVBoxLayout;
	setLayout(button_layout);
	connect(group,SIGNAL(buttonClicked(QAbstractButton*)),this,SLOT(selectSkill(QAbstractButton*)));
}

void PingjianDialog::popup()
{
	Self->removeTag(objectName());
	foreach(QAbstractButton*button,group->buttons()){
		button_layout->removeWidget(button);
		group->removeButton(button);
		delete button;
	}
	QStringList generals,ava_generals,pingjian_skills = Self->property("pingjian_has_used_skills").toString().split("+");
	foreach(QString general_name,Sanguosha->getLimitedGeneralNames()){
		if(ava_generals.contains(general_name))continue;
		const General*general = Sanguosha->getGeneral(general_name);
		if(!general)continue;
		foreach(const Skill*skill,general->getVisibleSkillList()){
			if(skill->objectName()== "pingjian")continue;
			if(pingjian_skills.contains(skill->objectName()))continue;
			QString translation = skill->getDescription();
			if(translation.contains("出牌阶段限一次，")|| translation.contains("阶段技，")){
				const ViewAsSkill*vs = Sanguosha->getViewAsSkill(skill->objectName());
				if(vs&& vs->isEnabledAtPlay(Self)){
					ava_generals << general_name;
					break;
				}
			}
		}
	}
	for(int i = 1; i <= 3; i++){
		if(ava_generals.isEmpty())break;
		QString general = ava_generals.at(qrand()% ava_generals.length());
		ava_generals.removeOne(general);
		generals << general;
	}
	if(generals.isEmpty())return;
	foreach(QString general,generals){
		bool has_general_button = false;
		foreach(const Skill*skill,Sanguosha->getGeneral(general)->getVisibleSkillList()){
			if(skill->objectName()== "pingjian" || pingjian_skills.contains(skill->objectName()))continue;
			const ViewAsSkill*vs = Sanguosha->getViewAsSkill(skill->objectName());
			if(!vs || !vs->isEnabledAtPlay(Self))continue;
			QString translation = skill->getDescription();
			if(translation.contains("出牌阶段限一次，")|| translation.contains("阶段技，")){
				if(!has_general_button){
					has_general_button = true;
					QAbstractButton*button = createSkillButton(general);
					button->setEnabled(false);
					button_layout->addWidget(button);
				}
				QAbstractButton*button = createSkillButton(skill->objectName());
				button->setEnabled(true);
				button_layout->addWidget(button);
			}
		}
	}
	exec();
}

void PingjianDialog::selectSkill(QAbstractButton*button)
{
	Self->setTag(objectName(), button->objectName());
	emit onButtonClick();
	accept();
}

QAbstractButton*PingjianDialog::createSkillButton(const QString&skill_name)
{
	const Skill*skill = Sanguosha->getSkill(skill_name);
	if(!skill){
		if(!Sanguosha->getGeneral(skill_name))return nullptr;
	}
	QCommandLinkButton*button = new QCommandLinkButton(Sanguosha->translate(skill_name));
	button->setObjectName(skill_name);
	if(skill){
		LuaLocker locker;
		button->setToolTip(skill->getDescription());
	}
	group->addButton(button);
	return button;
}

PingjianCard::PingjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

/*bool PingjianCard::targetFixed()const
{
	const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(Self->getTag("pingjian").toString());
	if(vs_skill){
		QList<const Card*> cards;
		foreach(int id,subcards)
			cards << Sanguosha->getCard(id);
		const Card*card = vs_skill->viewAs(cards);
		if(card){
			bool has = card->targetFixed();
			delete card;
			return has;
		}
	}
	return false;
}*/

bool PingjianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(Self->getTag("pingjian").toString());
	if(vs_skill){
		QList<const Card*> cards;
		foreach(int id,subcards)
			cards << Sanguosha->getCard(id);
		const Card*card = vs_skill->viewAs(cards);
		if(card){
			bool has = card->targetFilter(targets,to_select,Self);
			delete card;
			return has;
		}
	}
	return false;
}

bool PingjianCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(Self->getTag("pingjian").toString());
	if(vs_skill){
		QList<const Card*> cards;
		foreach(int id,subcards)
			cards << Sanguosha->getCard(id);
		const Card*card = vs_skill->viewAs(cards);
		if(card){
			bool has = card->targetsFeasible(targets,Self);
			delete card;
			return has;
		}
	}
	return false;
}

void PingjianCard::onUse(Room*room,CardUseStruct&card_use)const
{
	QString skill_name = getUserString().split("|").first();
	if(skill_name.isEmpty())return;
	QStringList skills = card_use.from->property("pingjian_has_used_skills").toString().split("+");
	skills << skill_name;
	room->setPlayerProperty(card_use.from,"pingjian_has_used_skills",skills.join("+"));
	const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(skill_name);
	if(vs_skill){
		QList<const Card*> cards;
		foreach(int id,subcards)
			cards << Sanguosha->getCard(id);
		ClientPlayer*sp = Self;
		Self =(ClientPlayer*)card_use.from;
		const Card*card = vs_skill->viewAs(cards);
		Self = sp;
		if(card){
			SkillCard*sc =(SkillCard*)card;
			sc->setUserString(getUserString().split("|").last());
			const Skill*skill = Sanguosha->getSkill(skill_name);
			if(skill){
				if(skill->inherits("TriggerSkill"))
					room->getThread()->addTriggerSkill((const TriggerSkill*)skill);
				foreach(const Skill*rs,Sanguosha->getRelatedSkills(skill_name)){
					if(rs->inherits("TriggerSkill"))
						room->getThread()->addTriggerSkill((const TriggerSkill*)rs);
				}
			}
			LogMessage log;
			log.from = card_use.from;
			log.type = "#InvokeSkill";
			log.arg = "pingjian";
			room->sendLog(log);
			room->broadcastSkillInvoke("pingjian",card_use.from);
			room->addPlayerHistory(card_use.from,sc->getClassName());
			room->notifySkillInvoked(card_use.from,"pingjian");
			CardUseStruct use;
			use.card = sc;
			use.to = card_use.to;
			use.from = card_use.from;
			if(!sc->isMute())room->broadcastSkillInvoke(skill_name,card_use.from);
			sc->onUse(room,use);
			sc->deleteLater();
		}
	}
}

class PingjianVS : public ViewAsSkill
{
public:
	PingjianVS(): ViewAsSkill("pingjian")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("PingjianCard");
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(Self->getTag("pingjian").toString());
		if(vs_skill)return vs_skill->viewFilter(selected,to_select);
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		QString skill_name = Self->getTag("pingjian").toString();
		const ViewAsSkill*vs_skill = Sanguosha->getViewAsSkill(skill_name);
		if(vs_skill){
			const Card*card = vs_skill->viewAs(cards);
			if(card){
				skill_name = skill_name+"|"+((const SkillCard*)card)->getUserString();
				PingjianCard*pj = new PingjianCard;
				pj->setUserString(skill_name);
				pj->addSubcards(cards);
				return pj;
			}
		}
		return nullptr;
	}
};

class Pingjian : public TriggerSkill
{
public:
	Pingjian(): TriggerSkill("pingjian")
	{
		events << Damaged << EventPhaseStart;
		view_as_skill = new PingjianVS;
	}

	QDialog*getDialog()const
	{
		return PingjianDialog::getInstance();
	}

	void getpingjianskill(ServerPlayer*source,const QString&str,const QString&strr,TriggerEvent event,QVariant&data)const
	{
		Room*room = source->getRoom();
		QStringList ava_generals,pingjian_skills = source->property("pingjian_has_used_skills").toString().split("+");
		foreach(QString general_name,Sanguosha->getLimitedGeneralNames()){
			if(ava_generals.contains(general_name))continue;
			const General*general = Sanguosha->getGeneral(general_name);
			foreach(const Skill*skill,general->getVisibleSkillList()){
				if(skill->objectName()== "pingjian")continue;
				if(pingjian_skills.contains(skill->objectName()))continue;
				if(!skill->inherits("TriggerSkill"))continue;
				const TriggerSkill*triggerskill = Sanguosha->getTriggerSkill(skill->objectName());
				if(!triggerskill)continue;
				bool has_event = false;
				if(triggerskill->hasEvent(event))
					has_event = true;
				else {
					foreach(const Skill*related_sk,Sanguosha->getRelatedSkills(skill->objectName())){
						if(!related_sk->inherits("TriggerSkill"))continue;
						const TriggerSkill*related_trigger = Sanguosha->getTriggerSkill(related_sk->objectName());
						if(!related_trigger->hasEvent(event))continue;
						has_event = true;
					}
				}
				if(!has_event)continue;
				QString translation = skill->getDescription();
				if(!translation.contains(str)&& !strr.isEmpty()&& !translation.contains(strr))continue;
				if(str=="结束阶段"&&(translation.contains("的结束阶段")|| translation.contains("结束阶段结束")))continue;
				ava_generals << general_name;
				break;
			}
		}

		QStringList generals;
		for(int i = 1; i <= 3; i++){
			if(ava_generals.isEmpty())break;
			QString general = ava_generals.at(qrand()% ava_generals.length());
			ava_generals.removeOne(general);
			generals << general;
		}
		if(generals.isEmpty())return;
		QString general = room->askForGeneral(source,generals);
		QStringList skills;
		foreach(const Skill*skill,Sanguosha->getGeneral(general)->getVisibleSkillList()){
			if(skill->objectName()== "pingjian")continue;
			if(pingjian_skills.contains(skill->objectName()))continue;
			if(!skill->inherits("TriggerSkill"))continue;
			const TriggerSkill*triggerskill = Sanguosha->getTriggerSkill(skill->objectName());
			if(!triggerskill)continue;
			bool has_event = false;
			if(triggerskill->hasEvent(event))
				has_event = true;
			else {
				foreach(const Skill*related_sk,Sanguosha->getRelatedSkills(skill->objectName())){
					if(!related_sk->inherits("TriggerSkill"))continue;
					const TriggerSkill*related_trigger = Sanguosha->getTriggerSkill(related_sk->objectName());
					if(!related_trigger->hasEvent(event))continue;
					has_event = true;
				}
			}
			if(!has_event)continue;
			QString translation = skill->getDescription();
			if(!translation.contains(str)&& !strr.isEmpty()&& !translation.contains(strr))continue;
			if(str=="结束阶段"&&(translation.contains("的结束阶段")|| translation.contains("结束阶段结束")))continue;
			skills << skill->objectName();
		}
		if(skills.isEmpty())return;
		QString skill_name = room->askForChoice(source,"pingjian",skills.join("+"));
		pingjian_skills << skill_name;
		room->setPlayerProperty(source,"pingjian_has_used_skills",pingjian_skills.join("+"));
		const TriggerSkill*triggerskill = Sanguosha->getTriggerSkill(skill_name);
		if(!triggerskill)return;
		bool has_event = false;
		if(triggerskill->getTriggerEvents().contains(event))
			has_event = true;
		else {
			foreach(const Skill*related_sk,Sanguosha->getRelatedSkills(skill_name)){
				if(!related_sk->inherits("TriggerSkill"))continue;
				const TriggerSkill*related_trigger = Sanguosha->getTriggerSkill(related_sk->objectName());
				if(!related_trigger->hasEvent(event))continue;
				has_event = true;
				triggerskill = related_trigger;
				break;
			}
		}
		room->setPlayerProperty(source,"pingjian_triggerskill",skill_name);
		if(!has_event || !triggerskill->triggerable(source)){
			room->setPlayerProperty(source,"pingjian_triggerskill","");
			return;
		}
		if(triggerskill->getFrequency(source)== Skill::Wake){
			if(!triggerskill->triggerable(source)){
				room->setPlayerProperty(source,"pingjian_triggerskill","");
				return;
			}
		}
		room->getThread()->addTriggerSkill(triggerskill);
		try {
			triggerskill->trigger(event,room,source,data);
		}catch(TriggerEvent triggerEvent){
			if(triggerEvent==TurnBroken || triggerEvent==StageChange)
				room->setPlayerProperty(source,"pingjian_triggerskill","");
			throw triggerEvent;
		}
		room->setPlayerProperty(source,"pingjian_triggerskill","");
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			if(!player->askForSkillInvoke(this))return false;
			room->broadcastSkillInvoke(objectName());
			getpingjianskill(player,"你受到伤害后","你受到1点伤害后",event,data);
		} else if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish)return false;
			if(!player->askForSkillInvoke(this))return false;
			room->broadcastSkillInvoke(objectName());
			getpingjianskill(player,"结束阶段","结束阶段开始",event,data);
		}
		return false;
	}
};

SecondYujueCard::SecondYujueCard(): YujueCard("secondzhihu")
{
	target_fixed = true;
}

class SecondYujueVS : public ZeroCardViewAsSkill
{
public:
	SecondYujueVS(): ZeroCardViewAsSkill("secondyujue")
	{
	}

	const Card*viewAs()const
	{
		return new SecondYujueCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->hasEquipArea()&& !player->hasUsed("SecondYujueCard");
	}
};

class SecondYujue : public PhaseChangeSkill
{
public:
	SecondYujue(): PhaseChangeSkill("secondyujue")
	{
		view_as_skill = new SecondYujueVS;
		waked_skills = "secondzhihu";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::RoundStart)return false;
		QStringList names = player->getTag("secondzhihu_names").toStringList();
		if(names.isEmpty())return false;
		player->removeTag("secondzhihu_names");
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(!names.contains(p->objectName()))continue;
			if(!p->hasSkill("secondzhihu",true))continue;
			targets << p;
		}
		if(targets.isEmpty())return false;
		room->sortByActionOrder(targets);
		foreach(ServerPlayer*p,targets){
			if(p->isDead()|| !p->hasSkill("secondzhihu",true))continue;
			room->detachSkillFromPlayer(p,"secondzhihu");
		}
		return false;
	}
};

class Zhihu : public TriggerSkill
{
public:
	Zhihu(): TriggerSkill("zhihu")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		if(player->getMark("zhihu-Clear")>= 2)return false;
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.from==damage.to)return false;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->addPlayerMark(player,"zhihu-Clear");
		player->drawCards(2,objectName());
		return false;
	}
};

class SecondZhihu : public TriggerSkill
{
public:
	SecondZhihu(): TriggerSkill("secondzhihu")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		if(player->getMark("secondzhihu-Clear")>= 3)return false;
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.from==damage.to)return false;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->addPlayerMark(player,"secondzhihu-Clear");
		player->drawCards(1,objectName());
		return false;
	}
};

class Gongjian : public TriggerSkill
{
public:
	Gongjian(): TriggerSkill("gongjian")
	{
		events << TargetSpecified << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;
		if(event==CardFinished){
			QStringList names;
			foreach(ServerPlayer*p,use.to)
				names << p->objectName();
			room->setTag("gongjian_slash_targets",names);
			return false;
		}
		QStringList names = room->getTag("gongjian_slash_targets").toStringList();

		bool same = false;
		foreach(ServerPlayer*p,use.to){
			if(names.contains(p->objectName())){
				same = true;
				break;
			}
		}
		if(!same)return false;

		foreach(ServerPlayer*player,room->getAllPlayers()){
			if(player->getMark("gongjian-Clear")>0||!player->hasSkill(objectName()))continue;
			foreach(ServerPlayer*p,use.to){
				if(!names.contains(p->objectName())||!player->canDiscard(p,"he"))continue;
				player->setTag("gongjianData", data);
				if(!player->askForSkillInvoke(this,p))continue;
				player->addMark("gongjian-Clear");
				player->peiyin(this);
				QList<int> ids;

				for(int i = 0; i < 2; ++i){
					if(p->getCardCount()<=i)break;
					int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard,ids,i > 0);
					if(id < 0)break;
					ids << id;
				}

				if(!ids.isEmpty()){
					room->throwCard(ids,objectName(),p,player);
					DummyCard*dc = new DummyCard;
					foreach(int id,ids){
						if(Sanguosha->getCard(id)->isKindOf("Slash"))
							dc->addSubcard(id);
					}
					dc->deleteLater();
					room->obtainCard(player,dc,true);
				}
				break;
			}
		}
		return false;
	}
};

class Kuimang : public TriggerSkill
{
public:
	Kuimang(): TriggerSkill("kuimang")
	{
		events << Death;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(player->getTag("kuimang_damage_"+death.who->objectName()).toBool()){
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->drawCards(2,objectName());
		}
		return false;
	}
};


class SpNiluanVS : public OneCardViewAsSkill
{
public:
	SpNiluanVS(): OneCardViewAsSkill("spniluan")
	{
		filter_pattern = ".|black";
		response_or_use = true;
	}

	const Card*viewAs(const Card*card)const
	{
		Card*c = Sanguosha->cloneCard("slash");
		c->setSkillName("spniluan");
		c->addSubcard(card);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return Slash::IsAvailable(player);
	}
};

class SpNiluan : public TriggerSkill
{
public:
	SpNiluan(): TriggerSkill("spniluan")
	{
		events << CardFinished;
		view_as_skill = new SpNiluanVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*,ServerPlayer*,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->getSkillNames().contains(objectName()))return false;
		if(use.card->hasFlag("DamageDone"))return false;
		use.m_addHistory = false;
		data = QVariant::fromValue(use);
		return false;
	}
};

WeiwuCard::WeiwuCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool WeiwuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	const Card*card = Sanguosha->getCard(getSubcards().first());
	Snatch*snatch = new Snatch(card->getSuit(),card->getNumber());
	snatch->addSubcard(card);
	snatch->setSkillName("weiwu");
	snatch->deleteLater();
	return !Self->isLocked(snatch)&& snatch->targetFilter(targets,to_select,Self)
		&& to_select->getHandcardNum()>= Self->getHandcardNum();
}

void WeiwuCard::onUse(Room*room,CardUseStruct&card_use)const
{
	Snatch*snatch = new Snatch(getSuit(),getNumber());
	snatch->setSkillName("weiwu");
	snatch->addSubcard(this);
	snatch->deleteLater();
	card_use.card = snatch;
	room->useCard(card_use,true);
}

class Weiwu : public OneCardViewAsSkill
{
public:
	Weiwu(): OneCardViewAsSkill("weiwu")
	{
		filter_pattern = ".|red";
		response_or_use = true;
	}

	const Card*viewAs(const Card*card)const
	{
		WeiwuCard*c = new WeiwuCard;
		c->addSubcard(card);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		Snatch*snatch = new Snatch(Card::NoSuit,0);
		snatch->setSkillName("weiwu");
		snatch->deleteLater();
		return !player->hasUsed("WeiwuCard")&& !player->isLocked(snatch);
	}
};


class Zhenze : public PhaseChangeSkill
{
public:
	Zhenze(): PhaseChangeSkill("zhenze")
	{
	}

	int zhenzeJudge(ServerPlayer*player)const
	{
		int hand = player->getHandcardNum(),hp = player->getHp();
		if(hand > hp)
			return 1;
		if(hand==hp)
			return 0;
		return -1;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Discard)return false;

		int zhenze = zhenzeJudge(player);
		QStringList choices;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(zhenzeJudge(p)!= zhenze&& !choices.contains("lose"))
				choices << "lose";
			else if(zhenzeJudge(p)== zhenze&& !choices.contains("recover")){
				if(p->isWounded())
					choices << "recover";
			}
		}
		if(choices.isEmpty())return false;
		choices << "cancel";

		QString choice = room->askForChoice(player,objectName(),choices.join("+"));
		if(choice=="cancel")return false;

		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player,objectName());

		zhenze = zhenzeJudge(player);
		if(choice=="lose"){
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(zhenzeJudge(p)!= zhenze&& p->isAlive())
					room->loseHp(HpLostStruct(p,1,objectName(),player));
			}
		} else {
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(zhenzeJudge(p)== zhenze&& p->isAlive())
					room->recover(p,RecoverStruct("zhenze",player));
			}
		}

		return false;
	}
};

AnliaoCard::AnliaoCard()
{
}

bool AnliaoCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(!targets.isEmpty()|| to_select->isNude())return false;
	if(to_select != Self)
		return true;
	else {
		bool can_recast_self = false;
		foreach(const Card*c,Self->getHandcards()+ Self->getEquips()){
			if(!Self->isCardLimited(c,Card::MethodRecast)){
				can_recast_self = true;
				break;
			}
		}
		return can_recast_self;
	}
}

void AnliaoCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	if(to->isNude())return;

	Room*room = from->getRoom();

	if(from==to){
		bool can_recast_self = false;
		foreach(const Card*c,from->getHandcards()+ from->getEquips()){
			if(!from->isCardLimited(c,Card::MethodRecast)){
				can_recast_self = true;
				break;
			}
		}
		if(!can_recast_self)return;
	}

	int id = room->askForCardChosen(from,to,"he","anliao",false,Card::MethodRecast);
	if(id < 0)return;

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST,to->objectName(),"anliao","");
	room->moveCardTo(Sanguosha->getCard(id),to,nullptr,Player::DiscardPile,reason);
	//from->broadcastSkillInvoke("@recast");

	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = to;
	log.card_str = QString::number(id);
	room->sendLog(log);

	to->drawCards(1,"recast");
}

class Anliao : public ZeroCardViewAsSkill
{
public:
	Anliao(): ZeroCardViewAsSkill("anliao")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		int qun = 0;
		if(player->getKingdom()== "qun")
			qun++;
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->getKingdom()== "qun")
				qun++;
		}
		return player->usedTimes("AnliaoCard")< qun;
	}

	const Card*viewAs()const
	{
		return new AnliaoCard;
	}
};

class XiangshuZK : public PhaseChangeSkill
{
public:
	XiangshuZK(): PhaseChangeSkill("xiangshuzk")
	{
		waked_skills = "#xiangshuzk";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->isDead())break;
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			QString choice = room->askForChoice(p,objectName(),"0+1+2+3+4+5+cancel",QVariant::fromValue(player));
			if(choice=="cancel")continue;

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = p;
			log.arg = objectName();
			room->sendLog(log);
			p->peiyin(this);
			room->notifySkillInvoked(p,objectName());

			if(!p->isKongcheng()&& room->askForDiscard(p,objectName(),1,1,true,false,"@xiangshuzk-card:" + choice))
				room->addPlayerMark(player,"&xiangshuzk+#" + p->objectName()+ "+" + choice + "-PlayClear",1,QList<ServerPlayer*>()<< p);
			else
				room->addPlayerMark(player,"&xiangshuzk+#" + p->objectName()+ "+" + choice + "-PlayClear");
		}
		return false;
	}
};

class XiangshuZKEffect : public TriggerSkill
{
public:
	XiangshuZKEffect(): TriggerSkill("#xiangshuzk")
	{
		events << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->isDead())break;
			if(p->isDead())continue;
			QList<int> numbers;
			foreach(QString mark,player->getMarkNames()){
				if(player->getMark(mark)<1||!mark.startsWith("&xiangshuzk+#"+p->objectName()))continue;
				QStringList marks1 = mark.split("-");
				if(marks1.length()!= 2)continue;
				QStringList marks2 = marks1.first().split("+");
				if(marks2.length()!= 3)continue;
				bool ok;
				int num = marks2.last().toInt(&ok);
				if(ok)numbers << num;
			}
			int hand = player->getHandcardNum();
			foreach(int num,numbers){
				if(p->isDead()|| player->isDead())break;
				if(qAbs(hand - num)<= 1){
					room->sendCompulsoryTriggerLog(p,"xiangshuzk",true,true);
					if(!player->isNude()){
						int id = room->askForCardChosen(p,player,"he","xiangshuzk");
						room->obtainCard(p,id);
					}
					if(hand==num&& p->isAlive()&& player->isAlive())
						room->damage(DamageStruct("xiangshuzk",p,player));
				}
			}
		}
		return false;
	}
};

class Juying : public TriggerSkill
{
public:
	Juying(): TriggerSkill("juying")
	{
		events << EventPhaseEnd;
		waked_skills = "#juying,#juying-target";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play)return false;
		int times = player->getMark("JuyingUsedSlashTimes-PlayClear");
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->deleteLater();
		if(times >= 1 + Sanguosha->correctCardTarget(TargetModSkill::Residue,player,slash))return false;
		QStringList choices,chosen;
		choices << "cishu" << "maxcards" << "draw" << "cancel";
		while(player->isAlive()){
			QString choice = room->askForChoice(player,objectName(),choices.join("+"),data,chosen.join("+"));
			if(choice=="cancel")break;
			choices.removeOne(choice);
			chosen << choice;
			LogMessage log;
			log.from = player;
			if(chosen.length()<2){
				log.type = "#InvokeSkill";
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player,objectName());
			}
			log.type = "#FumianFirstChoice";
			log.arg = "juying:" + choice.split("=").first();
			room->sendLog(log);
			if(choice=="cishu"){
				int turn = player->getMark("Global_TurnCount2")+ 1;
				room->addPlayerMark(player,"&juying_cishu+#" + QString::number(turn));
			} else if(choice=="maxcards")
				room->addMaxCards(player,2);
			else
				player->drawCards(3,objectName());
		}
		int length = chosen.length(),cha = qAbs(player->getHp()- chosen.length());
		if(player->isDead()|| length <= player->getHp()|| player->isNude()|| player->getCardCount()< cha)return false;
		room->askForDiscard(player,objectName(),cha,cha,false,true);
		return false;
	}
};

class JuyingClear : public TriggerSkill
{
public:
	JuyingClear(): TriggerSkill("#juying")
	{
		events << EventPhaseChanging << PreCardUsed;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			if(player->getPhase()!= Player::Play)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash"))
				player->addMark("JuyingUsedSlashTimes-PlayClear");
		}else{
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			int turn = player->getMark("Global_TurnCount2");
			room->setPlayerMark(player,"&juying_cishu+#" + QString::number(turn),0);
		}
		return false;
	}
};

class JuyingTargetMod : public TargetModSkill
{
public:
	JuyingTargetMod(): TargetModSkill("#juying-target")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player*from,const Card*,const Player*)const
	{
		return from->getMark(QString("&juying_cishu+#%1").arg(from->getMark("Global_TurnCount2")));
	}
};

class Suoliang : public TriggerSkill
{
public:
	Suoliang(): TriggerSkill("suoliang")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.to==player || damage.to->isNude()|| !room->hasCurrent()|| player->getMark("suoliangUsed-Clear")> 0)return false;
		int maxhp = qMin(damage.to->getMaxHp(),5);
		if(maxhp <= 0)return false;

		if(!player->askForSkillInvoke(this,damage.to))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"suoliangUsed-Clear");

		QList<int> cards;
		for(int i = 0; i < maxhp; ++i){
			if(damage.to->getCardCount()<=i)break;
			int id = room->askForCardChosen(player,damage.to,"he",objectName(),false,Card::MethodNone,cards,i > 0);
			if(id < 0)break;
			cards << id;
		}
		room->showCard(damage.to,cards);

		QList<int> gets;
		foreach(int id,cards){
			Card::Suit suit = Sanguosha->getCard(id)->getSuit();
			if(suit==Card::Heart || suit==Card::Club)
				gets << id;
		}
		if(gets.isEmpty()){
			foreach(int id,cards){
				if(player->canDiscard(damage.to,id))
					gets << id;
			}
			if(!gets.isEmpty()){
				DummyCard dummy(gets);
				room->throwCard(&dummy,objectName(),damage.to,player);
			}
		} else {
			DummyCard dummy(gets);
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,player->objectName());
			room->obtainCard(player,&dummy,reason);
		}
		return false;
	}
};

class Qinbao : public TriggerSkill
{
public:
	Qinbao(): TriggerSkill("qinbao")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("Slash")|| use.card->isNDTrick()){
			LogMessage log;
			int hand = player->getHandcardNum();
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getHandcardNum()< hand)continue;
				use.no_respond_list << p->objectName();
				log.to << p;
			}
			if(log.to.isEmpty())return false;

			log.type = "#FuqiNoResponse";
			log.from = player;
			log.arg = objectName();
			log.card_str = use.card->toString();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

KanjiCard::KanjiCard()
{
	target_fixed = true;
}

void KanjiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	if(source->isKongcheng())return;
	room->showAllCards(source);
	QList<Card::Suit> suits;
	foreach(const Card*c,source->getHandcards()){
		Card::Suit suit = c->getSuit();
		if(suits.contains(suit))
			return;
		suits << suit;
	}
	source->drawCards(2,"kanji");
}

class KanjiVS : public ZeroCardViewAsSkill
{
public:
	KanjiVS(): ZeroCardViewAsSkill("kanji")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->isKongcheng()&& player->usedTimes("KanjiCard")< 2;
	}

	const Card*viewAs()const
	{
		return new KanjiCard;
	}
};

class Kanji : public TriggerSkill
{
public:
	Kanji(): TriggerSkill("kanji")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new KanjiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			if(!room->hasCurrent())return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to != player || move.to_place != Player::PlaceHand || move.reason.m_reason != CardMoveReason::S_REASON_DRAW ||
					move.reason.m_skillName != objectName()|| !move.from_places.contains(Player::DrawPile))return false;
			QList<int> hands = player->handCards(),ids = move.card_ids;
			QList<Card::Suit> suits,old_suits;
			foreach(int id,hands){
				Card::Suit suit = Sanguosha->getCard(id)->getSuit();
				if(!suits.contains(suit))
					suits << suit;
				if(ids.contains(id))continue;
				if(old_suits.contains(suit))continue;
				old_suits << suit;
			}
			if(suits.length()>= 4&& old_suits.length()< 4)
				room->setPlayerMark(player,"&kanji_skip-Clear",1);
		} else {
			if(data.value<PhaseChangeStruct>().to != Player::Discard)return false;
			if(player->getMark("&kanji_skip-Clear")<= 0)return false;
			if(player->isSkipped(Player::Discard))return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->skip(Player::Discard);
		}
		return false;
	}
};

TenyearGueCard::TenyearGueCard()
{
}

bool TenyearGueCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		Card*c = Sanguosha->cloneCard(user_string.split("+").first());
		if(c){
			c->setSkillName("tenyeargue");
			c->deleteLater();
			if(c->targetFixed())
				return c->isAvailable(Self);
		}
		return c&& c->targetFilter(targets,to_select,Self);
	}

	Slash*slash = new Slash(Card::NoSuit,0);
	slash->deleteLater();
	slash->setSkillName("tenyeargue");
	return slash&& slash->targetFilter(targets,to_select,Self);
}

bool TenyearGueCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		Card*c = Sanguosha->cloneCard(user_string.split("+").first());
		if(c){
			c->setSkillName("tenyeargue");
			c->deleteLater();
		}
		return c&& c->targetsFeasible(targets,Self);
	}

	Slash*slash = new Slash(Card::NoSuit,0);
	slash->deleteLater();
	slash->setSkillName("tenyeargue");
	return slash&& slash->targetsFeasible(targets,Self);
}

const Card*TenyearGueCard::validate(CardUseStruct&cardUse)const
{
	Room*room = cardUse.from->getRoom();
	if(room->hasCurrent())
		room->addPlayerMark(cardUse.from,"tenyeargue_used-Clear");

	if(cardUse.from->isKongcheng())return nullptr;

	QString str = user_string;
	if((user_string.contains("Slash")|| user_string.contains("slash"))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		QStringList slashs = Sanguosha->getSlashNames();
		if(slashs.isEmpty())slashs << "slash";
		str = room->askForChoice(cardUse.from,"tenyeargue_slash",slashs.join("+"));
	}

	room->showAllCards(cardUse.from);
	int num = 0;
	foreach(const Card*c,cardUse.from->getHandcards()){
		if(c->isKindOf("Slash")|| c->isKindOf("Jink")){
			num++;
			if(num > 1)
				return nullptr;
		}
	}

	Card*c = Sanguosha->cloneCard(str);
	if(!c)return nullptr;
	c->setSkillName("tenyeargue");
	c->deleteLater();
	if(cardUse.from->isLocked(c))return nullptr;
	return c;
}

const Card*TenyearGueCard::validateInResponse(ServerPlayer*user)const
{
	Room*room = user->getRoom();
	if(room->hasCurrent())
		room->addPlayerMark(user,"tenyeargue_used-Clear");

	if(user->isKongcheng())return nullptr;

	QString str = user_string;
	if(user_string.contains("Slash")|| user_string.contains("slash")){
		QStringList slashs = Sanguosha->getSlashNames();
		if(slashs.isEmpty())slashs << "slash";
		str = room->askForChoice(user,"tenyeargue_slash",slashs.join("+"));
	} else if(user_string.contains("Jink")|| user_string.contains("jink"))
		str = "jink";

	room->showAllCards(user);
	int num = 0;
	foreach(const Card*c,user->getHandcards()){
		if(c->isKindOf("Slash")|| c->isKindOf("Jink")){
			num++;
			if(num > 1)
				return nullptr;
		}
	}

	Card*c = Sanguosha->cloneCard(str);
	if(!c)return nullptr;
	c->setSkillName("tenyeargue");
	c->deleteLater();

	if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE){
		if(user->isCardLimited(c,Card::MethodResponse))
			return nullptr;
	} else {
		if(user->isLocked(c))
			return nullptr;
	}
	return c;
}

class TenyearGue : public ZeroCardViewAsSkill
{
public:
	TenyearGue(): ZeroCardViewAsSkill("tenyeargue")
	{
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance("tenyeargue",true,false);
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer")){
				return !player->isKongcheng()&& Slash::IsAvailable(player)
				&& player->getMark("tenyeargue_used-Clear")<= 0;
			}
		}
		return false;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer")){
				return !player->isKongcheng()&& player->getMark("tenyeargue_used-Clear")<= 0
				&&((pattern.contains("Jink")|| pattern.contains("jink"))|| pattern.contains("Slash")|| pattern.contains("slash"));
			}
		}
		return false;
	}

	const Card*viewAs()const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE
			||Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			TenyearGueCard*c = new TenyearGueCard;
			c->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
			return c;
		}

		const Card*c = Self->getTag("tenyeargue").value<const Card*>();
		if(c&&c->isAvailable(Self)){
			TenyearGueCard*cc = new TenyearGueCard;
			cc->setUserString(c->objectName());
			return cc;
		}
		return nullptr;
	}
};

class TenyearSigong : public TriggerSkill
{
public:
	TenyearSigong(): TriggerSkill("tenyearsigong")
	{
		events << EventPhaseChanging << DamageDone << CardEffect << ConfirmDamage;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(player->isDead()|| player->getMark("tenyearsigong_xiangying-Clear")<= 0)return false;
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;

			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->isDead())break;
				if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("tenyearsigongWuxiao_lun")> 0)continue;

				int x = p->getHandcardNum()-1;
				if(x==0)continue;
				if(x < 0){
					if(!p->askForSkillInvoke(this,player))continue;
					p->drawCards(-x,objectName());
				} else if(x>0){
					QString prompt = QString("@tenyearsigong-discard:%1:%2").arg(player->objectName()).arg(x);
					const Card*c = room->askForDiscard(p,objectName(),x,x,true,false,prompt,".",objectName());
					if(!c)continue;
				}
				p->peiyin(this);

				Slash*slash = new Slash(Card::NoSuit,0);
				slash->setSkillName("_tenyearsigong");
				room->setCardFlag(slash,"tenyearsigong_useFrom_" + p->objectName());
				room->setCardFlag(slash,"tenyearsigong_used_slash");
				if(x > 1)
					room->setCardFlag(slash,"tenyearsigong_jinkNum_" + QString::number(x));
				if(p->canSlash(player,slash,false))
					room->useCard(CardUseStruct(slash,p,player));
				slash->deleteLater();
			}
		} else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash"))return false;
			if(damage.card->getSkillNames().contains(objectName())){
				foreach(QString flag,damage.card->getFlags()){
					if(!flag.startsWith("tenyearsigong_useFrom_"))continue;
					QStringList flags = flag.split("_");
					if(flags.length()!= 3)continue;
					ServerPlayer*from = room->findChild<ServerPlayer*>(flags.last());
					if(from&& from->isAlive())
						from->addMark("tenyearsigongWuxiao_lun");
					break;
				}
			}
		} else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash"))return false;
			if(damage.card->getSkillNames().contains(objectName())){
				damage.damage++;
				data = QVariant::fromValue(damage);
			}
		} else if(event==CardEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(!effect.card->isKindOf("Slash"))return false;
			foreach(QString flag,effect.card->getFlags()){
				if(!flag.startsWith("tenyearsigong_jinkNum_"))continue;
				QStringList flags = flag.split("_");
				if(flags.length()!= 3)continue;
				bool ok;
				int x = flags.last().toInt(&ok);
				if(ok){
					effect.offset_num = x;
					data = QVariant::fromValue(effect);
				}
				break;
			}
		}
		return false;
	}
};

class TenyearXuewei : public PhaseChangeSkill
{
public:
	TenyearXuewei(): PhaseChangeSkill("tenyearxuewei")
	{
		waked_skills = "#tenyearxuewei";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		QList<ServerPlayer*> targets;
		int hp = player->getHp();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->getHp()<= hp)
				targets << p;
		}
		if(targets.isEmpty())return false;
		ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@tenyearxuewei-invoke",true,true);
		if(!t)return false;
		player->peiyin(this);
		room->addPlayerMark(t,"&tenyearxuewei_buff+#" + player->objectName());
		return false;
	}

};

class TenyearXueweiEffect : public TriggerSkill
{
public:
	TenyearXueweiEffect(): TriggerSkill("#tenyearxuewei")
	{
		events << EventPhaseStart << DamageInflicted;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			foreach(ServerPlayer*p,room->getAllPlayers())
				room->setPlayerMark(p,"&tenyearxuewei_buff+#" + player->objectName(),0);
		} else {
			if(player->isDead())return false;
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->getMark("&tenyearxuewei_buff+#" + p->objectName())<= 0)continue;
				room->sendCompulsoryTriggerLog(p,"tenyearxuewei",true,true);

				LogMessage log;
				log.from = player;
				log.type = damage.from ? "#ZhengxuPrevent" : "#ZhengxuPrevent2";
				log.arg = QString::number(damage.damage);
				if(damage.from)
					log.to << damage.from;
				room->sendLog(log);

				room->loseHp(HpLostStruct(p,1,"tenyearxuewei",p));

				QList<ServerPlayer*> targets;
				targets << player << p;
				room->sortByActionOrder(targets);
				room->drawCards(targets,1,"tenyearxuewei");
				return true;
			}
		}
		return false;
	}
};

class TenyearYuguan : public TriggerSkill
{
public:
	TenyearYuguan(): TriggerSkill("tenyearyuguan")
	{
		events << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool isMostHpLostPlayer(ServerPlayer*player)const
	{
		Room*room = player->getRoom();
		int lost = player->getLostHp();
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getLostHp()> lost)
				return false;
		}
		return true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName())|| p->getLostHp()<= 0 || !isMostHpLostPlayer(p)|| p->getMaxHp()<= 0)continue;
			if(!p->askForSkillInvoke(this))continue;
			p->peiyin(this);
			room->loseMaxHp(p,1,objectName());
			if(p->isDead())continue;

			int lost = p->getLostHp();
			if(lost <= 0 || room->alivePlayerCount()< lost)continue;

			QList<ServerPlayer*> targets;
			if(room->alivePlayerCount()== lost)
				targets = room->getAllPlayers();
			else
				targets = room->askForPlayersChosen(p,room->getAllPlayers(),objectName(),-1,lost,"@tenyearyuguan-target:" + QString::number(lost));
			if(targets.isEmpty())continue;

			foreach(ServerPlayer*t,targets)
				room->doAnimate(1,p->objectName(),t->objectName());
			foreach(ServerPlayer*t,targets)
				t->drawCards(t->getMaxHp()- t->getHandcardNum(),objectName());
		}
		return false;
	}
};

TenyearQuanjianCard::TenyearQuanjianCard()
{
	mute = true;
}

bool TenyearQuanjianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	QString choice = Self->getTag("tenyearquanjian").toString();
	if(choice=="card")
		return targets.isEmpty()&& to_select != Self;
	else {
		if(targets.isEmpty())
			return to_select != Self;
		else if(targets.length()== 1)
			return targets.first()->inMyAttackRange(to_select);
	}
	return false;
}

bool TenyearQuanjianCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	QString choice = Self->getTag("tenyearquanjian").toString();
	if(choice=="card")
		return targets.length()== 1&& targets.first()!= Self;
	else
		return targets.length()== 2;
	return false;
}

void TenyearQuanjianCard::onUse(Room*room,CardUseStruct&use)const
{
	QVariant data = QVariant::fromValue(use);
	room->setTag("TenyearQuanjianCard",data);
	SkillCard::onUse(room,use);
}

void TenyearQuanjianCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QString choice = user_string;
	room->addPlayerMark(source,"tenyearquanjian_tiansuan_remove_" + choice + "-PlayClear");
	CardUseStruct use = room->getTag("TenyearQuanjianCard").value<CardUseStruct>();
	ServerPlayer*first = use.to.first();

	if(choice=="card"){
		source->peiyin("tenyearquanjian",2);
		int hand = first->getHandcardNum(),max = first->getMaxCards();
		if(hand > max){
			if(room->askForDiscard(first,"tenyearquanjian",hand - max,hand - max,true,false,"@tenyearquanjian-discard:" + QString::number(hand - max)))
				room->addPlayerMark(first,"tenyearquanjianLimit-Clear");
			else
				room->addPlayerMark(first,"&tenyearquanjian_debuff-Clear");
		} else {
			max = qMin(max,5);
			if(hand < max){
				if(first->askForSkillInvoke("tenyearquanjian","draw:" + QString::number(max - hand),false)){
					first->drawCards(max - hand,"tenyearquanjian");
					room->addPlayerMark(first,"tenyearquanjianLimit-Clear");
				} else
					room->addPlayerMark(first,"&tenyearquanjian_debuff-Clear");
			} else {
				if(first->askForSkillInvoke("tenyearquanjian","limit",false))
					room->addPlayerMark(first,"tenyearquanjianLimit-Clear");
				else
					room->addPlayerMark(first,"&tenyearquanjian_debuff-Clear");
			}
		}
	} else if(choice=="damage"){
		ServerPlayer*last = use.to.last();
		source->peiyin("tenyearquanjian",1);
		if(first->askForSkillInvoke("tenyearquanjian","dodamage:" + last->objectName(),false))
			room->damage(DamageStruct("tenyearquanjian",first,last));
		else
			room->addPlayerMark(first,"&tenyearquanjian_debuff-Clear");
	}
}

class TenyearQuanjianVS : public ZeroCardViewAsSkill
{
public:
	TenyearQuanjianVS(): ZeroCardViewAsSkill("tenyearquanjian")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("tenyearquanjian_tiansuan_remove_damage-PlayClear")<= 0
			|| player->getMark("tenyearquanjian_tiansuan_remove_card-PlayClear")<= 0;
	}

	const Card*viewAs()const
	{
		QString choice = Self->getTag("tenyearquanjian").toString();
		TenyearQuanjianCard*c = new TenyearQuanjianCard;
		c->setUserString(choice);
		return c;
	}
};

class TenyearQuanjian : public TriggerSkill
{
public:
	TenyearQuanjian(): TriggerSkill("tenyearquanjian")
	{
		events << DamageInflicted;
		view_as_skill = new TenyearQuanjianVS;
		waked_skills = "#tenyearquanjian-limit";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getMark("&tenyearquanjian_debuff-Clear")> 0;
	}

	QDialog*getDialog()const
	{
		return TiansuanDialog::getInstance("tenyearquanjian","damage,card");
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		int mark = player->getMark("&tenyearquanjian_debuff-Clear");
		room->setPlayerMark(player,"&tenyearquanjian_debuff-Clear",0);

		LogMessage log;
		log.type = "#YHHankaiDamaged";
		log.from = player;
		log.arg = objectName();
		log.arg2 = QString::number(damage.damage);
		log.arg3 = QString::number(damage.damage += mark);
		room->sendLog(log);

		data = QVariant::fromValue(damage);
		return false;
	}
};

class TenyearQuanjianLimit : public CardLimitSkill
{
public:
	TenyearQuanjianLimit(): CardLimitSkill("#tenyearquanjian-limit")
	{
	}

	QString limitList(const Player*)const
	{
		return "use";
	}

	QString limitPattern(const Player*target)const
	{
		if(target->getMark("tenyearquanjianLimit-Clear")> 0)
			return ".|.|.|hand";
		return "";
	}
};

class TenyearTujue : public TriggerSkill
{
public:
	TenyearTujue(): TriggerSkill("tenyeartujue")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@tenyeartujueMark";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if(dying.who != player || player->isNude())return false;
		if(player->getMark("@tenyeartujueMark")<= 0)return false;

		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@tenyeartujue-give",true,true);
		if(!t)return false;

		player->peiyin(this);
		room->removePlayerMark(player,"@tenyeartujueMark");
		room->doSuperLightbox(player,"tenyeartujue");

		QList<int> give = player->handCards()+ player->getEquipsId();
		room->giveCard(player,t,give,objectName());

		room->recover(player,RecoverStruct(player,nullptr,qMin(give.length(),player->getMaxHp())- player->getHp(),"tenyeartujue"));
		player->drawCards(give.length(),objectName());
		return false;
	}
};

class Zhubi : public TriggerSkill
{
public:
	Zhubi(): TriggerSkill("zhubi")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::DiscardPile)return false;
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			foreach(int id,move.card_ids){
				if(Sanguosha->getCard(id)->getSuit()== Card::Diamond){
					if(!player->askForSkillInvoke(this))return false;
					player->peiyin(this);

					int ex = -1;
					QList<int> ids;
					foreach(int id,room->getDrawPile()){
						if(Sanguosha->getCard(id)->isKindOf("ExNihilo"))
							ids << id;
					}
					if(!ids.isEmpty())
						ex = ids.at(qrand()% ids.length());

					if(ex > 0){
						int n = 1;
						foreach(int id,room->getDrawPile()){
							if(id==ex)break;
							n++;
						}
						QList<int> ncards,exs;
						if(n > 1)ncards = room->getNCards(n - 1,false);
						exs = room->getNCards(1,false);
						if(!ncards.isEmpty())
							room->returnToTopDrawPile(ncards);
						room->returnToTopDrawPile(exs);

						LogMessage log;
						log.type = "$YinshicaiPut";
						log.from = player;
						log.card_str = QString::number(ex);
						room->sendLog(log);
						return false;
					}

					ids.clear();
					foreach(int id,room->getDiscardPile()){
						if(Sanguosha->getCard(id)->isKindOf("ExNihilo"))
							ids << id;
					}
					if(!ids.isEmpty())
						ex = ids.at(qrand()% ids.length());
					else {
						LogMessage log;
						log.type = "#ZhubiNoExNihilo";
						log.arg = "ex_nihilo";
						room->sendLog(log);
						return false;
					}

					if(ex > 0){
						LogMessage log;
						log.type = "$YinshicaiPut";
						log.from = player;
						log.card_str = QString::number(ex);
						room->sendLog(log);

						CardMoveReason reason(CardMoveReason::S_REASON_PUT,player->objectName(),objectName(),"");
						room->moveCardTo(Sanguosha->getCard(ex),nullptr,Player::DrawPile,reason,true);
					}
					return false;
				}
			}
		}
		return false;
	}
};

class Liuzhuan : public ProhibitSkill
{
public:
	Liuzhuan(): ProhibitSkill("liuzhuan")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		if(from!=to&&!card->isKindOf("SkillCard")&&from->hasFlag("CurrentPlayer")&&to->hasSkill(objectName())){
			QList<int> subcards;
			if(card->isVirtualCard())subcards = card->getSubcards();
			else subcards << card->getEffectiveId();
			foreach(int id,subcards){
				if(from->getMark(QString::number(id)+"liuzhuan-Clear")>0)
					return true;
			}
		}
		return false;
	}
};

class Liuzhuanbf : public TriggerSkill
{
public:
	Liuzhuanbf(): TriggerSkill("#liuzhuan")
	{
		events << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();

		if(!room->getTag("FirstRound").toBool()&&move.to_place==Player::PlaceHand&&move.to!=player
			&&move.to->hasFlag("CurrentPlayer")&&move.to->getPhase()!=Player::Draw){
			foreach(int id,move.card_ids){
				room->setCardTip(id,"liuzhuan-Clear");
				room->addPlayerMark((ServerPlayer*)move.to,QString::number(id)+"liuzhuan-Clear");
			}
		}
		if(move.to_place==Player::DiscardPile&& move.from != player){
			DummyCard*dummy = new DummyCard();
			ServerPlayer*cp = room->getCurrent();
			foreach(int id,move.card_ids){
				if(cp->getMark(QString::number(id)+"liuzhuan-Clear")>0&& room->getCardPlace(id)==move.to_place){
					room->setPlayerMark(cp,QString::number(id)+"liuzhuan-Clear",0);
					dummy->addSubcard(id);
				}
			}
			dummy->deleteLater();
			if(dummy->subcardsLength()<1||cp==player||!player->hasSkill("liuzhuan"))return false;
			room->sendCompulsoryTriggerLog(player,"liuzhuan");
			room->obtainCard(player,dummy);
		}
		return false;
	}
};

class TenyearDeshao : public TriggerSkill
{
public:
	TenyearDeshao(): TriggerSkill("tenyeardeshao")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card || use.card->isKindOf("SkillCard")|| !use.card->isBlack()|| !use.to.contains(player))return false;
		if(!use.from || use.from==player)return false;
		if(player->getMark("tenyeardeshao_used-Clear")>= 2 || !player->askForSkillInvoke(this,use.from))return false;
		room->addPlayerMark(player,"tenyeardeshao_used-Clear");
		player->peiyin(this);
		player->drawCards(1,objectName());
		if(player->isAlive()&& use.from->isAlive()&& player->canDiscard(use.from,"he")&& use.from->getHandcardNum()>= player->getHandcardNum()){
			int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodDiscard);
			room->throwCard(id,objectName(),use.from,player);
		}
		return false;
	}
};

class TenyearMingfa : public TriggerSkill
{
public:
	TenyearMingfa(): TriggerSkill("tenyearmingfa")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play || player->getMark("tenyearmingfa_used-PlayClear")> 0)return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isVirtualCard(true))return false;
		if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
		if(!room->CardInPlace(use.card,Player::DiscardPile))return false;
		if(!player->getPile("tenyearmingfa").isEmpty())return false;
		if(!player->askForSkillInvoke(this,"tenyearmingfa:" + use.card->objectName()))return false;
		player->peiyin(this);

		room->addPlayerMark(player,"tenyearmingfa_used-PlayClear");
		player->addToPile(objectName(),use.card);

		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@tenyearmingfa-target");
		room->addPlayerMark(player,"tenyearmingfa_used-PlayClear");
		room->doAnimate(1,player->objectName(),t->objectName());

		foreach(ServerPlayer*p,room->getOtherPlayers(player))
			room->setPlayerMark(p,"&tenyearmingfa+#" + player->objectName(),0);
		room->setPlayerMark(t,"&tenyearmingfa+#" + player->objectName(),1);

		player->setTag("TenyearMingfaTarget", t->objectName());
		return false;
	}
};

class TenyearMingfaEffect : public TriggerSkill
{
public:
	TenyearMingfaEffect(): TriggerSkill("#tenyearmingfa")
	{
		events << EventPhaseChanging << Death;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != player)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getTag("TenyearMingfaTarget").toString()!= player->objectName())continue;
				p->removeTag("TenyearMingfaTarget");
				QList<int> mingfa = p->getPile("tenyearmingfa");
				if(mingfa.isEmpty())continue;
				room->sendCompulsoryTriggerLog(p,"tenyearmingfa",true,true);
				DummyCard*dummy = new DummyCard(mingfa);
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,p->objectName(),"tenyearmingfa","");
				room->throwCard(dummy,reason,nullptr);
				delete dummy;
			}
			return false;
		}
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			room->setPlayerMark(player,"&tenyearmingfa+#" + p->objectName(),0);
			if(p->getTag("TenyearMingfaTarget").toString()!= player->objectName())continue;
			p->removeTag("TenyearMingfaTarget");
			QList<int> mingfa = p->getPile("tenyearmingfa");
			if(mingfa.isEmpty())continue;
			room->sendCompulsoryTriggerLog(p,"tenyearmingfa",true,true);

			const Card*card = Sanguosha->getCard(mingfa.first());
			Card*c = Sanguosha->cloneCard(card->objectName());
			c->setSkillName("_tenyearmingfa");
			c->deleteLater();
			int hand = player->getHandcardNum();
			hand = qMin(hand,5);
			hand = qMax(hand,1);
			for(int i = 0; i < hand; i++){
				if(p->isDead()|| player->isDead()|| !p->canUse(c,player,true))break;
				room->useCard(CardUseStruct(c,p,player));
			}
			mingfa = p->getPile("tenyearmingfa");
			if(mingfa.isEmpty())continue;
			DummyCard*dummy = new DummyCard(mingfa);
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,p->objectName(),"tenyearmingfa","");
			room->throwCard(dummy,reason,nullptr);
			delete dummy;
		}
		return false;
	}
};

class Kuangcai : public PhaseChangeSkill
{
public:
	Kuangcai(): PhaseChangeSkill("kuangcai")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::Discard){
			LogMessage log;
			log.type = "#KuangcaiMaxCards";
			log.from = player;
			log.arg = objectName();
			if(player->getMark("tenyearjingce-Clear")<= 0){
				log.arg2 = "kuangcaiadd";
				room->sendLog(log);
				player->peiyin(objectName());
				room->notifySkillInvoked(player,objectName());
				room->addMaxCards(player,1,false);
			} else {
				if(player->getMark("damage_point_round")<= 0){
					log.arg2 = "kuangcaireduce";
					room->sendLog(log);
					player->peiyin(objectName());
					room->notifySkillInvoked(player,objectName());
					room->addMaxCards(player,-1,false);
				}
			}
		} else if(player->getPhase()== Player::Finish){
			int damage = player->getMark("damage_point_round");
			if(damage > 0){
				room->sendCompulsoryTriggerLog(player,this);
				player->drawCards(qMin(5,damage),objectName());
			}
		}
		return false;
	}
};

class KuangcaiTarget : public TargetModSkill
{
public:
	KuangcaiTarget(): TargetModSkill("#kuangcai")
	{
		pattern = ".";
	}

	int getDistanceLimit(const Player*from,const Card*,const Player*)const
	{
		if(from->hasFlag("CurrentPlayer")&&from->hasSkill("kuangcai"))
			return 999;
		return 0;
	}

	int getResidueNum(const Player*from,const Card*,const Player*)const
	{
		if(from->hasFlag("CurrentPlayer")&&from->hasSkill("kuangcai"))
			return 999;
		return 0;
	}
};

class Shejian : public TriggerSkill
{
public:
	Shejian(): TriggerSkill("shejian")
	{
		events  << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("SkillCard")|| !use.from || use.from==player || player->getMark("shejian-Clear")> 2 || !use.to.contains(player)
		|| player->hasFlag("Global_Dying")|| !player->canDiscard(player,"h")|| player->getHandcardNum()< 2)return false;
		const Card*card = room->askForDiscard(player,objectName()+"$-1",99,2,true,false,"@shejian:" + use.from->objectName(),".",objectName());
		if(!card)return false;
		player->addMark("shejian-Clear");
		QStringList choices;
		if(player->canDiscard(use.from,"he"))
			choices << "discard=" + use.from->objectName()+ "=" + QString::number(card->subcardsLength());
		choices << "damage=" + use.from->objectName();
		if(room->askForChoice(player,objectName(),choices.join("+"),data).startsWith("damage"))
			room->damage(DamageStruct(objectName(),player,use.from));
		else {
			QList<int> cards;
			for(int i = 0; i < card->subcardsLength(); ++i){
				if(use.from->getCardCount()<=i)break;
				int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodDiscard,cards);
				if(id<0)break;
				cards << id;
			}
			room->throwCard(cards,objectName(),use.from,player);
		}
		return false;
	}
};

class Yusui : public TriggerSkill
{
public:
	Yusui(): TriggerSkill("yusui")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.from || use.from==player || !use.to.contains(player)|| !use.card->isBlack()|| player->getMark("yusui-Clear")> 0)return false;

		if(!player->askForSkillInvoke(this,use.from))return false;
		player->peiyin(this);
		room->loseHp(HpLostStruct(player,1,objectName(),player));

		if(player->isDead())return false;

		int handf = use.from->getHandcardNum(),hand = player->getHandcardNum();
		int hpf = use.from->getHp(),hp = player->getHp();
		QStringList choices;
		if(handf > hand)
			choices << "discard=" + use.from->objectName()+ "=" + QString::number(hand);
		if(hpf > hp)
			choices << "hp=" + use.from->objectName()+ "=" + QString::number(hp);
		if(choices.isEmpty())return false;

		QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(use.from));
		room->addPlayerMark(player,"yusui-Clear");

		if(choice.startsWith("discard")){
			int dis = use.from->getHandcardNum()- player->getHandcardNum();
			if(dis > 0)room->askForDiscard(use.from,objectName(),dis,dis);
		} else if(choice.startsWith("hp")){
			int lose = use.from->getHp()- player->getHp();
			if(lose > 0)room->loseHp(HpLostStruct(use.from,lose,objectName(),player));
		}
		return false;
	}
};

BoyanCard::BoyanCard()
{
}

void BoyanCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	int draw = qMin(5,to->getMaxHp())- to->getHandcardNum();
	if(draw > 0)to->drawCards(draw,"boyan");
	room->addPlayerMark(to,"boyan-Clear");
}

class Boyan : public ZeroCardViewAsSkill
{
public:
	Boyan():ZeroCardViewAsSkill("boyan")
	{
	}

	const Card*viewAs()const
	{
		return new BoyanCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("BoyanCard");
	}
};

class BoyanLimit : public CardLimitSkill
{
public:
	BoyanLimit(): CardLimitSkill("#boyan-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player*)const
	{
		return "use,response";
	}

	QString limitPattern(const Player*target)const
	{
		if(target->getMark("boyan-Clear")> 0)
			return ".|.|.|hand";
		return "";
	}
};

JianliangCard::JianliangCard()
{
}

bool JianliangCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.length()< 2;
}

void JianliangCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		if(p->isAlive()){
			room->cardEffect(this,source,p);
		}
	}
}

void JianliangCard::onEffect(CardEffectStruct&effect)const
{
	effect.to->drawCards(1,"jianliang");
}

class JianliangVS : public ZeroCardViewAsSkill
{
public:
	JianliangVS():ZeroCardViewAsSkill("jianliang")
	{
		response_pattern = "@@jianliang";
	}

	const Card*viewAs()const
	{
		return new JianliangCard;
	}
};

class Jianliang : public PhaseChangeSkill
{
public:
	Jianliang(): PhaseChangeSkill("jianliang")
	{
		view_as_skill = new JianliangVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
	if(player->getPhase()!= Player::Draw)return false;
	int hand = player->getHandcardNum();
	foreach(ServerPlayer*p,room->getAlivePlayers()){
		if(p->getHandcardNum()> hand){
			room->askForUseCard(player,"@@jianliang","@jianliang",-1,Card::MethodNone);
			break;
		}
	}
	return false;
	}
};

WeimengCard::WeimengCard()
{
}

bool WeimengCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select != Self&& !to_select->isKongcheng();
}

void WeimengCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	if(to->isKongcheng()|| from->getHp()<= 0)return;

	Room*room = from->getRoom();
	QList<int> gets;

	for(int i = 0; i < from->getHp(); i++){
		if(to->getHandcardNum()<=i)break;
		int id = room->askForCardChosen(from,to,"h","weimeng",false,Card::MethodNone,gets,i > 0);
		if(id < 0)break;
		gets << id;
	}

	DummyCard dummy(gets);
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,from->objectName());
	room->obtainCard(from,&dummy,reason,false);

	if(from->isNude()|| from->isDead()|| to->isDead())return;

	int all_get = 0,get = gets.length(),all_give = 0;
	foreach(int id,gets)
		all_get += Sanguosha->getCard(id)->getNumber();

	QString prompt = "@weimeng:" + to->objectName()+ ":" + QString::number(get)+ ":" + QString::number(all_get);
	const Card*ex = room->askForExchange(from,"weimeng",get,get,true,prompt);
	room->giveCard(from,to,ex,"weimeng");
	foreach(int id,ex->getSubcards())
		all_give += Sanguosha->getCard(id)->getNumber();

	if(all_give > all_get)
		from->drawCards(1,"weimeng");
	else if(all_give < all_get){
		if(from->isDead()|| to->isDead()|| !from->canDiscard(to,"hej"))return;
		int id = room->askForCardChosen(from,to,"hej","weimeng",false,Card::MethodDiscard);
		room->throwCard(id,"weimeng",to,from);
	}
}

class Weimeng : public ZeroCardViewAsSkill
{
public:
	Weimeng():ZeroCardViewAsSkill("weimeng")
	{
	}

	const Card*viewAs()const
	{
		return new WeimengCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getHp()> 0&& !player->hasUsed("WeimengCard");
	}
};

TenyearFenglveCard::TenyearFenglveCard()
{
}

bool TenyearFenglveCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& Self->canPindian(to_select);
}

void TenyearFenglveCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*source = effect.from,*target = effect.to;
	if(!source->canPindian(target,false))return;

	PindianStruct*pindian = source->PinDian(target,"fenglve");
	if(!pindian)return;

	Room*room = source->getRoom();
	if(pindian->success){
		if(target->isDead()|| target->isAllNude())return;
		if(target->getCardCount(true,true)<= 2){
			DummyCard*dummy = new DummyCard(target->getJudgingAreaID()+ target->handCards()+ target->getEquipsId());
			dummy->deleteLater();
			room->giveCard(target,source,dummy,"tenyearfenglve");
		} else {
			QList<int> judge_ids = target->getJudgingAreaID();

			if(!judge_ids.isEmpty())
				room->notifyMoveToPile(target,judge_ids,"tenyearfenglve",Player::PlaceDelayedTrick,true);

			const Card*c = room->askForUseCard(target,"@@tenyearfenglve!","@tenyearfenglve:" + source->objectName());

			if(!judge_ids.isEmpty())
				room->notifyMoveToPile(target,judge_ids,"tenyearfenglve",Player::PlaceDelayedTrick,false);

			DummyCard*dummy = new DummyCard;
			dummy->deleteLater();

			if(!c){
				QList<int> ids = target->getJudgingAreaID()+ target->handCards()+ target->getEquipsId();
				for(int i = 0; i < 2; i++){
					int id = ids.at(qrand()% ids.length());
					ids.removeOne(id);
					dummy->addSubcard(id);
				}
			} else
				dummy->addSubcards(c->getSubcards());

			if(dummy->subcardsLength()> 0)
			room->giveCard(target,source,dummy,"tenyearfenglve");
		}
	} else if(pindian->from_number==pindian->to_number){
		LogMessage log;
		log.type = "#TenyearFenglvePingju";
		log.from = source;
		log.to << target;
		log.arg = "tenyearfenglve";
		room->sendLog(log);
		int used = source->usedTimes("TenyearFenglveCard");
		if(used==0)return;
		room->addPlayerHistory(source,"TenyearFenglveCard",-used);
	} else {
		if(target->isAlive()&& room->CardInPlace(pindian->from_card,Player::DiscardPile))
			room->obtainCard(target,pindian->from_card);
	}
}

TenyearFenglveGiveCard::TenyearFenglveGiveCard()
{
	handling_method = Card::MethodNone;
	mute = true;
	will_throw = false;
	target_fixed = true;
}

void TenyearFenglveGiveCard::onUse(Room*,CardUseStruct&)const
{
}

class TenyearFenglve : public ViewAsSkill
{
public:
	TenyearFenglve(): ViewAsSkill("tenyearfenglve")
	{
		expand_pile = "#tenyearfenglve";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			return false;
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@tenyearfenglve!")
				return selected.length()< 2;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearFenglveCard")&& player->canPindian();
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@tenyearfenglve!";
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			if(!cards.isEmpty())return nullptr;
			return new TenyearFenglveCard;
		} else {
			if(cards.length()!= 2)return nullptr;
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@tenyearfenglve!"){
				TenyearFenglveGiveCard*c = new TenyearFenglveGiveCard;
				c->addSubcards(cards);
				return c;
			}
		}
		return nullptr;
	}
};

class Anyong : public TriggerSkill
{
public:
	Anyong():TriggerSkill("anyong")
	{
		events << Damage << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==EventPhaseChanging)
			return 0;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage != 1||player->getMark("anyong_damage-Keep")!= 1)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName())|| !p->canDiscard(p,"he"))continue;
				if(!room->askForCard(p,"..","@anyong-discard:" + damage.to->objectName(),data,objectName()))continue;
				room->broadcastSkillInvoke(this);
				room->damage(DamageStruct(objectName(),p,damage.to));
				if(damage.to->isDead())break;
			}
		} else {
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getAlivePlayers())
				p->setMark("anyong_damage-Keep",0);
		}
		return false;
	}
};

class AnyongRecord : public TriggerSkill
{
public:
	AnyongRecord():TriggerSkill("#anyong")
	{
		events << Damage;
		global = true;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool trigger(TriggerEvent,Room*,ServerPlayer*player,QVariant&)const
	{
		if(player->hasFlag("CurrentPlayer"))
			player->addMark("anyong_damage-Keep");
		return false;
	}
};

class Wanggui : public TriggerSkill
{
public:
	Wanggui(): TriggerSkill("wanggui")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==Damage){
			if(!room->hasCurrent()|| player->getMark("wanggui-Clear")> 0)return false;
			QList<ServerPlayer*> targets;
			QString kingdom = player->getKingdom();
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->getKingdom()!= kingdom)
					targets << p;
			}
			if(targets.isEmpty())return false;
			ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@wanggui-damage",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke(this,1);
			room->addPlayerMark(player,"wanggui-Clear");
			room->damage(DamageStruct("wanggui",player,target));
		} else {
			QList<ServerPlayer*> targets;
			QString kingdom = player->getKingdom();
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->getKingdom()== kingdom)
					targets << p;
			}
			if(targets.isEmpty())return false;
			ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@wanggui-draw",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke(this,2);
			target->drawCards(1,objectName());
			if(target != player)
				player->drawCards(1,objectName());
		}
		return false;
	}
};

class Xibing : public TriggerSkill
{
public:
	Xibing(): TriggerSkill("xibing")
	{
		events << TargetSpecified;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play&& target->getRoom()->hasCurrent();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isBlack()|| use.to.length()!= 1)return false;
		if(use.card->isKindOf("Slash")|| use.card->isNDTrick()){
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->isDead())break;
				if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("xibing-Clear")> 0 || !p->askForSkillInvoke(this,player))continue;
				room->broadcastSkillInvoke(this);
				room->addPlayerMark(p,"xibing-Clear");
				int draw_num = player->getHp()- player->getHandcardNum();
				if(draw_num <= 0)continue;
				player->drawCards(draw_num,objectName());
				room->setPlayerCardLimitation(player,"use",".",true);
			}
		}
		return false;
	}
};

class Zhente : public TriggerSkill
{
public:
	Zhente(): TriggerSkill("zhente")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("zhente-Clear")> 0)return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("BasicCard")&& !use.card->isNDTrick())return false;
		if(!use.to.contains(player)||(!use.card->isRed()&& !use.card->isBlack())|| !use.from || use.from->isDead()|| use.from==player)return false;

		player->setTag("zhente_data", data);
		bool invoke = player->askForSkillInvoke(this,use.from);

		if(!invoke)return false;
		room->addPlayerMark(player,"zhente-Clear");
		room->broadcastSkillInvoke(objectName());

		QStringList choices;
		if(use.card->isRed())choices << "color=red";
		else choices << "color=black";
		choices << "wuxiao=" + player->objectName();
		use.from->setTag("zhente_usefrom_data", data);
		QString choice = room->askForChoice(use.from,objectName(),choices.join("+"),QVariant::fromValue(player));

		if(choice.startsWith("color")){
			QString pattern = use.card->isRed()? ".|red|.|." : ".|black|.|.";
			room->setPlayerCardLimitation(use.from,"use",pattern,true);
		} else {
			use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Zhiwei : public TriggerSkill
{
public:
	Zhiwei(): TriggerSkill("zhiwei")
	{
		events << GameStart << EventPhaseEnd << EventPhaseStart << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart){
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@zhiwei-invoke",false,true);
			room->broadcastSkillInvoke(objectName());
			room->setPlayerMark(target,"&zhiwei+#" + player->objectName(),1);
		} else if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->getMark("&zhiwei+#" + player->objectName())> 0)
					return false;
			}
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@zhiwei-invoke2",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke(objectName());
			room->setPlayerMark(target,"&zhiwei+#" + player->objectName(),1);
		} else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player || player->getPhase()!= Player::Discard)return false;
			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				QVariantList discard = player->getTag("ZhiweiDiscard").toList();
				foreach(int id,move.card_ids){
					if(discard.contains(QVariant(id)))continue;
					discard << id;
				}
				player->setTag("ZhiweiDiscard", discard);
			}
		} else {
			if(player->getPhase()!= Player::Discard)return false;
			QList<int> list;
			foreach(QVariant id,player->getTag("ZhiweiDiscard").toList()){
				int _id = id.toInt();
				if(!list.contains(_id)&& room->getCardPlace(_id)== Player::DiscardPile)
					list << _id;
			}
			player->removeTag("ZhiweiDiscard");
			if(list.isEmpty())return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getMark("&zhiwei+#" + player->objectName())<= 0)continue;
				room->sendCompulsoryTriggerLog(player,objectName(),true,true);
				DummyCard get(list);
				room->obtainCard(p,&get,true);
				break;
			}
		}
		return false;
	}
};

class ZhiweiEffect : public TriggerSkill
{
public:
	ZhiweiEffect(): TriggerSkill("#zhiwei")
	{
		events << Damage << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==Damage){
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->getMark("&zhiwei+#" + p->objectName())<= 0 || p->isDead()|| !p->hasSkill("zhiwei"))continue;
				room->sendCompulsoryTriggerLog(p,"zhiwei",true,true);
				p->drawCards(1,"zhiwei");
			}
		} else {
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->getMark("&zhiwei+#" + p->objectName())<= 0 || p->isDead()|| !p->hasSkill("zhiwei"))continue;

				QList<int> can_discard;
				foreach(int id,p->handCards()){
					if(!p->canDiscard(p,id))continue;
					can_discard << id;
				}
				if(can_discard.isEmpty())continue;

				room->sendCompulsoryTriggerLog(p,"zhiwei",true,true);
				int id = can_discard.at(qrand()% can_discard.length());
				room->throwCard(id,"zhiwei",p);
			}
		}
		return false;
	}
};

QiangzhiZHCard::QiangzhiZHCard()
{
}

bool QiangzhiZHCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(!targets.isEmpty()|| to_select==Self)return false;
	if(3 - subcardsLength()<= 0)return true;
	int can_dis = 0;
	foreach(int id,to_select->getEquipsId()){
		if(Self->canDiscard(to_select,id))
			can_dis++;
	}
	can_dis += to_select->getHandcardNum();
	return can_dis >= 3 - subcardsLength()&& Self->canDiscard(to_select,"he");
}

void QiangzhiZHCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	int dis_num = 3 - subcardsLength();
	if(dis_num <= 0){
		room->damage(DamageStruct("qiangzhizh",from,to));
		return;
	}

	QList<int> cards;
	for(int i = 0; i < dis_num; ++i){
		if(from->getCardCount()<=i)break;
		int id = room->askForCardChosen(from,to,"he","qiangzhizh",false,Card::MethodDiscard,cards);
		if(id<0)break;
		cards << id;
	}
	room->throwCard(cards,"qiangzhizh",to,from);

	if(cards.length()>= 3)
		room->damage(DamageStruct("qiangzhizh",to,from));
}

class QiangzhiZH : public ViewAsSkill
{
public:
	QiangzhiZH(): ViewAsSkill("qiangzhizh")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return selected.length()< 3&& !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		QiangzhiZHCard*c = new QiangzhiZHCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("QiangzhiZHCard");
	}
};

class Pitian : public TriggerSkill
{
public:
	Pitian(): TriggerSkill("pitian")
	{
		events << CardsMoveOneTime << Damaged << EventPhaseStart;
		waked_skills = "#pitian";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish)return false;
			if(player->getHandcardNum()>= player->getMaxCards())return false;
			if(!player->askForSkillInvoke(this))return false;
			player->peiyin(this);
			player->drawCards(qMin(5,player->getMaxCards()- player->getHandcardNum()),objectName());
			room->setPlayerMark(player,"&pitian_add",0);
		} else {
			if(event==CardsMoveOneTime){
				CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
				if(move.from != player)return false;
				if(!move.from_places.contains(Player::PlaceHand)&& !move.from_places.contains(Player::PlaceEquip))return false;
				if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)!= CardMoveReason::S_REASON_DISCARD)return false;
			}
			LogMessage log;
			log.type = "#PitianMaxCards";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());
			room->addPlayerMark(player,"&pitian_add");
		}
		return false;
	}
};

class PitianKeep : public MaxCardsSkill
{
public:
	PitianKeep(): MaxCardsSkill("#pitian")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player*target)const
	{
		return target->getMark("&pitian_add");
	}
};

XunliPutCard::XunliPutCard()
{
	mute = true;
	m_skillName = "xunli";
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void XunliPutCard::onUse(Room*,CardUseStruct&)const
{
}

XunliCard::XunliCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void XunliCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QList<int> pile = source->getPile("jpxlli");
	QList<int> to_handcard;
	QList<int> to_pile;
	foreach(int id,subcards){
		if(pile.contains(id))
			to_handcard << id;
		else
			to_pile << id;
	}

	if(to_handcard.length()!= to_pile.length())return;

	source->addToPile("jpxlli",to_pile);

	if(source->isDead())return;

	DummyCard to_handcard_x(to_handcard);
	CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE,source->objectName());
	room->obtainCard(source,&to_handcard_x,reason);
}

class XunliVS : public ViewAsSkill
{
public:
	XunliVS(): ViewAsSkill("xunli")
	{
		expand_pile = "jpxlli,#xunli";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@xunli2!"){
			int put = 9 - Self->getPile("jpxlli").length();
			return Self->getPile("#xunli").contains(to_select->getEffectiveId())&& selected.length()< put;
		} else if(pattern=="@@xunli1"){
			if(to_select->isEquipped())return false;
			if(selected.length()>= 2* Self->getPile("jpxlli").length())return false;
			if(Self->getHandcards().contains(to_select)&& to_select->isBlack())return true;
			if(Self->getPile("jpxlli").contains(to_select->getEffectiveId()))return true;
			return false;
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@xunli2!"){
			XunliPutCard*c = new XunliPutCard;
			c->addSubcards(cards);
			return c;
		} else if(pattern=="@@xunli1"){
			int hand = 0;
			int pile = 0;
			foreach(const Card*card,cards){
				if(Self->getHandcards().contains(card))
					hand++;
				else if(Self->getPile("jpxlli").contains(card->getEffectiveId()))
					pile++;
			}

			if(hand==pile){
				XunliCard*c = new XunliCard;
				c->addSubcards(cards);
				return c;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@xunli");
	}
};

class Xunli : public TriggerSkill
{
public:
	Xunli(): TriggerSkill("xunli")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		view_as_skill = new XunliVS;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			int put = 9 - player->getPile("jpxlli").length();
			if(put <= 0)return false;

			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				QList<int> blacks;
				foreach(int id,move.card_ids){
					if(room->getCardPlace(id)!= Player::DiscardPile)continue;
					if(!Sanguosha->getCard(id)->isBlack())continue;
					blacks << id;
				}
				if(blacks.isEmpty())return false;
				room->sendCompulsoryTriggerLog(player,this);

				if(blacks.length()<= put)
					player->addToPile("jpxlli",blacks);
				else {
					room->notifyMoveToPile(player,blacks,objectName(),Player::DiscardPile,true);
					const Card*card = room->askForUseCard(player,"@@xunli2!","@xunli2:" + QString::number(put),2,Card::MethodNone);
					room->notifyMoveToPile(player,blacks,objectName(),Player::DiscardPile,false);
					if(card)
					player->addToPile("jpxlli",card);
					else {
						QList<int> puts;
						for(int i = 0; i < put; i++){
							if(blacks.isEmpty())break;
							int id = blacks.at(qrand()% blacks.length());
							blacks.removeOne(id);
							puts << id;
						}
						if(!puts.isEmpty())
							player->addToPile("jpxlli",puts);
					}
				}
			}
		} else {
			if(player->getPhase()!= Player::Play || player->getPile("jpxlli").isEmpty()|| player->isKongcheng())return false;
			room->askForUseCard(player,"@@xunli1","@xunli1",1,Card::MethodNone);
		}
		return false;
	}
};

ZhishiCard::ZhishiCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ZhishiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,source->objectName(),"zhishi","");
	room->throwCard(this,reason,nullptr);
}

class ZhishiVS : public ViewAsSkill
{
public:
	ZhishiVS(): ViewAsSkill("zhishi")
	{
		expand_pile = "jpxlli";
	}

	bool viewFilter(const QList<const Card*>&,const Card*to_select)const
	{
		return Self->getPile("jpxlli").contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		ZhishiCard*c = new ZhishiCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@zhishi";
	}
};

class Zhishi : public TriggerSkill
{
public:
	Zhishi(): TriggerSkill("zhishi")
	{
		events << TargetConfirmed << Dying;
		view_as_skill = new ZhishiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& !target->getPile("jpxlli").isEmpty();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash"))return false;
			foreach(ServerPlayer*to,use.to){
				if(player->isDead()|| player->getPile("jpxlli").isEmpty())break;
				if(to->isDead()|| to->getMark("&zhishi+#" + player->objectName())<= 0)continue;
				player->setTag("ZhishiTarget", QVariant::fromValue(to));
				const Card*card = room->askForUseCard(player,"@@zhishi","@zhishi:" + to->objectName(),-1,Card::MethodNone);
				player->removeTag("ZhishiTarget");
				if(!card)continue;
				to->drawCards(card->subcardsLength(),objectName());
			}
		} else {
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who->isDead()|| dying.who->getMark("&zhishi+#" + player->objectName())<= 0)return false;
			player->setTag("ZhishiTarget", QVariant::fromValue(dying.who));
			const Card*card = room->askForUseCard(player,"@@zhishi","@zhishi:" + dying.who->objectName(),-1,Card::MethodNone);
			player->removeTag("ZhishiTarget");
			if(!card)return false;
			dying.who->drawCards(card->subcardsLength(),objectName());
		}
		return false;
	}
};

class ZhishiChoose : public PhaseChangeSkill
{
public:
	ZhishiChoose(): PhaseChangeSkill("#zhishi")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::Finish&&player->hasSkill("zhishi")){
			ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),"zhishi","@zhishi-invoke",true,true);
			if(t){
				player->peiyin("zhishi");
				room->setPlayerMark(t,"&zhishi+#" + player->objectName(),1);
			}
		}else if(player->getPhase()== Player::RoundStart){
			foreach(ServerPlayer*p,room->getAllPlayers())
				room->setPlayerMark(p,"&zhishi+#" + player->objectName(),0);
		}
		return false;
	}
};

LieyiCard::LieyiCard()
{
}

void LieyiCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	QList<int> lis = from->getPile("jpxlli");

	try {
		while(!lis.isEmpty()){
			if(from->isDead()|| to->isDead())break;

			QList<int> uses;
			foreach(int id,lis){
				const Card*c = Sanguosha->getCard(id);
				if(c->targetFixed())continue;
				room->setCardFlag(c,"lieyi_use_card");
				if(from->canUse(c,to))
					uses << id;
				room->setCardFlag(c,"-lieyi_use_card");
			}
			if(uses.isEmpty())break;

			room->fillAG(uses,from);
			int id = room->askForAG(from,uses,false,"lieyi","@lieyi-use:" + to->objectName());
			room->clearAG(from);
			lis.removeOne(id);

			room->setCardFlag(id,"lieyi_use_card");
			room->setCardFlag(id,"Global_SlashAvailabilityChecker");
			room->useCard(CardUseStruct(Sanguosha->getCard(id),from,to));
		}

		if(!lis.isEmpty()){
			DummyCard dis(lis);
			CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,from->objectName(),"lieyi","");
			room->throwCard(&dis,reason,nullptr);
		}

		if(to->getMark("lieyi_dying-Keep")> 0){
			room->setPlayerMark(to,"lieyi_dying-Keep",0);
			return;
		}
		room->loseHp(HpLostStruct(from,1,"lieyi",from));
	}catch(TriggerEvent triggerEvent){
		if(triggerEvent==TurnBroken || triggerEvent==StageChange)
			room->setPlayerMark(to,"lieyi_dying-Keep",0);
		throw triggerEvent;
	}
}

class LieyiVS : public ZeroCardViewAsSkill
{
public:
	LieyiVS(): ZeroCardViewAsSkill("lieyi")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("LieyiCard")&& !player->getPile("jpxlli").isEmpty();
	}

	const Card*viewAs()const
	{
		return new LieyiCard;
	}
};

class Lieyi : public TriggerSkill
{
public:
	Lieyi(): TriggerSkill("lieyi")
	{
		events << Dying;
		view_as_skill = new LieyiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		ServerPlayer*dy = dying.who;
		if(!dy || dy->isDead()|| !dying.damage || !dying.damage->card)return false;
		if(!dying.damage->card->hasFlag("lieyi_use_card"))return false;
		room->addPlayerMark(dy,"lieyi_dying-Keep");
		return false;
	}
};

class LieyiTarget : public TargetModSkill
{
public:
	LieyiTarget(): TargetModSkill("#lieyi")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player*,const Card*card,const Player*)const
	{
		if(card->hasFlag("lieyi_use_card"))
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*)const
	{
		if(card->hasFlag("lieyi_use_card"))
			return 999;
		return 0;
	}
};

class Chongwang : public TriggerSkill
{
public:
	Chongwang(): TriggerSkill("chongwang")
	{
		events << CardUsed;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if((!use.card->isKindOf("BasicCard")&& !use.card->isNDTrick()))return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				ServerPlayer*last = room->getTag("ChongwangLastUser").value<ServerPlayer*>();
				if(last != p)continue;
				QStringList choices;
				if(player->isAlive()&&room->CardInTable(use.card))
					choices << QString("get=%1=%2").arg(player->objectName()).arg(use.card->objectName());
				choices << QString("wuxiao=%1=%2").arg(player->objectName()).arg(use.card->objectName());
				choices << "cancel";
				QString choice = room->askForChoice(p,objectName(),choices.join("+"),data);
				if(choice=="cancel")continue;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = p;
				log.arg = objectName();
				room->sendLog(log);
				p->peiyin(this);
				room->notifySkillInvoked(p,objectName());
				if(choice.startsWith("get"))
					room->obtainCard(player,use.card);
				else {
					room->setPlayerFlag(player,"chongwang");
					use.nullified_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
};

class Huagui : public PhaseChangeSkill
{
public:
	Huagui(): PhaseChangeSkill("huagui")
	{
	}

	static bool CompareByNumber(int x,int y)
	{
		return x > y;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;

		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(!p->isNude())
				targets << p;
		}
		if(targets.isEmpty())return false;

		int lord = 0,rebel = 0,renegade = 0,other = 0;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getRole()== "lord" || p->getRole()== "loyalist")
				lord++;
			else if(p->getRole()== "rebel")
				rebel++;
			else if(p->getRole()== "renegade")
				renegade++;
			else
				other++;
		}

		QList<int> zhenyings;
		zhenyings << lord << rebel << renegade << other;
		std::sort(zhenyings.begin(),zhenyings.end(),CompareByNumber);
		int num = zhenyings.first();
		if(num <= 0)return false;

		LogMessage log;
		log.to = room->askForPlayersChosen(player,targets,objectName(),0,num,"@huagui-invoke:" + QString::number(num));
		if(log.to.isEmpty())return false;

		player->peiyin(this);

		log.type = "#ChoosePlayerWithSkill";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log,player);

		log.type = "#InvokeSkill";
		room->sendLog(log,room->getOtherPlayers(player,true));
		targets.clear();
		targets << player;
		room->notifySkillInvoked(player,objectName());

		foreach(ServerPlayer*p,log.to)
			room->doAnimate(1,player->objectName(),p->objectName(),targets);

		QHash<ServerPlayer*,int> hash;
		QHash<ServerPlayer*,bool> hash2;
		foreach(ServerPlayer*p,log.to){
			if(p->isDead()|| p->isNude())continue;
			QStringList choices;
			if(player->isAlive())
				choices << "give=" + player->objectName();
			choices << "show";
			p->setFlags("ignoreFocus");
			QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(player));
			p->setFlags("ignoreFocus");
			const Card*c = room->askForExchange(p,objectName(),1,1,true,"@huagui-card");
			hash[p] = c->getEffectiveId();
			hash2[p] = choice.startsWith("give");
		}

		bool all_show = true;

		foreach(ServerPlayer*p,log.to){
			if(p->isDead()|| p->isNude())continue;
			int id = hash[p];
			if(id < 0)continue;

			if(hash2[p]){
				all_show = false;
				if(player->isAlive())
					room->giveCard(p,player,Sanguosha->getCard(id),objectName());
			}else
				room->showCard(p,id);
		}

		if(all_show){
			foreach(ServerPlayer*p,log.to){
				if(player->isDead())break;
				if(p->isDead()|| p->isNude())continue;
				int id = hash[p];
				if(id < 0)continue;
				CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,player->objectName());
				room->obtainCard(player,Sanguosha->getCard(id),reason);
			}
		}
		return false;
	}
};

MiduCard::MiduCard()
{
	target_fixed = true;
}

void MiduCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList choices;
	if(source->hasEquipArea())
		choices << "throw";
	for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
		if(!source->hasEquipArea(i)){
			choices << "obtain";
			break;
		}
	}
	QString choice = room->askForChoice(source,"midu",choices.join("+"));
	choices.clear();
	if(choice=="throw"){
		QStringList chosen;
		for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
			if(source->hasEquipArea(i))
				choices << QString::number(i);
		}
		if(choices.isEmpty())return;

		while(choices.first()!= "cancel"){
			choice = room->askForChoice(source,"midu",choices.join("+"),QVariant(),chosen.join("+"));
			if(choice=="cancel")break;
			choices.removeOne(choice);
			chosen << choice;
			if(!choices.contains("cancel"))
				choices << "cancel";
		}

		source->throwEquipArea(ListS2I(chosen));
		if(source->isDead())return;

		ServerPlayer*t = room->askForPlayerChosen(source,room->getAlivePlayers(),"midu","@midu-draw:" + QString::number(chosen.length()));
		room->doAnimate(1,source->objectName(),t->objectName());
		t->drawCards(chosen.length(),"midu");
	} else {
		for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
			if(!source->hasEquipArea(i))
				choices << QString::number(i + 10);
		}
		if(choices.isEmpty())return;

		choice = room->askForChoice(source,"midu",choices.join("+"));
		int area = choice.toInt()- 10;
		source->obtainEquipArea(area);
		room->acquireNextTurnSkills(source,"midu","huomo");
	}
}

class Midu : public ZeroCardViewAsSkill
{
public:
	Midu(): ZeroCardViewAsSkill("midu")
	{
		waked_skills = "huomo";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("MiduCard");
	}

	const Card*viewAs()const
	{
		return new MiduCard;
	}
};

class Xianwang : public DistanceSkill
{
public:
	Xianwang(): DistanceSkill("xianwang")
	{
	}

	int getCorrect(const Player*,const Player*to)const
	{
		if(to->hasSkill(objectName())){
			int add = 0,lose = 0;
			for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
				if(!to->hasEquipArea(i))
					lose++;
			}
			if(lose > 0)
				add++;
			if(lose > 2)
				add++;
			return add;
		}
		return 0;
	}
};

TenyearJiezhenCard::TenyearJiezhenCard()
{
}

void TenyearJiezhenCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	QStringList detachList;
	foreach(const Skill*skill,to->getVisibleSkillList()){
		if(!skill->inherits("SPConvertSkill")&& !skill->isAttachedLordSkill()&& skill->getFrequency(to)!= Skill::Compulsory
			&& !skill->isLimitedSkill()&& skill->getFrequency(to)!= Skill::Wake&& !skill->isLordSkill())
			detachList.append("-" + skill->objectName());
	}
	if(!detachList.isEmpty()){
		to->setTag("TenyearJiezhenSkills_" + from->objectName(), detachList);
		room->handleAcquireDetachSkills(to,detachList);
	}
	room->acquireSkill(to,"bazhen");
}

class TenyearJiezhenVS : public ZeroCardViewAsSkill
{
public:
	TenyearJiezhenVS(): ZeroCardViewAsSkill("tenyearjiezhen")
	{
	}

	const Card*viewAs()const
	{
		return new TenyearJiezhenCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearJiezhenCard");
	}
};

class TenyearJiezhen : public TriggerSkill
{
public:
	TenyearJiezhen(): TriggerSkill("tenyearjiezhen")
	{
		events << EventPhaseStart << StartJudge;
		view_as_skill = new TenyearJiezhenVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				QStringList detachList = p->getTag("TenyearJiezhenSkills_" + player->objectName()).toStringList();
				if(detachList.isEmpty())continue;
				room->sendCompulsoryTriggerLog(player,this);
				p->removeTag("TenyearJiezhenSkills_" + player->objectName());
				QStringList skills;
				skills << "-bazhen";
				foreach(QString sk,detachList)
					skills << sk.mid(1);
				room->handleAcquireDetachSkills(p,skills);
				if(!p->isAllNude()&& player->isAlive()){
					int id = room->askForCardChosen(player,p,"hej",objectName());
					room->obtainCard(player,id,false);
				}
			}
		} else {
			JudgeStruct*judge = data.value<JudgeStruct*>();
			if(judge->reason != "eight_diagram")return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				QStringList detachList = player->getTag("TenyearJiezhenSkills_" + p->objectName()).toStringList();
				if(detachList.isEmpty())continue;
				room->sendCompulsoryTriggerLog(p,this);
				player->removeTag("TenyearJiezhenSkills_" + p->objectName());
				QStringList skills;
				skills << "-bazhen";
				foreach(QString sk,detachList)
					skills << sk.mid(1);
				room->handleAcquireDetachSkills(player,skills);
				if(!player->isAllNude()&& p->isAlive()){
					int id = room->askForCardChosen(p,player,"hej",objectName());
					room->obtainCard(p,id,false);
				}
			}
		}
		return false;
	}
};

class TenyearZecai : public TriggerSkill
{
public:
	TenyearZecai(): TriggerSkill("tenyearzecai")
	{
		events << RoundEnd;
		frequency = Limited;
		limit_mark = "@tenyearzecaiMark";
		waked_skills = "nosjizhi";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getMark("@tenyearzecaiMark")<= 0)return false;

		int mark = 0;
		QList<ServerPlayer*> mosts;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			int mark_p = p->getMark("tenyearzecai_trick_lun");
			if(mark_p > mark)mark = mark_p;
		}
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getMark("tenyearzecai_trick_lun")== mark)
				mosts << p;
		}
		ServerPlayer*most = mosts.length()== 1 ? mosts.first(): nullptr;
		if(most==player)most = nullptr;
		QString prompt = most ? "@tenyearzecai-target1:" + most->objectName()+ "::" + QString::number(most->getPlayerSeat()): "@tenyearzecai-target2";

		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),prompt,true,true);
		if(!t)return false;
		player->peiyin(this);

		room->doSuperLightbox(player,"tenyearzecai");
		room->removePlayerMark(player,"@tenyearzecaiMark");

		if(!t->hasSkill("nosjizhi",true)){
			t->setTag("TenyearZecaiJizhi_" + player->objectName(), true);
			room->acquireSkill(t,"nosjizhi");
		}
		if(most==t)
			t->gainAnExtraTurn();
		return false;
	}
};

class TenyearZecaiLose : public TriggerSkill
{
public:
	TenyearZecaiLose(): TriggerSkill("#tenyearzecai")
	{
		events << RoundEnd << PreCardUsed;
		global = true;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		return TriggerSkill::getPriority(triggerEvent)+ 1;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard"))
				player->addMark("tenyearzecai_trick_lun");
		}else{
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getTag("TenyearZecaiJizhi_" + player->objectName()).toBool()){
					p->removeTag("TenyearZecaiJizhi_" + player->objectName());
					room->detachSkillFromPlayer(p,"nosjizhi");
				}
			}
		}
		return false;
	}
};

class TenyearYinshi : public TriggerSkill
{
public:
	TenyearYinshi(): TriggerSkill("tenyearyinshi")
	{
		events << DamageInflicted << FinishJudge;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DamageInflicted){
			if(!room->hasCurrent())return false;
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card ||(!damage.card->isRed()&& !damage.card->isBlack())){
				player->addMark("tenyearyinshi_damage-Clear");
				if(player->getMark("tenyearyinshi_damage-Clear")!= 1 || !player->hasSkill(objectName()))return false;
				room->sendCompulsoryTriggerLog(player,"tenyearyinshi",true,true);
				return true;
			}
		}else{
			JudgeStruct*judge = data.value<JudgeStruct*>();
			if(judge->reason != "eight_diagram")return false;
			int id = judge->card->getEffectiveId();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(room->getCardPlace(id)!= Player::PlaceJudge)break;
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				room->sendCompulsoryTriggerLog(p,this);
				room->obtainCard(p,id);
			}
		}
		return false;
	}
};

DunshiCard::DunshiCard()
{
	mute = true;
}

bool DunshiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets,to_select,Self);
	}

	const Card*dc = Self->getTag("dunshi").value<const Card*>();
	return dc&& dc->targetFilter(targets,to_select,Self);
}

bool DunshiCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("dunshi");
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool DunshiCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets,Self);
	}

	const Card*dc = Self->getTag("dunshi").value<const Card*>();
	return dc&& dc->targetsFeasible(targets,Self);
}

const Card*DunshiCard::validate(CardUseStruct&card_use)const
{
	ServerPlayer*player = card_use.from;
	Room*room = player->getRoom();

	room->addPlayerMark(player,"dunshi_used-Clear");

	QString to_dunshi = user_string;
	if((user_string.contains("slash")|| user_string.contains("Slash"))&& Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		if(player->getMark("dunshi_used_slash")> 0)return nullptr;
		to_dunshi = "slash";
	}
	QStringList cards;
	cards << "slash" << "jink" << "peach" << "analeptic";
	room->setPlayerMark(player,"dunshi_card-Clear",cards.indexOf(to_dunshi)+ 1);

	Card*use_card = Sanguosha->cloneCard(to_dunshi);
	use_card->setSkillName("dunshi");
	use_card->deleteLater();
	return use_card;
}

const Card*DunshiCard::validateInResponse(ServerPlayer*player)const
{
	Room*room = player->getRoom();

	room->addPlayerMark(player,"dunshi_used-Clear");

	QString to_dunshi;
	if(user_string=="peach+analeptic"){
		QStringList guhuo_list;
		if(player->getMark("dunshi_used_peach")<= 0)
			guhuo_list << "peach";
		if(Sanguosha->hasCard("analeptic")&& player->getMark("dunshi_used_analeptic")<= 0)
			guhuo_list << "analeptic";
		if(guhuo_list.isEmpty())return nullptr;
		to_dunshi = room->askForChoice(player,"dunshi_saveself",guhuo_list.join("+"));
	} else if(user_string.contains("slash")|| user_string.contains("Slash")){
		if(player->getMark("dunshi_used_slash")> 0)return nullptr;
		to_dunshi = "slash";
	} else {
		if(player->getMark("dunshi_used_" + user_string)> 0)return nullptr;
		to_dunshi = user_string;
	}
	QStringList cards;
	cards << "slash" << "jink" << "peach" << "analeptic";
	room->setPlayerMark(player,"dunshi_card-Clear",cards.indexOf(to_dunshi)+ 1);

	Card*use_card = Sanguosha->cloneCard(to_dunshi);
	use_card->setSkillName("dunshi");
	use_card->deleteLater();
	return use_card;
}

class DunshiVS : public ZeroCardViewAsSkill
{
public:
	DunshiVS(): ZeroCardViewAsSkill("dunshi")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("dunshi_used-Clear")> 0)return false;
		return player->getMark("dunshi_used_slash")<= 0 || player->getMark("dunshi_used_peach")<= 0
			|| player->getMark("dunshi_used_analeptic")<= 0;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(pattern.contains("slash")|| pattern.contains("Slash")|| pattern.contains("Jink")|| pattern.contains("jink")||
			pattern.contains("peach")|| pattern.contains("analeptic")){
			if(player->getMark("dunshi_used-Clear")> 0)return false;
			if(pattern.startsWith(".")|| pattern.startsWith("@"))return false;
			if(pattern=="peach"&& player->getMark("Global_PreventPeach")> 0)return false;
			for(int i = 0; i < pattern.length(); i++){
				QChar ch = pattern[i];
				if(ch.isUpper()|| ch.isDigit())return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
			}

			bool ok = false;
			foreach(QString pa,pattern.split("+")){
				if(!pa.contains("slash")&& !pa.contains("Slash")&& !pa.contains("Jink")&& !pa.contains("jink")&&
						!pa.contains("peach")&& !pa.contains("analeptic"))continue;
				QString name = pa;
				if(pa.contains("slash")|| pa.contains("Slash"))
					name = "slash";
				else if(pa.contains("jink")|| pa.contains("Jink"))
					name = "jink";

				if(player->getMark("dunshi_used_" + pa)<= 0){
					ok = true;
					break;
				}
			}

			return ok;
		}
		return false;
	}

	const Card*viewAs()const
	{
		if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			DunshiCard*card = new DunshiCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			return card;
		}

		const Card*c = Self->getTag("dunshi").value<const Card*>();
		if(c&& c->isAvailable(Self)){
			DunshiCard*card = new DunshiCard;
			card->setUserString(c->objectName());
			return card;
		}
		return nullptr;
	}
};

class Dunshi : public TriggerSkill
{
public:
	Dunshi(): TriggerSkill("dunshi")
	{
		events << DamageCaused;
		view_as_skill = new DunshiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance("dunshi",true,false);
	}

	QStringList getSkills(ServerPlayer*player)const
	{
		QStringList skills,choices;
		foreach(QString name,Sanguosha->getLimitedGeneralNames()){
			const General*general = Sanguosha->getGeneral(name);
			if(!general)continue;
			foreach(const Skill*sk,general->getVisibleSkillList()){
				if(skills.contains(sk->objectName())||player->hasSkill(sk,true))continue;
				QString sk_name = Sanguosha->translate(sk->objectName());
				if(!sk_name.contains("仁")&& !sk_name.contains("义")&& !sk_name.contains("礼")&& !sk_name.contains("智")
					&& !sk_name.contains("信"))continue;
				skills << sk->objectName();
			}
		}
		for(int i = 0; i < 3; i++){
			if(skills.isEmpty())break;
			QString choice = skills.at(qrand()% skills.length());
			skills.removeOne(choice);
			choices << choice;
		}
		return choices;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		ServerPlayer*current = room->getCurrent();
		if(player != current)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			int mark = p->getMark("dunshi_card-Clear");
			if(mark <= 0)continue;
			room->setPlayerMark(p,"dunshi_card-Clear",0);
			room->sendCompulsoryTriggerLog(p,this);
			QStringList cards;
			cards << "slash" << "jink" << "peach" << "analeptic";
			QString card = cards[mark - 1];
			bool prevent = false,draw_card = false,delete_card = false;
			for(int i = 0; i < 2; i++){
				if(p->isDead())break;
				QStringList choices;
				if(!prevent)choices << "skill=" + player->objectName();
				if(!draw_card)choices << "draw=" + QString::number(p->getMark("SkillDescriptionArg1_dunshi"));
				if(!delete_card)choices << "delete=" + card;
				QString choice = room->askForChoice(p,objectName(),choices.join("+"),data);
				if(choice.startsWith("skill")){
					prevent = true;
					if(player->isAlive()){
						QStringList skills = getSkills(player);
						if(skills.isEmpty())continue;
						QString sk = room->askForChoice(p,"dunshi_chooseskill",skills.join("+"),QVariant::fromValue(player));
						room->acquireSkill(player,sk);
					}
				} else if(choice.startsWith("draw")){
					draw_card = true;
					int draw = p->getMark("SkillDescriptionArg1_dunshi");
					room->loseMaxHp(p,1,objectName());
					p->drawCards(draw,objectName());
				} else {
					delete_card = true;
					room->addPlayerMark(p,"SkillDescriptionArg1_dunshi");
					room->addPlayerMark(p,"dunshi_used_" + card);
					choices = p->property("SkillDescriptionRecord_dunshi").toStringList();
					if(!choices.contains(card)){
						LogMessage log;
						log.type = "#DunshiDelete";
						log.from = p;
						log.arg = objectName();
						log.arg2 = card;
						room->sendLog(log);
						choices << card;
						room->setPlayerProperty(p,"SkillDescriptionRecord_dunshi",choices);
					}
					QStringList choices2;
					foreach(QString src,choices)
						choices2 << src << "|";
					p->setSkillDescriptionSwap(objectName(),"%arg22",choices2.join("+"));
					p->setSkillDescriptionSwap(objectName(),"%arg1",QString::number(choices.length()));
					room->changeTranslation(p,objectName(),1);
				}
			}
			return prevent;
		}
		return false;
	}
};

class Aishou : public TriggerSkill
{
public:
	Aishou(): TriggerSkill("aishou")
	{
		events << EventPhaseStart << CardsMoveOneTime << BeforeCardsMove;
		frequency = Frequent;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==BeforeCardsMove)
			return 0;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()== Player::Finish){
				if(player->getMaxHp()<= 0 || !player->askForSkillInvoke(this))return false;
				player->peiyin(this);
				QList<int> draw = room->drawCardsList(player,player->getMaxHp(),objectName());
				QList<int> hand = player->handCards();
				foreach(int id,draw){
					if(!hand.contains(id))continue;
					room->setCardTip(id,"qrasai");
				}
			} else if(player->getPhase()== Player::Start){
				DummyCard*dummy = new DummyCard;
				foreach(int id,player->handCards()){
					const Card*c = Sanguosha->getCard(id);
					if(!c->hasTip("qrasai"))continue;
					room->setCardTip(id,"-qrasai");
					if(player->canDiscard(player,id))
						dummy->addSubcard(id);
				}
				if(dummy->subcardsLength()> 0){  //有隘但是不能弃置，应该展示，这里偷懒
					room->sendCompulsoryTriggerLog(player,this);
					room->throwCard(dummy,objectName(),player);
					if(dummy->subcardsLength()> player->getHp()&& player->getMaxHp()< 8)
						room->gainMaxHp(player,1,objectName());
				}
				delete dummy;
			}
		} else if(event==BeforeCardsMove){
			if(player->hasFlag("CurrentPlayer"))return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&& move.from_places.contains(Player::PlaceHand)){
				QList<int> ids = move.card_ids;
				bool lose_ai = false;
				foreach(int id,ids){
					const Card*c = Sanguosha->getCard(id);
					if(!c->hasTip("qrasai"))continue;
					lose_ai = true;
					break;
				}
				if(!lose_ai)return false;

				foreach(int id,player->handCards()){
					if(ids.contains(id))continue;
					const Card*c = Sanguosha->getCard(id);
					if(c->hasTip("qrasai"))
						return false;
				}
				player->setFlags("aishouLast");
			}
		} else {
			if(!player->hasFlag("aishouLast"))return false;
			player->setFlags("-aishouLast");
			//if(player->getMaxHp()<= 0)return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->loseMaxHp(player,1,objectName());
		}
		return false;
	}
};

class SaoweiVS :public OneCardViewAsSkill
{
public:
	SaoweiVS():OneCardViewAsSkill("saowei")
	{
		response_pattern = "@@saowei";
	}

	bool viewFilter(const Card*to_select)const
	{
		Slash*slash = new Slash(Card::SuitToBeDecided,-1);
		slash->addSubcard(to_select);
		slash->setSkillName("saowei");
		slash->deleteLater();
		return to_select->hasTip("qrasai")&& slash->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard)const
	{
		Slash*slash = new Slash(Card::SuitToBeDecided,-1);
		slash->addSubcard(originalCard);
		slash->setSkillName("saowei");
		return slash;
	}
};

class Saowei : public TriggerSkill
{
public:
	Saowei(): TriggerSkill("saowei")
	{
		events << CardFinished;
		view_as_skill = new SaoweiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool hasAi(ServerPlayer*player)const
	{
		foreach(const Card*c,player->getHandcards()){
			if(c->hasTip("qrasai"))
				return true;
		}
		return false;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash")|| use.to.isEmpty())return false;
		QList<ServerPlayer*> tos = use.to;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| !p->hasSkill(objectName())|| !hasAi(p))continue;

			bool saowei = false;
			foreach(ServerPlayer*to,tos){
				if(to->isAlive()&& to != p&& p->inMyAttackRange(to)){
					saowei = true;
					break;
				}
			}
			if(!saowei)continue;

			room->setPlayerFlag(p,"slashTargetFix");
			foreach(ServerPlayer*to,tos)
				room->setPlayerFlag(to,"SlashAssignee");

			if(!room->askForUseCard(p,"@@saowei","@saowei")){
				room->setPlayerFlag(p,"-slashTargetFix");
				foreach(ServerPlayer*to,tos)
					room->setPlayerFlag(to,"-SlashAssignee");
			}
		}
		return false;
	}
};

class Cuijin : public TriggerSkill
{
public:
	Cuijin(): TriggerSkill("cuijin")
	{
		events << CardUsed << ConfirmDamage << CardFinished;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash"))return false;

			int add = 0;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName())|| p->isNude())continue;
				if(p != player&& !p->inMyAttackRange(player))continue;
				if(!room->askForCard(p,"..","@cuijin-discard:" + player->objectName(),data,objectName()))continue;
				p->peiyin(this);
				add++;
				room->setCardFlag(use.card,"cuijin_invoker_" + p->objectName());
			}
			if(add <= 0)return false;
			room->setCardFlag(use.card,"cuijinAddDamage_" + QString::number(add));
		} else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash"))return false;

			foreach(QString flag,damage.card->getFlags()){
				if(!flag.startsWith("cuijinAddDamage_"))continue;
				QStringList flags = flag.split("_");
				if(flags.length()!= 2)continue;
				damage.damage += flags.last().toInt();
				data = QVariant::fromValue(damage);
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash")|| use.card->hasFlag("DamageDone"))return false;

			foreach(QString flag,use.card->getFlags()){
				if(!flag.startsWith("cuijin_invoker_"))continue;
				QStringList flags = flag.split("_");
				ServerPlayer*p = room->findChild<ServerPlayer*>(flags.last());
				if(p&& p->isAlive()){
					room->sendCompulsoryTriggerLog(p,objectName());
					p->drawCards(1,objectName());
					if(use.from->isAlive())
						room->damage(DamageStruct(objectName(),p,use.from));
				}
			}
		}
		return false;
	}
};

class Suizheng : public TriggerSkill
{
public:
	Suizheng(): TriggerSkill("suizheng")
	{
		events << EventPhaseEnd << EventPhaseStart << DamageDone;
		waked_skills = "#suizheng";  //在这里添加关联的隐藏技就行了
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish || !player->hasSkill(objectName()))return false;
			ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),"suizheng","@suizheng-target",true,true);
			if(t){
				player->peiyin("suizheng");
				room->addPlayerMark(t,"&suizheng_buff+#" + player->objectName());
			}
		}else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.from || damage.from->getPhase()!= Player::Play)return false;
			room->addPlayerMark(player,damage.from->objectName()+"suizheng_record-PalyClear");
		}else{
			if(player->getPhase()!= Player::Play)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				int mark = player->getMark("&suizheng_buff+#" + p->objectName());
				if(mark <= 0)continue;
				room->setPlayerMark(player,"&suizheng_buff+#" + p->objectName(),0);
				Slash*slash = new Slash(Card::NoSuit,0);
				slash->setSkillName(objectName());
				slash->deleteLater();
				for(int i = 0; i < mark; i++){
					if(p->isDead()|| p->isLocked(slash))break;
					QList<ServerPlayer*> targets;
					foreach(ServerPlayer*q,room->getOtherPlayers(p)){
						if(q->getMark(player->objectName()+"suizheng_record-PalyClear")<1||!p->canSlash(q,slash,false))continue;
						targets << q;
					}

					ServerPlayer*to = room->askForPlayerChosen(p,targets,"suizheng1","@suizheng",true);
					if(!to)break;
					room->useCard(CardUseStruct(slash,p,to));
				}
			}
		}
		return false;
	}
};

class SuizhengTargetMod : public TargetModSkill
{
public:
	SuizhengTargetMod(): TargetModSkill("#suizheng")
	{
		frequency = NotFrequent;
	}

	int getSuiZhengMark(const Player*from,bool stop)const
	{
		int num = 0;
		foreach(QString mark,from->getMarkNames()){
			if(mark.startsWith("&suizheng_buff+#")){
				if(stop)return 1;
				num++;
			}
		}
		return num;
	}

	int getResidueNum(const Player*from,const Card*,const Player*)const
	{
		if(from->getPhase()== Player::Play)
			return getSuiZhengMark(from,false);
		return 0;
	}

	int getDistanceLimit(const Player*from,const Card*,const Player*)const
	{
		if(getSuiZhengMark(from,true)> 0)
			return 999;
		return 0;
	}
};

class Bingjie : public PhaseChangeSkill
{
public:
	Bingjie(): PhaseChangeSkill("bingjie")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);
		room->loseMaxHp(player,1,objectName());
		if(player->isAlive())
			room->addPlayerMark(player,"&bingjie-Clear");
		return false;
	}
};

class BingjieEffect : public TriggerSkill
{
public:
	BingjieEffect(): TriggerSkill("#bingjie")
	{
		events  << TargetSpecified;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->getMark("&bingjie-Clear")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
		int mark = player->getMark("&bingjie-Clear");
		foreach(ServerPlayer*p,use.to){
			if(p==player)continue;
			for(int i = 0; i < mark; i++){
				if(p->isDead()|| !p->canDiscard(p,"he"))continue;

				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = "bingjie";
				room->sendLog(log);
				player->peiyin("bingjie");
				room->notifySkillInvoked(player,"bingjie");

				room->askForDiscard(p,"bingjie",1,1,false,true);
			}
		}
		return false;
	}
};

class Zhengding : public TriggerSkill
{
public:
	Zhengding(): TriggerSkill("zhengding")
	{
		events  << CardUsed << CardResponded;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->hasFlag("CurrentPlayer"))return false;

		const Card*my_card = nullptr,*card = nullptr;
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			my_card = use.card;
			card = use.whocard;
		} else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(res.m_isRetrial)return false;
			my_card = res.m_card;
			card = res.m_toCard;
		}

		if(!card || !my_card || card->isKindOf("SkillCard")|| my_card->isKindOf("SkillCard")|| !card->sameColorWith(my_card))return false;

		ServerPlayer*from = room->getCardUser(card);
		if(!from || from==player)return false;

		room->sendCompulsoryTriggerLog(player,this);
		room->gainMaxHp(player,1,objectName());
		return false;
	}
};

class Haochong : public TriggerSkill
{
public:
	Haochong(): TriggerSkill("haochong")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("SkillCard"))return false;
		int hand = player->getHandcardNum(),max = player->getMaxCards();
		if(hand < max){
			if(!player->askForSkillInvoke(this,QString("haochong:%1").arg(max - hand)))return false;
			player->peiyin(this);
			hand = player->getHandcardNum();
			max = player->getMaxCards();
			player->drawCards(qMin(5,max - hand),objectName());
			room->addMaxCards(player,-1,false);
		} else if(hand > max){
			if(!room->askForDiscard(player,objectName(),hand - max,hand - max,true,false,
							QString("@haochong-discard:%1").arg(hand - max),".",objectName()))return false;
			player->peiyin(this);
			room->addMaxCards(player,1,false);
		}
		return false;
	}
};

class Jinjin : public TriggerSkill
{
public:
	Jinjin(): TriggerSkill("jinjin")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("jinjinUsed-Clear")> 0)return false;

		int max = player->getMaxCards(),hp = player->getHp(),change = 0;
		if(!player->askForSkillInvoke(this,QString("jinjin:%1:%2").arg(max).arg(hp)))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"jinjinUsed-Clear");

		max = player->getMaxCards(),hp = player->getHp();
		room->addMaxCards(player,hp - max,false);
		change = qAbs(hp - max);
		change = qMax(change,1);

		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from->isDead())return false;

		if(damage.from->isNude()){
			player->drawCards(change,objectName());
			return false;
		}

		int discard = 0;
		damage.from->setTag("JinjinData", data);
		const Card*c = room->askForDiscard(damage.from,objectName(),change,1,true,true,QString("@jinjin-discard:%1").arg(change));
		damage.from->removeTag("JinjinData");
		if(c)discard = c->subcardsLength();
		player->drawCards(change - discard,objectName());
		return false;
	}
};

class Xieshou : public MasochismSkill
{
public:
	Xieshou(): MasochismSkill("xieshou")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		Room*room = player->getRoom();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead())return;
			if(p->isDead()|| !p->hasSkill(objectName())|| p->distanceTo(player)> 2 || p->getMark("xieshouUsed-Clear")> 0)continue;
			if(!p->askForSkillInvoke(this,player))continue;
			p->peiyin(this);
			room->addPlayerMark(p,"xieshouUsed-Clear");
			room->addMaxCards(p,-1,false);

			QStringList choices;
			if(player->isWounded())
				choices << "recover";
			choices << "fuyuan";

			QString choice = room->askForChoice(player,objectName(),choices.join("+"));
			if(choice=="recover")
				room->recover(player,RecoverStruct("xieshou",p));
			else {
				room->setPlayerChained(player,false);
				if(!player->faceUp())
					player->turnOver();
				player->drawCards(2,objectName());
			}
		}
	}
};

class Qingyan : public TriggerSkill
{
public:
	Qingyan(): TriggerSkill("qingyan")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isBlack()|| use.card->isKindOf("SkillCard"))return false;
		if(!use.from || use.from==player || !use.to.contains(player))return false;
		if(player->getMark("qingyanUsed-Clear")> 2)return false;

		int hand = player->getHandcardNum(),hp = player->getHp();
		if(hand < hp){
			int num = player->getMaxHp()- hand;
			if(num <= 0)return false;
			if(!player->askForSkillInvoke(this,"qingyan:" + QString::number(num)))return false;
			player->peiyin(this);
			room->addPlayerMark(player,"qingyanUsed-Clear");
			player->drawCards(player->getMaxHp()- player->getHandcardNum(),objectName());
		} else {
			if(!player->canDiscard(player,"h"))return false;
			if(!room->askForCard(player,".|.|.|hand","@qingyan",data,objectName()))return false;
			player->peiyin(this);
			room->addPlayerMark(player,"qingyanUsed-Clear");
			room->addMaxCards(player,1,false);
		}
		return false;
	}
};

class Qizi : public TriggerSkill
{
public:
	Qizi(): TriggerSkill("qizi")
	{
		events << AskForPeaches;
		frequency = Compulsory;
	}
	int getPriority(TriggerEvent)const
	{
		return 1;
	}

	bool trigger(TriggerEvent ,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if(player->distanceTo(dying.who)> 2){
			room->sendCompulsoryTriggerLog(player,this);
			return true;
		}
		return false;
	}
};

class Yingtu : public TriggerSkill
{
public:
	Yingtu(): TriggerSkill("yingtu")
	{
		events << CardsMoveOneTime;
	}

	ServerPlayer*getAdjacentPlayer(ServerPlayer*player,bool next)const
	{
		int seat = player->getSeat(),alive_length = player->aliveCount();
		Room*room = player->getRoom();
		if(next){
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getSeat()== seat + 1 ||(alive_length==seat&& p->getSeat()== 1))
					return p;
			}
		} else {
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getSeat()== seat - 1 ||(seat==1&& p->getSeat()== alive_length))
					return p;
			}
		}
		return nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(room->getTag("FirstRound").toBool()|| !room->hasCurrent()|| player->getMark("yingtuUsed-Clear")> 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::PlaceHand)return false;
		if(!move.to->isAdjacentTo(player)|| move.to->getPhase()== Player::Draw || move.to->isNude())return false;

		ServerPlayer*to =(ServerPlayer*)move.to;
		if(!player->askForSkillInvoke(this,to))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"yingtuUsed-Clear");

		int id = room->askForCardChosen(player,to,"he",objectName());
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,player->objectName());
		room->obtainCard(player,Sanguosha->getCard(id),reason,room->getCardPlace(id)!= Player::PlaceHand);

		if(player->isDead()|| player->isNude())return false;

		ServerPlayer*adjacent = nullptr,*last = getAdjacentPlayer(player,false),*next = getAdjacentPlayer(player,true);
		if(to==last)adjacent = next;
		else if(to==next)adjacent = last;
		if(!adjacent || adjacent->isDead())return false;

		player->setTag("YingtuAdjacentPlayer", QVariant::fromValue(adjacent));
		const Card*c = room->askForExchange(player,objectName(),1,1,true,"@yingtu-give:" + adjacent->objectName());
		room->giveCard(player,adjacent,c,objectName());
		c = Sanguosha->getCard(c->getEffectiveId());

		if(c->isKindOf("EquipCard")&& adjacent->handCards().contains(c->getEffectiveId())&& adjacent->canUse(c,adjacent,true))
			room->useCard(CardUseStruct(c,adjacent));

		return false;
	}
};

class Congshi : public TriggerSkill
{
public:
	Congshi(): TriggerSkill("congshi")
	{
		events << CardFinished;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("EquipCard"))return false;
		int length = player->getEquips().length();

		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getEquips().length()> length)
				return false;
		}

		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			room->sendCompulsoryTriggerLog(p,this);
			p->drawCards(1,objectName());
		}
		return false;
	}
};

class Yingyu : public PhaseChangeSkill
{
public:
	Yingyu(): PhaseChangeSkill("yingyu")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::Start ||(player->getPhase()== Player::Finish&& player->getMark(objectName())> 0)){
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->isKongcheng())continue;
				players << p;
			}
			if(players.length()< 2)return false;

			QList<ServerPlayer*> targets = room->askForPlayersChosen(player,players,objectName(),-1,2,"@yingyu-invoke",true);
			if(targets.length()!= 2)return false;
			player->peiyin(this);

			QHash<ServerPlayer*,const Card*> hash;
			foreach(ServerPlayer*p,targets){
				if(player->isDead())return false;
				if(p->isDead()|| p->isKongcheng())return false;
				int id = room->askForCardChosen(player,p,"h",objectName());
				room->showCard(p,id);
				hash[p] = Sanguosha->getCard(id);
			}

			if(player->isDead()||hash[targets.first()]->getSuit()==hash[targets.last()]->getSuit())return false;
			ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@yingyu-target");
			targets.removeOne(t);
			const Card*c = hash[targets.first()];
			if(!c)return false;
			room->obtainCard(t,c);
		}
		return false;
	}
};

YongbiCard::YongbiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool YongbiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select->isMale()&& to_select != Self;
}

void YongbiCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	room->doSuperLightbox(from,"yongbi");
	room->removePlayerMark(from,"@yongbiMark");

	if(from->isKongcheng())return;

	QList<Card::Suit> suits;
	foreach(const Card*c,from->getHandcards()){
		Card::Suit suit = c->getSuit();
		if(suits.contains(suit))continue;
		suits << suit;
	}

	DummyCard*handcards = from->wholeHandCards();
	room->giveCard(from,to,handcards,"yongbi");

	room->addPlayerMark(from,"yingyu");

	QList<ServerPlayer*> targets;
	targets << from << to;
	room->sortByActionOrder(targets);

	if(suits.length()>= 3){
		foreach(ServerPlayer*p,targets){
			if(p->isDead())continue;
			room->addPlayerMark(p,"&yongbi_buff2");
		}
	} else if(suits.length()>= 2){
		foreach(ServerPlayer*p,targets){
			if(p->isDead())continue;
			room->addPlayerMark(p,"&yongbi_buff1");
		}
	}
}

class YongbiVS : public ZeroCardViewAsSkill
{
public:
	YongbiVS(): ZeroCardViewAsSkill("yongbi")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->isKongcheng()&& player->getMark("@yongbiMark")> 0;
	}

	const Card*viewAs()const
	{
		return new YongbiCard;
	}
};

class Yongbi : public TriggerSkill
{
public:
	Yongbi(): TriggerSkill("yongbi")
	{
		events << DamageInflicted;
		view_as_skill = new YongbiVS;
		frequency = Limited;
		limit_mark = "@yongbiMark";
		waked_skills = "#yongbi";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getMark("&yongbi_buff2")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.damage <= 1)return false;
		int mark = player->getMark("&yongbi_buff2");

		LogMessage log;
		log.type = "#YongbiBuff";
		log.from = player;
		log.arg = objectName();
		log.arg2 = QString::number(mark);
		room->sendLog(log);

		damage.damage -= mark;
		data = QVariant::fromValue(damage);
		if(damage.damage <= 0)
			return true;
		return false;
	}
};

class YongbiKeep : public MaxCardsSkill
{
public:
	YongbiKeep(): MaxCardsSkill("#yongbi")
	{
		frequency = Limited;
	}

	int getExtra(const Player*target)const
	{
		return target->getMark("&yongbi_buff1")* 2 + target->getMark("&yongbi_buff2")* 2;
	}
};

TenyearShuheCard::TenyearShuheCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearShuheCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	room->showCard(source,subcards);
	int number = Sanguosha->getCard(subcards.first())->getNumber();

	QList<CardsMoveStruct> moves;
	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		foreach(const Card*c,p->getCards("ej")){
			if(c->getNumber()== number){
				CardsMoveStruct move(c->getEffectiveId(),source,Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,source->objectName()));
				moves << move;
			}
		}
	}

	if(!moves.isEmpty())
		room->moveCardsAtomic(moves,true);
	else {
		room->fillAG(subcards,source);
		ServerPlayer*t = room->askForPlayerChosen(source,room->getOtherPlayers(source),"tenyearshuhe","@tenyearshuhe-give");
		room->clearAG(source);
		room->giveCard(source,t,this,"tenyearshuhe",true);
		if(source->isAlive()&& source->getMark("&tenyearliehou_buff")< 5){
			if(source->getMark("&tenyearliehou_buff")== 0)
				room->addPlayerMark(source,"&tenyearliehou_buff",2);
			else
				room->addPlayerMark(source,"&tenyearliehou_buff");
			source->setSkillDescriptionSwap("tenyearliehou","%arg1",QString::number(source->getMark("&tenyearliehou_buff")));
			room->changeTranslation(source,"tenyearliehou",1);
		}
	}
}

class TenyearShuhe : public OneCardViewAsSkill
{
public:
	TenyearShuhe(): OneCardViewAsSkill("tenyearshuhe")
	{
	filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearShuheCard");
	}

	const Card*viewAs(const Card*originalCard)const
	{
		TenyearShuheCard*card = new TenyearShuheCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class TenyearLiehou : public TriggerSkill
{
public:
	TenyearLiehou(): TriggerSkill("tenyearliehou")
	{
		events << DrawNCards << AfterDrawNCards;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="draw_phase")return false;
		if(event==DrawNCards){
			if(!player->hasSkill(objectName()))return false;
			room->sendCompulsoryTriggerLog(player,this);
			int extra = qMax(1,player->getMark("&tenyearliehou_buff"));
			room->setPlayerMark(player,"tenyearliehou",extra);
			draw.num += extra;
			data.setValue(draw);
		}else{
			int extra = player->getMark("tenyearliehou");
			if(extra<1)return false;
			if(room->askForDiscard(player,"tenyearliehou",extra,extra,true,true,"@tenyearliehou-discard:" + QString::number(extra)))return false;
			room->loseHp(HpLostStruct(player,1,"tenyearliehou",player));
		}
		return false;
	}
};

class Lianzhou : public PhaseChangeSkill
{
public:
	Lianzhou(): PhaseChangeSkill("lianzhou")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		room->sendCompulsoryTriggerLog(player,this);
		room->setPlayerChained(player,true);

		int hp = player->getHp();
		QList<ServerPlayer*> players;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->getHp()== hp)
				players << p;
		}
		if(players.isEmpty())return false;

		QList<ServerPlayer*> targets = room->askForPlayersChosen(player,players,objectName(),0,9999,"@lianzhou-target");
		if(targets.isEmpty())return false;

		foreach(ServerPlayer*p,targets)
			room->doAnimate(1,player->objectName(),p->objectName());
		foreach(ServerPlayer*p,targets){
			if(p->isDead())continue;
			room->setPlayerChained(p,true);
		}
		return false;
	}
};

class Jinglan : public TriggerSkill
{
public:
	Jinglan(): TriggerSkill("jinglan")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		int hp = player->getHp(),hand = player->getHandcardNum();
		if(hand > hp){
			if(!player->canDiscard(player,"h"))return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->askForDiscard(player,objectName(),3,3);
		} else if(hand==hp){
			if(!player->canDiscard(player,"h"))return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->askForDiscard(player,objectName(),1,1);
			room->recover(player,RecoverStruct("jinglan",player));
		} else {
			room->sendCompulsoryTriggerLog(player,this);
			room->damage(DamageStruct(objectName(),nullptr,player,1,DamageStruct::Fire));
			player->drawCards(4,objectName());
		}
		return false;
	}
};

class TenyearXizhen : public PhaseChangeSkill
{
public:
	TenyearXizhen(): PhaseChangeSkill("tenyearxizhen")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@tenyearxizhen-target",true,true);
		if(!t)return false;
		player->peiyin(this);

		QStringList choices;

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_tenyearxizhen");
		slash->deleteLater();
		if(player->canSlash(t,slash,false))
			choices << "slash=" + t->objectName();
		Duel*duel = new Duel(Card::NoSuit,0);
		duel->setSkillName("_tenyearxizhen");
		duel->deleteLater();
		if(player->canUse(duel,t,true))
			choices << "duel=" + t->objectName();

		if(choices.isEmpty())return false;

		QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(t));

		QString name = choice.split("=").first();
		Card*c = Sanguosha->cloneCard(name);
		c->deleteLater();
		c->setSkillName("_tenyearxizhen");
		room->useCard(CardUseStruct(c,player,t));

		if(player->isAlive()){
			room->addPlayerMark(player,"tenyearxizhen_from-PlayClear");
			QStringList target_names = player->getTag("TenyearXizhenTargets").toStringList();
			target_names << t->objectName();
			player->setTag("TenyearXizhenTargets", target_names);
		}
		return false;
	}
};

class TenyearXizhenEffect : public TriggerSkill
{
public:
	TenyearXizhenEffect(): TriggerSkill("#tenyearxizhen")
	{
		events << CardUsed << CardResponded << EventPhaseEnd;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==EventPhaseEnd)
			return 0;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()!= Player::Play)return false;
			player->removeTag("TenyearXizhenTargets");
		} else {
			ServerPlayer*who = nullptr;
			const Card*card = nullptr,*tocard = nullptr;
			if(event==CardUsed){
				CardUseStruct use = data.value<CardUseStruct>();
				card = use.card;
				tocard = use.whocard;
				who = use.who;
			} else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if(res.m_isRetrial)return false;  //不加改判会崩，不知为啥
				card = res.m_card;
				tocard = res.m_toCard;
				who = res.m_who;
			}
			if(card==nullptr || tocard==nullptr || card->isKindOf("SkillCard")|| tocard->isKindOf("SkillCard"))return false;
			if(!who || who->isDead()|| who->getMark("tenyearxizhen_from-PlayClear")<= 0)return false;

			QList<ServerPlayer*> targets;
			foreach(QString name,who->getTag("TenyearXizhenTargets").toStringList()){
				ServerPlayer*p = room->findChild<ServerPlayer*>(name);
				if(p)targets << p;
			}
			room->sortByActionOrder(targets);
			foreach(ServerPlayer*p,targets){
				//if(who->isDead())return false;
				room->sendCompulsoryTriggerLog(who,"tenyearxizhen");
				int num = 1;
				if(p->isAlive()&& p->getHp()== p->getMaxHp())
					num = 2;
				who->drawCards(num,"tenyearxizhen");
				room->recover(p,RecoverStruct("tenyearxizhen",who));
			}
		}
		return false;
	}
};

class Wangzu : public TriggerSkill
{
public:
	Wangzu(): TriggerSkill("wangzu")
	{
		events << DamageInflicted;
	}

	int getFriends(ServerPlayer*player)const
	{
		Room*room = player->getRoom();
		int fri = 0;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isYourFriend(player))
				fri++;
		}
		return fri;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("wangzuUsed-Clear")> 0 || !player->canDiscard(player,"h"))return false;
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from==player)return false;

		bool most = true;
		int fri = getFriends(player);
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(getFriends(p)> fri){
				most = false;
				break;
			}
		}

		bool invoke = false;
		QString number = QString::number(damage.damage);
		player->setTag("WangzuDamage", data);

		if(most)
			invoke = room->askForCard(player,".|.|.|hand","@wangzu-discard:" + number,data,objectName());
		else
			invoke = player->askForSkillInvoke(this,"wangzu:" + number);

		player->removeTag("WangzuDamage");
		if(!invoke)return false;

		room->addPlayerMark(player,"wangzuUsed-Clear");
		player->peiyin(this);

		if(!most){
			QList<int> ids;
			foreach(int id,player->handCards()){
				if(player->canDiscard(player,id))
					ids << id;
			}
			if(ids.isEmpty())return false;
			int id = ids.at(qrand()% ids.length());
			room->throwCard(id,objectName(),player);
		}

		damage.damage--;
		data = QVariant::fromValue(damage);
		if(damage.damage <= 0)
			return true;
		return false;
	}
};

YingruiCard::YingruiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool YingruiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& Self->inMyAttackRange(to_select)&& to_select != Self;
}

void YingruiCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	room->giveCard(from,to,this,"yingrui");

	if(to->isDead())return;

	if(from->isDead())
		room->damage(DamageStruct("yingrui",nullptr,to));
	else {
		const Card*card = room->askForExchange(to,"yingrui",99999,2,true,"@yingrui-give:" + from->objectName(),true,"EquipCard");
		if(card){
		room->giveCard(to,from,card,"yingrui",true);
		} else
			room->damage(DamageStruct("yingrui",from->isAlive()? from : nullptr,to));
	}
}

class Yingrui : public OneCardViewAsSkill
{
public:
	Yingrui(): OneCardViewAsSkill("yingrui")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("YingruiCard");
	}

	const Card*viewAs(const Card*c)const
	{
		YingruiCard*card = new YingruiCard();
		card->addSubcard(c);
		return card;
	}
};

class Fuyuan : public TriggerSkill
{
public:
	Fuyuan(): TriggerSkill("fuyuan")
	{
		events << TargetConfirmed;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("SkillCard"))return false;
		if(!use.to.contains(player))return false;

		int n = 0;
		if(use.card->isRed()){
			player->addMark("fuyuan_red-Clear");
			if(use.card->isKindOf("Slash"))
				n++;
		}

		if(use.card->isKindOf("Slash")&& player->getMark("fuyuan_red-Clear")== n){
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(!p->askForSkillInvoke(this,player))continue;
				room->broadcastSkillInvoke(this);
				player->drawCards(1,objectName());
				if(player->isDead())break;
			}
		}
		return false;
	}
};

class TenyearKuanshiEffect : public TriggerSkill
{
public:
	TenyearKuanshiEffect(): TriggerSkill("#tenyearkuanshi-effect")
	{
		events << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead())continue;
			if(damage.to->isDead()|| damage.to->getMark("&tenyearkuanshi+#" + p->objectName())<= 0)continue;
			room->addPlayerMark(damage.to,"tenyearkuanshi_damage-Clear",damage.damage);
			if(damage.to->getMark("tenyearkuanshi_damage-Clear")>= 2){
				room->setPlayerMark(damage.to,"&tenyearkuanshi+#" + p->objectName(),0);
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = damage.to;
				log.arg = "tenyearkuanshi";
				room->sendLog(log);
				room->broadcastSkillInvoke("tenyearkuanshi");
				room->notifySkillInvoked(p,"tenyearkuanshi");
				room->recover(damage.to,RecoverStruct("tenyearkuanshi",p));
			}
		}
		return false;
	}
};

class TenyearQingren : public PhaseChangeSkill
{
public:
	TenyearQingren(): PhaseChangeSkill("tenyearqingren")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer*player,Room*)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		int mark = player->getMark("tenyearqingren_yizan-Clear");
		if(mark <= 0)return false;
		if(!player->askForSkillInvoke(this,"tenyearqingren:" + QString::number(mark)))return false;
		player->peiyin(this);
		player->drawCards(mark,objectName());
		return false;
	}
};

class TenyearLongyuan : public TriggerSkill
{
public:
	TenyearLongyuan(): TriggerSkill("tenyearlongyuan")
	{
		events << EventPhaseChanging;
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*player,room->getAllPlayers()){
			if(player->isDead()|| !player->hasSkill(objectName())|| player->getMark(objectName())> 0)continue;
			if(player->getMark("&yizan")>2){
				LogMessage log;
				log.type = "#LongyuanWake";
				log.from = player;
				log.arg = QString::number(player->getMark("&yizan"));
				log.arg2 = objectName();
				room->sendLog(log);
			}else if(!player->canWake(objectName()))
				continue;
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());

			room->doSuperLightbox(player,"tenyearlongyuan");
			room->setPlayerMark(player,"tenyearlongyuan",1);

			if(room->changeMaxHpForAwakenSkill(player,0,objectName())){
				player->drawCards(2,objectName());
				room->recover(player,RecoverStruct(objectName(),player));
				room->setPlayerProperty(player,"yizan_level",1);
				room->changeTranslation(player,"yizan",2);
			}
			room->setPlayerMark(player,"&yizan",0);
		}
		return false;
	}
};

class Yiyong : public TriggerSkill
{
public:
	Yiyong(): TriggerSkill("yiyong")
	{
		events << DamageCaused;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("yiyongUsed-Clear")> 1)return false;
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer*to = damage.to;
		if(player==damage.to || damage.to->isDead()|| to->isNude())return false;
		if(!player->canDiscard(player,"he")|| !player->askForSkillInvoke(this,to))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"yiyongUsed-Clear");

		QStringList cards;
		foreach(int id,player->handCards()+ player->getEquipsId()){
			if(player->canDiscard(player,id))
				cards << QString::number(id);
		}
		if(cards.isEmpty())return false;

		player->setTag("YiyongTarget", QVariant::fromValue(to));
		const Card*player_card = room->askForExchange(player,objectName(),999999,1,true,
								"@yiyong-discard:" + to->objectName(),false,cards.join(","));
		player->removeTag("YiyongTarget");

		cards.clear();

		foreach(int id,to->handCards()+ to->getEquipsId()){
			if(to->canDiscard(to,id))
				cards << QString::number(id);
		}
		if(cards.isEmpty()){
			QList<int> handcards = to->handCards();

			JsonArray gongxinArgs;
			gongxinArgs << to->objectName();
			gongxinArgs << false;
			gongxinArgs << JsonUtils::toJsonArray(handcards);

			foreach(int cardId,handcards){
				WrappedCard*card = Sanguosha->getWrappedCard(cardId);
				if(card->isModified())
					room->broadcastUpdateCard(room->getOtherPlayers(to),cardId,card);
				else
					room->broadcastResetCard(room->getOtherPlayers(to),cardId);
			}

			LogMessage log;
			log.type = "$JileiShowAllCards";
			log.from = to;
			foreach(int card_id,handcards)
				Sanguosha->getCard(card_id)->setFlags("visible");
			log.card_str = ListI2S(handcards).join("+");
			room->sendLog(log);

			room->doBroadcastNotify(QSanProtocol::S_COMMAND_SHOW_ALL_CARDS,gongxinArgs);

			QVariant data = ListI2V(handcards);
			room->getThread()->trigger(ShowCards,room,to,data);

			return false;
		}

		to->setTag("YiyongTarget", QVariant::fromValue(player));
		const Card*to_card = room->askForExchange(to,objectName(),999999,1,true,
								"@yiyong-discard:" + player->objectName(),false,cards.join(","));
		to->removeTag("YiyongTarget");

		QList<int> player_to_discard = player_card->getSubcards(),to_to_discard = to_card->getSubcards();

		LogMessage log;
		log.type = "$DiscardCard";
		log.from = player;
		log.card_str = ListI2S(player_to_discard).join("+");
		room->sendLog(log);

		log.from = to;
		log.card_str = ListI2S(to_to_discard).join("+");
		room->sendLog(log);

		QList<CardsMoveStruct> moves;

		CardMoveReason reason1(CardMoveReason::S_REASON_THROW,player->objectName(),objectName(),"");
		CardsMoveStruct move1(player_to_discard,player,nullptr,Player::PlaceUnknown,Player::DiscardPile,reason1);
		moves.append(move1);

		CardMoveReason reason2(CardMoveReason::S_REASON_THROW,to->objectName(),objectName(),"");
		CardsMoveStruct move2(to_to_discard,to,nullptr,Player::PlaceUnknown,Player::DiscardPile,reason2);
		moves.append(move2);

		room->moveCardsAtomic(moves,true);

		int player_num = 0,to_num = 0;
		foreach(int id,player_to_discard)
			player_num += Sanguosha->getCard(id)->getNumber();
		foreach(int id,to_to_discard)
			to_num += Sanguosha->getCard(id)->getNumber();

		log.type = "#YiyongNum";
		log.from = player;
		log.arg = QString::number(player_num);
		room->sendLog(log);

		log.from = to;
		log.arg = QString::number(to_num);
		room->sendLog(log);

		if(player_num <= to_num)
			player->drawCards(to_to_discard.length(),objectName());
		if(player_num >= to_num){
			log.type = "#YiyongDamage";
			log.from = player;
			room->sendLog(log);
			++damage.damage;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Xingchong : public TriggerSkill
{
public:
	Xingchong(): TriggerSkill("xingchong")
	{
		events << RoundStart << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player || !move.from_places.contains(Player::PlaceHand))return false;
			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)!= Player::PlaceHand)continue;
				int id = move.card_ids.at(i);
				if(player->getMark("xingchong_" + QString::number(id)+ "_lun")<= 0)continue;
				room->setPlayerMark(player,"xingchong_" + QString::number(id)+ "_lun",0);
				if(player->isDead())continue;
				room->sendCompulsoryTriggerLog(player,"xingchong");
				player->drawCards(2,"xingchong");
			}
		}else{
			int max = player->getMaxHp();
			if(max <= 0 || !player->askForSkillInvoke(this))return false;
			player->peiyin(this);

			int _max = max + 1;
			QStringList choices;
			for(int i = 0; i < _max; i++)
				choices << "xingchong=" + QString::number(i);
			QString choice = room->askForChoice(player,objectName(),choices.join("+"));

			int draw = choice.split("=").last().toInt();
			player->drawCards(draw,objectName());
			if(player->isNude())return false;

			int show = max - draw;
			if(show <= 0)return false;

			const Card*ex = room->askForExchange(player,objectName(),show,1,false,"@xingchong-show:" + QString::number(show),true);
			if(!ex)return false;

			foreach(int id,ex->getSubcards()){
				room->setPlayerMark(player,"xingchong_" + QString::number(id)+ "_lun",1);
				room->setCardTip(id,"xingchong");
			}
			room->showCard(player,ex->getSubcards());
		}
		return false;
	}
};

class Liunian : public TriggerSkill
{
public:
	Liunian(): TriggerSkill("liunian")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		int times = room->getTag("SwapPile").toInt();
		if(times > 2)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(times==1){
				if(p->getMark("liunian1-Keep")> 0)continue;
				room->sendCompulsoryTriggerLog(p,this);
				room->setPlayerMark(p,"liunian1-Keep",1);
				room->gainMaxHp(p,1,objectName());
			} else if(times==2){
				if(p->getMark("liunian2-Keep")> 0)continue;
				room->sendCompulsoryTriggerLog(p,this);
				room->setPlayerMark(p,"liunian2-Keep",1);
				room->recover(p,RecoverStruct("liunian",p));
				room->addMaxCards(p,10);
				room->addPlayerMark(p,"&liunian2");
			}
		}
		return false;
	}
};

GuowuCard::GuowuCard()
{
	mute = true;
}

bool GuowuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.length()< 2* Self->getMark("guowu_three-PlayClear")&& to_select->hasFlag("guowu_canchoose");
}

void GuowuCard::onUse(Room*room,CardUseStruct&card_use)const
{
	foreach(ServerPlayer*p,card_use.to)
		room->setPlayerFlag(p,"guowu_choose");
}

class GuowuVS : public ZeroCardViewAsSkill
{
public:
	GuowuVS(): ZeroCardViewAsSkill("guowu")
	{
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@guowu");
	}

	const Card*viewAs()const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@guowu1")
			return new ExtraCollateralCard;
		return new GuowuCard;
	}
};

class Guowu : public TriggerSkill
{
public:
	Guowu(): TriggerSkill("guowu")
	{
		events << PreCardUsed << EventPhaseStart;
		view_as_skill = new GuowuVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getPhase()== Player::Play;
	}

	int getTypeNum(ServerPlayer*player)const
	{
		QList<int> type_ids;
		foreach(const Card*c,player->getHandcards()){
			int id = c->getTypeId();
			if(type_ids.contains(id))continue;
			type_ids << id;
		}
		return type_ids.length();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			if(player->getMark("guowu_three-PlayClear")<1)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
			QList<ServerPlayer*> extras = room->getCardTargets(player,use.card,use.to);
			if(extras.isEmpty())return false;
			int mark = 2*player->getMark("guowu_three-PlayClear");
			if(use.card->isKindOf("Collateral")){
				for(int i = 0; i < mark; i++){
					extras = room->getCardTargets(player,use.card,use.to);
					if(extras.isEmpty())break;
					QStringList tos;
					tos << use.card->toString();
					foreach(ServerPlayer*t,use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player,"extra_collateral",tos.join("+"));
					if(!room->askForUseCard(player,"@@guowu1","@guowu1:" + use.card->objectName(),1))break;
					ServerPlayer*p = player->getTag("ExtraCollateralTarget").value<ServerPlayer*>();
					player->removeTag("ExtraCollateralTarget");
					if(p){
						use.to.append(p);
						room->sortByActionOrder(use.to);
						data = QVariant::fromValue(use);
					}
				}
			} else {
				foreach(ServerPlayer*p,extras)
					room->setPlayerFlag(p,"guowu_canchoose");
				room->askForUseCard(player,"@@guowu2","@guowu2:" + use.card->objectName()+ "::" + QString::number(mark),2);
				LogMessage log;
				foreach(ServerPlayer*p,extras){
					room->setPlayerFlag(p,"-guowu_canchoose");
					if(p->hasFlag("guowu_choose")){
						room->setPlayerFlag(p,"-guowu_choose");
						log.to << p;
					}
				}
				if(log.to.isEmpty())return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "guowu";
				room->sendLog(log);
				use.to << log.to;
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		}else{
			if(player->isKongcheng()|| !player->hasSkill(objectName()))return false;
			int n = getTypeNum(player);
			if(!player->askForSkillInvoke("guowu","guowu:" + QString::number(n)))return false;
			room->broadcastSkillInvoke("guowu");
			room->showAllCards(player);
			n = getTypeNum(player);
			LogMessage log;
			log.type = "#GuoWuType";
			log.from = player;
			log.arg = QString::number(n);
			room->sendLog(log);
			if(n >= 1){
				QList<int> slashs;
				foreach(int id,room->getDiscardPile()){
					if(Sanguosha->getCard(id)->isKindOf("Slash"))
						slashs << id;
				}
				if(!slashs.isEmpty()){
					int id = slashs.at(qrand()% slashs.length());
					room->obtainCard(player,id);
				}
			}
			if(n >= 2&& player->isAlive())
				room->addPlayerMark(player,"guowu_two-PlayClear");
			if(n >= 3&& player->isAlive())
				room->addPlayerMark(player,"guowu_three-PlayClear");
		}
		return false;
	}
};

class Zhuangrong : public TriggerSkill
{
public:
	Zhuangrong(): TriggerSkill("zhuangrong")
	{
		events << EventPhaseChanging;
		frequency = Wake;
		waked_skills = "shenwei,wushuang";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark(objectName())> 0)continue;
			if(p->canWake(objectName())|| p->getHandcardNum()== 1 || p->getHp()== 1){
				room->sendCompulsoryTriggerLog(p,this);
				room->doSuperLightbox(p,"zhuangrong");
				room->setPlayerMark(p,objectName(),1);

				if(room->changeMaxHpForAwakenSkill(p,-1,objectName())){
					int recover = p->getMaxHp()- p->getHp();
					room->recover(p,RecoverStruct(p,nullptr,recover,"zhuangrong"));
					p->drawCards(p->getMaxHp()- p->getHandcardNum(),objectName());
					room->handleAcquireDetachSkills(p,"shenwei|wushuang");
				}
			}
		}
		return false;
	}
};

class Fengxiang : public TriggerSkill
{
public:
	Fengxiang(): TriggerSkill("fengxiang")
	{
		events << Damaged << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	static bool sortByXi(ServerPlayer*a,ServerPlayer*b)
	{
		return a->getMark("&lyznxi")> b->getMark("&lyznxi");
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			if(player->isDead()||!player->hasSkill(objectName()))return false;
			room->sendCompulsoryTriggerLog(player,this);
			QList<ServerPlayer*> players = room->getAlivePlayers();
			std::sort(players.begin(),players.end(),sortByXi);
			int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
			if(mark2 != mark1)
				room->recover(players.first(),RecoverStruct("fengxiang",player));
			else
				player->drawCards(1,objectName());
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.reason.m_reason==CardMoveReason::S_REASON_GIVE
				&&move.to_place==Player::PlaceHand&&move.reason.m_skillName=="zhuning"){
				//QVariantList xi = room->getTag("ZhuningXi").toList();
				int n = 0;
				foreach(int id,player->handCards()){
					if(move.card_ids.contains(id))
						room->setCardTip(id,"lyznxi");
					if(Sanguosha->getCard(id)->hasTip("lyznxi"))
						n++;
				}
				room->setPlayerMark(player,"&lyznxi",n);

				QList<ServerPlayer*> players = room->getAlivePlayers();
				std::sort(players.begin(),players.end(),sortByXi);
				int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
				ServerPlayer*mostxi = room->getTag("MostXiPlayer").value<ServerPlayer*>();
				if(mostxi){
					if(mark1>0){
						if(mostxi!=players.first()){
							room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
							foreach(ServerPlayer*p,room->getAllPlayers()){
								if(p->isDead()|| !p->hasSkill(objectName()))continue;
								room->sendCompulsoryTriggerLog(p,this);
								p->drawCards(1,objectName());
							}
						}
					}else{
						room->removeTag("MostXiPlayer");
						foreach(ServerPlayer*p,room->getAllPlayers()){
							if(p->isDead()|| !p->hasSkill(objectName()))continue;
							room->sendCompulsoryTriggerLog(p,this);
							p->drawCards(1,objectName());
						}
					}
				}else if(mark2 != mark1){
					room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
					foreach(ServerPlayer*p,room->getAllPlayers()){
						if(p->isDead()|| !p->hasSkill(objectName()))continue;
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(1,objectName());
					}
				}
			}else if(move.from==player&&move.from_places.contains(Player::PlaceHand)){
				int n = 0;
				foreach(const Card*c,player->getHandcards()){
					if(c->hasTip("lyznxi"))
						n++;
				}
				room->setPlayerMark(player,"&lyznxi",n);
				QList<ServerPlayer*> players = room->getAlivePlayers();
				std::sort(players.begin(),players.end(),sortByXi);
				int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
				ServerPlayer*mostxi = room->getTag("MostXiPlayer").value<ServerPlayer*>();
				if(mostxi){
					if(mark1>0){
						if(mostxi!=players.first()){
							room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
							foreach(ServerPlayer*p,room->getAllPlayers()){
								if(p->isDead()|| !p->hasSkill(objectName()))continue;
								room->sendCompulsoryTriggerLog(p,this);
								p->drawCards(1,objectName());
							}
						}
					}else{
						room->removeTag("MostXiPlayer");
						foreach(ServerPlayer*p,room->getAllPlayers()){
							if(p->isDead()|| !p->hasSkill(objectName()))continue;
							room->sendCompulsoryTriggerLog(p,this);
							p->drawCards(1,objectName());
						}
					}
				}else if(mark2 != mark1){
					room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
					foreach(ServerPlayer*p,room->getAllPlayers()){
						if(p->isDead()|| !p->hasSkill(objectName()))continue;
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(1,objectName());
					}
				}
			}

			/*if(!move.from_places.contains(Player::PlaceHand)&& move.to_place != Player::PlaceHand)return false;

			ServerPlayer*mostxi = room->getTag("MostXiPlayer").value<ServerPlayer*>();
			std::sort(players.begin(),players.end(),sortByXi);

			int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
			if(mark1 > 0&& mark2 != mark1){
				room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
				if(players.first()!= mostxi&& mostxi){
					foreach(ServerPlayer*p,room->getAllPlayers()){
						if(p->isDead()|| !p->hasSkill(objectName()))continue;
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(1,objectName());
					}
				}
			}
			if(data.toString()!= "fengxiang")return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(1,objectName());*/
		}
		return false;
	}
};

ZhuningCard::ZhuningCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void ZhuningCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->giveCard(from,to,this,"zhuning");

	if(from->isDead())return;
	QList<int> cards = room->getAvailableCardList(from,"basic,trick","zhuning");
	foreach(int id,cards){
		const Card*card = Sanguosha->getEngineCard(id);
		if(!card->isKindOf("Slash")&& !card->isDamageCard())
			cards.removeOne(id);
	}
	if(cards.isEmpty())return;

	room->fillAG(cards,from);
	int id = room->askForAG(from,cards,true,"zhuning");
	room->clearAG(from);
	if(id < 0)return;
	room->setPlayerMark(from,"zhuning_id-PlayClear",id + 1);

	room->askForUseCard(from,"@@zhuning","@zhuning:" + Sanguosha->getEngineCard(id)->objectName(),-1,
		Card::MethodUse,false,nullptr,nullptr,"zhuning_used_card_" + from->objectName());
}

class ZhuningVS : public ViewAsSkill
{
public:
	ZhuningVS(): ViewAsSkill("zhuning")
	{
	}

	bool viewFilter(const QList<const Card*>&,const Card*)const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@zhuning")
			return false;
		return true;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@zhuning"){
			if(!cards.isEmpty())return nullptr;
			int id = Self->getMark("zhuning_id-PlayClear")- 1;
			if(id < 0)return nullptr;
			const Card*card = Sanguosha->getEngineCard(id);
			Card*c = Sanguosha->cloneCard(card->objectName(),Card::NoSuit,0);
			c->setSkillName("_zhuning");
			return c;
		}

		if(cards.isEmpty())return nullptr;
		ZhuningCard*c = new ZhuningCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("zhuning_extra-PlayClear")> 0)
			return player->usedTimes("ZhuningCard")< 2;
		return !player->hasUsed("ZhuningCard");
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@zhuning";
	}
};

class Zhuning : public TriggerSkill
{
public:
	Zhuning(): TriggerSkill("zhuning")
	{
		events << DamageDone << CardFinished;
		view_as_skill = new ZhuningVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			QVariantList xi = room->getTag("ZhuningXi").toList();

			if(move.from==player&& move.from_places.contains(Player::PlaceHand)){
				foreach(int id,move.card_ids){
					if(xi.contains(QVariant(id)))
						xi.removeOne(id);
				}
				room->setTag("ZhuningXi",xi);

				int mark = 0;
				foreach(int id,player->handCards()){
					if(xi.contains(QVariant(id)))
						mark++;
				}
				room->setPlayerMark(player,"&lyznxi",mark);

				const TriggerSkill*fengxiang = Sanguosha->getTriggerSkill("fengxiang");  //神杀对CardsMoveOneTimeStruct的处理，导致只能这样做了
				if(fengxiang){
					ServerPlayer*mostxi = room->getTag("MostXiPlayer").value<ServerPlayer*>();
					QList<ServerPlayer*> players = room->getAlivePlayers();
					std::sort(players.begin(),players.end(),Fengxiang::sortByXi);
					int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
					if(mark1 > 0&& mark2 != mark1){
						room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
						if(players.first()!= mostxi&& mostxi){
							foreach(ServerPlayer*p,room->getAllPlayers()){
								if(p->isDead()|| !p->hasSkill("fengxiang"))continue;
								QVariant _data = "fengxiang";
								fengxiang->trigger(CardsMoveOneTime,room,p,_data);
							}
						}
					}
				}
			}

			if(move.to != player || move.to_place != Player::PlaceHand || move.reason.m_reason != CardMoveReason::S_REASON_GIVE
				|| move.reason.m_skillName != objectName())return false;
			foreach(int id,move.card_ids){
				if(xi.contains(QVariant(id)))continue;
				room->setCardTip(id,"lyznxi");
				xi << id;
			}
			room->setTag("ZhuningXi",xi);

			int mark = 0;
			foreach(int id,player->handCards()){
				if(xi.contains(QVariant(id)))
					mark++;
			}
			room->setPlayerMark(player,"&lyznxi",mark);

			const TriggerSkill*fengxiang = Sanguosha->getTriggerSkill("fengxiang");
			if(fengxiang){
				ServerPlayer*mostxi = room->getTag("MostXiPlayer").value<ServerPlayer*>();
				QList<ServerPlayer*> players = room->getAlivePlayers();
				std::sort(players.begin(),players.end(),Fengxiang::sortByXi);
				int mark1 = players.first()->getMark("&lyznxi"),mark2 = players.at(1)->getMark("&lyznxi");
				if(mark1 > 0&& mark2 != mark1){
					room->setTag("MostXiPlayer",QVariant::fromValue(players.first()));
					if(players.first()!= mostxi&& mostxi){
						foreach(ServerPlayer*p,room->getAllPlayers()){
							if(p->isDead()|| !p->hasSkill("fengxiang"))continue;
							QVariant _data = "fengxiang";
							fengxiang->trigger(CardsMoveOneTime,room,p,_data);
						}
					}
				}
			}
		} else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card)return false;
			if(damage.card->isKindOf("Slash")||(damage.card->isDamageCard()&& damage.card->isNDTrick()))
				room->setCardFlag(damage.card,"zhuning_damaged");
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card || use.card->hasFlag("zhuning_damaged"))return false;
			if(use.card->isKindOf("Slash")||(use.card->isDamageCard()&& use.card->isNDTrick())){
				ServerPlayer*user = nullptr;
				foreach(QString flag,use.card->getFlags()){
					if(!flag.startsWith("zhuning_used_card_"))continue;
					QString name = flag.split("_").last();
					user = room->findChild<ServerPlayer*>(name);
					if(user)break;
				}
				if(user&& user->isAlive())
					room->addPlayerMark(user,"zhuning_extra-PlayClear");
			}
		}
		return false;
	}
};

class ZhengeVS : public ZeroCardViewAsSkill
{
public:
	ZhengeVS(): ZeroCardViewAsSkill("zhenge")
	{
		response_pattern = "@@zhenge!";
	}

	const Card*viewAs()const
	{
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_zhenge");
		return slash;
	}
};

class Zhenge : public PhaseChangeSkill
{
public:
	Zhenge(): PhaseChangeSkill("zhenge")
	{
		view_as_skill = new ZhengeVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"@zhenge-invoke",true,true);
		if(!t)return false;
		room->broadcastSkillInvoke(objectName());

		QStringList names = player->property("ZhengeTargets").toStringList();
		if(!names.contains(t->objectName())){
			names << t->objectName();
			room->setPlayerProperty(player,"ZhengeTargets",names);
		}

		if(t->getMark("&zhenge")< 5){
			room->addAttackRange(t,1,false);
			room->addPlayerMark(t,"&zhenge");
		}
		if(t->isDead()|| player->isDead())return false;

		bool can_slash = false;
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->deleteLater();
		slash->setSkillName("_zhenge");

		foreach(ServerPlayer*p,room->getOtherPlayers(t)){
			if(!t->inMyAttackRange(p))return false;
			if(t->canSlash(p,slash))
				can_slash = true;
		}
		if(!can_slash || !player->askForSkillInvoke(this,"zhenge_slash:" + t->objectName(),false))return false;

		if(room->askForUseCard(t,"@@zhenge!","@zhenge"))return false;

		QList<ServerPlayer*> tos;
		foreach(ServerPlayer*p,room->getOtherPlayers(t)){
			if(t->canSlash(p,slash))
				tos << p;
		}
		if(tos.isEmpty())return false;
		ServerPlayer*to = tos.at(qrand()% tos.length());
		room->useCard(CardUseStruct(slash,t,to));
		return false;
	}
};

class Xinghan : public TriggerSkill
{
public:
	Xinghan(): TriggerSkill("xinghan")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*)const
	{
		return true;
	}

	bool isOnlyMostHandcardNumPlayer(ServerPlayer*player)const
	{
		int num = player->getHandcardNum();
		Room*room = player->getRoom();
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHandcardNum()>= num)
				return true;
		}
		return false;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.card || !damage.card->isKindOf("Slash")|| !damage.card->hasFlag("xinghan_first_slash"))return false;

		ServerPlayer*user = room->getCardUser(damage.card);
		if(!user)return false;
		QString name = user->objectName();

		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			QStringList names = p->property("ZhengeTargets").toStringList();
			if(!names.contains(name))continue;
			room->sendCompulsoryTriggerLog(p,this);
			int x = 1;
			if(isOnlyMostHandcardNumPlayer(p))//&& user->isAlive()
				x = qMin(5,user->getAttackRange());
			p->drawCards(x,objectName());
		}
		return false;
	}
};

CuijianCard::CuijianCard()
{
}

bool CuijianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*)const
{
	return targets.isEmpty()&& !to_select->isKongcheng();
}

void CuijianCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	DummyCard*jink = new DummyCard;
	jink->deleteLater();
	foreach(const Card*card,effect.to->getHandcards()){
		if(card->isKindOf("Jink"))
			jink->addSubcard(card);
	}

	int length = jink->subcardsLength();
	if(length > 0){
		if(effect.from==effect.to)return;
		if(effect.to->getArmor())
			jink->addSubcard(effect.to->getArmor());
		room->giveCard(effect.to,effect.from,jink,"cuijian",true);

		if(effect.from->isAlive()&& effect.to->isAlive()){
			int give = length;
			if(effect.from->getMark("tongyuan_peach-Keep")> 0)
				give = 1;
			if(give <= 0 || effect.from->isNude())return;
			const Card*ex = room->askForExchange(effect.from,"cuijian",give,give,true,
												"@cuijian-give:" + effect.to->objectName()+ "::" + QString::number(give));
			room->giveCard(effect.from,effect.to,ex,"cuijian");
		}
	} else {
		if(effect.from->isDead())return;
		if(effect.from->getMark("tongyuan_nullification-Keep")> 0)
			effect.from->drawCards(1,"cuijian");
		else {
			if(!effect.from->canDiscard(effect.from,"h"))return;
			room->askForDiscard(effect.from,"cuijian",1,1,false,true);
		}
	}
}

class Cuijian : public ZeroCardViewAsSkill
{
public:
	Cuijian(): ZeroCardViewAsSkill("cuijian")
	{
	}

	const Card*viewAs()const
	{
		return new CuijianCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("CuijianCard");
	}
};

class Tongyuan : public TriggerSkill
{
public:
	Tongyuan(): TriggerSkill("tongyuan")
	{
		events << CardFinished;
		frequency = Compulsory;
	}

	void sendLog(ServerPlayer*player)const
	{
		if(!player->hasSkill("cuijian",true))return;
		Room*room = player->getRoom();
		LogMessage log;
		log.type = "#TongyuanChangeTranslation";
		log.arg = objectName();
		log.arg2 = "cuijian";
		log.from = player;
		room->sendLog(log);
		room->broadcastSkillInvoke(this);
		room->notifySkillInvoked(player,objectName());
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->hasFlag("CurrentPlayer"))return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("Peach")){
			if(player->getMark("tongyuan_peach-Keep")<= 0){
				sendLog(player);
				room->addPlayerMark(player,"tongyuan_peach-Keep");
				int num = player->getMark("tongyuan_nullification-Keep")> 0 ? 3 : 2;
				room->changeTranslation(player,"cuijian",num);
				room->changeTranslation(player,"tongyuan",num);
				if(num==3)
					room->setPlayerMark(player,"&tongyuan-Keep",1);
			}
		} else if(use.card->isKindOf("Nullification")){
			if(player->getMark("tongyuan_nullification-Keep")<= 0){
				sendLog(player);
				room->addPlayerMark(player,"tongyuan_nullification-Keep");
				int num = player->getMark("tongyuan_peach-Keep")> 0 ? 3 : 1;
				room->changeTranslation(player,"cuijian",num);
				room->changeTranslation(player,"tongyuan",num);
				if(num==3)
					room->setPlayerMark(player,"&tongyuan-Keep",1);
			}
		}
		return false;
	}
};

class TongyuanEffect : public TriggerSkill
{
public:
	TongyuanEffect(): TriggerSkill("#tongyuan")
	{
		events << CardUsed << PreHpRecover;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	void sendLog(ServerPlayer*player,const QString&name)const
	{
		Room*room = player->getRoom();
		LogMessage log;
		log.type = "#Tongyuan" + name;
		log.arg = "tongyuan";
		log.arg2 = name;
		log.from = player;
		room->sendLog(log);
		room->broadcastSkillInvoke("tongyuan");
		room->notifySkillInvoked(player,"tongyuan");
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			if(player->getMark("&tongyuan-Keep")<= 0)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Nullification")){
				sendLog(player,"nullification");
				use.no_offset_list << "_ALL_TARGETS";
				data = QVariant::fromValue(use);
			} else if(use.card->isKindOf("Peach")){
				sendLog(player,"peach");
				room->setCardFlag(use.card,"tongyuan_peach");
			}
		} else {
			RecoverStruct rec = data.value<RecoverStruct>();
			if(!rec.card || !rec.card->isKindOf("Peach")|| !rec.card->hasFlag("tongyuan_peach"))return false;
			room->setCardFlag(rec.card,"-tongyuan_peach");
			++rec.recover;
			data = QVariant::fromValue(rec);
		}
		return false;
	}
};

SecondCuijianCard::SecondCuijianCard()
{
}

bool SecondCuijianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*)const
{
	return targets.isEmpty()&& !to_select->isKongcheng();
}

void SecondCuijianCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	DummyCard*get = new DummyCard;
	get->deleteLater();
	bool jink = false;
	foreach(const Card*card,effect.to->getCards("he")){
		if(card->isKindOf("Jink"))
			jink = true;
		if(card->isKindOf("Jink")|| card->isKindOf("Armor"))
			get->addSubcard(card);
	}

	if(!jink){
		if(effect.from->getMark("secondtongyuan_redtrick-Keep")> 0)
			effect.from->drawCards(2,"secondcuijian");
		return;
	}

	int length = get->subcardsLength();
	if(length > 0){
		if(effect.from==effect.to)return;
		room->giveCard(effect.to,effect.from,get,"secondcuijian",true);
		if(effect.from->isAlive()&& effect.to->isAlive()){
			int give = length;
			if(effect.from->getMark("secondtongyuan_redbasic-Keep")> 0)
				give = 0;
			if(give <= 0 || effect.from->isNude())return;
			const Card*ex = room->askForExchange(effect.from,"secondcuijian",give,give,true,
												"@cuijian-give:" + effect.to->objectName()+ "::" + QString::number(give));
			room->giveCard(effect.from,effect.to,ex,"secondcuijian");
		}
	}
}

class SecondCuijian : public ZeroCardViewAsSkill
{
public:
	SecondCuijian(): ZeroCardViewAsSkill("secondcuijian")
	{
	}

	const Card*viewAs()const
	{
		return new SecondCuijianCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("SecondCuijianCard");
	}
};

class SecondTongyuan : public TriggerSkill
{
public:
	SecondTongyuan(): TriggerSkill("secondtongyuan")
	{
		events << CardFinished << CardResponded;
		frequency = Compulsory;
	}

	void sendLog(ServerPlayer*player)const
	{
		if(!player->hasSkill("secondcuijian",true))return;
		Room*room = player->getRoom();
		LogMessage log;
		log.type = "#TongyuanChangeTranslation";
		log.arg = objectName();
		log.arg2 = "secondcuijian";
		log.from = player;
		room->sendLog(log);
		room->broadcastSkillInvoke(this);
		room->notifySkillInvoked(player,objectName());
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isRed())return false;
			if(use.card->isKindOf("TrickCard")){
				if(player->getMark("secondtongyuan_redtrick-Keep")<= 0){
					sendLog(player);
					room->addPlayerMark(player,"secondtongyuan_redtrick-Keep");
					int num = player->getMark("secondtongyuan_redbasic-Keep")> 0 ? 3 : 1;
					room->changeTranslation(player,"secondcuijian",num);
					room->changeTranslation(player,"secondtongyuan",num);
					if(num==3)
						room->setPlayerMark(player,"&secondtongyuan-Keep",1);
				}
			} else if(use.card->isKindOf("BasicCard")){
				if(player->getMark("secondtongyuan_redbasic-Keep")<= 0){
					sendLog(player);
					room->addPlayerMark(player,"secondtongyuan_redbasic-Keep");
					int num = player->getMark("secondtongyuan_redtrick-Keep")> 0 ? 3 : 2;
					room->changeTranslation(player,"secondcuijian",num);
					room->changeTranslation(player,"secondtongyuan",num);
					if(num==3)
						room->setPlayerMark(player,"&secondtongyuan-Keep",1);
				}
			}
		} else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(res.m_isUse || !res.m_card->isRed()|| !res.m_card->isKindOf("BasicCard"))return false;
			if(player->getMark("secondtongyuan_redbasic-Keep")<= 0){
				sendLog(player);
				room->addPlayerMark(player,"secondtongyuan_redbasic-Keep");
				int num = player->getMark("secondtongyuan_redtrick-Keep")> 0 ? 3 : 2;
				room->changeTranslation(player,"secondcuijian",num);
				room->changeTranslation(player,"secondtongyuan",num);
				if(num==3)
					room->setPlayerMark(player,"&secondtongyuan-Keep",1);
			}
		}
		return false;
	}
};

class SecondTongyuanEffect : public TriggerSkill
{
public:
	SecondTongyuanEffect(): TriggerSkill("#secondtongyuan")
	{
		events << CardUsed << PreCardUsed;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getMark("&secondtongyuan-Keep")> 0;
	}

	void sendLog(ServerPlayer*player,const QString&name)const
	{
		Room*room = player->getRoom();
		LogMessage log;
		log.type = "#SecondTongyuanredtrick";
		log.arg = "secondtongyuan";
		log.arg2 = name;
		log.from = player;
		room->sendLog(log);
		room->broadcastSkillInvoke("secondtongyuan");
		room->notifySkillInvoked(player,"secondtongyuan");
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(event==CardUsed){
			if(use.card->isRed()&& use.card->isNDTrick()){
				sendLog(player,use.card->objectName());
				use.no_respond_list << "_ALL_TARGETS";
				data = QVariant::fromValue(use);
			}
		} else {
			if(use.card->isRed()&& use.card->isKindOf("BasicCard")){
				QList<ServerPlayer*> targets = room->getCardTargets(player,use.card,use.to);
				if(targets.isEmpty())return false;
				player->setTag("SecondTongyuanData", data);
				ServerPlayer*t = room->askForPlayerChosen(player,targets,"secondtongyuan","@secondtongyuan-add:" + use.card->objectName(),true);
				player->removeTag("SecondTongyuanData");
				if(!t)return false;
				LogMessage log;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.to << t;
				log.card_str = use.card->toString();
				log.arg = "secondtongyuan";
				room->sendLog(log);
				room->broadcastSkillInvoke("secondtongyuan");
				room->notifySkillInvoked(player,"secondtongyuan");
				use.to << t;
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class TenyearXiecui : public TriggerSkill
{
public:
	TenyearXiecui(): TriggerSkill("tenyearxiecui")
	{
		events << DamageCaused;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from->isDead()|| damage.from->getPhase()== Player::NotActive
			|| !damage.card || damage.card->isKindOf("SkillCard"))return false;
		player->addMark("tenyearxiecui_damage-Clear");

		if(player->getMark("tenyearxiecui_damage-Clear")!= 1)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			QString prompt = QString("tenyearxiecui:%1::%2").arg(player->objectName()).arg(damage.damage + 1);
			if(damage.to)prompt = QString("tenyearxiecui:%1:%2:%3").arg(player->objectName()).arg(damage.to->objectName()).arg(damage.damage + 1);
			p->setTag("TenyearXiecuiDamage", data);

			if(!p->askForSkillInvoke(this,prompt))continue;
			p->peiyin(this);

			damage.damage++;
			data = QVariant::fromValue(damage);
			if(player->isAlive()&& player->getKingdom()== "wu"){
				room->addPlayerMark(player,"&tenyearxiecui_add-Clear");
				if(room->CardInTable(damage.card))
					room->obtainCard(player,damage.card);
				room->addMaxCards(player,1);
			}
			if(player->isDead())break;
		}
		return false;
	}
};

class TenyearYouxu : public TriggerSkill
{
public:
	TenyearYouxu(): TriggerSkill("tenyearyouxu")
	{
		events << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& !target->isKongcheng()&& target->getHandcardNum()> target->getHp();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;

		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead()|| player->isKongcheng()|| player->getHandcardNum()<= player->getHp())return false;
			if(p->isDead()|| !p->hasSkill(objectName()))continue;

			if(!p->askForSkillInvoke(this,player))continue;
			p->peiyin(this);

			int id = room->askForCardChosen(p,player,"h",objectName());
			room->showCard(player,id);

			QList<ServerPlayer*> targets = room->getOtherPlayers(p);
			if(targets.contains(player))
				targets.removeOne(player);
			if(targets.isEmpty())continue;

			room->fillAG(QList<int>()<< id,p);
			ServerPlayer*t = room->askForPlayerChosen(p,targets,objectName(),"@tenyearyouxu-give");
			room->clearAG(p);

			room->giveCard(p,t,Sanguosha->getCard(id),objectName(),true);

			if(t->isAlive()&& t->isLowestHpPlayer())
				room->recover(t,RecoverStruct("tenyearyouxu",p));
		}
		return false;
	}
};

class Tongli : public TriggerSkill
{
public:
	Tongli(): TriggerSkill("tongli")
	{
		events << TargetSpecified << CardFinished;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play&&player->hasSkill(objectName())){
				QList<Card::Suit> suits;
				foreach(const Card*c,player->getHandcards()){
					Card::Suit suit = c->getSuit();
					if(suits.contains(suit))continue;
					suits << suit;
				}
				int num = player->getMark("secondmobilexinzifu-PlayClear");
				if(num != suits.length())return false;
				QString prompt = QString("tongli:%1::%2").arg(use.card->objectName()).arg(num);
				if(!player->askForSkillInvoke(objectName()+"$-1",prompt))return false;
				LogMessage log;
				log.type = "#TongliTimes";
				log.card_str = use.card->toString();
				log.arg = QString::number(num);
				log.to = use.to;
				room->sendLog(log);
				if(use.card->isKindOf("EquipCard")|| use.card->isKindOf("DelayedTrick"))return false;
				player->setMark("tongliUse",num);
				player->setTag("tongliUse", data);
			}
		}else{
			int num = player->getMark("tongliUse");
			CardUseStruct use = data.value<CardUseStruct>();
			CardUseStruct use2 = player->getTag("tongliUse").value<CardUseStruct>();
			if(num>0&&use.card==use2.card){
				use2.m_addHistory = false;
				for(int i = 0; i < num; i++){
					foreach(ServerPlayer*p,use2.to){
						if(p->isDead())return false;
					}
					use2.card->use(room,player,use2.to);
				}
			}
		}
		return false;
	}
};

class Shezang : public TriggerSkill
{
public:
	Shezang(): TriggerSkill("shezang")
	{
		events << Dying;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getMark("shezang_used_lun")> 0)return false;
		DyingStruct dying = data.value<DyingStruct>();
		if(dying.who==player || player->hasFlag("CurrentPlayer")){
			if(!player->askForSkillInvoke(this))return false;
			player->peiyin(this);
			room->addPlayerMark(player,"shezang_used_lun");

			QList<int> heart,diamond,club,spade,other,ids,draw = room->getDrawPile();
			foreach(int id,draw){
				Card::Suit suit = Sanguosha->getCard(id)->getSuit();
				if(suit==Card::Heart)
				heart << id;
				else if(suit==Card::Diamond)
					diamond << id;
				else if(suit==Card::Spade)
					spade << id;
				else if(suit==Card::Club)
					club << id;
				else
					other << id;
			}

			if(!heart.isEmpty()){
				int id = heart.at(qrand()% heart.length());
				ids << id;
			}
			if(!diamond.isEmpty()){
				int id = diamond.at(qrand()% diamond.length());
				ids << id;
			}
			if(!club.isEmpty()){
				int id = club.at(qrand()% club.length());
				ids << id;
			}
			if(!spade.isEmpty()){
				int id = spade.at(qrand()% spade.length());
				ids << id;
			}
			if(!other.isEmpty()){
				int id = other.at(qrand()% other.length());
				ids << id;
			}
			if(ids.isEmpty())return false;

			DummyCard get(ids);
			room->obtainCard(player,&get);
		}
		return false;
	}
};

FupingCard::FupingCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FupingCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets,to_select,Self);
	}
	return false;
}

bool FupingCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool FupingCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetsFeasible(targets,Self);
	}
	return false;
}

const Card*FupingCard::validate(CardUseStruct&card_use)const
{
	ServerPlayer*source = card_use.from;
	Room*room = source->getRoom();

	QString record = source->property("SkillDescriptionRecord_fuping").toString();
	if(record.isEmpty())return nullptr;

	QString to_fuping = user_string;
	if(user_string.contains("+")){
		QStringList to_fupings;
		foreach(QString cn,user_string.split("+")){
			if(source->getMark("fuping_guhuo_remove_"+cn+"-Clear")<1&&record.contains(cn))
				to_fupings << cn;
		}
		if(to_fupings.isEmpty())return nullptr;
		to_fuping = room->askForChoice(source,"fuping",to_fupings.join("+"));
	}

	Card*c = Sanguosha->cloneCard(to_fuping);
	if(!c)return nullptr;
	c->addSubcards(subcards);
	c->setSkillName("fuping");
	c->deleteLater();
	if(source->isLocked(c))return nullptr;
	room->addPlayerMark(source,"fuping_guhuo_remove_"+to_fuping+"-Clear");
	return c;
}

const Card*FupingCard::validateInResponse(ServerPlayer*source)const
{
	Room*room = source->getRoom();
	QString record = source->property("SkillDescriptionRecord_fuping").toString();
	if(record.isEmpty())return nullptr;

	QString to_fuping = user_string;
	if(user_string.contains("+")){
		QStringList to_fupings;
		foreach(QString cn,user_string.split("+")){
			if(source->getMark("fuping_guhuo_remove_"+cn+"-Clear")<1&&record.contains(cn))
				to_fupings << cn;
		}
		if(to_fupings.isEmpty())return nullptr;
		to_fuping = room->askForChoice(source,"fuping",to_fupings.join("+"));
	}

	Card*c = Sanguosha->cloneCard(to_fuping);
	if(!c)return nullptr;
	c->addSubcards(subcards);
	c->setSkillName("fuping");
	c->deleteLater();
	if(source->isLocked(c))return nullptr;
	room->addPlayerMark(source,"fuping_guhuo_remove_"+to_fuping+"-Clear");
	return c;
}

class FupingVS : public OneCardViewAsSkill
{
public:
	FupingVS(): OneCardViewAsSkill("fuping")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card*to_select)const
	{
		return !to_select->isKindOf("BasicCard");
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		QString record = player->property("SkillDescriptionRecord_fuping").toString();
		if(record.isEmpty()||pattern.startsWith(".")|| pattern.startsWith("@"))return false;
		QStringList patterns = pattern.split("+");
		foreach(QString cn,record.split("+")){
			if(player->getMark("fuping_guhuo_remove_" + cn + "-Clear")> 0)continue;
			foreach(QString patt,patterns){
				if(cn.endsWith(patt))
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->property("SkillDescriptionRecord_fuping").toString().isEmpty();
	}

	const Card*viewAs(const Card*originalCard)const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag(objectName()).value<const Card*>();
			if(c)pattern = c->objectName();
			else return nullptr;
		}
		FupingCard*sc = new FupingCard;
		sc->addSubcard(originalCard);
		sc->setUserString(pattern);
		return sc;
	}
};

class Fuping : public TriggerSkill
{
public:
	Fuping(): TriggerSkill("fuping")
	{
		events << CardFinished << EventAcquireSkill << EventLoseSkill << ThrowEquipArea << CardUsed;
		view_as_skill = new FupingVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance(objectName(),true,true,true,true,true,true);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			QString name = player->getTag("fupingCn"+use.card->toString()).toString();
			foreach(ServerPlayer*p,use.to){
				if(p->isDead()|| p==use.from || !p->hasEquipArea()|| !p->hasSkill(objectName()))continue;
				QStringList records = p->property("SkillDescriptionRecord_fuping").toString().split("+");
				if(records.contains(name))continue;
				if(name.endsWith("slash")){
					bool slash = false;
					foreach(QString rec,records){
						if(rec.endsWith("slash")){
							slash = true;
							break;
						}
					}
					if(slash)continue;
				}
				if(!p->askForSkillInvoke(this,"fuping:" + name))continue;
				p->peiyin(this);
				QStringList choices;
				for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
					if(p->hasEquipArea(i))
						choices << QString::number(i);
				}
				if(choices.isEmpty())continue;
				QString choice = room->askForChoice(p,objectName(),choices.join("+"));

				p->throwEquipArea(choice.toInt());

				records << name;
				choices.clear();
				foreach(QString src,records){
					if(src.isEmpty())records.removeOne(src);
					else choices << src << "|";
				}
				room->setPlayerProperty(p,"SkillDescriptionRecord_fuping",records.join("+"));
				p->setSkillDescriptionSwap(objectName(),"%arg11",choices.join("+"));
				room->changeTranslation(p,objectName(),1);
				QVariant _data = "fuping_record";
				room->getThread()->trigger(EventForDiy,room,p,_data);
			}
		} else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			player->setTag("fupingCn"+use.card->toString(), use.card->objectName());
		} else if(event==EventAcquireSkill){
			if(data.toString()!= "fuping" || player->getMark("fuping_lose_all_area")<= 0 || !player->hasSkill("fuping",true))return false;
			room->setPlayerMark(player,"&fuping_buff",1);
		} else if(event==EventLoseSkill){
			if(data.toString()!= "fuping" || player->hasSkill("fuping",true))return false;
			room->setPlayerMark(player,"&fuping_buff",0);
		}else{
			if(player->getMark("fuping_lose_all_area")> 0)return false;
			foreach(QVariant area,data.toList())
				player->setMark("fuping_lose_area_" + area.toString(),1);
			for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
				if(player->getMark("fuping_lose_area_" + QString::number(i))<= 0)
					return false;
			}
			player->setMark("fuping_lose_all_area",1);
			if(player->hasSkill("fuping",true))
				room->setPlayerMark(player,"&fuping_buff",1);
		}
		return false;
	}
};

WeilieCard::WeilieCard()
{
}

bool WeilieCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void WeilieCard::onUse(Room*room,CardUseStruct&use)const
{
	room->addPlayerMark(use.from,"weilie_used_times");
	SkillCard::onUse(room,use);
}

void WeilieCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->recover(to,RecoverStruct("weilie",from));
	if(to->getHp()< to->getMaxHp())
		to->drawCards(1,"weilie");
}

class WeilieVS : public OneCardViewAsSkill
{
public:
	WeilieVS(): OneCardViewAsSkill("weilie")
	{
		filter_pattern = ".!";
	}

	const Card*viewAs(const Card*c)const
	{
		WeilieCard*card = new WeilieCard;
		card->addSubcard(c);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("weilie_used_times")< player->getMark("&weilie_time")+ 1;
	}
};

class Weilie : public TriggerSkill
{
public:
	Weilie(): TriggerSkill("weilie")
	{
		events << EventForDiy;
		view_as_skill = new WeilieVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(data.toString()!= "fuping_record")return false;
		LogMessage log;
		log.from = player;
		log.arg = objectName();
		log.type = "#WeilieLog";
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player,objectName());
		room->addPlayerMark(player,"&weilie_time");
		return false;
	}
};

YuanyuCard::YuanyuCard()
{
	target_fixed = true;
}

void YuanyuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	source->drawCards(1,"yuanyu");
	if(!source->isKongcheng()){
		const Card* c = room->askForExchange(source,"yuanyu",1,1,false,"@yuanyu-put");
		source->addToPile("zyyyyuan",c);
	}
	if(source->isDead())return;
	ServerPlayer*t = room->askForPlayerChosen(source,room->getOtherPlayers(source),"yuanyu","@yuanyu-target");
	room->doAnimate(1,source->objectName(),t->objectName());
	room->setPlayerMark(t,"&yuanyu+#" + source->objectName(),1);
}

class YuanyuVS : public ZeroCardViewAsSkill
{
public:
	YuanyuVS(): ZeroCardViewAsSkill("yuanyu")
	{
	}

	const Card*viewAs()const
	{
		return new YuanyuCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("YuanyuCard");
	}
};

class Yuanyu : public TriggerSkill
{
public:
	Yuanyu(): TriggerSkill("yuanyu")
	{
		events << Damage;
		view_as_skill = new YuanyuVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& !target->isKongcheng();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		int n = damage.damage;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| player->getMark("&yuanyu+#" + p->objectName())<= 0)continue;
			if(player->isDead()|| player->isKongcheng())return false;
			room->sendCompulsoryTriggerLog(p,this);
			for(int i = 0; i < n; i++){
				if(player->isDead()|| player->isKongcheng())return false;
				const Card* c = room->askForExchange(player,"yuanyu",1,1,false,"@yuanyu-put2:" + p->objectName());
				p->addToPile("zyyyyuan",c);
			}
		}
		return false;
	}
};

class Xiyan : public TriggerSkill
{
public:
	Xiyan(): TriggerSkill("xiyan")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to==player&& move.to_place==Player::PlaceSpecial&& move.to_pile_name=="zyyyyuan"){
			QList<Card::Suit> suits;
			QList<int> yuans = player->getPile("zyyyyuan");
			foreach(int id,yuans){
				const Card*card = Sanguosha->getCard(id);
				Card::Suit suit = card->getSuit();
				if(suits.contains(suit))continue;
				suits << suit;
			}
			if(suits.length()< 4)return false;
			room->sendCompulsoryTriggerLog(player,this);

			foreach(ServerPlayer*p,room->getOtherPlayers(player))
				room->setPlayerMark(p,"&yuanyu+#" + player->objectName(),0);

			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = player;
			log.arg = "zyyyyuan";
			log.card_str = ListI2S(yuans).join("+");
			room->sendLog(log);
			DummyCard dummy(yuans);
			room->obtainCard(player,&dummy,true);

			ServerPlayer*current = room->getCurrent();
			if(!current)return false;

			if(current==player){
				room->addPlayerMark(current,"&xiyan1-Clear");
				room->addMaxCards(current,4);
			} else {
				room->addPlayerMark(current,"&xiyan2-Clear");
				room->addMaxCards(current,-4);
				room->setPlayerCardLimitation(current,"use","BasicCard",true);
			}
		}
		return false;
	}
};

ChenjianCard::ChenjianCard()
{
	mute = true;
	will_throw = false;
}

bool ChenjianCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.length()== 0;
}

void ChenjianCard::onUse(Room*room,CardUseStruct&use)const
{
	room->addPlayerMark(use.to.first(),"chenjian_target-Clear");
}

class ChenjianVS : public OneCardViewAsSkill
{
public:
	ChenjianVS(): OneCardViewAsSkill("chenjian")
	{
		expand_pile = "#chenjian";
	}

	bool viewFilter(const Card*to_select)const
	{
		int id = to_select->getEffectiveId();
		if(Self->hasCard(id))
			return Self->canDiscard(Self,id);
		else if(Self->getPile("#chenjian").contains(id)){
			const Card*card = Sanguosha->getCard(id);
			return card->isAvailable(Self)&& !Self->isLocked(card)&& Sanguosha->getCurrentCardUsePattern()!= "@@chenjian2";
		}
		return false;
	}

	const Card*viewAs(const Card*originalCard)const
	{
		if(Self->getHandcards().contains(originalCard)){
			ChenjianCard*card = new ChenjianCard;
			card->addSubcard(originalCard);
			return card;
		}
		return originalCard;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@chenjian");
	}
};

class Chenjian : public PhaseChangeSkill
{
public:
	Chenjian(): PhaseChangeSkill("chenjian")
	{
		view_as_skill = new ChenjianVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		int num = player->getMark("SkillDescriptionArg1_chenjian");
		num = qMax(num,3);
		if(!player->askForSkillInvoke(this,"chenjian:" + QString::number(num)))return false;
		player->peiyin(this);

		QList<int> ids = room->showDrawPile(player,num,objectName());

		QString pattern = "@@chenjian1",prompt = "@chenjian1";
		int xxx = 0;
		try {
			for(int i = 0; i < 2; i++){
				if(player->isDead()|| ids.isEmpty())break;

				if(!pattern.endsWith("2"))
					room->notifyMoveToPile(player,ids,objectName(),Player::PlaceTable,true);

				const Card*c = room->askForUseCard(player,pattern,prompt,1);

				if(!pattern.endsWith("2"))
					room->notifyMoveToPile(player,ids,objectName(),Player::PlaceTable,false);
				if(!c)break;

				xxx++;

				int card_id = c->getSubcards().first();
				const Card*ccc = Sanguosha->getCard(card_id);
				Card::Suit suit = ccc->getSuit();

				ServerPlayer*geter = nullptr;
				foreach(ServerPlayer*p,room->getAllPlayers(true)){
					if(p->getMark("chenjian_target-Clear")> 0){
						room->setPlayerMark(p,"chenjian_target-Clear",0);
						geter = p;
						room->doAnimate(1,player->objectName(),p->objectName());
						break;
					}
				}

				if(geter){
					pattern = "@@chenjian3";
					prompt = "@chenjian3";

					room->throwCard(ccc,"chenjian",player);
					if(geter->isDead())continue;

					DummyCard*dummy = new DummyCard;
					dummy->deleteLater();
					foreach(int id,ids){
						if(Sanguosha->getCard(id)->getSuit()== suit){
							ids.removeOne(id);
							dummy->addSubcard(id);
						};
					}
					if(dummy->subcardsLength()> 0)
						room->obtainCard(geter,dummy);
				} else {
					ids.removeOne(card_id);

					pattern = "@@chenjian2";
					prompt = "@chenjian2";
				}
			}

			if(xxx >= 2){
				if(num < 5){
					num++;
					room->setPlayerMark(player,"SkillDescriptionArg1_chenjian",num);
					player->setSkillDescriptionSwap(objectName(),"%arg1",QString::number(num));
					room->changeTranslation(player,objectName(),1);
				}

				if(player->isAlive()){
					QList<int> recast;
					foreach(int id,player->handCards()){
						if(!player->isCardLimited(Sanguosha->getCard(id),Card::MethodRecast,true))
							recast << id;
					}

					if(!recast.isEmpty()){
						CardMoveReason reason(CardMoveReason::S_REASON_RECAST,player->objectName());
						reason.m_skillName = objectName();
						CardsMoveStruct move(recast,nullptr,Player::DiscardPile,reason);
						room->moveCardsAtomic(move,true);
						player->broadcastSkillInvoke("@recast");
						LogMessage log;
						log.type = "$RecastCard";
						log.from = player;
						log.card_str = ListI2S(recast).join("+");
						room->sendLog(log);
						player->drawCards(recast.length(),"recast");
					}
				}
			}

			if(!ids.isEmpty()){
				DummyCard*dummy = new DummyCard(ids);
				dummy->deleteLater();
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),objectName(),"");
				room->throwCard(dummy,reason,nullptr);
			}
		}
		catch(TriggerEvent triggerEvent){
			if(triggerEvent==TurnBroken || triggerEvent==StageChange){
				foreach(ServerPlayer*p,room->getAllPlayers(true))
					room->setPlayerMark(p,"chenjian_target-Clear",0);
				if(!ids.isEmpty()){
					DummyCard*dummy = new DummyCard(ids);
					dummy->deleteLater();
					CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),objectName(),"");
					room->throwCard(dummy,reason,nullptr);
				}
			}
			throw triggerEvent;
		}
		return false;
	}
};

class Xixiu : public TriggerSkill
{
public:
	Xixiu(): TriggerSkill("xixiu")
	{
		events << TargetConfirming << BeforeCardsMove;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard")|| !use.to.contains(player)|| !use.from || use.from==player)return false;
			if(!use.card->hasSuit())return false;
			Card::Suit suit = use.card->getSuit();
			foreach(const Card*card,player->getEquips()){
				if(card->getSuit()!= suit)continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->drawCards(1,objectName());
				break;
			}
		} else {
			if(player->getEquips().length()!= 1)return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player || !move.from_places.contains(Player::PlaceEquip))return false;
			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				QString from = move.reason.m_playerId;
				if(from.isEmpty()|| from==player->objectName())return false;
				int equip_id = player->getEquipsId().first();
				foreach(int id,move.card_ids){
					if(id != equip_id)continue;
					room->sendCompulsoryTriggerLog(player,this);
					move.card_ids.removeOne(id);
					data = QVariant::fromValue(move);
					return false;
				}
			}
		}
		return false;
	}
};

JinhuiCard::JinhuiCard()
{
	target_fixed = true;
}

void JinhuiCard::usecard(Room*room,ServerPlayer*source,ServerPlayer*target,const Card*card)const
{
	QList<ServerPlayer*> targets;
	targets << target;
	if(card->isKindOf("Collateral")){
		QList<ServerPlayer*> victims;
		foreach(ServerPlayer*p,room->getOtherPlayers(target)){
			if(target->canSlash(p)) victims << p;
		}
		ServerPlayer*victim = room->askForPlayerChosen(source,victims,"jinhui_collateral","@jinhui-collateral:" + target->objectName());
		if(victim){
			target->setTag("attachTarget", QVariant::fromValue(victim));
			targets << victim;
		}
	}
	if(card->targetFixed())
		room->useCard(CardUseStruct(card,source));
	else
	room->useCard(CardUseStruct(card,source,targets));
}

void JinhuiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList names;
	QList<int> drawpile,show;
	foreach(int id,room->getDrawPile()){
		const Card*card = Sanguosha->getCard(id);
		if(!card->isSingleTargetCard())continue;
		QString name = card->objectName();
		if(!names.contains(name))
			names << name;
		drawpile << id;
	}
	qShuffle(drawpile);

	for(int i = 0; i < 3; i++){
		if(drawpile.isEmpty())break;
		int id = drawpile.takeFirst();
		show << id;
		const Card*card = Sanguosha->getCard(id);
		foreach(int idd,drawpile){
			if(card->sameNameWith(Sanguosha->getCard(idd)))
				drawpile.removeOne(idd);
		}
	}
	if(show.isEmpty())return;

	CardsMoveStruct move(show,nullptr,Player::PlaceTable,
		CardMoveReason(CardMoveReason::S_REASON_TURNOVER,source->objectName(),"jinhui",""));
	room->moveCardsAtomic(move,true);

	ServerPlayer*t = room->askForPlayerChosen(source,room->getOtherPlayers(source),"jinhui","@jinhui-choose");
	room->doAnimate(1,source->objectName(),t->objectName());

	try {
		QList<int> uses;
		foreach(int id,show){
			const Card*c = Sanguosha->getCard(id);
			room->setCardFlag(c,"jinhui_card");
			if(t->canUse(c,c->targetFixed()? t : source,true))
				uses << id;
			room->setCardFlag(c,"-jinhui_card");
		}
		if(!uses.isEmpty()){
			room->notifyMoveToPile(t,uses,"jinhui",Player::PlaceTable,true);
			const Card*card = room->askForUseCard(t,"@@jinhui2!","@jinhui2",2);
			room->notifyMoveToPile(t,uses,"jinhui",Player::PlaceTable,false);
			int id = -1;
			if(!card){
				id = show.at(qrand()% show.length());
				card = Sanguosha->getCard(id);
			} else
				id = card->getSubcards().first();
			show.removeOne(id);
			const Card*t_card = Sanguosha->getCard(id);
			usecard(room,t,source,t_card);
		}
		for(int i = 0; i < 2; i++){
			if(source->isDead())break;
			uses.clear();
			foreach(int id,show){
				const Card*c = Sanguosha->getCard(id);
				room->setCardFlag(c,"jinhui_card");
				ServerPlayer*sp = c->targetFixed()? source : t;
				if(sp->isAlive()&&source->canUse(c,sp,true))
					uses << id;
				room->setCardFlag(c,"-jinhui_card");
			}
			if(!uses.isEmpty()){
				room->notifyMoveToPile(source,uses,"jinhui",Player::PlaceTable,true);
				const Card*card = room->askForUseCard(source,"@@jinhui1","@jinhui1",1);
				room->notifyMoveToPile(source,uses,"jinhui",Player::PlaceTable,false);
				if(!card)break;
				int id = card->getSubcards().first();
				show.removeOne(id);
				const Card*t_card = Sanguosha->getCard(id);
				usecard(room,source,t,t_card);
			}
		}
		if(!show.isEmpty()){
			DummyCard*dummy = new DummyCard(show);
			dummy->deleteLater();
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,source->objectName(),"jinhui","");
			room->throwCard(dummy,reason,nullptr);
		}
	}
	catch(TriggerEvent triggerEvent){
		if(triggerEvent==TurnBroken || triggerEvent==StageChange){
			if(!show.isEmpty()){
				DummyCard*dummy = new DummyCard(show);
				dummy->deleteLater();
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,source->objectName(),"jinhui","");
				room->throwCard(dummy,reason,nullptr);
			}
		}
		throw triggerEvent;
	}
}

JinhuiUseCard::JinhuiUseCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodUse;
}

void JinhuiUseCard::onUse(Room*,CardUseStruct&)const
{
}

class Jinhui : public ViewAsSkill
{
public:
	Jinhui(): ViewAsSkill("jinhui")
	{
		expand_pile = "#jinhui";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return false;
		return selected.isEmpty()&& Self->getPile("#jinhui").contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return new JinhuiCard();

		if(cards.isEmpty())return nullptr;
		JinhuiUseCard*card = new JinhuiUseCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("JinhuiCard");
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@jinhui");
	}
};

class JinhuiTarget : public TargetModSkill
{
public:
	JinhuiTarget(): TargetModSkill("#jinhui-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getResidueNum(const Player*from,const Card*card,const Player*)const
	{
		if(card->hasFlag("jinhui_card"))
			return 999;
		if(from->hasFlag("wanchanUse"))
			return 999;
		if(from->getMark("&xiyan1-Clear")> 0)
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player*from,const Card*card,const Player*)const
	{
		if(card->hasFlag("jinhui_card"))
			return 999;
		if(from->hasFlag("wanchanUse"))
			return 999;
		if(from->getMark("fuping_lose_all_area")>0&&from->hasSkill("fuping"))
			return 999;
		if(from->getMark("guowu_two-PlayClear")> 0)
			return 999;
		if(card->hasFlag("fanyin_use_card"))
			return 999;
		return 0;
	}

	int getExtraTargetNum(const Player*from,const Card*card)const
	{
		if(card->getSkillName()=="shimou"&&from->getMark("shimouBf")==1)
			return 1;
		return 0;
	}
};

class Qingman : public TriggerSkill
{
public:
	Qingman(): TriggerSkill("qingman")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	int getnum(ServerPlayer*player)const
	{
		int num = 0;
		for(int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
			if(player->hasEquipArea(i)&& !player->getEquip(i))
				num++;
		}
		return num;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		ServerPlayer*current = room->getCurrent();
		if(!current || current->isDead())return false;
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			int num = getnum(current);
			if(num > p->getHandcardNum()){
				room->sendCompulsoryTriggerLog(p,this);
				p->drawCards(num - p->getHandcardNum(),objectName());
			}
		}
		return false;
	}
};

class Mingluan : public TriggerSkill
{
public:
	Mingluan(): TriggerSkill("mingluan")
	{
		events << HpRecover << EventPhaseStart;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==HpRecover){
			room->setTag("MingLuanRecover",true);
		} else {
			if(player->getPhase()== Player::Finish){
				if(room->getTag("MingLuanRecover").toBool()){
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						if(p->isDead()|| !p->hasSkill(objectName())|| !p->canDiscard(p,"he"))continue;
						if(!room->askForCard(p,"..","@mingluan-discard",QVariant(),objectName()))continue;
						room->broadcastSkillInvoke(this);
						ServerPlayer*current = room->getCurrent();
						if(!current || current->isDead())continue;
						int hand = current->getHandcardNum(),han = p->getHandcardNum();
						if(hand + han > 5)hand = 5 - han;
						if(hand > 0)p->drawCards(hand,objectName());
					}
				}
				room->setTag("MingLuanRecover",false);
			}
		}
		return false;
	}
};

class Huguan : public TriggerSkill
{
public:
	Huguan(): TriggerSkill("huguan")
	{
		events << CardResponded << CardUsed;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play&& target->getMark("wanglie-PlayClear")== 1;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		const Card*card = nullptr;
		if(event==CardUsed)
			card = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if(!resp.m_isUse)return false;
			card = resp.m_card;
		}
		if(!card || card->isKindOf("SkillCard")|| !card->isRed())return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead())return false;
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(!p->askForSkillInvoke(this,player))continue;

			int index = qrand()% 2 + 1;
			if(p->getGeneralName().contains("wangyue")|| p->getGeneral2Name().contains("wangyue"))
				index += 2;
			room->broadcastSkillInvoke(this,index);

			int suit = int(room->askForSuit(p,objectName()));

			LogMessage log;
			log.type = "#ChooseSuit";
			log.from = p;
			log.arg = Card::Suit2String(Card::Suit(suit));
			room->sendLog(log);

			if(!room->hasCurrent())continue;
			QVariantList suits = player->getTag("HuguanSuits").toList();
			if(suits.contains(QVariant(suit)))continue;
			suits << suit;
			player->setTag("HuguanSuits", suits);
		}
		return false;
	}
};

class HuguanIgnore : public TriggerSkill
{
public:
	HuguanIgnore(): TriggerSkill("#huguan")
	{
		events << EventPhaseProceeding << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseProceeding){
			if(player->isDead()|| player->getPhase()!= Player::Discard)return false;
			QVariantList suits = player->getTag("HuguanSuits").toList();
			if(suits.isEmpty())return false;
			QList<int> _suits = ListV2I(suits);

			foreach(const Card*card,player->getHandcards()){
				int suit = int(card->getSuit());
				if(!_suits.contains(suit))continue;
				room->ignoreCards(player,card);
			}
		} else {
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			player->removeTag("HuguanSuits");
		}
		return false;
	}
};

class Yaopei : public TriggerSkill
{
public:
	Yaopei(): TriggerSkill("yaopei")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(triggerEvent==CardsMoveOneTime&& TriggerSkill::triggerable(player)){
			ServerPlayer*current = room->getCurrent();
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();

			if(!current || player==current || current->getPhase()!= Player::Discard)
				return false;


			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				int i = 0;
				QVariantList discard_suits = current->getTag("YaopeiDiscardSuits").toList();
				foreach(int card_id,move.card_ids){
					if(move.from==current&&(move.from_places[i]==Player::PlaceHand || move.from_places[i]==Player::PlaceEquip)){
						discard_suits <<(int)Sanguosha->getCard(card_id)->getSuit();
					}
					i++;
				}
				current->setTag("YaopeiDiscardSuits", discard_suits);
			}
		} else if(triggerEvent==EventPhaseEnd&& player->getPhase()== Player::Discard){
			if(player->getTag("YaopeiDiscardSuits").toList().isEmpty())return false;

			try {
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->isDead()|| !p->hasSkill(objectName()))continue;

					QVariantList discard_suits = player->getTag("YaopeiDiscardSuits").toList();

					QStringList can_discards;
					foreach(const Card*card,p->getCards("he")){
						if(!discard_suits.contains(QVariant((int)card->getSuit()))&& p->canDiscard(p,card->getId()))
							can_discards << card->toString();
					}
					if(can_discards.isEmpty()|| !room->askForCard(p,can_discards.join(","),"@yaopei-discard",
												QVariant::fromValue(player),objectName()+"$-1"))continue;

					if(p->isDead())continue;

					QStringList choices;
					choices << "self=" + player->objectName();
					if(player->isAlive())
						choices << "other=" + player->objectName();

					QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(player));
					if(choice.startsWith("self")){
						room->recover(p,RecoverStruct("yaopei",p));
						player->drawCards(2,objectName());
					} else {
						room->recover(player,RecoverStruct("yaopei",p));
						p->drawCards(2,objectName());
					}
				}
			}
			catch(TriggerEvent triggerEvent){
				if(triggerEvent==TurnBroken || triggerEvent==StageChange)
					player->removeTag("YaopeiDiscardSuits");
				throw triggerEvent;
			}

			player->removeTag("YaopeiDiscardSuits");
		}
		return false;
	}
};

class Yachai : public MasochismSkill
{
public:
	Yachai(): MasochismSkill("yachai")
	{
	}

	int getCeilHandcardNum(ServerPlayer*player)const
	{
		int num = player->getHandcardNum();
		num++;
		return floor(num / 2);
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&damage)const
	{
		ServerPlayer*from = damage.from;
		if(!from || from->isDead()|| !player->askForSkillInvoke(this,from))return;
		Room*room = player->getRoom();
		room->broadcastSkillInvoke(this);

		QStringList choices;

		//int hand = ceil(from->getHandcardNum()/ 2); 依然是向下取整，大概是因为int除以int还是int
		int hand = getCeilHandcardNum(from);
		if(hand > 0&& from->canDiscard(from,"h"))
			choices << "discard=" + QString::number(hand);
		choices << "limit=" + player->objectName();
		if(!damage.from->isKongcheng())
			choices << "show=" + player->objectName();
		if(choices.isEmpty())return;

		QString choice = room->askForChoice(from,objectName(),choices.join("+"),QVariant::fromValue(damage));

		if(choice.startsWith("discard")){
			//int hand = ceil(from->getHandcardNum()/ 2);
			int hand = getCeilHandcardNum(from);
			if(hand > 0&& from->canDiscard(from,"h"))
				room->askForDiscard(from,objectName(),hand,hand);
		} else if(choice.startsWith("limit")){
			room->addPlayerMark(from,"yachai_limit-Clear");
			player->drawCards(2,objectName());
		} else {
			if(from->isKongcheng())return;
			room->showAllCards(from);
			QStringList suits;
			foreach(const Card*c,from->getHandcards()){
				QString suit = c->getSuitString();
				if(suits.contains(suit))continue;
				suits << suit;
			}
			if(suits.isEmpty())return;
			QString suit = room->askForChoice(from,"yachai_suit",suits.join("+"),QVariant::fromValue(player));
			QList<int> ids;
			foreach(const Card*c,from->getHandcards()){
				if(c->getSuitString()== suit)
					ids << c->getEffectiveId();
			}
			if(ids.isEmpty()|| player->isDead())return;
			room->giveCard(from,player,ids,objectName(),true);
		}
	}
};

class YachaiLimit : public CardLimitSkill
{
public:
	YachaiLimit(): CardLimitSkill("#yachai-limit")
	{
	}

	QString limitList(const Player*)const
	{
		return "use";
	}

	QString limitPattern(const Player*target)const
	{
		if(target->getMark("zhengyueUse-Clear")> 1)
			return ".";
		if(target->getMark("yachai_limit-Clear")> 0)
			return ".|.|.|hand";
		return "";
	}
};

QingtanCard::QingtanCard()
{
	target_fixed = true;
}

void QingtanCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QHash<ServerPlayer*,int> hash;
	QStringList suits,cards;
	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		if(p->isDead()|| p->isKongcheng())continue;
		const Card*card = room->askForCardShow(p,source,"qingtan");
		hash[p] = card->getEffectiveId()+ 1;
		cards << card->toString();
		QString suit = card->getSuitString();
		if(!suits.contains(suit))
			suits << suit;
	}
	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		int id = hash[p] - 1;
		if(id < 0)continue;
		room->showCard(p,id);
	}

	suits << "cancel";
	QString suit = room->askForChoice(source,"qingtan",suits.join("+"),cards);
	if(suit=="cancel")return;

	QList<ServerPlayer*> drawers;
	QList<CardsMoveStruct> moves;
	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		int id = hash[p] - 1;
		const Card*card = Sanguosha->getCard(id);
		if(!card || card->getSuitString()!= suit)continue;
		CardsMoveStruct move(id,source,Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,source->objectName()));
		moves << move;
		drawers << p;
	}

	if(!moves.isEmpty()&& source->isAlive())
		room->moveCardsAtomic(moves,true);

	if(!drawers.isEmpty())
		room->drawCards(drawers,1,"qingtan");

	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		if(p->isDead())continue;
		int id = hash[p] - 1;
		const Card*card = Sanguosha->getCard(id);
		if(!card || card->getSuitString()== suit || !p->hasCard(card)|| !p->canDiscard(p,id))continue;
		room->throwCard(id,"qingtan",p);
	}
}

class Qingtan : public ZeroCardViewAsSkill
{
public:
	Qingtan(): ZeroCardViewAsSkill("qingtan")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("QingtanCard");
	}

	const Card*viewAs()const
	{
		return new QingtanCard;
	}
};

class TenyearXiahuiMove : public TriggerSkill
{
public:
	TenyearXiahuiMove(): TriggerSkill("#tenyearxiahui-move")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(!move.from || move.from->isDead())return false;
		if(!move.from_places.contains(Player::PlaceHand)&& !move.from_places.contains(Player::PlaceEquip))return false;
		ServerPlayer*from =(ServerPlayer*)move.from;
		QString string = "tenyearxiahui_limited" + player->objectName();
		QVariantList limited = from->getTag(string).toList();
		if(limited.isEmpty())return false;
		QList<int> limited_ids = ListV2I(limited);

		for(int i = 0; i < move.card_ids.length(); i++){
			if(move.from_places.at(i)!= Player::PlaceHand&& move.from_places.at(i)!= Player::PlaceEquip)continue;
			if(!limited_ids.contains(move.card_ids.at(i)))continue;
			room->addPlayerMark(from,"tenyearxiahui_lose_" + player->objectName()+ "-Clear");
			break;
		}
		return false;
	}
};

class TenyearMeibu : public PhaseChangeSkill
{
public:
	TenyearMeibu(const QString&meibu): PhaseChangeSkill(meibu),meibu(meibu)
	{
		waked_skills = "tenyearzhixi";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->isDead())return false;
			if(p->isDead()|| !p->hasSkill(objectName())|| !player->inMyAttackRange(p))continue;
			if(!p->canDiscard(p,"he"))continue;
			const Card*card = room->askForCard(p,"..","@" + meibu + "-dis:" + player->objectName(),QVariant::fromValue(player),objectName());
			if(!card)continue;
			room->broadcastSkillInvoke(objectName());

			if(meibu=="secondtenyearmeibu"){
				QString mark;
				foreach(QString mk,p->getMarkNames()){
					if(!mk.startsWith("&" + meibu + "+")|| !mk.endsWith("-Clear")|| p->getMark(mk)<= 0)continue;
					mark = mk;
					break;
				}
				QString string = card->getSuitString()+ "_char";
				if(mark.isEmpty())
					mark = "&" + meibu + "+" + string + "-Clear";
				else {
					if(mark.contains(string))return false;
					room->setPlayerMark(p,mark,0);
					QString clear = "-Clear";
					mark.chop(clear.length());
					mark = mark + "+" + string + clear;
				}
				room->addPlayerMark(p,mark);
			}

			room->acquireOneTurnSkills(player,meibu,"tenyearzhixi");
		}
		return false;
	}
private:
	QString meibu;
};

class SecondTenyearMeibuGet : public TriggerSkill
{
public:
	SecondTenyearMeibuGet(): TriggerSkill("#secondtenyearmeibu-get")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!player->hasSkill("secondtenyearmeibu"))return false;
		QString mark;
		foreach(QString mk,player->getMarkNames()){
			if(!mk.startsWith("&secondtenyearmeibu+")|| !mk.endsWith("-Clear")|| player->getMark(mk)<= 0)continue;
			mark = mk;
			break;
		}
		if(mark.isEmpty())return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			if(move.reason.m_skillName != "tenyearzhixi")return false;
			QList<int> gets;
			foreach(int id,move.card_ids){
				if(room->getCardPlace(id)!= Player::DiscardPile)continue;
				const Card*card = Sanguosha->getCard(id);
				QString string = card->getSuitString()+ "_char";
				if(!mark.contains(string))continue;
				gets << id;
			}
			if(gets.isEmpty())return false;
			room->sendCompulsoryTriggerLog(player,"secondtenyearmeibu",true,true);
			DummyCard get(gets);
			room->obtainCard(player,&get,true);
		}
		return false;
	}
};

class TenyearMumu : public PhaseChangeSkill
{
public:
	TenyearMumu(): PhaseChangeSkill("tenyearmumu")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		QList<ServerPlayer*> targets,targets2;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p != player&& !p->getEquips().isEmpty()&& player->canDiscard(p,"e"))
				targets << p;
			if(p->getArmor())
				targets2 << p;
		}

		QStringList choices;
		if(!targets.isEmpty())
			choices << "discard";
		if(!targets2.isEmpty())
			choices << "get";
		if(choices.isEmpty())return false;
		if(!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());
		QString choice = room->askForChoice(player,objectName(),choices.join("+"));
		if(choice=="discard"){
			ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@tenyearmumu-dis");
			room->doAnimate(1,player->objectName(),target->objectName());
			if(!player->canDiscard(target,"e"))return false;
			int id = room->askForCardChosen(player,target,"e",objectName(),false,Card::MethodDiscard);
			room->throwCard(id,objectName(),target,player);
			room->addSlashCishu(player,1);
		} else {
			ServerPlayer*target = room->askForPlayerChosen(player,targets2,objectName(),"@tenyearmumu-get");
			room->doAnimate(1,player->objectName(),target->objectName());
			if(!target->getArmor())return false;
			room->obtainCard(player,target->getArmor(),true);
			room->addSlashCishu(player,-1);
		}
		return false;
	}
};

class SecondTenyearMumu : public PhaseChangeSkill
{
public:
	SecondTenyearMumu(): PhaseChangeSkill("secondtenyearmumu")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		QList<ServerPlayer*> targets,targets2;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p != player&& !p->getEquips().isEmpty()&& player->canDiscard(p,"e"))
				targets << p;
			if(!p->getEquips().isEmpty())
				targets2 << p;
		}

		QStringList choices;
		if(!targets.isEmpty())
			choices << "discard";
		if(!targets2.isEmpty())
			choices << "get";
		if(choices.isEmpty())return false;
		if(!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());
		QString choice = room->askForChoice(player,objectName(),choices.join("+"));
		if(choice=="discard"){
			ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@secondtenyearmumu-dis");
			room->doAnimate(1,player->objectName(),target->objectName());
			if(!player->canDiscard(target,"e"))return false;
			int id = room->askForCardChosen(player,target,"e",objectName(),false,Card::MethodDiscard);
			room->throwCard(id,objectName(),target,player);
			room->addSlashCishu(player,1);
		} else {
			ServerPlayer*target = room->askForPlayerChosen(player,targets2,objectName(),"@secondtenyearmumu-get");
			room->doAnimate(1,player->objectName(),target->objectName());
			if(target->getEquips().isEmpty())return false;
			int id = room->askForCardChosen(player,target,"e",objectName());
			room->obtainCard(player,id,true);
			room->addSlashCishu(player,-1);
		}
		return false;
	}
};

class TenyearZhixi : public TriggerSkill
{
public:
	TenyearZhixi(): TriggerSkill("tenyearzhixi")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play)return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash")&& !use.card->isNDTrick())return false;
		if(player->isKongcheng())return false;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->askForDiscard(player,objectName(),1,1);
		return false;
	}
};

XunjiCard::XunjiCard()
{
}

void XunjiCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	from->getRoom()->addPlayerMark(to,"&xunji_debuff+#" + from->objectName()+"-SelfClear");
}

class XunjiVS : public ZeroCardViewAsSkill
{
public:
	XunjiVS(): ZeroCardViewAsSkill("xunji")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("XunjiCard");
	}

	const Card*viewAs()const
	{
		return new XunjiCard;
	}
};

class Xunji : public PhaseChangeSkill
{
public:
	Xunji(): PhaseChangeSkill("xunji")
	{
		view_as_skill = new XunjiVS;
		waked_skills = "#xunji";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Finish&& target->getMark("damage_point_round")> 0;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead())return false;

			int mark = player->getMark("&xunji_debuff+#" + p->objectName()+"-SelfClear");
			room->setPlayerMark(player,"&xunji_debuff+#" + p->objectName()+"-SelfClear",0);
			if(p->isDead()|| mark <= 0)continue;

			Duel*du = new Duel(Card::NoSuit,0);
			du->deleteLater();
			du->setSkillName("_xunji");
			room->setCardFlag(du,QString("xunji_duel_%1_%2").arg(p->objectName()).arg(player->objectName()));

			for(int i = 0; i < mark; i++){
				if(player->isDead())return false;
				if(p->isDead()|| !p->canUse(du,player,true))break;

				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);

				room->useCard(CardUseStruct(du,p,player));
			}
		}
		return false;
	}
};

class XunjiLose : public TriggerSkill
{
public:
	XunjiLose(): TriggerSkill("#xunji")
	{
		events << Damage;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.card || !damage.card->isKindOf("Duel")|| !damage.to)return false;
		if(!damage.card->hasFlag(QString("xunji_duel_%1_%2").arg(player->objectName()).arg(damage.to->objectName())))return false;
		room->loseHp(HpLostStruct(player,damage.damage,"xunji",player));
		return false;
	}
};

class Jiaofeng : public TriggerSkill
{
public:
	Jiaofeng(): TriggerSkill("jiaofeng")
	{
		events << DamageCaused;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		player->addMark("jiaofeng_damage-Clear");
		if(player->getMark("jiaofeng_damage-Clear")!=1||!player->hasSkill(objectName()))return false;

		int lose = player->getLostHp();
		if(lose <= 0)return false;
		room->sendCompulsoryTriggerLog(player,this);

		if(lose > 0)
			player->drawCards(1,objectName());
		if(lose > 1){
			DamageStruct damage = data.value<DamageStruct>();
			damage.damage++;
			data = QVariant::fromValue(damage);
		}
		if(lose > 2)
			room->recover(player,RecoverStruct("zhongjie",player));
		return false;
	}
};

class Liedan : public PhaseChangeSkill
{
public:
	Liedan(): PhaseChangeSkill("liedan")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive()&& target->getPhase()== Player::Start;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->isDead())return false;
			if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("&zhuangdan-SelfClear")> 0)continue;
			room->sendCompulsoryTriggerLog(p,this);
			int handpl = player->getHandcardNum(),handp = p->getHandcardNum(),
				hppl = player->getHp(),hpp = p->getHp(),
				equippl = player->getEquips().length(),equipp = p->getEquips().length(),
				num = 0;
			if(handp > handpl)num++;
			if(hpp > hppl)num++;
			if(equipp > equippl)num++;
			p->drawCards(num,objectName());
			if(num==3&& p->isAlive()&& p->getMaxHp()< 8)
				room->gainMaxHp(p,1,objectName());
			else if(num==0&& p->isAlive()){
				room->loseHp(HpLostStruct(p,1,objectName(),p));
				if(p->isAlive())p->gainMark("&xhjldlie");
			}
		}
		if(player->hasSkill(objectName())&&player->getMark("&zhuangdan-SelfClear")<1&&player->getMark("&xhjldlie")>4){
			room->sendCompulsoryTriggerLog(player,"liedan",true,true);
			room->killPlayer(player);
		}
		return false;
	}
};

class Zhuangdan : public TriggerSkill
{
public:
	Zhuangdan(): TriggerSkill("zhuangdan")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool isMaxHandcardnumPlayer(ServerPlayer*player)const
	{
		Room*room = player->getRoom();
		int hand = player->getHandcardNum();
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHandcardNum()>= hand)
				return false;
		}
		return true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| !p->hasSkill(objectName())|| !isMaxHandcardnumPlayer(p))continue;
			room->sendCompulsoryTriggerLog(p,this);
			room->setPlayerMark(p,"&zhuangdan-SelfClear",1);
		}
		return false;
	}
};

class Cangchu : public TriggerSkill
{
public:
	Cangchu(): TriggerSkill("cangchu")
	{
		events << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->gainMark("&ccliang",qMin(3,room->alivePlayerCount()));
		} else {
			if(room->getTag("FirstRound").toBool())return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to&& move.to->isAlive()&& move.to_place==Player::PlaceHand&& move.to->getMark("cangchu-Clear")<= 0
				&& !move.to->hasFlag("CurrentPlayer")&& move.to==player&& room->hasCurrent()){
				int mark = player->getMark("&ccliang");
				if(mark >= room->alivePlayerCount())return false;
				room->sendCompulsoryTriggerLog(player,objectName(),true,true);
				room->addPlayerMark(player,"cangchu-Clear");
				player->gainMark("&ccliang");
			}
		}
		return false;
	}
};

class CangchuKeep : public MaxCardsSkill
{
public:
	CangchuKeep(): MaxCardsSkill("#cangchu-keep")
	{
	}

	int getExtra(const Player*target)const
	{
		if(target->hasSkill("cangchu"))
			return target->getMark("&ccliang");
		return 0;
	}
};

class Liangying : public PhaseChangeSkill
{
public:
	Liangying(): PhaseChangeSkill("liangying")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Discard)return false;
		int mark = player->getMark("&ccliang");
		if(mark <= 0 || !player->askForSkillInvoke(objectName()))return false;
		room->broadcastSkillInvoke(objectName());

		QStringList nums;
		for(int i = 1; i <= mark; i++)
			nums << QString::number(i);

		QString num = room->askForChoice(player,objectName(),nums.join("+"));
		player->drawCards(num.toInt(),objectName());
		if(player->isDead()|| player->isKongcheng())return false;

		QList<ServerPlayer*> alives = room->getAlivePlayers(),tos;
		while(!alives.isEmpty()){
			if(player->isDead()|| player->isKongcheng()|| alives.isEmpty())break;
			if(tos.length()>= num.toInt())break;
			QList<int> hands = player->handCards();
			ServerPlayer*to = room->askForYiji(player,hands,objectName(),false,false,false,1,alives,CardMoveReason(),"liangying-give");
			tos << to;
			alives.removeOne(to);
			room->addPlayerMark(to,"liangying-Clear");
		}

		foreach(ServerPlayer*p,room->getAllPlayers(true)){
			if(p->getMark("liangying-Clear")> 0)
				room->setPlayerMark(p,"liangying-Clear",0);
		}
		return false;
	}
};

class Shishou : public TriggerSkill
{
public:
	Shishou(): TriggerSkill("shishou")
	{
		events << CardUsed << Damaged << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			const Card*card = data.value<CardUseStruct>().card;
			if(!card->isKindOf("Analeptic"))return false;
			if(player->getMark("&ccliang")<= 0)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->loseMark("&ccliang");
		} else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.nature != DamageStruct::Fire)return false;
			int lose = qMin(player->getMark("&ccliang"),damage.damage);
			if(lose <= 0)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->loseMark("&ccliang");
		} else {
			if(player->getPhase()!= Player::Start)return false;
			if(player->getMark("&ccliang")> 0)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			room->loseHp(HpLostStruct(player,1,objectName(),player));
		}
		return false;
	}
};

LiushiCard::LiushiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool LiushiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Slash*slash = new Slash(NoSuit,0);
	slash->setSkillName("_liushi");
	slash->deleteLater();
	return targets.isEmpty()&&!Self->isLocked(slash)&&Self->canSlash(to_select,slash,false);
}

void LiushiCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	CardMoveReason reason(CardMoveReason::S_REASON_PUT,effect.from->objectName(),"liushi","");
	room->moveCardTo(this,nullptr,Player::DrawPile,reason,true);
	if(effect.from->isDead()|| effect.to->isDead())return;
	Slash*slash = new Slash(NoSuit,0);
	slash->setSkillName("_liushi");
	slash->deleteLater();
	room->setCardFlag(slash,"liushi_slash");
	room->useCard(CardUseStruct(slash,effect.from,effect.to));
}

class LiushiVS : public OneCardViewAsSkill
{
public:
	LiushiVS(): OneCardViewAsSkill("liushi")
	{
		filter_pattern = ".|heart";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		LiushiCard*c = new LiushiCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Liushi : public TriggerSkill
{
public:
	Liushi(): TriggerSkill("liushi")
	{
		events << DamageComplete;
		view_as_skill = new LiushiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.card || !damage.card->isKindOf("Slash")
			||(!damage.card->getSkillNames().contains(objectName())&& !damage.card->hasFlag("liushi_slash")))return false;
		int n = player->getTag("LiushiDamage").toInt();
		player->setTag("LiushiDamage", ++n);
		room->addMaxCards(player,-1,false);
		room->setPlayerMark(player,"&liushi",n);
		return false;
	}
};

class Zhanwan : public TriggerSkill
{
public:
	Zhanwan(): TriggerSkill("zhanwan")
	{
		events << EventPhaseEnd;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getPhase()== Player::Discard&& target->getTag("LiushiDamage").toInt()> 0
			&& target->getMark("liushi_num-Clear")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			int mark = player->getMark("liushi_num-Clear");
			if(mark <= 0)return false;
			room->sendCompulsoryTriggerLog(p,objectName(),true,true);
			p->drawCards(mark,objectName());
			int n = player->getTag("LiushiDamage").toInt();
			room->addMaxCards(player,n,false);
			player->removeTag("LiushiDamage");
			room->setPlayerMark(player,"&liushi",0);
		}
		return false;
	}
};

class ZhanwanMove : public TriggerSkill
{
public:
	ZhanwanMove(): TriggerSkill("#zhanwan-move")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(!move.from || move.from->isDead()|| move.from->getPhase()!= Player::Discard)return false;
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			ServerPlayer*from = room->findChild<ServerPlayer*>(move.from->objectName());
			if(!from || from->isDead())return false;
			room->addPlayerMark(from,"liushi_num-Clear",move.card_ids.length());
		}
		return false;
	}
};

class Xuhe : public TriggerSkill
{
public:
	Xuhe():TriggerSkill("xuhe")
	{
		events << EventPhaseStart << EventPhaseEnd;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getPhase()!= Player::Play)return false;
		if(event==EventPhaseStart){
			if(!player->askForSkillInvoke(this))return false;
			room->broadcastSkillInvoke(objectName());
			room->loseMaxHp(player,1,"xuhe");
			if(player->isDead())return false;

			QStringList choices;
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->distanceTo(p)<= 1)
					players << p;
			}
			if(players.isEmpty())return false;

			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->distanceTo(p)<= 1&& player->canDiscard(p,"he")){
					choices << "discard";
					break;
				}
			}
			choices << "draw";
			if(room->askForChoice(player,objectName(),choices.join("+"))== "discard"){
				foreach(ServerPlayer*p,players){
					if(player->isDead())return false;
					if(p->isAlive()&& player->distanceTo(p)<= 1&& player->canDiscard(p,"he")){
						int card_id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard);
						room->throwCard(card_id,objectName(),p,player);
					}
				}
			} else {
				room->drawCards(players,1,objectName());
			}

		} else {
			int maxhp = player->getMaxHp();
			bool invoke = false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getMaxHp()> maxhp){
					invoke = true;
					break;
				}
			}
			if(!invoke)return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->gainMaxHp(player,1,objectName());

			QStringList choices;
			if(player->isWounded())
				choices << "recover";
			choices << "selfdraw";
			QString choice = room->askForChoice(player,objectName(),choices.join("+"));
			if(choice=="recover")
				room->recover(player,RecoverStruct("xuhe",player));
			else
				player->drawCards(2,objectName());
		}
		return false;
	}
};

TenyearKuangfuCard::TenyearKuangfuCard()
{
}

bool TenyearKuangfuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& Self->canDiscard(to_select,"e");
}

void TenyearKuangfuCard::onEffect(CardEffectStruct&effect)const
{
	if(effect.to->getEquips().isEmpty())return;
	Room*room = effect.from->getRoom();
	int card_id = room->askForCardChosen(effect.from,effect.to,"e","tenyearkuangfu",false,Card::MethodDiscard);
	room->throwCard(card_id,"tenyearkuangfu",effect.to,effect.from);
	if(effect.from->isDead())return;
	Slash*slash = new Slash(Card::NoSuit,0);
	slash->setSkillName("_tenyearkuangfu");
	slash->deleteLater();
	QList<ServerPlayer*> targets;
	foreach(ServerPlayer*p,room->getOtherPlayers(effect.from)){
		if(effect.from->canSlash(p,slash,false))
			targets << p;
	}
	if(targets.isEmpty())return;
	if(!room->askForUseCard(effect.from,"@@tenyearkuangfu!","@tenyearkuangfu")){
		ServerPlayer*target = targets.at(qrand()% targets.length());
		room->useCard(CardUseStruct(slash,effect.from ,target),false);
	}
	bool damage = effect.from->getMark("tenyearkuangfu_damage-Clear")> 0;
	room->setPlayerMark(effect.from,"tenyearkuangfu_damage-Clear",0);
	if(effect.to==effect.from&& damage)
		effect.from->drawCards(2,"tenyearkuangfu");
	else if(effect.to != effect.from&& !damage)
		room->askForDiscard(effect.from,"tenyearkuangfu",2,2);
}

class TenyearKuangfuVS : public ZeroCardViewAsSkill
{
public:
	TenyearKuangfuVS(): ZeroCardViewAsSkill("tenyearkuangfu")
	{
		response_pattern = "@@tenyearkuangfu!";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearKuangfuCard");
	}

	const Card*viewAs()const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUsePattern()== "@@tenyearkuangfu!"){
			Slash*slash = new Slash(Card::NoSuit,0);
			slash->setSkillName("_tenyearkuangfu");
			return slash;
		}
		return new TenyearKuangfuCard;
	}
};

class TenyearKuangfu : public TriggerSkill
{
public:
	TenyearKuangfu(): TriggerSkill("tenyearkuangfu")
	{
		events << PreCardUsed << Damage;
		view_as_skill = new TenyearKuangfuVS;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains("tenyearkuangfu")&& !use.card->isKindOf("SkillCard")){
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
			}
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&& damage.card->getSkillNames().contains("tenyearkuangfu")&& !damage.card->isKindOf("SkillCard"))
				room->setPlayerMark(player,"tenyearkuangfu_damage-Clear",1);
		}
		return false;
	}
};

class Silve : public TriggerSkill
{
public:
	Silve(): TriggerSkill("silve")
	{
		events << GameStart << Damage << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart&&player->hasSkill(objectName())){
			ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"silve0:");
			if(to){
				QList<ServerPlayer*> ps;
				ps << player;
				room->doAnimate(1,player->objectName(),to->objectName(),ps);
				player->skillInvoked(this);
				room->setPlayerMark(to,"&silve+#"+player->objectName(),1,ps);
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(player->getMark("&silve+#"+p->objectName())<1)continue;
				if(p->getMark(damage.to->objectName()+"silve-Clear")>0)continue;
				if(damage.to->getCardCount()>0&&p->askForSkillInvoke(this,data)){
					room->broadcastSkillInvoke(objectName(),-1,p);
					p->addMark(damage.to->objectName()+"silve-Clear");
					int id = room->askForCardChosen(p,damage.to,"he",objectName());
					if(id>=0)room->obtainCard(p,id,false);
				}
			}
		}else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->getMark("&silve+#"+p->objectName())<1)continue;
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				room->sendCompulsoryTriggerLog(p,this);
				if(!damage.from||damage.from->isDead()||!room->askForUseSlashTo(p,damage.from,"silve1:"+damage.from->objectName())){
					QList<int> ids = p->handCards();
					qShuffle(ids);
					foreach(int id,ids){
						if(p->canDiscard(p,id)){
							room->throwCard(id,objectName(),p);
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

ShuaijieCard::ShuaijieCard()
{
	target_fixed = true;
}

void ShuaijieCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	room->removePlayerMark(source,"@shuaijieMark");
	room->doSuperLightbox(source,"shuaijie");
	room->loseMaxHp(source,1,"shuaijie");
	bool has = false;
	foreach(ServerPlayer*p,room->getAllPlayers(source)){
		if(p->getMark("&silve+#"+source->objectName())<1)continue;
		room->setPlayerMark(p,"&silve+#"+source->objectName(),0);
		if(source->getHp()>p->getHp()&&source->getEquips().length()>p->getEquips().length()){
			has = true;
			DummyCard*dummy = new DummyCard;
			for(int i = 0; i < 3; i++){
				if(dummy->subcardsLength()>=p->getCardCount())break;
				int id = room->askForCardChosen(source,p,"he","shuaijie",false,Card::MethodNone,dummy->getSubcards());
				if(id>=0)dummy->addSubcard(id);
				else break;
			}
			if(dummy->subcardsLength()>0)
				room->obtainCard(source,dummy,false);
			delete dummy;
		}
	}
	if(!has){
		DummyCard*dummy = new DummyCard;
		QStringList types;
		foreach(int id,room->getDrawPile()){
			if(!types.contains(Sanguosha->getCard(id)->getType())){
				types.append(Sanguosha->getCard(id)->getType());
				dummy->addSubcard(id);
			}
		}
		if(dummy->subcardsLength()>0)
			room->obtainCard(source,dummy,false);
		delete dummy;
	}
	room->setPlayerMark(source,"&silve+#"+source->objectName(),1);
}

class ShuaijieVS : public ZeroCardViewAsSkill
{
public:
	ShuaijieVS(): ZeroCardViewAsSkill("shuaijie")
	{
		response_pattern = "@@shuaijie";
		frequency = Limited;
		limit_mark = "@shuaijieMark";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		bool has = false;
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->getMark("&silve+#"+player->objectName())<1)continue;
			if(player->getHp()>p->getHp()&&player->getEquips().length()>p->getEquips().length())
				return player->getMark("@shuaijieMark")>0;
			has = true;
		}
		return !has&&player->getMark("@shuaijieMark")>0;
	}

	const Card*viewAs()const
	{
		return new ShuaijieCard;
	}
};

QianzhengCard::QianzhengCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void QianzhengCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	LogMessage log;
	log.type = "$RecastCard";
	log.from = source;
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST,source->objectName(),"qianzheng","");
	room->moveCardTo(this,nullptr,Player::DiscardPile,reason,true);
	source->drawCards(2,"recast");
}

class Qianzhengvs : public ViewAsSkill
{
public:
	Qianzhengvs(): ViewAsSkill("qianzheng")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return selected.length()<2&& !Self->isCardLimited(to_select,Card::MethodRecast);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()<2)return nullptr;
		QianzhengCard*card = new QianzhengCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@qianzheng");
	}
};

class Qianzheng : public TriggerSkill
{
public:
	Qianzheng():TriggerSkill("qianzheng")
	{
		events << TargetConfirming << CardsMoveOneTime;
		view_as_skill = new Qianzhengvs;
	}
    int getPriority(TriggerEvent event) const
    {
        if(event==CardsMoveOneTime) return 3;
		return TriggerSkill::getPriority(event);
    }

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isNDTrick()){
				if(use.from!=player&&player->hasTurn()&&player->getMark("qianzheng-Clear")<2&&player->getCardCount()>0){
					const Card*dc = room->askForUseCard(player,"@@qianzheng","@qianzheng-recast:"+use.card->objectName(),-1,Card::MethodRecast);
					if(dc){
						player->addMark("qianzheng-Clear");
						foreach(int id,dc->getSubcards()){
							if(Sanguosha->getCard(id)->getType()==use.card->getType())
								return false;
						}
						player->setTag("qianzhengCard", use.card->toString());
					}
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE||move.to_place!=Player::DiscardPile)return false;
			if(move.reason.m_useStruct.card->toString()==player->getTag("qianzhengCard").toString()){
				player->removeTag("qianzhengCard");
				if(room->getCardPlace(move.reason.m_useStruct.card->getEffectiveId())==Player::DiscardPile
				&&player->askForSkillInvoke(this,"qianzheng",false)){
					room->obtainCard(player,move.reason.m_useStruct.card,false);
				}
			}
		}
		return false;
	}
};

class Shuangrui : public TriggerSkill
{
public:
	Shuangrui():TriggerSkill("shuangrui")
	{
		events << EventPhaseStart << ConfirmDamage;
		waked_skills = "shaxue,shouxing";
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())){
				QList<ServerPlayer*>tos;
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(player->canSlash(p,false))
						tos << p;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tos,objectName(),"shuangrui0",true,true);
				if(tp){
					player->peiyin(this);
					Card*dc = Sanguosha->cloneCard("slash");
					dc->setSkillName("_shuangrui");
					if(player->inMyAttackRange(tp)){
						player->addMark("shuangrui_shaxue");
						room->acquireSkill(player,"shaxue");
						room->setCardFlag(dc,"shuangrui_shaxue");
					}else{
						player->addMark("shuangrui_shouxing");
						room->acquireSkill(player,"shouxing");
						room->setCardFlag(dc,"SlashNoRespond");
					}
					room->useCard(CardUseStruct(dc,player,tp));
					dc->deleteLater();
				}
			}else if(player->getPhase()==Player::NotActive){
				if(player->getMark("shuangrui_shaxue")>0){
					player->setMark("shuangrui_shaxue",0);
					room->detachSkillFromPlayer(player,"shaxue");
				}
				if(player->getMark("shuangrui_shouxing")>0){
					player->setMark("shuangrui_shouxing",0);
					room->detachSkillFromPlayer(player,"shouxing");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("shuangrui_shaxue")){
				player->damageRevises(data,1);
			}
		}
		return false;
	}
};

FuxieCard::FuxieCard()
{
}

bool FuxieCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& Self!=to_select;
}

void FuxieCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	if(subcardsLength()<1){
		QStringList skills;
		foreach(const Skill*s,source->getVisibleSkillList()){
			if(s->isAttachedLordSkill()||s->objectName()=="fuxie")continue;
			skills << s->objectName();
		}
		QString choice = room->askForChoice(source,"fuxie",skills.join("+"));
		room->detachSkillFromPlayer(source,choice);
	}
	foreach(ServerPlayer*p,targets){
		room->askForDiscard(p,"fuxie",2,2,false,true);
	}
}

class Fuxie : public ViewAsSkill
{
public:
	Fuxie(): ViewAsSkill("fuxie")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return selected.isEmpty()&&to_select->isKindOf("Weapon")
		&&!Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty()){
			foreach(const Skill*s,Self->getVisibleSkillList()){
				if(s->isAttachedLordSkill()||s->objectName()=="fuxie")continue;
				return new FuxieCard();
			}
			return nullptr;
		}
		Card*card = new FuxieCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return true;
	}
};

class Shaxue : public TriggerSkill
{
public:
	Shaxue():TriggerSkill("shaxue")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to!=player&&damage.to->isAlive()&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				player->drawCards(2,objectName());
				int n = player->distanceTo(damage.to);
				room->askForDiscard(player,objectName(),n,n,false,true);
			}
		}
		return false;
	}
};

ShouxingCard::ShouxingCard()
{
	handling_method = Card::MethodUse;
}

bool ShouxingCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(targets.isEmpty()&&!Self->inMyAttackRange(to_select)&&Self->distanceTo(to_select)==subcardsLength()){
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("shouxing");
		dc->addSubcards(subcards);
		dc->deleteLater();
		return Self->canSlash(to_select,dc,false)
		&&Self->getSlashCount()<=Sanguosha->correctCardTarget(TargetModSkill::Residue,Self,dc,to_select);
	}
	return false;
}

const Card*ShouxingCard::validate(CardUseStruct&card_use)const
{
	card_use.m_addHistory = false;
	Card*c = Sanguosha->cloneCard("slash");
	c->setSkillName("shouxing");
	c->addSubcards(subcards);
	c->deleteLater();
	return c;
}

class Shouxing : public ViewAsSkill
{
public:
	Shouxing(): ViewAsSkill("shouxing")
	{
        response_or_use = true;
	}

	bool viewFilter(const QList<const Card*>&,const Card*)const
	{
		return true;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		Card*card = new ShouxingCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("shouxing");
		dc->deleteLater();
		foreach(const Player*p,player->getAliveSiblings()){
			if(player->getSlashCount()>Sanguosha->correctCardTarget(TargetModSkill::Residue,player,dc,p))
				continue;
			if(!player->inMyAttackRange(p)&&player->canSlash(p,dc,false))
				return true;
		}
		return false;
	}
};

PingzhiCard::PingzhiCard()
{
}

bool PingzhiCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*)const
{
	return targets.isEmpty()&&!to->isKongcheng();
}

void PingzhiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		int id = room->doGongxin(source,p,p->handCards(),"pingzhi");
		room->showCard(p,id);
		if(source->getChangeSkillState("pingzhi")==1){
			room->setChangeSkillState(source,"pingzhi",2);
			if(source->canDiscard(p,id)){
				room->throwCard(id,"pingzhi",source);
				Card*dc = Sanguosha->cloneCard("FireAttack");
				dc->setSkillName("_pingzhi");
				if(p->canUse(dc,source)){
					room->setCardFlag(dc,"PingzhiDamage");
					p->setMark("PingzhiDamage"+dc->toString(),0);
					room->useCard(CardUseStruct(dc,p,source),true);
					if(p->getMark("PingzhiDamage"+dc->toString())<1)
						room->addPlayerHistory(source,"PingzhiCard",-1);
				}
				dc->deleteLater();
			}
		}else{
			room->setChangeSkillState(source,"pingzhi",1);
			const Card*c = Sanguosha->getCard(id);
			room->setCardFlag(id,"PingzhiDamage");
			p->setMark("PingzhiDamage"+c->toString(),0);
			if(c->isAvailable(p)){
				if(!room->askForUseCard(p,c->toString()+"!","pingzhi0:"+c->objectName())){
					if(c->targetFixed()){
						room->useCard(CardUseStruct(c,p),true);
					}else
						room->useCard(CardUseStruct(c,p,room->getCardTargets(p,c).first()),true);
				}
				if(p->getMark("PingzhiDamage"+c->toString())>0)
					room->addPlayerHistory(source,"PingzhiCard",-1);
			}
		}
	}
}

class PingzhiVs : public ZeroCardViewAsSkill
{
public:
	PingzhiVs(): ZeroCardViewAsSkill("pingzhi")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("PingzhiCard")<1;
	}

	const Card*viewAs()const
	{
		return new PingzhiCard;
	}
};

class Pingzhi : public TriggerSkill
{
public:
	Pingzhi():TriggerSkill("pingzhi")
	{
		events << DamageDone;
		view_as_skill = new PingzhiVs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*,ServerPlayer*,QVariant&data)const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("PingzhiDamage")&&damage.from)
				damage.from->addMark("PingzhiDamage"+damage.card->toString());
		}
		return false;
	}
};

class Gangjian : public TriggerSkill
{
public:
	Gangjian():TriggerSkill("gangjian")
	{
		events << ShowCards << EventPhaseChanging << DamageDone;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==ShowCards){
			QStringList ids = room->getTag("gangjianIds").toStringList();
			foreach(QString t,data.toString().split(":").first().split("+")){
				if(ids.contains(t))continue;
				ids.append(t);
			}
			room->setTag("gangjianIds",ids);
		}else if(event==DamageDone){
			player->addMark("gangjianDamage-Clear");
		}else{
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				QStringList ids = room->getTag("gangjianIds").toStringList();
				room->removeTag("gangjianIds");
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->getMark("gangjianDamage-Clear")<1&&ids.length()>0
					&&p->isAlive()&&p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(qMin(5,ids.length()),objectName());
					}
				}
			}
		}
		return false;
	}
};

class Guyin : public TriggerSkill
{
public:
	Guyin():TriggerSkill("guyin")
	{
		events << DrawNCards << CardsMoveOneTime;
		frequency = Compulsory;
	}
	int getPriority(TriggerEvent event)const
	{
		if(event==DrawNCards)
			return 3;
		return 2;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards")return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->hasSkill(objectName()))
					draw.num++;
			}
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				draw.num = 0;
			}
			data.setValue(draw);
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
					QList<int> ids = ListV2I(move.from->property("InitialHandCards").toList());
					if(ids.isEmpty()||!player->hasSkill(objectName()))return false;
					foreach(int id,move.card_ids){
						if(ids.contains(id)){
							room->sendCompulsoryTriggerLog(player,this);
							player->drawCards(1,objectName());
							break;
						}
					}
                }else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE&&player->hasSkill(objectName())){
					foreach(int id,move.card_ids){
						if(Sanguosha->getCard(id)->hasFlag("GuyinIds")){
							room->sendCompulsoryTriggerLog(player,this);
							player->drawCards(1,objectName());
							break;
						}
					}
				}
            }else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE&&move.from){
				QList<int> ids = ListV2I(move.from->property("InitialHandCards").toList());
				if(ids.isEmpty())return false;
				foreach(int id,move.card_ids){
					if(ids.contains(id))
						room->setCardFlag(id,"GuyinIds");
				}
			}
		}
		return false;
	}
};

PingluCard::PingluCard()
{
	target_fixed = true;
}

void PingluCard::onUse(Room*room,CardUseStruct&use)const
{
	foreach(ServerPlayer*p,room->getAlivePlayers()){
		if(use.from->inMyAttackRange(p))use.to << p;
	}
	SkillCard::onUse(room,use);
}

void PingluCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		int id = p->getRandomHandCardId();
		if(id>-1){
			room->obtainCard(source,id,false);
			if(source->handCards().contains(id))
				room->setCardTip(id,"pinglu");
		}
	}
}

class Pinglu : public ZeroCardViewAsSkill
{
public:
	Pinglu(): ZeroCardViewAsSkill("pinglu")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		foreach(const Card*h,player->getHandcards()){
			if(h->hasTip("pinglu"))
				return false;
		}
		foreach(const Player*p,player->getAliveSiblings()){
			if(player->inMyAttackRange(p)&&p->getHandcardNum()>0)
				return true;
		}
		return false;
	}

	const Card*viewAs()const
	{
		return new PingluCard;
	}
};

class ThZhangji : public TriggerSkill
{
public:
	ThZhangji():TriggerSkill("thzhangji")
	{
		events << TargetSpecifying;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()>1){
				foreach(ServerPlayer*tp,use.to){
					if(tp->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(player,this);
						use.to.removeOne(tp);
						use.to.prepend(tp);
						data.setValue(use);
						tp->drawCards(use.to.length()-1,objectName());
					}
				}
			}
		}
		return false;
	}
};

ThZhenguiCard::ThZhenguiCard()
{
	handling_method = Card::MethodNone;
	will_throw = false;
}

bool ThZhenguiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&&Self!=to_select;
}

void ThZhenguiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		room->giveCard(source,p,this,"thzhengui");
		p->addMark("thzhenguiTarget");
		foreach(int id,subcards){
			if(p->handCards().contains(id))
				room->setCardTip(id,"thzhengui");
		}
	}
	source->drawCards(subcardsLength(),"thzhengui");
}

class ThZhenguiVs : public ViewAsSkill
{
public:
	ThZhenguiVs(): ViewAsSkill("thzhengui")
	{
	}

	bool viewFilter(const QList<const Card*>&cards,const Card*)const
	{
		return cards.length()<Self->getMaxHp();
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;
		Card*card = new ThZhenguiCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getCardCount()>0&&player->usedTimes("ThZhenguiCard")<1;
	}
};

class ThZhengui : public TriggerSkill
{
public:
	ThZhengui():TriggerSkill("thzhengui")
	{
		events << CardFinished << HpRecover;
		view_as_skill = new ThZhenguiVs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getMark("thzhenguiTarget")>0;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1)return false;
		}
		player->setMark("thzhenguiTarget",0);
		room->showAllCards(player);
		int n = 0;
		foreach(const Card*h,player->getHandcards()){
			if(h->hasTip("thzhengui")){
				room->setCardTip(h->getId(),"-thzhengui");
				n++;
			}
		}
		if(n>0)
			room->loseHp(player,n,true,player,objectName());
		return false;
	}
};

class Dufeng : public TriggerSkill
{
public:
	Dufeng():TriggerSkill("dufeng")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				QStringList choices;
				choices << "dufeng1";
				for(int i = 0; i < 5; i++){
					if(player->hasEquipArea(i))
						choices << QString("dufeng2=EquipArea%1").arg(i);
				}
				for(int i = 0; i < 2; i++){
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					if(choice=="dufeng1"){
						room->loseHp(player,1,true,player,objectName());
						choices.removeOne(choice);
					}else if(choice!="cancel"){
						foreach(QString cp,choices){
							if(cp!="dufeng1")
								choices.removeOne(cp);
						}
						choice = choice.split("ea").last();
						player->throwEquipArea(choice.toInt());
					}
					choices << "cancel";
				}
				int n = player->getLostHp();
				for(int i = 0; i < 5; i++){
					if(!player->hasEquipArea(i))
						n++;
				}
				n = qMin(n,player->getMaxHp());
				player->drawCards(n,objectName());
				room->setPlayerMark(player,"&dufeng-PlayClear",n);
			}
		}
		return false;
	}
};

class DufengRange : public AttackRangeSkill
{
public:
	DufengRange(): AttackRangeSkill("#DufengRange")
	{
	}

	int getFixed(const Player*target,bool)const
	{
		if(target->getMark("&dufeng-PlayClear")>0)
			return target->getMark("&dufeng-PlayClear");
		return -1;
	}
};

YanzuoCard::YanzuoCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void YanzuoCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	source->addToPile("yanzuo",this);
	QStringList choices;
	source->addMark("&xiyan1-Clear");
	foreach(int id,source->getPile("yanzuo")){
		const Card*c = Sanguosha->getCard(id);
		if(choices.contains(c->objectName()))continue;
		if(c->isKindOf("BasicCard")||c->isNDTrick()){
			Card*dc = Sanguosha->cloneCard(c->objectName());
			dc->setSkillName("yanzuo");
			dc->deleteLater();
			if(dc->isAvailable(source))
				choices << c->objectName();
		}
	}
	source->removeMark("&xiyan1-Clear");
	if(choices.isEmpty())return;
	QString choice = room->askForChoice(source,"yanzuo",choices.join("+"));
	room->setPlayerProperty(source,"yanzuoCard",choice);
	room->askForUseCard(source,"@@yanzuo!","yanzuo0:"+choice);
}

class Yanzuo : public ViewAsSkill
{
public:
	Yanzuo(): ViewAsSkill("yanzuo")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUsePattern().contains("@@yanzuo"))
			return false;
		if(selected.isEmpty()){
			if(to_select->isKindOf("BasicCard")||to_select->isNDTrick()){
				Card*dc = Sanguosha->cloneCard(to_select->objectName());
				dc->deleteLater();
				if(dc->isAvailable(Self))
					return true;
			}
			foreach(int id,Self->getPile("yanzuo")){
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("BasicCard")||c->isNDTrick()){
					Card*dc = Sanguosha->cloneCard(c->objectName());
					dc->deleteLater();
					if(dc->isAvailable(Self))
						return true;
				}
			}
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern().contains("@@yanzuo")){
			Card*dc = Sanguosha->cloneCard(Self->property("yanzuoCard").toString());
			dc->setSkillName("_yanzuo");
			return dc;
		}
		if(cards.isEmpty())return nullptr;
		Card*card = new YanzuoCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getCardCount()>0&&player->usedTimes("YanzuoCard")<=player->getMark("YanzuoUse");
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("@@yanzuo");
	}
};

class ThZhuyin : public TriggerSkill
{
public:
	ThZhuyin():TriggerSkill("thzhuyin")
	{
		events << TargetConfirmed;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isNDTrick()){
				if(use.to.contains(player)&&player!=use.from){
					Card*dc = dummyCard();
					foreach(int id,player->getPile("yanzuo")){
						const Card*c = Sanguosha->getCard(id);
						if(c->sameNameWith(use.card))
							dc->addSubcard(id);
					}
					room->sendCompulsoryTriggerLog(player,this);
					if(dc->subcardsLength()>0){
						use.nullified_list << "_ALL_TARGETS";
						data.setValue(use);
						room->throwCard(dc,objectName(),player);
					}else{
						int n = qMin(2,player->getMark("YanzuoUse")+1);
						room->setPlayerMark(player,"YanzuoUse",n);
						room->changeTranslation(player,"yanzuo",1);
						player->setSkillDescriptionSwap("yanzuo","%src",QString::number(n+1));
						foreach(int id,room->getDrawPile()+room->getDiscardPile()){
							const Card*c = Sanguosha->getCard(id);
							if(c->sameNameWith(use.card)){
								player->addToPile("yanzuo",c);
								break;
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Pijian : public TriggerSkill
{
public:
	Pijian():TriggerSkill("pijian")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish&&player->getPile("yanzuo").length()>=room->getAlivePlayers().length()){
				room->sendCompulsoryTriggerLog(player,this);
				room->throwCard(player->getPile("yanzuo"),objectName(),player);
				if(player->isAlive()){
					ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"pijian0");
					room->doAnimate(1,player->objectName(),tp->objectName());
					room->damage(DamageStruct(objectName(),player,tp,2));
				}
			}
		}
		return false;
	}
};

class Tanban : public TriggerSkill
{
public:
	Tanban():TriggerSkill("tanban")
	{
		events << EventPhaseEnd << AfterDrawNCards;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Draw&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				foreach(const Card*c,player->getHandcards()){
					if(c->hasTip("tanban"))
						room->setCardTip(c->getId(),"-tanban");
					else
						room->setCardTip(c->getId(),"tanban");
				}
			}
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards")return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach(const Card*c,player->getHandcards()){
				room->setCardTip(c->getId(),"tanban");
			}
		}
		return false;
	}
};

class TanbanLimit : public CardLimitSkill
{
public:
	TanbanLimit(): CardLimitSkill("#TanbanLimit")
	{
	}

	QString limitList(const Player*)const
	{
		return "ignore";
	}

	QString limitPattern(const Player*target,const Card*card)const
	{
		if(card->hasTip("tanban")&&target->hasSkill("tanban"))
			return card->toString();
		if(card->hasTip("thjiaowei_xuan")&&target->hasSkill("thjiaowei"))
			return card->toString();
		if(card->hasTip("pigua")&&target->hasSkill("pigua"))
			return card->toString();
		if(card->hasTip("xi_di")&&target->hasSkill("xidi"))
			return card->toString();
		return "";
	}
};

class DiouVs : public ZeroCardViewAsSkill
{
public:
	DiouVs(): ZeroCardViewAsSkill("diou")
	{
		response_pattern = "@@diou";
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	const Card*viewAs()const
	{
		Card*dc = Sanguosha->cloneCard(Self->property("diouCard").toString());
		dc->setSkillName("_diou");
		return dc;
	}
};

class Diou : public TriggerSkill
{
public:
	Diou():TriggerSkill("diou")
	{
		events << PreCardUsed << CardFinished;
		view_as_skill = new DiouVs;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasTip("tanban")){
				room->setCardFlag(use.card,"tanbanUse");
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("tanbanUse")){
				QStringList ids;
				foreach(const Card*c,player->getHandcards()){
					if(!c->hasTip("tanban"))ids << c->toString();
				}
				if(ids.isEmpty())return false;
				const Card*c = room->askForCard(player,ids.join(","),"diou0",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					room->showCard(player,c->getId());
					player->addMark("diouShow-Clear");
					if(player->getMark("diouShow-Clear")==1||c->sameNameWith(use.card))
						player->drawCards(2,objectName());
					Card*dc = Sanguosha->cloneCard(c->objectName());
					dc->deleteLater();
					if(player->isAlive()&&dc->isAvailable(player)){
						room->setPlayerProperty(player,"diouCard",c->objectName());
						room->askForUseCard(player,"@@diou","diou1:"+c->objectName());
					}
				}
			}
		}
		return false;
	}
};

ZhengyueCard::ZhengyueCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ZhengyueCard::onUse(Room*room,CardUseStruct&use)const
{
	room->moveCardTo(this,use.from,Player::PlaceTable);
	if(use.from->isAlive())use.from->addToPile("zyzheng",subcards);
}

class ZhengyueVs : public ViewAsSkill
{
public:
	ZhengyueVs(): ViewAsSkill("zhengyue")
	{
		expand_pile = "zyzheng,#zhengyue";
	}

	bool viewFilter(const QList<const Card*>&,const Card*to_select)const
	{
		return Self->getPileName(to_select->getId()).contains("zheng");
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int n = qMax(Self->getPile("zyzheng").length(),Self->getPile("#zhengyue").length());
		if(cards.length()<n)return nullptr;
		Card*card = new ZhengyueCard();
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("@@zhengyue");
	}
};

class Zhengyue : public TriggerSkill
{
public:
	Zhengyue():TriggerSkill("zhengyue")
	{
		events << EventPhaseStart << CardFinished;
		view_as_skill = new ZhengyueVs;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart&&player->getPile("zyzheng").isEmpty()
				&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				int n = room->askForChoice(player,objectName(),"1+2+3+4+5").toInt();
				QList<int>ids = room->getNCards(n);
				room->notifyMoveToPile(player,ids,"zhengyue");
				if(!room->askForUseCard(player,"@@zhengyue","zhengyue0:"))
					player->addToPile("zyzheng",ids);
				room->notifyMoveToPile(player,ids,"zhengyue",Player::DrawPile,false);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1)return false;
			QList<int>ids = player->getPile("zyzheng");
			if(ids.isEmpty())return false;
			const Card*c = Sanguosha->getCard(ids.first());
			if(c->sameNameWith(use.card)||c->getSuit()==use.card->getSuit()||c->getNumber()==use.card->getNumber()){
				room->throwCard(c,objectName(),player);
				player->drawCards(2,objectName());
			}else if(ids.length()<5&&!room->getCardOwner(use.card->getEffectiveId())){
				room->addPlayerMark(player,"zhengyueUse-Clear");
				player->addToPile("zyzheng",use.card);
				if(player->isAlive())
					room->askForUseCard(player,"@@zhengyue","zhengyue1:");
			}
		}
		return false;
	}
};

class Tongguan : public TriggerSkill
{
public:
	Tongguan():TriggerSkill("tongguan")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart){
				player->addMark("tongguanRoundStart");
			}else if(player->getPhase()==Player::Start&&player->getMark("tongguanRoundStart")==1){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						QStringList choices;
						if(p->getMark("tg_wuyong")<2)
							choices << "tg_wuyong";
						if(p->getMark("tg_gangying")<2)
							choices << "tg_gangying";
						if(p->getMark("tg_duomou")<2)
							choices << "tg_duomou";
						if(p->getMark("tg_guojue")<2)
							choices << "tg_guojue";
						if(p->getMark("tg_renzhi")<2)
							choices << "tg_renzhi";
						if(choices.isEmpty())continue;
						room->sendCompulsoryTriggerLog(p,this);
						QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(player));
						p->addMark(choice);
						room->setPlayerMark(player,"&tongguan+"+choice,1);
					}
				}
			}
		}
		return false;
	}
};

class Mengjie : public TriggerSkill
{
public:
	Mengjie():TriggerSkill("mengjie")
	{
		events << EventPhaseChanging << DamageDone << HpRecover << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->hasSkill(objectName())){
					foreach(QString m,player->getMarkNames()){
						if(m.contains("&tongguan+")&&player->getMark(m)>0){
							m.remove("&tongguan+");
							if(player->getMark(m+"Num-Clear")>0||(m=="tg_gangying"&&player->getHp()<player->getHandcardNum())){
								room->sendCompulsoryTriggerLog(p,this);
								if(m.contains("tg_wuyong")){
									ServerPlayer*tp = room->askForPlayerChosen(p,room->getOtherPlayers(p),"tg_wuyong","tg_wuyong0");
									room->doAnimate(1,p->objectName(),tp->objectName());
									room->damage(DamageStruct(objectName(),p,tp));
								}else if(m.contains("tg_gangying")){
									ServerPlayer*tp = room->askForPlayerChosen(p,room->getAlivePlayers(),"tg_gangying","tg_gangying0");
									room->doAnimate(1,p->objectName(),tp->objectName());
									room->recover(tp,RecoverStruct(objectName(),p));
								}else if(m.contains("tg_duomou")){
									p->drawCards(2,objectName());
								}else if(m.contains("tg_guojue")){
									QList<ServerPlayer*>tps;
									foreach(ServerPlayer*q,room->getOtherPlayers(p)){
										if(p->canDiscard(q,"hej"))tps << q;
									}
									ServerPlayer*tp = room->askForPlayerChosen(p,tps,"tg_guojue","tg_guojue0");
									if(tp){
										room->doAnimate(1,p->objectName(),tp->objectName());
										Card*dc = dummyCard();
										for(int i = 0; i < 2; i++){
											int id = room->askForCardChosen(p,tp,"hej","tg_guojue",false,Card::MethodDiscard,dc->getSubcards(),i>0);
											if(id<0)break;
											dc->addSubcard(id);
										}
										room->throwCard(dc,objectName(),tp,p);
									}
								}else if(m.contains("tg_renzhi")){
									ServerPlayer*tp = room->askForPlayerChosen(p,room->getOtherPlayers(p),"tg_renzhi","tg_renzhi0");
									room->doAnimate(1,p->objectName(),tp->objectName());
									int n = tp->getMaxHp()-tp->getHandcardNum();
									if(n>0)tp->drawCards(qMin(5,n),objectName());
								}
							}
						}
					}
				}
			}
		}else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from)damage.from->addMark("tg_wuyongNum-Clear");
		}else if(event==HpRecover){
			player->addMark("tg_gangyingNum-Clear");
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player){
				if(move.reason.m_reason==CardMoveReason::S_REASON_DRAW&&player->hasFlag("CurrentPlayer")&&player->getPhase()!=Player::Draw)
					player->addMark("tg_duomouNum-Clear");
				if(move.from!=player&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
					player->addMark("tg_guojueNum-Clear");
				}
			}else if(move.reason.m_reason==CardMoveReason::S_REASON_DISMANTLE
			&&move.reason.m_playerId==player->objectName()&&move.reason.m_targetId!=player->objectName()){
				player->addMark("tg_guojueNum-Clear");
			}else if(move.reason.m_reason==CardMoveReason::S_REASON_GIVE
			&&move.reason.m_playerId==player->objectName()){
				player->addMark("tg_renzhiNum-Clear");
			}
		}
		return false;
	}
};

YinluCard::YinluCard()
{
}

bool YinluCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*,int&m)const
{
	if(Sanguosha->getCurrentCardUsePattern()=="@@yinlu1"){
		if(targets.isEmpty()){
			if(to->getMark("&yl_lequan")>0||to->getMark("&yl_huoxi")>0
			||to->getMark("&yl_zhangqi")>0){//||to->getMark("&yl_yunxiang")>0
				m = 1;
			}
		}else if(targets.length()<2&&!targets.contains(to))
			m = 1;
		return m==1;
	}
	m = targets.length()<3?1:0;
	return m==1;
}

bool YinluCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	if(Sanguosha->getCurrentCardUsePattern()=="@@yinlu1")
		return targets.length()==2;
	return targets.length()==3;
}

void YinluCard::onUse(Room*room,CardUseStruct&use)const
{
	room->setTag("YinluUse",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void YinluCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList choices;
	choices << "yl_lequan" << "yl_huoxi" << "yl_zhangqi" << "yl_yunxiang";
	CardUseStruct use = room->getTag("YinluUse").value<CardUseStruct>();
	if(Sanguosha->getCurrentCardUsePattern()=="@@yinlu1"){
		foreach(QString m,choices){
			if(use.to.first()->getMark("&"+m)<1)
				choices.removeOne(m);
		}
		QString choice = room->askForChoice(source,"jiaohao",choices.join("+"));
		use.to.first()->loseAllMarks("&"+choice);
		use.to.last()->gainMark("&"+choice);
	}else{
		foreach(ServerPlayer*p,use.to)
			p->gainMark("&"+choices.takeFirst());
		room->acquireSkill(source,choices.takeLast());
	}
}

class YinluVs : public ZeroCardViewAsSkill
{
public:
	YinluVs(): ZeroCardViewAsSkill("yinlu")
	{
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("@@yinlu");
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	const Card*viewAs()const
	{
		return new YinluCard;
	}
};

class Yinlu : public TriggerSkill
{
public:
	Yinlu():TriggerSkill("yinlu")
	{
		events << GameStart << Death << EventPhaseStart << MarkChanged;
		waked_skills = "yl_lequan,yl_huoxi,yl_zhangqi,yl_yunxiang";
		view_as_skill = new YinluVs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart){
			if(player->isAlive()&&player->hasSkill(objectName()))
				room->askForUseCard(player,"@@yinlu!","yinlu0");
		}else if(event==EventPhaseStart){
			if(player->isAlive()&&player->getPhase()==Player::Start&&player->hasSkill(objectName())){
				room->askForUseCard(player,"@@yinlu1","yinlu1");
			}
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&yl_lequan"){
				if(mark.gain>0)room->acquireSkill(player,"yl_lequan");
				else room->detachSkillFromPlayer(player,"yl_lequan");
			}else if(mark.name=="&yl_huoxi"){
				if(mark.gain>0)room->acquireSkill(player,"yl_huoxi");
				else room->detachSkillFromPlayer(player,"yl_huoxi");
			}else if(mark.name=="&yl_zhangqi"){
				if(mark.gain>0)room->acquireSkill(player,"yl_zhangqi");
				else room->detachSkillFromPlayer(player,"yl_zhangqi");
			}/*else if(mark.name=="&yl_yunxiang"){
				if(mark.gain>0)room->acquireSkill(player,"yl_yunxiang");
				else room->detachSkillFromPlayer(player,"yl_yunxiang");
			}*/
		}else if(player->isAlive()&&player->hasSkill(objectName())){
			DeathStruct death = data.value<DeathStruct>();
			QStringList choices;
			choices << "yl_lequan" << "yl_huoxi" << "yl_zhangqi";// << "yl_yunxiang";
			foreach(QString m,choices){
				if(death.who->getMark("&"+m)>0){
					ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"yinlu2:"+m,true,true);
					if(tp){
						death.who->loseAllMarks("&"+m);
						tp->gainMark("&"+m);
					}
				}
			}
		}
		return false;
	}
};

class Youqi : public TriggerSkill
{
public:
	Youqi(): TriggerSkill("youqi")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from!=player&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				if(move.reason.m_skillName=="yl_lequan"||move.reason.m_skillName=="yl_huoxi"
				||move.reason.m_skillName=="yl_zhangqi"||move.reason.m_skillName=="yl_yunxiang"){
					int n = player->distanceTo(move.from);
					if(qrand()%5>=n){
						room->sendCompulsoryTriggerLog(player,this);
						Card*dc = dummyCard();
						foreach(int id,move.card_ids){
							if(room->getCardOwner(id))continue;
							dc->addSubcard(id);
						}
						player->obtainCard(dc);
					}
				}
			}
		}
		return false;
	}
};

class YlLequan : public TriggerSkill
{
public:
	YlLequan():TriggerSkill("yl_lequan")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				if(player->isWounded()&&player->canDiscard(player,"he")
				&&room->askForCard(player,".|diamond","yl_lequan0",QVariant(),objectName())){
					room->recover(player,RecoverStruct(objectName(),player));
				}
			}
		}
		return false;
	}
};

class YlHuoxi : public TriggerSkill
{
public:
	YlHuoxi():TriggerSkill("yl_huoxi")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				if(player->canDiscard(player,"he")&&room->askForCard(player,".|heart","yl_huoxi0",QVariant(),objectName())){
					player->drawCards(2,objectName());
				}
			}
		}
		return false;
	}
};

class YlZhangqi : public TriggerSkill
{
public:
	YlZhangqi():TriggerSkill("yl_zhangqi")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				room->sendCompulsoryTriggerLog(player,this);
				if(player->canDiscard(player,"he")&&room->askForCard(player,".|spade","yl_zhangqi0")){
				}else room->loseHp(player,1,true,player,objectName());
			}
		}
		return false;
	}
};

class YlYunxiang : public TriggerSkill
{
public:
	YlYunxiang():TriggerSkill("yl_yunxiang")
	{
		events << EventPhaseStart << DamageInflicted;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				if(player->canDiscard(player,"he")&&room->askForCard(player,".|club","yl_yunxiang0",QVariant(),objectName())){
					player->gainMark("&yl_yunxiang");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			int n = player->getMark("&yl_yunxiang");
			if(n>0&&player->askForSkillInvoke(this,data)){
				player->loseMark("&yl_yunxiang",n);
				n = qMin(n,damage.damage);
				return player->damageRevises(data,-n);
			}
		}
		return false;
	}
};

JichunCard::JichunCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool JichunCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(targets.isEmpty()){
		if(to_select->getHandcardNum()>Self->getHandcardNum())
			return Self->getMark("jichun2-PlayClear")<1&&!Self->isJilei(this);
		return Self->getMark("jichun1-PlayClear")<1&&to_select->getHandcardNum()<Self->getHandcardNum();
	}
	return false;
}

void JichunCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	room->showCard(source,getEffectiveId());
	int n = Sanguosha->getCard(getEffectiveId())->nameLength();
	foreach(ServerPlayer*p,targets){
		if(p->getHandcardNum()<source->getHandcardNum()){
			room->addPlayerMark(source,"jichun1-PlayClear");
			room->giveCard(source,p,this,"jichun",true);
			source->drawCards(n,"jichun");
		}else{
			room->addPlayerMark(source,"jichun2-PlayClear");
			room->throwCard(this,"jichun",source);
			if(source->canDiscard(p,"hej")){
				Card*dc = dummyCard();
				for(int i = 0; i < n; i++){
					int id = room->askForCardChosen(source,p,"hej","jichun",false,Card::MethodDiscard,dc->getSubcards(),i>0);
					if(id<0)break;
					dc->addSubcard(id);
					if(dc->subcardsLength()>=p->getCardCount(true,true))break;
				}
				room->throwCard(dc,"jichun",p,source);
			}
		}
	}
}

class Jichun : public OneCardViewAsSkill
{
public:
	Jichun(): OneCardViewAsSkill("jichun")
	{
		filter_pattern = ".";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		Card*c = new JichunCard;
		c->addSubcard(originalCard);
		return c;
	}
	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("jichun1-PlayClear")<1||player->getMark("jichun2-PlayClear")<1;
	}
};

class Hanying : public TriggerSkill
{
public:
	Hanying():TriggerSkill("hanying")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start){
				foreach(int id,room->getDrawPile()){
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("EquipCard")){
						if(player->askForSkillInvoke(this)){
							player->peiyin(this);
							player->setMark("hanyingId",id);
							room->fillAG(QList<int>()<< id);
							QList<ServerPlayer*>tps;
							foreach(ServerPlayer*p,room->getAlivePlayers()){
								if(p->getHandcardNum()==player->getHandcardNum())
									tps << p;
							}
							ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"hanying0:"+c->objectName());
							room->clearAG();
							room->doAnimate(1,player->objectName(),tp->objectName());
							if(c->isAvailable(tp))
								room->useCard(CardUseStruct(c,tp));
						}
						break;
					}
				}
			}
		}
		return false;
	}
};

class Kengqiang : public TriggerSkill
{
public:
	Kengqiang():TriggerSkill("kengqiang")
	{
		events << DamageCaused;
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->hasTurn()){
			QStringList choices;
			if(player->getMark("kengqiang1-Clear")<1)
				choices << "kengqiang1";
			if(player->getMark("kengqiang2-Clear")<1)
				choices << "kengqiang2";
			if(choices.isEmpty()||!player->askForSkillInvoke(objectName()+"$-1",data))return false;
			QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
			if(player->getTag("shangjueUse").toBool())
				player->addMark(choice+"-Clear");
			else{
				player->addMark("kengqiang1-Clear");
				player->addMark("kengqiang2-Clear");
			}
			if(choice=="kengqiang1"){
				player->drawCards(player->getMaxHp(),objectName());
			}else{
				DamageStruct damage = data.value<DamageStruct>();
				player->damageRevises(data,1);
				if(damage.card&&!room->getCardOwner(damage.card->getEffectiveId()))
					player->obtainCard(damage.card);
			}
		}
		return false;
	}
};

class Kuishang : public TriggerSkill
{
public:
	Kuishang():TriggerSkill("kuishang")
	{
		events << EventPhaseChanging << DamageDone << CardsMoveOneTime;
		frequency = Compulsory;
		global = true;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			if(player->getMark("kuishangDamage-Clear")>=player->getMaxHp()
				&&player->getMark("kuishangDRAW-Clear")>=player->getMaxHp()){
				if(player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					room->loseHp(player,1,true,player,objectName());
				}
			}
		}else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from)damage.from->addMark("kuishangDamage-Clear");
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player==move.to&&move.reason.m_reason==CardMoveReason::S_REASON_DRAW){
				player->addMark("kuishangDRAW-Clear",move.card_ids.length());
			}
		}
		return false;
	}
};

class Shangjue : public TriggerSkill
{
public:
	Shangjue():TriggerSkill("shangjue")
	{
		events << Dying;
		frequency = Wake;
		waked_skills = "kunli";
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(player->getMark("shangjue")<1&&dying.who->getHp()<1){
				if(player->canWake(objectName())||dying.who==player){
					room->sendCompulsoryTriggerLog(player,this);
					room->addPlayerMark(player,"shangjue");
					room->doSuperLightbox(player,objectName());
					room->recover(player,RecoverStruct(objectName(),player,1-player->getHp()));
					room->gainMaxHp(player,1,objectName());
					room->acquireSkill(player,"kunli");
					player->setTag("shangjueUse", true);
					room->changeTranslation(player,"kengqiang",1);
				}
			}
		}
		return false;
	}
};

class Kunli : public TriggerSkill
{
public:
	Kunli():TriggerSkill("kunli")
	{
		events << Dying;
		frequency = Wake;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(player->getMark("kunli")<1&&dying.who->getHp()<1){
				if(player->canWake(objectName())||dying.who==player){
					room->sendCompulsoryTriggerLog(player,this);
					room->addPlayerMark(player,"kunli");
					room->doSuperLightbox(player,objectName());
					room->recover(player,RecoverStruct(objectName(),player,2-player->getHp()));
					room->gainMaxHp(player,1,objectName());
					room->detachSkillFromPlayer(player,"kuishang");
				}
			}
		}
		return false;
	}
};

WanchanCard::WanchanCard()
{
}

bool WanchanCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void WanchanCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		int n = source->distanceTo(p);
		p->drawCards(qMin(n,3),"wanchan");
		if(p->isAlive()){
			QStringList ids;
			room->setPlayerFlag(p,"wanchanUse");
			foreach(const Card*h,p->getHandcards()){
				if(h->isKindOf("BasicCard")||h->isNDTrick()){
					if(h->isAvailable(p))ids << h->toString();
				}
			}
			if(!ids.isEmpty())
				room->askForUseCard(p,ids.join(","),"wanchan0",-1,Card::MethodUse,true,nullptr,nullptr,"wanchanUse");
			room->setPlayerFlag(p,"-wanchanUse");
		}
	}
}

class WanchanVs : public ZeroCardViewAsSkill
{
public:
	WanchanVs(): ZeroCardViewAsSkill("wanchan")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("WanchanCard")<1;
	}

	const Card*viewAs()const
	{
		return new WanchanCard;
	}
};

class Wanchan : public TriggerSkill
{
public:
	Wanchan():TriggerSkill("wanchan")
	{
		events << PreCardUsed;
		view_as_skill = new WanchanVs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("wanchanUse")){
				room->setPlayerFlag(player,"-wanchanUse");
				foreach(ServerPlayer*t,use.to){
					foreach(ServerPlayer*p,room->getAlivePlayers()){
						if(use.to.contains(p))continue;
						if(t->isAdjacentTo(p))
							use.to.append(p);
					}
				}
				room->sortByActionOrder(use.to);
				data.setValue(use);
			}
		}
		return false;
	}
};

class Jiangzhi : public TriggerSkill
{
public:
	Jiangzhi():TriggerSkill("jiangzhi")
	{
		events << TargetConfirmed;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("BasicCard")||use.card->isNDTrick()){
				if(use.to.contains(player)&&use.to.length()>1&&player->askForSkillInvoke(this,data)){
					player->peiyin(this);
					JudgeStruct judge;
					judge.reason = objectName();
					judge.who = player;
					judge.pattern = ".|red,black";
					room->judge(judge);
					if(judge.card->isRed())
						player->drawCards(2,objectName());
					else if(judge.card->isBlack()){
						QList<ServerPlayer*>tps;
						foreach(ServerPlayer*p,room->getAlivePlayers()){
							if(use.to.contains(p))continue;
							if(player->canDiscard(p,"he"))
								tps << p;
						}
						ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"jiangzhi0");
						if(tp){
							room->doAnimate(1,player->objectName(),tp->objectName());
							Card*dc = dummyCard();
							for(int i = 0; i < 2; i++){
								int id = room->askForCardChosen(player,tp,"hej",objectName(),false,Card::MethodDiscard,dc->getSubcards(),i>0);
								if(id<0)break;
								dc->addSubcard(id);
								if(dc->subcardsLength()>=tp->getCardCount())break;
							}
							room->throwCard(dc,objectName(),tp,player);
						}
					}
				}
			}
		}
		return false;
	}
};

class ThTunchu : public TriggerSkill
{
public:
	ThTunchu():TriggerSkill("thtunchu")
	{
		events << CardUsed << EventPhaseStart << GameStart;
	}
	bool triggerable(const ServerPlayer*player)const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("thtunchuBf")){
				player->addMark("thtunchuBf-Clear");
				if(player->getMark("thtunchuBf-Clear")==3)
					room->setPlayerCardLimitation(player,"use",".",true);
			}
		}else if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())&&player->getHandcardNum()>player->getHp()){
				room->sendCompulsoryTriggerLog(player,this);
				player->setFlags("thtunchuBf");
			}
		}else if(event==GameStart){
			if(player->hasSkill(objectName())){
				int n = room->getPlayers().length()*4-player->getHandcardNum();
				if(n>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(n,objectName());
				}
			}
		}
		return false;
	}
};

class ThTunchuLimit : public CardLimitSkill
{
public:
	ThTunchuLimit(): CardLimitSkill("#ThTunchuLimit")
	{
	}

	QString limitList(const Player*)const
	{
		return "discard";
	}

	QString limitPattern(const Player*target,const Card*c)const
	{
		foreach(const Player*p,target->getAliveSiblings(true)){
			if(p->handCards().contains(c->getId())&&p->hasSkill("thtunchu"))
				return c->toString();
		}
		return "";
	}
};

ThShuliangCard::ThShuliangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ThShuliangCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	return targets.length()<subcardsLength()&&to!=Self&&to->isKongcheng();
}

bool ThShuliangCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	return targets.length()==subcardsLength();
}

void ThShuliangCard::onUse(Room*room,CardUseStruct&use)const
{
	room->setTag("thshuliangUse",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void ThShuliangCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	int i = 0;
	CardUseStruct use = room->getTag("thshuliangUse").value<CardUseStruct>();
	foreach(ServerPlayer*p,use.to){
		if(p->isAlive()){
			const Card*c = Sanguosha->getCard(subcards[i]);
			room->giveCard(source,p,c,"thshuliang");
			if(p->handCards().contains(subcards[i])&&p->canUse(c,p)){
				room->askForUseCard(p,c->toString(),"thshuliang1:"+c->objectName());
			}
		}
		i++;
	}
}

class ThShuliangVs : public ViewAsSkill
{
public:
	ThShuliangVs(): ViewAsSkill("thshuliang")
	{
		response_pattern = "@@thshuliang";
	}

	bool viewFilter(const QList<const Card*>&cards,const Card*)const
	{
		return cards.length()<=Self->getAliveSiblings().length();
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()>0){
			Card*c = new ThShuliangCard();
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

};

class ThShuliang : public TriggerSkill
{
public:
	ThShuliang():TriggerSkill("thshuliang")
	{
		events << EventPhaseChanging;
		view_as_skill = new ThShuliangVs;
	}
	bool triggerable(const ServerPlayer*player)const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->getHandcardNum()>0&&p->hasSkill(objectName())){
					foreach(ServerPlayer*q,room->getAlivePlayers()){
						if(q->isKongcheng()){
							room->askForUseCard(p,"@@thshuliang","thshuliang0");
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class ThJiaowei : public TriggerSkill
{
public:
	ThJiaowei():TriggerSkill("thjiaowei")
	{
		events << CardsMoveOneTime << AfterDrawNCards << DamageInflicted;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&move.from==player){
				foreach(int id,move.card_ids){
					if(Sanguosha->getCard(id)->hasFlag("thjiaowei_xuan")){
						room->setPlayerMark(player,"&thjiaowei-Clear",1);
						room->setCardFlag(id,"-tyjiaowei_xuan");
					}
				}
			}
		}else if(event==DamageInflicted){
			if(player->getMark("&thjiaowei-Clear")>0){
				DamageStruct damage = data.value<DamageStruct>();
				room->setPlayerMark(player,"&thjiaowei-Clear",0);
				return player->damageRevises(data,-damage.damage);
			}
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards")return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach(int id,player->handCards()){
				room->setCardTip(id,"thjiaowei_xuan");
				room->setCardFlag(id,"thjiaowei_xuan");
			}
		}
		return false;
	}
};

class Feibai : public TriggerSkill
{
public:
	Feibai():TriggerSkill("feibai")
	{
		events << CardFinished;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1)return false;
			if(player->getMark("feibaiBan-Clear")<1&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				int pn = use.card->nameLength();
				if(player->getMark("feibaiName-Clear")>0)
					pn += player->getTag("feibaiName").toInt();
				QList<int>ids = room->getDrawPile();
				qShuffle(ids);
				foreach(int id,ids){
					if(Sanguosha->getCard(id)->nameLength()==pn){
						room->obtainCard(player,id);
						ids.clear();
						break;
					}
				}
				if(ids.length()>0){
					player->addMark("feibaiBan-Clear");
					ids = player->drawCardsList(1,objectName());
					room->setCardTip(ids.last(),"thjiaowei_xuan");
					room->setCardFlag(ids.last(),"thjiaowei_xuan");
				}
			}
			player->addMark("feibaiName-Clear");
			player->setTag("feibaiName", use.card->nameLength());
		}
		return false;
	}
};

class ThQixin : public TriggerSkill
{
public:
	ThQixin():TriggerSkill("thqixin")
	{
		events << CardUsed << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("thqixinUse")){
				player->addMark("thqixinUse-Clear");
				player->peiyin(objectName());
			}else if(use.card->getTypeId()==1&&player->getMark("thqixinUse-Clear")<2
			&&player->hasTurn()&&player->askForSkillInvoke(objectName()+"$-1",data)){
				player->addMark("thqixinUse-Clear");
				player->drawCards(2,objectName());
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_skillName!="InitialHandCards"&&player->getMark("thqixinUse-Clear")<2
			&&move.card_ids.length()==2&&move.reason.m_skillName!=objectName()&&player->hasTurn()&&move.reason.m_reason==CardMoveReason::S_REASON_DRAW){
				room->askForUseCard(player,"$BasicCard","thqixin0",-1,Card::MethodUse,true,nullptr,nullptr,"thqixinUse");
			}
		}
		return false;
	}
};

JiusiCard::JiusiCard()
{
	handling_method = Card::MethodUse;
}

bool JiusiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return false;
	Card*card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFilter(targets,to_select,Self);
}

bool JiusiCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFixed();
}

bool JiusiCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetsFeasible(targets,Self);
}

const Card*JiusiCard::validate(CardUseStruct&use)const
{
	Room*room = use.from->getRoom();
	QString pattern = room->askForChoice(use.from,"jiusi",user_string);
	room->addPlayerMark(use.from,"jiusiBan-Clear");
	use.from->turnOver();
	Card*c = Sanguosha->cloneCard(pattern);
	c->setSkillName("jiusi");
	c->deleteLater();
	return c;
}

const Card*JiusiCard::validateInResponse(ServerPlayer*source)const
{
	Room*room = source->getRoom();
	QString pattern = room->askForChoice(source,"jiusi",user_string);
	room->addPlayerMark(source,"jiusiBan-Clear");
	source->turnOver();
	Card*c = Sanguosha->cloneCard(pattern);
	c->setSkillName("jiusi");
	c->deleteLater();
	return c;
}

class Jiusi : public ZeroCardViewAsSkill
{
public:
	Jiusi(): ZeroCardViewAsSkill("jiusi")
	{
        response_or_use = true;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance("jiusi",true,false);
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(player->getMark("jiusiBan-Clear")>0||Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		||!player->hasTurn())return false;
		foreach(QString cn,pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(dc->getTypeId()==1)
					return true;
			}
		}
		return pattern.contains("@@jiusi");
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("jiusiBan-Clear")<1;
	}

	const Card*viewAs()const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag("jiusi").value<const Card*>();
			if(c)pattern = c->objectName();
		}
		if(pattern.isEmpty())return nullptr;
		JiusiCard*sc = new JiusiCard;
		sc->setUserString(pattern);
		return sc;
	}
};

class Laoyan : public TriggerSkill
{
public:
	Laoyan():TriggerSkill("laoyan")
	{
		events << TargetSpecified << EventPhaseChanging;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*player)const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1||use.to.length()<2)return false;
			foreach(ServerPlayer*p,use.to){
				if(use.from!=p&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					foreach(ServerPlayer*q,use.to){
						if(q!=p)use.nullified_list << q->objectName();
					}
					data.setValue(use);
					Card*dc = dummyCard();
					QList<int>ids;
					foreach(int id,room->getDrawPile()){
						const Card*c = Sanguosha->getCard(id);
						if(c->getNumber()<use.card->getNumber()&&!ids.contains(c->getNumber())){
							ids << c->getNumber();
							dc->addSubcard(id);
						}
					}
					room->obtainCard(p,dc);
					foreach(int id,dc->getSubcards()){
						if(p->handCards().contains(id))
							room->setCardTip(id,"laoyan");
					}
				}
			}
		}else{
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->hasSkill(objectName(),true)){
					Card*dc = dummyCard();
					foreach(const Card*h,p->getHandcards()){
						if(h->hasTip("laoyan"))
							dc->addSubcard(h);
					}
					room->throwCard(dc,objectName(),nullptr);
				}
			}
		}
		return false;
	}
};

class ThJueyan : public TriggerSkill
{
public:
	ThJueyan():TriggerSkill("thjueyan")
	{
		events << PreCardUsed << CardFinished;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&player->hasTurn()
			&&player->getMark(use.card->getType()+"Ttyjueyan-Clear")<1){
				player->addMark(use.card->getType()+"Ttyjueyan-Clear");
				room->setCardFlag(use.card,"thjueyanBf");
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("thjueyanBf")&&player->askForSkillInvoke(objectName()+"$-1",data)){
				QStringList choices;
				choices << "thjueyan1="+QString::number(qMax(1,player->getMark("thjueyan1")));
				choices << "thjueyan2="+QString::number(qMax(1,player->getMark("thjueyan2")));
				foreach(const Player*p,player->getAliveSiblings()){
					if(player->canPindian(p)){
						choices << "thjueyan3="+QString::number(qMax(1,player->getMark("thjueyan3")));
						break;
					}
				}
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				foreach(QString st,choices){
					QStringList sts = st.split("=");
					if(choice==st){
						player->setMark(sts.first(),1);
					}else{
						int n = qMax(1,player->getMark(sts.first()))+1;
						player->setMark(sts.first(),qMin(3,n));
					}
					int n = player->getMark(sts.first());
					if(sts.first().contains("yan1"))
						player->setSkillDescriptionSwap(objectName(),"%arg1",QString::number(n));
					else if(sts.first().contains("yan2"))
						player->setSkillDescriptionSwap(objectName(),"%arg2",QString::number(n));
					else if(sts.first().contains("yan3"))
						player->setSkillDescriptionSwap(objectName(),"%arg3",QString::number(n));
				}
				room->changeTranslation(player,objectName(),1);
				choices = choice.split("=");
				int n = choices.last().toInt();
				if(choice.contains("yan1")){
					player->drawCards(n,objectName());
				}else if(choice.contains("yan2")){
					QList<int>ids = room->getDrawPile();
					qShuffle(ids);
					Card*dc = dummyCard();
					foreach(int id,ids){
						dc->addSubcard(id);
						if(dc->subcardsLength()>=n)break;
					}
					player->obtainCard(dc);
				}else if(choice.contains("yan3")){
					QList<ServerPlayer*>tps;
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						if(player->canPindian(p))tps << p;
					}
					ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName());
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						PindianStruct*pd = player->PinDian(tp,objectName());
						if(pd->success){
							room->damage(DamageStruct(objectName(),player,tp));
						}else if(pd->to_number>pd->from_number){
							room->damage(DamageStruct(objectName(),tp,player));
						}
					}
				}
			}
		}
		return false;
	}
};

ThWuyanCard::ThWuyanCard()
{
	handling_method = Card::MethodUse;
}

bool ThWuyanCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	if(targets.isEmpty())return to->isMale();
	return targets.length()<2&&to!=Self;
}

bool ThWuyanCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	return targets.length()==2;
}

void ThWuyanCard::onUse(Room*room,CardUseStruct&use)const
{
	room->setTag("thwuyanUse",QVariant::fromValue(use));
	use.to.takeLast();
	SkillCard::onUse(room,use);
}

void ThWuyanCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	CardUseStruct use = room->getTag("thwuyanUse").value<CardUseStruct>();
	room->setPlayerFlag(use.to.first(),"thwuyanUseF");
	room->setPlayerFlag(use.to.last(),"thwuyanUseT");
	if(room->askForUseCard(use.to.first(),"$.|.|.|hand","thwuyan1:"+use.to.last()->objectName(),-1,Card::MethodUse,true,nullptr,nullptr,"thwuyanUseC"))
		source->drawCards(2,"thwuyan");
	room->setPlayerFlag(use.to.first(),"-tywuyanUseF");
	room->setPlayerFlag(use.to.last(),"-tywuyanUseT");
	if(use.to.first()->hasFlag("DamageDone")){
		use.to.first()->setFlags("-DamageDone");
	}else if(source->askForSkillInvoke("thwuyan",use.to.first(),false)){
		room->addPlayerMark(source,"thwuyanBan-PlayClear");
		room->loseHp(use.to.first(),1,true,source,"thwuyan");
	}
}

class ThWuyanVs : public ZeroCardViewAsSkill
{
public:
	ThWuyanVs(): ZeroCardViewAsSkill("thwuyan")
	{
		change_skill = true;
		response_pattern = "@@thwuyan";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("thwuyanBan-PlayClear")<1;
	}

	const Card*viewAs()const
	{
		return new ThWuyanCard;
	}
};

class ThWuyan : public TriggerSkill
{
public:
	ThWuyan():TriggerSkill("thwuyan")
	{
		events << Damaged << PreCardUsed << CardFinished;
		view_as_skill = new ThWuyanVs;
		waked_skills = "#ThWuyanBf";
	}
	bool triggerable(const ServerPlayer*player)const
	{
		return player!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			if(player->isAlive()&&player->hasSkill(objectName()))
				room->askForUseCard(player,"@@thwuyan","thwuyan0");
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("thwuyanUseC")){
				room->setPlayerFlag(player,"-tywuyanUseF");
				room->setPlayerFlag(use.to.first(),"-tywuyanUseT");
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("thwuyanUseC")&&use.card->hasFlag("DamageDone")){
				player->setFlags("DamageDone");
			}
		}
		return false;
	}
};

class ThWuyanBf : public ProhibitSkill
{
public:
	ThWuyanBf(): ProhibitSkill("#ThWuyanBf")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		if(from!=to&&from->hasFlag("thwuyanUseF")&&from->handCards().contains(card->getId())){
			return !to->hasFlag("thwuyanUseT");
		}
		return false;
	}
};

class Zhanyu : public TriggerSkill
{
public:
	Zhanyu():TriggerSkill("zhanyu")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart&&player->getHandcardNum()>0){
				const Card*c = room->askForCard(player,".","zhanyu0",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					room->showCard(player,c->getEffectiveId());
					QList<int>ids;
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						QList<const Card*>hs = p->getHandcards();
						qShuffle(hs);
						foreach(const Card*h,hs){
							if(p->isJilei(h))continue;
							room->throwCard(h,objectName(),p);
							ids << h->getId();
							break;
						}
					}
					foreach(int id,ids){
						if(room->getCardOwner(id))
							ids.removeOne(id);
					}
					if(ids.isEmpty())return false;
					room->fillAG(ids,player);
					int id = room->askForAG(player,ids,true,objectName(),"zhanyu1");
					room->clearAG(player);
					if(id>=0)room->obtainCard(player,id);
				}
			}
		}
		return false;
	}
};

class Langxi : public PhaseChangeSkill
{
public:
	Langxi(): PhaseChangeSkill("langxi")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHp()<= player->getHp())
				targets << p;
		}
		if(targets.isEmpty())return false;
		ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@langxi-invoke",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(objectName());
		int n = qrand()% 3;
		if(n==0){
			LogMessage log;
			log.type = "#Damage";
			log.from = player;
			log.to << target;
			log.arg = QString::number(n);
			log.arg2 = "normal_nature";
			room->sendLog(log);
			return false;
		}
		room->damage(DamageStruct(objectName(),player,target,n));
		return false;
	}
};

class Yisuan : public TriggerSkill
{
public:
	Yisuan(): TriggerSkill("yisuan")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play || player->getMark("yisuan-PlayClear")> 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::DiscardPile || move.from != player)return false;
		if(move.from_places.contains(Player::PlaceTable)
           &&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE){
			const Card*card = move.reason.m_extraData.value<const Card*>();
			if(!card || !card->isKindOf("TrickCard"))return false;
			player->setTag("yisuanForAI", QVariant::fromValue(card));
			if(!player->askForSkillInvoke(this,QString("yisuan_invoke:%1").arg(card->objectName())))return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player,"yisuan-PlayClear");
			room->loseMaxHp(player,1,"yisuan");
			if(player->isDead())return false;
			room->obtainCard(player,card,true);
		}
		return false;
	}
};

TanbeiCard::TanbeiCard()
{
}

void TanbeiCard::onEffect(CardEffectStruct&effect)const
{
	if(effect.to->isDead())return;
	Room*room = effect.from->getRoom();
	QStringList choices;
	if(!effect.to->isAllNude())
		choices << "get";
	choices << "nolimit";

	QString choice = room->askForChoice(effect.to,"tanbei",choices.join("+"),QVariant::fromValue(effect.from));
	if(choice=="get"){
		const Card*c = effect.to->getCards("hej").at(qrand()% effect.to->getCards("hej").length());
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName());
		room->obtainCard(effect.from,c,reason,room->getCardPlace(c->getEffectiveId())!= Player::PlaceHand);
		room->addPlayerMark(effect.from,"tanbei_pro_from-Clear");
		room->addPlayerMark(effect.to,"tanbei_pro_to-Clear");
	} else {
		room->addPlayerMark(effect.from,"tanbei_from-Clear");
		room->addPlayerMark(effect.to,"tanbei_to-Clear");
	}
}

class Tanbei : public ZeroCardViewAsSkill
{
public:
	Tanbei(): ZeroCardViewAsSkill("tanbei")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TanbeiCard");
	}

	const Card*viewAs()const
	{
		return new TanbeiCard;
	}
};

class TanbeiTargetMod : public TargetModSkill
{
public:
	TanbeiTargetMod(): TargetModSkill("#tanbei-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getResidueNum(const Player*from,const Card*card,const Player*to)const
	{
		if(from->getMark("tanbei_from-Clear")>0&&to&&to->getMark("tanbei_to-Clear")>0)
			return 999;
		if(card->hasTip("lianjie")||card->hasTip("qingzheng"))
			return 999;
		if(from->hasSkill("yinbi")){
			foreach(const Player*p,from->getAliveSiblings()){
				if(from->getHandcardNum()==p->getHandcardNum())return 0;
			}
			return 999;
		}
		return 0;
	}

	int getDistanceLimit(const Player*from,const Card*card,const Player*to)const
	{
		if(from->getMark("tanbei_from-Clear")>0&&to&&to->getMark("tanbei_to-Clear")>0)
			return 999;
		if(card->hasTip("lianjie"))
			return 999;
		if(from->hasSkill("huashang")){
			QList<Card::Color>cs;
			foreach(const Card*e,from->getEquips()){
				if(cs.contains(e->getColor()))continue;
				cs.append(e->getColor());
				if(cs.length()>1)
					return 999;
			}
		}
		if(from->hasSkill("yinbi")){
			foreach(const Player*p,from->getAliveSiblings()){
				if(from->getHandcardNum()==p->getHandcardNum())return 0;
			}
			return 999;
		}
		return 0;
	}
};

class TanbeiPro : public ProhibitSkill
{
public:
	TanbeiPro(): ProhibitSkill("#tanbei-pro")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		return from->getMark("tanbei_pro_from-Clear")> 0&& to->getMark("tanbei_pro_to-Clear")> 0&& !card->isKindOf("SkillCard");
	}
};

SidaoCard::SidaoCard()
{
	mute = true;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodUse;
}

void SidaoCard::onUse(Room*,CardUseStruct&)const
{
}

class SidaoVS : public OneCardViewAsSkill
{
public:
	SidaoVS(): OneCardViewAsSkill("sidao")
	{
		response_pattern = "@@sidao";
		response_or_use = true;
	}

	bool viewFilter(const Card*to_select)const
	{
		if(to_select->isEquipped())return false;
		const Player*to = nullptr;
		foreach(const Player*p,Self->getAliveSiblings()){
			if(p->objectName()== Self->property("sidao_target").toString()){
				to = p;
				break;
			}
		}
		if(!to)return false;
		Snatch*snatch = new Snatch(to_select->getSuit(),to_select->getNumber());
		snatch->setSkillName("sidao");
		snatch->addSubcard(to_select);
		snatch->deleteLater();
		return snatch->targetFilter(QList<const Player*>(),to,Self)&& !Self->isLocked(snatch);
	}

	const Card*viewAs(const Card*originalcard)const
	{
		SidaoCard*c = new SidaoCard;
		c->addSubcard(originalcard);
		return c;
	}
};

class Sidao : public TriggerSkill
{
public:
	Sidao(): TriggerSkill("sidao")
	{
		events << CardFinished;
		view_as_skill = new SidaoVS;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.from->isDead()|| use.card->isKindOf("SkillCard"))return false;
		foreach(ServerPlayer*p,use.to){
			if(p==use.from || p->isDead())continue;
			p->addMark("sidao_" + use.from->objectName()+ "-PlayClear");
			if(!p->isAllNude()&& p->getMark("sidao_" + use.from->objectName()+ "-PlayClear")== 2
			&& use.from->getMark("sidao-PlayClear")<1&& use.from->hasSkill(objectName())){
				room->setPlayerProperty(use.from,"sidao_target",p->objectName());
				const Card*c = room->askForUseCard(use.from,"@@sidao","@sidao:" + p->objectName());
				if(!c)continue;
				use.from->addMark("sidao-PlayClear");
				c = Sanguosha->getCard(c->getSubcards().first());
				Snatch*snatch = new Snatch(c->getSuit(),c->getNumber());
				snatch->setSkillName("sidao");
				snatch->addSubcard(c);
				room->useCard(CardUseStruct(snatch,use.from,p),true);
				snatch->deleteLater();
			}
		}
		return false;
	}
};

class SidaoTargetMod : public TargetModSkill
{
public:
	SidaoTargetMod(): TargetModSkill("#sidao-target")
	{
		frequency = NotFrequent;
		pattern = "Snatch";
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*)const
	{
		if(card->getSkillName()== "sidao")
			return 999;
		return 0;
	}
};

class Lulve : public PhaseChangeSkill
{
public:
	Lulve(): PhaseChangeSkill("lulve")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		int num = player->getHandcardNum();
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isKongcheng()|| p->getHandcardNum()>= num)continue;
			targets << p;
		}
		if(targets.isEmpty())return false;

		ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@lulve-invoke",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(this);

		if(target->isDead())return false;
		QStringList choices;
		if(!target->isKongcheng())
			choices << "give=" + player->objectName();
		choices << "fanmian=" + player->objectName();

		QString choice = room->askForChoice(target,objectName(),choices.join("+"),QVariant::fromValue(player));

		if(choice.startsWith("give")){
			room->giveCard(target,player,target->handCards(),objectName(),false);
			if(player->isAlive())
				player->turnOver();
		} else {
			target->turnOver();
			if(target->isDead()|| player->isDead())return false;
			Slash*slash = new Slash(Card::NoSuit,0);
			slash->setSkillName("_lulve");
			slash->deleteLater();
			if(!target->canSlash(player,slash,false))return false;
			room->setCardFlag(slash,"YUANBEN");
			room->useCard(CardUseStruct(slash,target,player));
		}
		return false;
	}
};

class Zhuixi : public TriggerSkill
{
public:
	Zhuixi(): TriggerSkill("zhuixi")
	{
		events << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.to->isDead())return false;

		bool from = damage.from->faceUp();
		bool to = damage.to->faceUp();
		if(from==to)return false;

		QList<ServerPlayer*> players;
		if(damage.from->hasSkill(objectName()))
			players << damage.from;
		if(damage.to->hasSkill(objectName()))
			players << damage.to;
		room->sortByActionOrder(players);

		foreach(ServerPlayer*p,players){
			room->sendCompulsoryTriggerLog(p,this);
			++damage.damage;
		}
		data = QVariant::fromValue(damage);
		return false;
	}
};

class Kangge : public TriggerSkill
{
public:
	Kangge(): TriggerSkill("kangge")
	{
		events << EventPhaseStart << TurnStart << CardsMoveOneTime << Dying << Death;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TurnStart){
			if(!player->faceUp())return false;
			if(player->getMark("kanggeRound-Keep")>= 2)return false;
			room->addPlayerMark(player,"kanggeRound-Keep");
		} else if(event==EventPhaseStart){
			if(player->getMark("kanggeRound-Keep")!= 1 || player->getPhase()!= Player::RoundStart)return false;
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@kangge-target",false,true);
			room->broadcastSkillInvoke(this);
			room->setPlayerMark(target,"&kangge+#" + player->objectName(),1);
		} else if(event==Dying){
			if(player->getMark("kangge_lun")> 0)return false;
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who->getMark("&kangge+#" + player->objectName())<= 0)return false;
			if(!player->askForSkillInvoke(this,dying.who))return false;
			room->broadcastSkillInvoke(this);
			room->addPlayerMark(player,"kangge_lun");
			int recover_num = qMin(1 - dying.who->getHp(),dying.who->getMaxHp()- dying.who->getHp());
			room->recover(dying.who,RecoverStruct(player,nullptr,recover_num,"kangge"));
		} else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who->getMark("&kangge+#" + player->objectName())<= 0)return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->throwAllHandCardsAndEquips();
			room->loseHp(HpLostStruct(player,1,objectName(),player));
		} else {
			if(!room->hasCurrent()|| player->getMark("kangge-Clear")> 0)return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(!move.to || move.to->getMark("&kangge+#" + player->objectName())<= 0 || move.to->hasFlag("CurrentPlayer"))return false;
			if(!move.from_places.contains(Player::DrawPile)|| move.reason.m_reason != CardMoveReason::S_REASON_DRAW)return false;
			int num = 0;
			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)== Player::DrawPile)
					num++;
			}
			num = qMin(num,3);
			if(num <= 0)return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->addPlayerMark(player,"kangge-Clear");
			player->drawCards(num,objectName());
		}
		return false;
	}
};

class Jielie : public TriggerSkill
{
public:
	Jielie(): TriggerSkill("jielie")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from==player || damage.from->getMark("&kangge+#" + player->objectName())> 0)return false;
		if(damage.damage <= 0)return false;
		player->setTag("jielie_damage_data", data);
		bool invoke = player->askForSkillInvoke(this,"jielie:" + QString::number(damage.damage));
		player->removeTag("jielie_damage_data");
		if(!invoke)return false;
		room->broadcastSkillInvoke(this);
		Card::Suit suit = room->askForSuit(player,objectName());
		LogMessage log;
		log.type = "#ChooseSuit";
		log.from = player;
		log.arg = Card::Suit2String(suit);
		room->sendLog(log);
		room->loseHp(HpLostStruct(player,damage.damage,objectName(),player));
		if(player->isDead())return true;
		QList<int> list,get;
		foreach(int id,room->getDiscardPile()){
			const Card*card = Sanguosha->getCard(id);
			if(card->getSuit()!= suit)continue;
			list << id;
		}
		for(int i = 0; i < damage.damage; i++){
			if(list.isEmpty())break;
			int id = list.at(qrand()% list.length());
			list.removeOne(id);
			get << id;
		}
		if(get.isEmpty())return true;
		DummyCard _get(get);
		room->obtainCard(player,&_get);
		return true;
	}
};

class Langmie : public TriggerSkill
{
public:
	Langmie(): TriggerSkill("langmie")
	{
		events << EventPhaseEnd << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()!= Player::Play)return false;
			bool can_trigger = false;
			for(int i = 0; i < S_CARD_TYPE_LENGTH; i++){
				if(i==0)continue;
				if(player->getMark("langmie_" + QString::number(i)+ "-PlayClear")>= 2){
					can_trigger = true;
					break;
				}
			}
			if(!can_trigger)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(!p->askForSkillInvoke(this,"draw"))continue;
				room->broadcastSkillInvoke(this);
				p->drawCards(1,objectName());
			}
		} else {
			if(player->getPhase()!= Player::Finish)return false;
			if(player->getMark("damage_point_round")< 2)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->isDead())return false;
				if(p->isDead()|| !p->hasSkill(objectName())|| !p->canDiscard(p,"he"))continue;
				if(!room->askForCard(p,"..","@langmie-dis:" + player->objectName(),data,objectName()))continue;
				room->broadcastSkillInvoke(this);
				room->damage(DamageStruct("langmie",p,player));
			}
		}
		return false;
	}
};

class SecondLangmie : public PhaseChangeSkill
{
public:
	SecondLangmie(): PhaseChangeSkill("secondlangmie")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getPhase()== Player::Finish;
	}

	QStringList Choices(ServerPlayer*player)const
	{
		QStringList choices;
		for(int i = 1; i < S_CARD_TYPE_LENGTH; i++){
			if(player->getMark("secondlangmie_" + QString::number(i)+ "-Clear")>= 2){
				choices << "draw";
				break;
			}
		}
		if(player->getMark("damage_point_round")>= 2)
			choices << "damage=" + player->objectName();
		return choices;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| !p->hasSkill(objectName())|| !p->canDiscard(p,"he"))continue;
			QStringList choices = Choices(player);
			if(choices.isEmpty())continue;
			QString prompt = "@secondlangmie:";
			if(choices.length()== 1){
				if(choices.first()== "draw")
					prompt = "@secondlangmie-draw:";
				else
					prompt = "@secondlangmie-damage:";
			}
			if(room->askForCard(p,"..",prompt + player->objectName(),QVariant::fromValue(player),objectName())){
				room->broadcastSkillInvoke(this);
				QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(player));
				if(choice=="draw")
					p->drawCards(2,objectName());
				else
					room->damage(DamageStruct(objectName(),p,player));
			}
		}
		return false;
	}
};

class XiaoxiNF : public PhaseChangeSkill
{
public:
	XiaoxiNF(): PhaseChangeSkill("xiaoxinf")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play || player->getMaxHp()<= 1)return false;

		room->sendCompulsoryTriggerLog(player,this);

		QStringList choices;
		choices << "lose=1";
		if(player->getMaxHp()> 1)
			choices << "lose=2";
		QString choice = room->askForChoice(player,objectName(),choices.join("+"));

		int num = choice.split("=").last().toInt();
		if(num <= 0)return false;
		room->loseMaxHp(player,num,objectName());
		if(player->isDead())return false;

		choices.clear();

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_xiaoxinf");
		slash->deleteLater();

		bool obtain = false,_slash = false;

		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(!player->inMyAttackRange(p))continue;
			if(!p->isNude()&& !obtain)
				obtain = true;
			if(player->canSlash(p,slash,true)&& !_slash)
				_slash = true;
			if(obtain&& _slash)break;
		}

		if(obtain)
			choices << "obtain=" + QString::number(num);
		if(_slash)
			choices << "slash=" + QString::number(num);
		if(choices.isEmpty())return false;

		choice = room->askForChoice(player,objectName(),choices.join("+"));

		QList<ServerPlayer*> targets;
		if(choice.startsWith("obtain")){
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(!player->inMyAttackRange(p)|| p->isNude())continue;
				targets << p;
			}
			if(targets.isEmpty())return false;

			ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@xiaoxinf-obtain");

			QList<int> cards;

			for(int i = 0; i < num; ++i){
				if(t->getCardCount()<=i)break;
				int id = room->askForCardChosen(player,t,"he",objectName(),false,Card::MethodNone,cards);
				if(id<0)break;
				cards << id;
			}

			if(cards.isEmpty())return false;
			DummyCard dummy(cards);
			room->obtainCard(player,&dummy,false);
		} else {
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(!player->inMyAttackRange(p)|| !player->canSlash(p,slash,true))continue;
				targets << p;
			}
			if(targets.isEmpty())return false;

			ServerPlayer*t = room->askForPlayerChosen(player,targets,"xiaoxinf_slash","@xiaoxinf-slash");

			for(int i = 0; i < num; ++i){
				if(player->isDead()|| t->isDead()|| !player->canSlash(t,slash,true))break;
				room->useCard(CardUseStruct(slash,player,t));
			}
		}
		return false;
	}
};

class Xiongrao : public PhaseChangeSkill
{
public:
	Xiongrao(): PhaseChangeSkill("xiongrao")
	{
		frequency = Limited;
		limit_mark = "@xiongraoMark";
		waked_skills = "#xiongrao,#xiongrao-invalidity";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start || player->getMark("@xiongraoMark")<= 0)return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);
		room->doSuperLightbox(player,"xiongrao");
		room->removePlayerMark(player,"@xiongraoMark");

		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			room->addPlayerMark(p,"xiongrao_debuff");
			foreach(ServerPlayer*pl,room->getAllPlayers())
				room->filterCards(pl,pl->getCards("he"),true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,args);
		}

		int num = 7 - player->getMaxHp();
		if(num > 0){
			room->gainMaxHp(player,num,objectName());
			player->drawCards(num,objectName());
		}
		return false;
	}
};

class XiongraoClear : public TriggerSkill
{
public:
	XiongraoClear(): TriggerSkill("#xiongrao")
	{
		events << EventPhaseChanging << Death;
		frequency = Limited;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*target,QVariant&data)const
	{
		if(triggerEvent==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to != Player::NotActive)
				return false;
		} else if(triggerEvent==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != target || target != room->getCurrent())
				return false;
		}
		foreach(ServerPlayer*player,room->getAllPlayers(true)){
			if(player->getMark("xiongrao_debuff")== 0)continue;
			room->setPlayerMark(player,"xiongrao_debuff",0);

			foreach(ServerPlayer*p,room->getAllPlayers())
				room->filterCards(p,p->getCards("he"),false);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,args);
		}
		return false;
	}
};

class XiongraoInvalidity : public InvaliditySkill
{
public:
	XiongraoInvalidity(): InvaliditySkill("#xiongrao-invalidity")
	{
		frequency = Limited;
	}

	bool isSkillValid(const Player*player,const Skill*skill)const
	{
		return player->getMark("xiongrao_debuff")<1 || skill->getFrequency(player)== Skill::Compulsory
			|| skill->getFrequency(player)== Skill::Wake || skill->isLimitedSkill();
	}
};

FuhaiCard::FuhaiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void FuhaiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	int sid = getSubcards().first();
	int tid = -1;
	ServerPlayer*now = source;
	int times = 0;

	while(!source->isKongcheng()){
		if(source->isDead()|| source->getMark("fuhai_disable-PlayClear")> 0)return;

		QStringList choices;
		ServerPlayer*next = now->getNextAlive();
		ServerPlayer*last = now->getNextAlive(room->alivePlayerCount()- 1);

		if(last->isAlive()&& !last->isKongcheng()&& last->getMark("fuhai-PlayClear")<= 0&& last != source)
			choices << "last";
		if(next != last&& next->isAlive()&& !next->isKongcheng()&& next->getMark("fuhai-PlayClear")<= 0&& next != source)
			choices << "next";
		if(choices.isEmpty())return;

		if(sid < 0){
			source->setTag("FuhaiNow", QVariant::fromValue(now));
			sid = room->askForCardShow(source,source,"fuhai")->getEffectiveId();
			source->removeTag("FuhaiNow");
			if(sid < 0)return;
		}
		room->showCard(source,sid);

		QString  choice = room->askForChoice(source,"fuhai",choices.join("+"),QVariant::fromValue(now));
		if(choice=="last")
			now = last;
		else
			now = next;
		room->addPlayerMark(now,"fuhai-PlayClear");
		times++;

		if(now->isDead()|| now->isKongcheng())return;
		room->doAnimate(1,source->objectName(),now->objectName());
		now->setTag("FuhaiID", sid + 1);
		tid = room->askForCardShow(now,source,"fuhai")->getEffectiveId();
		now->removeTag("FuhaiID");
		if(tid < 0)return;
		room->showCard(now,tid);

		int snum = Sanguosha->getCard(sid)->getNumber();
		int tnum = Sanguosha->getCard(tid)->getNumber();
		if(snum >= tnum){
			room->throwCard(sid,"fuhai",source);
		} else {
			room->throwCard(tid,"fuhai",now);
			QList<ServerPlayer*> players;
			players << source << now;
			room->sortByActionOrder(players);
			room->drawCards(players,times,"fuhai");
			room->addPlayerMark(source,"fuhai_disable-PlayClear");
		}
		sid = -1;
	}
}

class Fuhai : public OneCardViewAsSkill
{
public:
	Fuhai(): OneCardViewAsSkill("fuhai")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("fuhai_disable-PlayClear")> 0)return false;
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->getMark("fuhai-PlayClear")<= 0)
				return true;
		}
		return false;
	}

	const Card*viewAs(const Card*originalCard)const
	{
		FuhaiCard*c = new FuhaiCard;
		c->addSubcard(originalCard);
		return c;
	}
};

SpQianxinCard::SpQianxinCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool SpQianxinCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void SpQianxinCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	int length = room->getDrawPile().length();
	int alive = room->alivePlayerCount();
	if(alive > length)
		room->swapPile();

	QVariantList list = room->getTag("spqianxin_xin").toList();
	foreach(int id,getSubcards()){
		if(!list.contains(QVariant(id)))
			list << id;
	}
	room->setTag("spqianxin_xin",QVariant::fromValue(list));
	room->addPlayerMark(effect.to,"spspqianxin_target" + effect.from->objectName());
	foreach(ServerPlayer*p,room->getAllPlayers(true))
		room->addPlayerMark(p,"spqianxin_disabled");

	if(room->getDrawPile().length()<= alive)
		room->moveCardsToEndOfDrawpile(effect.from,getSubcards(),"spqianxin");
	else {
		QStringList choices;
		int n = 1;
		int len = room->getDrawPile().length();
		while(n* alive <= len){
			choices << QString::number(n* alive);
			n++;
		}
		if(choices.isEmpty())return;
		QString choice = room->askForChoice(effect.from,"spqianxin",choices.join("+"));
		room->moveCardsInToDrawpile(effect.from,this,"spqianxin",choice.toInt());
	}
}

class SpQianxinVS : public ViewAsSkill
{
public:
	SpQianxinVS(): ViewAsSkill("spqianxin")
	{
	}

	bool viewFilter(const QList<const Card*>&,const Card*to_select)const
	{
		return !to_select->isEquipped();
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("SpQianxinCard")&& player->getMark("spqianxin_disabled")== 0;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())
			return nullptr;

		SpQianxinCard*card = new SpQianxinCard;
		card->addSubcards(cards);
		return card;
	}
};

class SpQianxin : public TriggerSkill
{
public:
	SpQianxin(): TriggerSkill("spqianxin")
	{
		events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new SpQianxinVS;
		global = true;
	}

	int getPriority(TriggerEvent event)const
	{
		if(event==EventPhaseChanging)return 0;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Discard)return false;
			if(player->getMark("spqianxin-Clear")<= 0)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->isDead())break;
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(player->getMark("spspqianxin_target" + p->objectName())<= 0)continue;
				if(room->getTag("spqianxin_xin").toList().isEmpty())
					room->setPlayerMark(player,"spspqianxin_target" + p->objectName(),0);
				QStringList choices;
				choices << "draw";
				if(player->getMaxCards()> 0)
					choices << "maxcards";
				if(choices.isEmpty())continue;
				room->sendCompulsoryTriggerLog(p,objectName(),true,true);
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(p));

				LogMessage log;
				log.type = "#FumianFirstChoice";
				log.from = player;
				log.arg = "spqianxin:" + choice;
				room->sendLog(log);

				if( choice=="draw"){
					if(p->getHandcardNum()< 4)
						p->drawCards(4 - p->getHandcardNum(),objectName());
				} else
					room->addMaxCards(player,-2);
			}
		} else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(!move.from&& move.from_places.contains(Player::DrawPile)){
				QVariantList list = room->getTag("spqianxin_xin").toList();
				QList<int> ids = ListV2I(list);
				foreach(int id,move.card_ids){
					if(ids.contains(id)){
						ids.removeOne(id);
						if(move.to&& move.to->isAlive()&& move.to_place==Player::PlaceHand){
							QVariantList get = room->getTag("spqianxin_xin_get").toList();
							if(!get.contains(QVariant(id)))get << id;
							room->setTag("spqianxin_xin_get",get);
							ServerPlayer*to = room->findPlayerByObjectName(move.to->objectName());
							if(to&& !to->isDead())
								to->addMark("spqianxin-Clear");
						}
					}
				}
				QVariantList new_list = ListI2V(ids);
				room->setTag("spqianxin_xin",QVariant::fromValue(new_list));
				if(ids.isEmpty()){
					room->removeTag("spqianxin_xin");
					foreach(ServerPlayer*p,room->getAllPlayers(true))
						room->setPlayerMark(p,"spqianxin_disabled",0);
				}
			}
			if(move.from&& move.from->isAlive()&& move.from_places.contains(Player::PlaceHand)){
				QVariantList get = room->getTag("spqianxin_xin_get").toList();
				QList<int> ids = ListV2I(get);
				ServerPlayer*from = room->findPlayerByObjectName(move.from->objectName());
				if(!from || from->isDead())return false;
				foreach(int id,move.card_ids){
					if(ids.contains(id)){
						ids.removeOne(id);
						from->removeMark("spqianxin-Clear");
					}
				}
				QVariantList new_list = ListI2V(ids);
				room->setTag("spqianxin_xin_get",QVariant::fromValue(new_list));
			}
		} else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to != Player::NotActive)return false;
			room->removeTag("spqianxin_xin_get");
		}
		return false;
	}
};

class Zhenxing : public TriggerSkill
{
public:
	Zhenxing(): TriggerSkill("zhenxing")
	{
		events << EventPhaseStart << Damaged;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish)return false;
		}
		if(!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());

		QStringList choices;
		choices << "1" << "2" << "3";
		QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
		QList<int> views = room->getNCards(choice.toInt());

		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.arg = choice;
		log.card_str = ListI2S(views).join("+");
		room->sendLog(log,player);
		log.type = "#ViewDrawPile";
		room->sendLog(log,room->getOtherPlayers(player,true));

		QStringList suits,duplication;
		foreach(int id,views){
			QString suit = Sanguosha->getCard(id)->getSuitString();
			if(!suits.contains(suit))suits << suit;
			else duplication << suit;
		}

		QList<int> enabled,disabled;
		foreach(int id,views){
			if(duplication.contains(Sanguosha->getCard(id)->getSuitString()))
				disabled << id;
			else
				enabled << id;
		}

		room->fillAG(views,player,disabled);
		int id = room->askForAG(player,enabled,enabled.length()<2,objectName());
		room->clearAG(player);
		room->returnToTopDrawPile(views);
		if(!enabled.isEmpty()){
			if(id<0)id = enabled.first();
			room->obtainCard(player,id,false);
		}
		return false;
	}
};

class Jili : public TriggerSkill
{
public:
	Jili(): TriggerSkill("jili")
	{
		events << CardUsed << CardResponded;
		frequency = Frequent;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		const Card*card = nullptr;
		if(event==CardUsed)
			card = data.value<CardUseStruct>().card;
		else
			card = data.value<CardResponseStruct>().m_card;
		if(card==nullptr || card->isKindOf("SkillCard"))return false;

		player->addMark("jili-Clear");
		int attackrange = player->getAttackRange();
		if(player->getMark("jili-Clear")== attackrange&& player->hasSkill(objectName())&& player->askForSkillInvoke(this,data)){
			room->broadcastSkillInvoke(objectName());
			player->drawCards(attackrange,objectName());
		}
		return false;
	}
};

class Jiedao : public TriggerSkill
{
public:
	Jiedao():TriggerSkill("jiedao")
	{
		events << DamageCaused << DamageComplete;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(event==DamageCaused){
			if(player->getMark("jiedao-Clear")>0||player->getLostHp()<1||!player->hasSkill(objectName()))return false;
			if(damage.to->isDead()||!player->askForSkillInvoke(this,damage.to))return false;
			room->broadcastSkillInvoke(objectName());
			player->addMark("jiedao-Clear");

			QStringList choices;
			for(int i = 1; i <= player->getLostHp(); i++){
				choices << QString::number(i);
			}
			QString choice = room->askForChoice(player,"jiedao",choices.join("+"),data);
			LogMessage log;
			log.type = "#JiedaoDamage";
			log.from = player;
			log.arg = choice;
			room->sendLog(log);

			damage.damage += choice.toInt();
			damage.tips << "jiedao_addDamage:" + choice + ":" + player->objectName();
			data = QVariant::fromValue(damage);
		} else {
			if(player->isDead())return false;
			foreach(QString tip,damage.tips){
				if(tip.contains("jiedao_addDamage:")){
					QStringList tips = tip.split(":");
					if(tips.length()!= 3)continue;
					int n = tips.at(1).toInt();
					ServerPlayer*from = room->findChild<ServerPlayer*>(tips.last());
					if(n>0&&from&&from->isAlive())
						room->askForDiscard(from,objectName(),n,n,false,true);
					break;
				}
			}
		}
		return false;
	}
};

SpMouzhuCard::SpMouzhuCard()
{
}

bool SpMouzhuCard::targetFilter(const QList<const Player*>&,const Player*to_select,const Player*Self)const
{
	if(to_select==Self)return false;
	QString choice = Self->getTag("spmouzhu").toString();
	if(choice=="distance")
		return Self->distanceTo(to_select)== 1;
	else
		return to_select->getHp()== Self->getHp();
	return false;
}

void SpMouzhuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		if(source->isDead())return;
		if(p->isDead()|| p->isKongcheng())continue;
		room->cardEffect(this,source,p);
	}
}

void SpMouzhuCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*source = effect.from,*p = effect.to;
	Room*room = source->getRoom();

	const Card*c = room->askForExchange(p,"spmouzhu",1,1,false,"@spmouzhu-give:" + source->objectName());
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE,p->objectName(),source->objectName(),"spmouzhu","");
	room->obtainCard(source,c,reason,false);
	if(p->isDead()|| source->isDead()|| p->getHandcardNum()>= source->getHandcardNum())return;

	Slash*slash = new Slash(Card::NoSuit,0);
	Duel*duel = new Duel(Card::NoSuit,0);
	slash->setSkillName("_spmouzhu");
	duel->setSkillName("_spmouzhu");
	slash->deleteLater();
	duel->deleteLater();

	QStringList choices;
	if(!p->isCardLimited(slash,Card::MethodUse)&& p->canSlash(source,slash,false))
		choices << "slash";
	if(!p->isCardLimited(duel,Card::MethodUse)&& !p->isProhibited(source,duel))
		choices << "duel";
	if(choices.isEmpty())return;

	QString choice = room->askForChoice(p,"spmouzhu",choices.join("+"));
	if(choice=="slash")
		room->useCard(CardUseStruct(slash,p,source));
	else
		room->useCard(CardUseStruct(duel,p,source));
}

class SpMouzhu : public ZeroCardViewAsSkill
{
public:
	SpMouzhu(): ZeroCardViewAsSkill("spmouzhu")
	{
	}

	const Card*viewAs()const
	{
		return new SpMouzhuCard;
	}

	QDialog*getDialog()const
	{
		return TiansuanDialog::getInstance("spmouzhu","distance,hp");
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("SpMouzhuCard");
	}
};

class SpYanhuo : public TriggerSkill
{
public:
	SpYanhuo(): TriggerSkill("spyanhuo")
	{
		events << Death << ConfirmDamage;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != player || !player->hasSkill(objectName()))return false;
			if(!player->askForSkillInvoke(this))return false;
			player->peiyin(this);
			int num = room->getTag("SpYanhuoSlashDamage").toInt();
			room->setTag("SpYanhuoSlashDamage",++num);
			room->addPlayerMark(player,"&spyanhuo-Keep");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash"))return false;
			int num = room->getTag("SpYanhuoSlashDamage").toInt();
			if(num <= 0)return false;

			LogMessage log;
			log.type = "#SPYanhuoDamage";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(num);
			room->sendLog(log);

			damage.damage += num;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class YangzhongVS : public ViewAsSkill
{
public:
	YangzhongVS(): ViewAsSkill("yangzhong")
	{
		response_pattern = "@@yangzhong";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return selected.length()< 2&& !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()!= 2)
			return nullptr;

		DummyCard*card = new DummyCard;
		card->setSkillName(objectName());
		card->addSubcards(cards);
		return card;
	}
};

class Yangzhong : public TriggerSkill
{
public:
	Yangzhong(): TriggerSkill("yangzhong")
	{
		events << Damage << Damaged;
		view_as_skill = new YangzhongVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from->isDead()|| damage.to->isDead())return false;
		if((event==Damage&& damage.from->hasSkill(objectName()))||(event==Damaged&& damage.to->hasSkill(objectName()))){
			if(!damage.from->canDiscard(damage.from,"he")|| damage.from->getCardCount()< 2)return false;
			if(!room->askForCard(damage.from,"@@yangzhong","@yangzhong:" + damage.to->objectName(),data,objectName()))return false;
			room->broadcastSkillInvoke(objectName());
			if(damage.to->isAlive())
				room->loseHp(HpLostStruct(damage.to,1,objectName(),event==Damage ? damage.from : damage.to));
		}
		return false;
	}
};

class Huangkong : public TriggerSkill
{
public:
	Huangkong(): TriggerSkill("huangkong")
	{
		events << TargetConfirmed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->hasFlag("CurrentPlayer")|| !player->isKongcheng())return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.to.contains(player))return false;
		if(use.card->isKindOf("Slash")|| use.card->isNDTrick()){
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(2,objectName());
		}
		return false;
	}
};

class Diting : public PhaseChangeSkill
{
public:
	Diting(): PhaseChangeSkill("diting")
	{
		waked_skills = "#diting";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(player->isDead())return false;
			if(p->isDead()|| !p->hasSkill(objectName())|| !player->inMyAttackRange(p))continue;
			int hp = qMin(p->getHp(),player->getHandcardNum());
			if(hp <= 0 || !p->askForSkillInvoke(this,player))continue;
			player->peiyin("diting");

			QList<int> cards;

			for(int i = 0; i < hp; ++i){
				if(player->getHandcardNum()<=i)break;
				int id = room->askForCardChosen(p,player,"h",objectName(),false,Card::MethodNone,cards);
				if(id<0)break;
				cards << id;
			}

			if(cards.isEmpty())continue;

			room->showCard(player,cards,p,false);
			room->fillAG(cards,p);
			int id = room->askForAG(p,cards,false,objectName());
			room->clearAG(p);

			room->addPlayerMark(player,QString("diting_show_%1_%2-PlayClear").arg(id).arg(p->objectName()));
		}
		return false;
	}
};

class DitingEffect : public TriggerSkill
{
public:
	DitingEffect(): TriggerSkill("#diting")
	{
		events << EventPhaseEnd << TargetSpecified;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->getPhase()== Player::Play;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			int id = use.card->getEffectiveId();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				QString mark = QString("diting_show_%1_%2-PlayClear").arg(id).arg(p->objectName());
				int num = player->getMark(mark);
				room->setPlayerMark(player,mark,0);
				if(p->isDead())continue;
				if(num > 0){
					if(use.to.contains(p)){
						room->sendCompulsoryTriggerLog(p,"diting",true);
						use.nullified_list << p->objectName();
						data = QVariant::fromValue(use);
					} else {
						for(int i = 0; i < num; i++){
							if(p->isDead())break;
							room->sendCompulsoryTriggerLog(p,"diting",true);
							p->drawCards(2,"diting");
						}
					}

				}
			}
		} else {
			foreach(ServerPlayer*p,room->getAllPlayers()){
				DummyCard*dummy = new DummyCard;
				dummy->deleteLater();

				foreach(int id,player->handCards()){
					QString mark = QString("diting_show_%1_%2-PlayClear").arg(id).arg(p->objectName());
					int num = player->getMark(mark);
					if(num > 0)dummy->addSubcard(id);
					room->setPlayerMark(player,mark,0);
				}

				if(p->isAlive()&& dummy->subcardsLength()> 0){
					room->sendCompulsoryTriggerLog(p,"diting",true,true);
					room->obtainCard(p,dummy,false);
				}
			}
		}
		return false;
	}
};

class Bihuof : public TriggerSkill
{
public:
	Bihuof(): TriggerSkill("bihuof")
	{
		events << Damage << Damaged;
		waked_skills = "#bihuof";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.from==damage.to || !damage.to || !damage.from)return false;

		QString prompt = "@bihuof-jia",reason = objectName();

		if(event==Damage)
			prompt = "@bihuof-jian",reason = "bihuof2";

		ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),reason,prompt,true,true);
		if(!t)return false;
		player->peiyin(this);

		int turn = t->getMark("Global_TurnCount2")+ 1;
		QString mark = "&bihuofjia+#" + QString::number(turn)+"-SelfClear";
		if(event==Damage)
			mark = "&bihuofjian+#" + QString::number(turn)+"-SelfClear";

		room->addPlayerMark(t,mark);
		return false;
	}
};

class BihuofDraw : public DrawCardsSkill
{
public:
	BihuofDraw(): DrawCardsSkill("#bihuof")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	int getDrawNum(ServerPlayer*player,int n)const
	{
		QString turn = QString::number(player->getMark("Global_TurnCount2"));
		int jia = player->getMark("&bihuofjia+#" + turn + "-SelfClear"),jian = player->getMark("&bihuofjian+#" + turn + "-SelfClear");
		int draw = n + jia - jian;
		Room*room = player->getRoom();
		//room->setPlayerMark(player,"&bihuofjia+#" + QString::number(turn)+"-SelfClear",0);   //是下回合摸牌阶段摸牌数受影响，不是下一个摸牌阶段摸牌数
		//room->setPlayerMark(player,"&bihuofjian+#" + QString::number(turn)+"-SelfClear",0);
		if(jia > 0 || jian > 0){
			LogMessage log;
			log.type = "#BihuofDraw";
			log.from = player;
			log.arg = "bihuof";
			log.arg2 = QString::number(draw); //qMax(0,draw)
			room->sendLog(log);
		}
		return draw;
	}
};

class Jingjian : public TriggerSkill
{
public:
	Jingjian(): TriggerSkill("jingjian")
	{
		events << Damage << Damaged;
		waked_skills = "#jingjian";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		room->sendCompulsoryTriggerLog(player,this);
		player->gainMark("&msjjjin");
		if(damage.from&& damage.from->isAlive()&& player->canPindian(damage.from)){
			if(player->askForSkillInvoke(this,"jingjian:" + damage.from->objectName())&& player->pindian(damage.from,objectName()))
				room->recover(player,RecoverStruct("jingjian",player));
		}
		return false;
	}
};

class JingjianAttackRange : public AttackRangeSkill
{
public:
	JingjianAttackRange(): AttackRangeSkill("#jingjian")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player*target,bool)const
	{
		if(target->hasSkill("jingjian"))
			return target->getMark("&msjjjin");
		return 0;
	}
};

class Shizhao : public TriggerSkill
{
public:
	Shizhao(): TriggerSkill("shizhao")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
		waked_skills = "#shizhao";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->hasFlag("CurrentPlayer")|| player->getMark("shizhao-Clear")> 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.from==player&& move.from_places.contains(Player::PlaceHand)&& move.is_last_handcard){
			room->sendCompulsoryTriggerLog(player,this);
			room->addPlayerMark(player,"shizhao-Clear");
			if(player->getMark("&msjjjin")> 0){
				player->loseMark("&msjjjin");
				player->drawCards(2,objectName());
			} else
				room->addPlayerMark(player,"&shizhao_debuff-Clear");
		}
		return false;
	}
};

class ShizhaoDamage : public TriggerSkill
{
public:
	ShizhaoDamage(): TriggerSkill("#shizhao")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getMark("&shizhao_debuff-Clear")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		int mark = player->getMark("&shizhao_debuff-Clear");
		room->setPlayerMark(player,"&shizhao_debuff-Clear",0);

		LogMessage log;
		log.from = player;
		log.type = "#YHHankaiDamaged";
		log.arg = "shizhao";
		log.arg2 = QString::number(damage.damage);
		log.arg3 = QString::number(damage.damage += mark);
		room->sendLog(log);
		player->peiyin("shizhao");
		room->notifySkillInvoked(player,"shizhao");
		data = QVariant::fromValue(damage);
		return false;
	}
};

JijieCard::JijieCard()
{
	target_fixed = true;
}

void JijieCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QList<int> ids = room->getNCards(1,true,false);

	QList<ServerPlayer*> _source;
	_source.append(source);
	CardsMoveStruct move(ids,nullptr,source,Player::PlaceTable,Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_PREVIEW,source->objectName(),"jijie",""));
	QList<CardsMoveStruct> moves;
	moves.append(move);
	room->notifyMoveCards(true,moves,false,_source);
	room->notifyMoveCards(false,moves,false,_source);

	QList<int> jijie_ids = ids;
	CardsMoveStruct jijie_move = room->askForYijiStruct(source,jijie_ids,"jijie",true,false,true,-1,room->getAlivePlayers(),
														CardMoveReason(),"",false,false);

	CardsMoveStruct move2(ids,source,nullptr,Player::PlaceHand,Player::PlaceTable,
						CardMoveReason(CardMoveReason::S_REASON_PREVIEW,source->objectName(),"jijie",""));
	moves.clear();
	moves.append(move2);
	room->notifyMoveCards(true,moves,false,_source);
	room->notifyMoveCards(false,moves,false,_source);

	ServerPlayer*target =(ServerPlayer*)jijie_move.to;
	if(!target)target = source;

	room->returnToEndDrawPile(ids);

	CardMoveReason reason(CardMoveReason::S_REASON_PREVIEWGIVE,source->objectName(),"jijie","");
	room->obtainCard(target,Sanguosha->getCard(ids.first()),reason,false);
}

class Jijie : public ZeroCardViewAsSkill
{
public:
	Jijie(): ZeroCardViewAsSkill("jijie")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("JijieCard");
	}

	const Card*viewAs()const
	{
		return new JijieCard;
	}
};

class Jiyuan : public TriggerSkill
{
public:
	Jiyuan(): TriggerSkill("jiyuan")
	{
		events << Dying << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(!dying.who || dying.who->isDead())return false;
			if(!player->askForSkillInvoke(this,QVariant::fromValue(dying.who)))return false;
			room->broadcastSkillInvoke(objectName());
			dying.who->drawCards(1,objectName());
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player&& move.reason.m_playerId != player->objectName())return false;
			if((move.to&& move.to==player)|| !move.to || move.to->isDead())return false;
			if(move.reason.m_reason != CardMoveReason::S_REASON_GIVE&& move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE)return false;
			if(move.to_place != Player::PlaceHand)return false;
			ServerPlayer*to = room->findPlayerByObjectName(move.to->objectName());
			if(!to || to->isDead())return false;
			if(!player->askForSkillInvoke(this,QVariant::fromValue(to)))return false;
			room->broadcastSkillInvoke(objectName());
			to->drawCards(1,objectName());
		}
		return false;
	}
};

class Lixun : public TriggerSkill
{
public:
	Lixun():TriggerSkill("lixun")
	{
		events << DamageInflicted << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->gainMark("&lxzhu",damage.damage);
			return true;
		} else {
			if(player->getPhase()!= Player::Play)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			JudgeStruct judge;
			judge.pattern = ".";
			judge.play_animation = false;
			judge.who = player;
			judge.reason = objectName();
			room->judge(judge);

			int zhu = player->getMark("&lxzhu");
			if(judge.card->getNumber()>= zhu)return false;

			const Card*card = room->askForDiscard(player,objectName(),zhu,zhu);
			if(card)zhu -= card->subcardsLength();
			if(zhu <= 0)return false;
			room->loseHp(HpLostStruct(player,zhu,objectName(),player));
		}
		return false;
	}
};

SpKuizhuCard::SpKuizhuCard()
{
	mute = true;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void SpKuizhuCard::onUse(Room*,CardUseStruct&)const
{
}

class SpKuizhuVS : public ViewAsSkill
{
public:
	SpKuizhuVS(): ViewAsSkill("spkuizhu")
	{
		expand_pile = "#spkuizhu";
	}

	bool viewFilter(const QList<const Card*>&,const Card*to_select)const
	{
		return !to_select->isEquipped()&& !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())
			return nullptr;

		int hand = 0,pile = 0;
		foreach(const Card*card,cards){
			if(Self->getHandcards().contains(card))
				hand++;
			else if(Self->getPile("#spkuizhu").contains(card->getEffectiveId()))
				pile++;
		}

		if(hand==pile){
			SpKuizhuCard*c = new SpKuizhuCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@spkuizhu";
	}
};

class SpKuizhu : public TriggerSkill
{
public:
	SpKuizhu():TriggerSkill("spkuizhu")
	{
		events << EventPhaseEnd;
		view_as_skill = new SpKuizhuVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getPhase()!= Player::Play)return false;

		int hp = player->getNextAlive()->getHp();
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHp()> hp)
				hp = p->getHp();
		}

		QList<ServerPlayer*> players;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHp()== hp)
				players << p;
		}
		if(players.isEmpty())return false;

		ServerPlayer*target = room->askForPlayerChosen(player,players,objectName(),"@spkuizhu-invoke",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(objectName());

		int hand = player->getHandcardNum();
		int num = qMin(5 - hand,target->getHandcardNum()- hand);
		if(num > 0)
			player->drawCards(num,objectName());

		if(player->isKongcheng())return false;

		LogMessage log;
		log.type = "$ViewAllCards";
		log.from = target;
		log.to << player;
		log.card_str = ListI2S(player->handCards()).join("+");
		room->sendLog(log,target);

		if(!target->canDiscard(target,"h"))return false;
		room->notifyMoveToPile(target,player->handCards(),objectName(),Player::PlaceHand,true);

		const Card*c = room->askForUseCard(target,"@@spkuizhu","@spkuizhu:" + player->objectName());

		room->notifyMoveToPile(target,player->handCards(),objectName(),Player::PlaceHand,false);

		if(c){
			QList<int> ids1,ids2;
			foreach(int id,c->getSubcards()){
				if(target->handCards().contains(id))
					ids1 << id;
				else if(player->handCards().contains(id))
					ids2 << id;
			}
			CardMoveReason reason1(CardMoveReason::S_REASON_THROW,target->objectName());
			CardsMoveStruct move1(ids1,target,nullptr,Player::PlaceHand,Player::DiscardPile,reason1);
			CardMoveReason reason2(CardMoveReason::S_REASON_EXTRACTION,target->objectName());
			CardsMoveStruct move2(ids2,player,target,Player::PlaceHand,Player::PlaceHand,reason2);
			QList<CardsMoveStruct> moves;
			moves << move1 << move2;

			LogMessage log;
			log.type = "$DiscardCard";
			log.from = target;
			log.card_str = ListI2S(ids1).join("+");
			room->sendLog(log);

			room->moveCardsAtomic(moves,false);

			if(ids2.length()< 2)return false;

			QStringList choices;
			if(player->getMark("&lxzhu")> 0)
				choices << "mark";
			if(target->isAlive()){
				foreach(ServerPlayer*p,room->getOtherPlayers(target)){
					if(target->inMyAttackRange(p)){
						choices << "damage";
						break;
					}
				}
			}
			if(choices.isEmpty())return false;

			QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(target));
			if(choice=="mark"&& player->getMark("&lxzhu")> 0)
				player->loseMark("&lxzhu");
			else if(choice=="damage"&& target->isAlive()){
				QList<ServerPlayer*> attack;
				foreach(ServerPlayer*p,room->getOtherPlayers(target)){
					if(target->inMyAttackRange(p))
						attack << p;
				}
				if(attack.isEmpty())return false;
				ServerPlayer*to = room->askForPlayerChosen(target,attack,objectName(),"@spkuizhu-damage");
				room->doAnimate(1,target->objectName(),to->objectName());
				room->damage(DamageStruct(objectName(),target,to));
			}
		}
		return false;
	}
};

SongshuCard::SongshuCard()
{
}

bool SongshuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& Self->canPindian(to_select);
}

void SongshuCard::onEffect(CardEffectStruct&effect)const
{
	if(!effect.from->canPindian(effect.to,false))return;
	Room*room = effect.from->getRoom();
	bool pindian = effect.from->pindian(effect.to,"songshu");
	if(pindian){
		int n = effect.from->usedTimes("SongshuCard");
		room->addPlayerHistory(effect.from,"SongshuCard",-n);
	} else {
		QList<ServerPlayer*> drawers;
		drawers << effect.from << effect.to;
		room->sortByActionOrder(drawers);
		room->drawCards(drawers,2,"songshu");
	}
}

class Songshu : public ZeroCardViewAsSkill
{
public:
	Songshu(): ZeroCardViewAsSkill("songshu")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("SongshuCard")&& player->canPindian();
	}

	const Card*viewAs()const
	{
		return new SongshuCard;
	}
};

class Sibian : public PhaseChangeSkill
{
public:
	Sibian(): PhaseChangeSkill("sibian")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Draw)return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);

		QList<int> show = room->showDrawPile(player,4,objectName());

		int max = Sanguosha->getCard(show.first())->getNumber();
		int min = Sanguosha->getCard(show.first())->getNumber();
		foreach(int id,show){
			int num = Sanguosha->getCard(id)->getNumber();
			if(num > max)
				max = num;
			if(num < min)
				min = num;
		}
		DummyCard*dummy = new DummyCard;
		foreach(int id,show){
			int num = Sanguosha->getCard(id)->getNumber();
			if(num==min || num==max){
				dummy->addSubcard(id);
				show.removeOne(id);
			}
		}
		int length = dummy->subcardsLength();
		if(length > 0)
			room->obtainCard(player,dummy,true);
		delete dummy;

		DummyCard*dum = new DummyCard;
		dum->deleteLater();
		foreach(int id,show){
			if(room->getCardPlace(id)== Player::PlaceTable)
				dum->addSubcard(id);
		}
		if(dum->subcardsLength()<= 0)return false;

		int hand = player->getHandcardNum();
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHandcardNum()< hand)
				hand = p->getHandcardNum();
		}
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->getHandcardNum()== hand)
				targets << p;
		}

		if(!targets.isEmpty()){
			room->fillAG(dum->getSubcards(),player);
			ServerPlayer*target = room->askForPlayerChosen(player,targets,objectName(),"@sibian-give",true,true);
			room->clearAG(player);

			if(target)
				room->giveCard(player,target,dum,objectName(),true);
			else {
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),"sibian","");
				room->throwCard(dum,reason,nullptr);
			}
		} else {
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),"sibian","");
			room->throwCard(dum,reason,nullptr);
		}
		return true;
	}
};

SpCuoruiCard::SpCuoruiCard()
{
}

bool SpCuoruiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return !to_select->isKongcheng()&& to_select != Self&& targets.length()< Self->getHp();
}

void SpCuoruiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		if(source->isDead())return;
		if(p->isAlive()&& !p->isKongcheng())
			room->cardEffect(this,source,p);
	}
}

void SpCuoruiCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	int id = room->askForCardChosen(effect.from,effect.to,"h","spcuorui");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName());
	room->obtainCard(effect.from,Sanguosha->getCard(id),reason,false);
}

class SpCuoruiVS : public ZeroCardViewAsSkill
{
public:
	SpCuoruiVS(): ZeroCardViewAsSkill("spcuorui")
	{
		response_pattern = "@@spcuorui";
	}

	const Card*viewAs()const
	{
		return new SpCuoruiCard;
	}
};

class SpCuorui : public PhaseChangeSkill
{
public:
	SpCuorui(): PhaseChangeSkill("spcuorui")
	{
		view_as_skill = new SpCuoruiVS;
		global = true;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()==Player::RoundStart){
			player->addMark("spcuorui_round-Keep");
			if(player->getHp()>0&&player->getMark("spcuorui_round-Keep")==1&&player->hasSkill(objectName()))
				room->askForUseCard(player,"@@spcuorui","@spcuorui");
		}
		return false;
	}
};

class SpLiewei : public TriggerSkill
{
public:
	SpLiewei(): TriggerSkill("spliewei")
	{
		events << Dying;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if(!dying.damage || !dying.damage->from || dying.damage->from != player || dying.who==player)return false;
		if(!room->hasCurrent())return false;
		if(player->getMark("spliewei-Clear")>= player->getHp())return false;
		if(!player->askForSkillInvoke(this))return false;
		room->addPlayerMark(player,"spliewei-Clear");
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1,objectName());
		return false;
	}
};

SecondSpCuoruiCard::SecondSpCuoruiCard()
{
}

bool SecondSpCuoruiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return !to_select->isKongcheng()&& to_select != Self&& targets.length()< Self->getHp();
}

void SecondSpCuoruiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	room->doSuperLightbox(source,"secondspcuorui");
	room->removePlayerMark(source,"@secondspcuoruiMark");
	foreach(ServerPlayer*p,targets){
		if(source->isDead())return;
		if(p->isAlive()&& !p->isKongcheng())
			room->cardEffect(this,source,p);
	}
}

void SecondSpCuoruiCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	int id = room->askForCardChosen(effect.from,effect.to,"h","secondspcuorui");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName());
	room->obtainCard(effect.from,Sanguosha->getCard(id),reason,false);
}

class SecondSpCuorui : public ZeroCardViewAsSkill
{
public:
	SecondSpCuorui(): ZeroCardViewAsSkill("secondspcuorui")
	{
		frequency = Limited;
		limit_mark = "@secondspcuoruiMark";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("@secondspcuoruiMark")> 0&& player->getHp()> 0;
	}

	const Card*viewAs()const
	{
		return new SecondSpCuoruiCard;
	}
};

class SecondSpLiewei : public TriggerSkill
{
public:
	SecondSpLiewei(): TriggerSkill("secondspliewei")
	{
		events << Dying;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(!player->hasFlag("CurrentPlayer")||!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1,objectName());
		return false;
	}
};

class FengshiMF : public TriggerSkill
{
public:
	FengshiMF(): TriggerSkill("fengshimf")
	{
		events << TargetSpecified << TargetConfirmed;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.to.length()!= 1)return false;
		if(!use.card->isKindOf("BasicCard")&& !use.card->isKindOf("TrickCard"))return false;
		if(event==TargetSpecified){
			foreach(ServerPlayer*p,use.to){
				if(player->isDead())return false;
				if(p->isDead())continue;
				if(p->getHandcardNum()< player->getHandcardNum()){
					if(!player->canDiscard(player,"he")|| !player->canDiscard(p,"he"))continue;
					if(!player->askForSkillInvoke(this,p))continue;
					room->broadcastSkillInvoke(this);
					if(player->canDiscard(player,"he")){
						int id = room->askForCardChosen(player,player,"he",objectName(),false,Card::MethodDiscard);
						room->throwCard(id,objectName(),player);
					}
					if(player->canDiscard(p,"he")){
						int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard);
						room->throwCard(id,objectName(),p,player);
					}
					int num = room->getTag("FengshiMFDamage_" + use.card->toString()).toInt();
					num++;
					room->setTag("FengshiMFDamage_" + use.card->toString(),num);
				}
			}
		} else if(event==TargetConfirmed&& use.to.contains(player)){
			if(use.from&& use.from->isAlive()&& use.from->getHandcardNum()> player->getHandcardNum()){
				if(!player->canDiscard(player,"he")|| !player->canDiscard(use.from,"he"))return false;
				if(!use.from->askForSkillInvoke("fengshimf_other",player,false))return false;

				LogMessage log;
				log.type = "#InvokeOthersSkill";
				log.from = use.from;
				log.to << player;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(this);
				room->notifySkillInvoked(player,objectName());

				if(player->canDiscard(player,"he")){
					int id = room->askForCardChosen(player,player,"he",objectName(),false,Card::MethodDiscard);
					room->throwCard(id,objectName(),player);
				}
				if(player->canDiscard(use.from,"he")){
					int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodDiscard);
					room->throwCard(id,objectName(),use.from,player);
				}
				int num = room->getTag("FengshiMFDamage_" + use.card->toString()).toInt();
				num++;
				room->setTag("FengshiMFDamage_" + use.card->toString(),num);
			}
		}
		return false;
	}
};

class FengshiMFDamage : public TriggerSkill
{
public:
	FengshiMFDamage(): TriggerSkill("#fengshimf")
	{
		events << DamageCaused << CardFinished;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card)return false;
			int num = room->getTag("FengshiMFDamage_" + damage.card->toString()).toInt();
			damage.damage += num;
			data = QVariant::fromValue(damage);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			room->removeTag("FengshiMFDamage_" + use.card->toString());
		}
		return false;
	}
};

YijiaoCard::YijiaoCard()
{
}

void YijiaoCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	QString choice = room->askForChoice(from,"yijiao","1+2+3+4",QVariant::fromValue(to));
	int mark = 10* choice.toInt();
	to->gainMark("&lcwyjyi",mark);
}

class YijiaoVS : public ZeroCardViewAsSkill
{
public:
	YijiaoVS(): ZeroCardViewAsSkill("yijiao")
	{
	}

	const Card*viewAs()const
	{
		return new YijiaoCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("YijiaoCard");
	}
};

class Yijiao : public TriggerSkill
{
public:
	Yijiao(): TriggerSkill("yijiao")
	{
		events  << PreCardUsed << PreCardResponded << EventPhaseChanging << EventPhaseStart;
		view_as_skill = new YijiaoVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			player->loseAllMarks("&lcwyjyi");
		} else if(event==EventPhaseStart){
			if(player->getPhase()== Player::Finish){
				int yi = player->getMark("&lcwyjyi"),num = player->getMark("&yijiao_num-Clear"),
						licaiwei = room->findPlayersBySkillName(objectName()).length();
				if(yi <=0)return false;
				if(num > yi){
					foreach(ServerPlayer*p,room->getAllPlayers()){
						if(p->isDead()|| !p->hasSkill(objectName()))continue;
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(2,objectName());
					}
				} else if(num < yi){
					if(player->isDead())return false;
					LogMessage log;
					log.from = player;
					log.type = "#ZhenguEffect";
					log.arg = objectName();
					room->sendLog(log);
					for(int i = 0; i < licaiwei; i++){
						if(player->isDead()|| !player->canDiscard(player,"h"))break;
						QList<int> ids;
						foreach(int id,player->handCards()){
							if(player->canDiscard(player,id))
								ids << id;
						}
						if(ids.isEmpty())break;
						int id = ids.at(qrand()% ids.length());
						room->throwCard(id,objectName(),player);
					}
				} else
					room->addPlayerMark(player,"yijiao_extra_turn",licaiwei);
			} else if(player->getPhase()== Player::NotActive){
				if(player->getMark("yijiao_extra_turn")<= 0)return false;
				LogMessage log;
				log.from = player;
				log.type = "#ZhenguEffect";
				log.arg = objectName();
				room->sendLog(log);
				room->removePlayerMark(player,"yijiao_extra_turn");
				if(player->isAlive())
					player->gainAnExtraTurn();
			}
		} else {
			if(!room->hasCurrent()|| player->isDead()|| player->getMark("&lcwyjyi")<= 0)return false;
			const Card*card = nullptr;
			if(event==PreCardUsed)
				card = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if(!res.m_isUse)return false;
				card = res.m_card;
			}
			if(!card || card->isKindOf("SkillCard"))return false;
			int number = card->getNumber();
			if(number==0)return false;
			room->addPlayerMark(player,"&yijiao_num-Clear",number);
		}
		return false;
	}
};

class Qibie : public TriggerSkill
{
public:
	Qibie(): TriggerSkill("qibie")
	{
		events << Death;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who==player || !player->canDiscard(player,"h"))return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);

		QList<int> hands = player->handCards();
		DummyCard*dummy = new DummyCard();
		dummy->deleteLater();

		foreach(int id,hands){
			if(player->canDiscard(player,id))
				dummy->addSubcard(id);
		}

		int length = dummy->subcardsLength();
		if(length > 0){
			CardMoveReason reason(CardMoveReason::S_REASON_THROW,player->objectName(),objectName(),"");
			room->throwCard(dummy,reason,player);
			if(player->isDead())return false;
			room->recover(player,RecoverStruct(objectName(),player));
			player->drawCards(++length,objectName());
		}
		return false;
	}
};

class Ruizhan : public TriggerSkill
{
public:
	Ruizhan(): TriggerSkill("ruizhan")
	{
		events << Damage << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card||!damage.card->isKindOf("Slash"))return false;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->isDead())continue;
				foreach(ServerPlayer*q,room->getAlivePlayers()){
					if(q->isDead()|| q->isNude())continue;
					if(!damage.card->hasFlag(QString("ruizhan_%1_%2").arg(q->objectName()).arg(p->objectName())))continue;
					int card_id = room->askForCardChosen(p,q,"he","ruizhan");
					CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,p->objectName());
					room->obtainCard(p,Sanguosha->getCard(card_id),reason,room->getCardPlace(card_id)!= Player::PlaceHand);
				}
			}
		}else if(player->getPhase()== Player::Start){
			if(player->getHandcardNum()< player->getHp())return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(!p->hasSkill(objectName())|| !p->canPindian(player))continue;
				if(!p->askForSkillInvoke(this,player))continue;
				p->peiyin(this);
	
				PindianStruct*pindian = p->PinDian(player,objectName());
				if(!pindian)continue;
	
				bool success = pindian->success,has_slash = pindian->from_card->isKindOf("Slash")|| pindian->to_card->isKindOf("Slash");
				if(success || has_slash){
					Slash*slash = new Slash(Card::NoSuit,0);
					slash->setSkillName("_ruizhan");
					slash->deleteLater();
					if(!p->canSlash(player,slash,false))continue;
					if(success&& has_slash)
						room->setCardFlag(slash,QString("ruizhan_%1_%2").arg(player->objectName()).arg(p->objectName()));
					room->useCard(CardUseStruct(slash,p,player));
				}
			}
		}
		return false;
	}
};

ShilieCard::ShilieCard()
{
	target_fixed = true;
}

void ShilieCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList choices;
	if(source->isWounded())
		choices << "recover";
	choices << "lose";
	QString choice = room->askForChoice(source,"shilie",choices.join("+"));
	if(choice=="recover"){
		room->recover(source,RecoverStruct("shilie",source));
		if(source->isAlive()&& !source->isNude()){
			const Card* c = room->askForExchange(source,"shilie",2,2,true,"@shilie-put");
			source->addToPile("shilie",c);

			DummyCard*dummy = new DummyCard();
			QList<int> pile = source->getPile("shilie");
			while(pile.length()> room->alivePlayerCount()){
				int id = pile.takeFirst();
				dummy->addSubcard(id);
			}
			if(dummy->subcardsLength()> 0){
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,source->objectName(),"shilie","");
				room->throwCard(dummy,reason,nullptr);
			}
			delete dummy;
		}
	} else {
		room->loseHp(HpLostStruct(source,1,"shilie",source));
		QList<int> pile = source->getPile("shilie");
		if(source->isAlive()&& !pile.isEmpty()){
			QList<int> get;
			if(pile.length()<= 2)
				get = pile;
			else {
				const Card*c = room->askForUseCard(source,"@@shilie!","@shilie",-1,Card::MethodNone);
				if(c)
					get = c->getSubcards();
				else {
					int id = pile.at(qrand()% pile.length());
					get << id;
					pile.removeOne(id);
					int id2 = pile.at(qrand()% pile.length());
					get << id2;
				}
			}
			if(get.isEmpty())return;

			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = source;
			log.arg = "shilie";
			log.card_str = ListI2S(get).join("+");
			room->sendLog(log);

			DummyCard shilie(get);
			room->obtainCard(source,&shilie);
		}
	}
}

ShilieGetCard::ShilieGetCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
	mute = true;
	m_skillName = "shilie";
}

void ShilieGetCard::onUse(Room*,CardUseStruct&)const
{
}

class ShilieVS : public ViewAsSkill
{
public:
	ShilieVS(): ViewAsSkill("shilie")
	{
		expand_pile = "shilie";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return false;
		return selected.length()< 2&& Self->getPile("shilie").contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			if(!cards.isEmpty())return nullptr;
			return new ShilieCard;
		}

		if(cards.length()!= 2)return nullptr;

		ShilieGetCard*c = new ShilieGetCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("ShilieCard");
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@shilie!";
	}
};

class Shilie : public TriggerSkill
{
public:
	Shilie(): TriggerSkill("shilie")
	{
		events << Death;
		view_as_skill = new ShilieVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->hasSkill(objectName())&& !target->getPile("shilie").isEmpty();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who != player)return false;
		QList<ServerPlayer*> targets = room->getOtherPlayers(player);
		if(death.damage&& death.damage->from&& targets.contains(death.damage->from))
			targets.removeOne(death.damage->from);
		if(targets.isEmpty())return false;
		ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@shilie-give",true,true);
		if(!t)return false;
		player->peiyin(this);
		room->giveCard(player,t,player->getPile("shilie"),objectName(),true);
		return false;
	}
};

class TenyearFuning : public TriggerSkill
{
public:
	TenyearFuning(): TriggerSkill("tenyearfuning")
	{
		events << CardUsed << CardResponded;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		const Card*c = nullptr;
		if(event==CardUsed)
			c = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(!res.m_isUse)return false;
			c = res.m_card;
		}
		if(!c || c->isKindOf("SkillCard"))return false;

		int mark = player->getMark("&tenyearfuningUsed-Clear")+ 1;
		if(!player->askForSkillInvoke(this,"tenyearfuning:" + QString::number(mark)))return false;
		player->peiyin(this);

		player->drawCards(2,objectName());
		room->addPlayerMark(player,"&tenyearfuningUsed-Clear");
		//if(!player->canDiscard(player,"he"))return false;
		if(player->isNude())return false;
		mark = player->getMark("&tenyearfuningUsed-Clear");
		room->askForDiscard(player,objectName(),mark,mark,false,true);
		return false;
	}
};

TenyearBingjiCard::TenyearBingjiCard()
{
	target_fixed = true;
}

void TenyearBingjiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	if(source->isKongcheng())return;

	QString suit = source->getHandcards().first()->getSuitString();
	QString _char = suit + "_char";

	room->showAllCards(source);
	room->addPlayerMark(source,QString("tenyearbingji_%1-PlayClear").arg(suit));

	QStringList suits;
	foreach(QString mark,source->getMarkNames()){
		if(!mark.startsWith("&tenyearbingjiSuit")|| !mark.endsWith("-PlayClear")|| source->getMark(mark)<= 0)continue;
		suits = mark.split("+");
		suits.removeOne("&tenyearbingjiSuit");
		room->setPlayerMark(source,mark,0);
		break;
	}
	if(!suits.contains(_char))
		suits << _char;
	_char = suits.join("+");
	room->setPlayerMark(source,"&tenyearbingjiSuit+" + _char + "-PlayClear",1);

	bool _slash = false,_peach = false;
	Slash*slash = new Slash(Card::NoSuit,0);
	slash->setSkillName("_tenyearbingji");
	slash->deleteLater();
	Peach*peach = new Peach(Card::NoSuit,0);
	peach->setSkillName("_tenyearbingji");
	peach->deleteLater();

	foreach(ServerPlayer*p,room->getOtherPlayers(source)){
		if(!source->isLocked(slash)&& source->canSlash(p,slash))
			_slash = true;
		if(!source->isLocked(peach)&& p->isWounded()&& !source->isProhibited(p,peach))
			_peach = true;
		if(_peach&& _slash)
			break;
	}

	QStringList choices;
	if(_slash)choices << "slash";
	if(_peach)choices << "peach";
	if(choices.isEmpty())return;

	QString choice = room->askForChoice(source,"tenyearbingji",choices.join("+"));

	QList<ServerPlayer*> targets;

	if(choice=="slash"){
		if(source->isLocked(slash))return;
		foreach(ServerPlayer*p,room->getOtherPlayers(source)){
			if(source->canSlash(p,slash))
				targets << p;
		}
		if(targets.isEmpty())return;
		if(room->askForUseCard(source,"@@tenyearbingji!","@tenyearbingji:slash"))return;
		ServerPlayer*t = targets.at(qrand()% targets.length());
		room->useCard(CardUseStruct(slash,source,t));
	} else {
		if(source->isLocked(peach))return;
		foreach(ServerPlayer*p,room->getOtherPlayers(source)){
			if(p->isWounded()&& !source->isProhibited(p,peach))
				targets << p;
		}
		if(targets.isEmpty())return;
		ServerPlayer*t = room->askForPlayerChosen(source,targets,"tenyearbingji","@tenyearbingji:peach");
		room->useCard(CardUseStruct(peach,source,t));
	}
}

class TenyearBingji : public ZeroCardViewAsSkill
{
public:
	TenyearBingji(): ZeroCardViewAsSkill("tenyearbingji")
	{
		response_pattern = "@@tenyearbingji!";
	}

	const Card*viewAs()const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return new TenyearBingjiCard;

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_tenyearbingji");
		return slash;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->isKongcheng())return false;
		QString suit = player->getHandcards().first()->getSuitString();
		if(player->getMark(QString("tenyearbingji_%1-PlayClear").arg(suit))> 0)return false;
		foreach(const Card*c,player->getHandcards()){
			if(c->getSuitString()!= suit)
				return false;
		}
		return true;
	}
};

class Douzhen : public TriggerSkill
{
public:
	Douzhen(): TriggerSkill("douzhen")
	{
		events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging << CardUsed << CardResponded
			<< EventAcquireSkill << EventLoseSkill;
		change_skill = true;
		frequency = Compulsory;
		waked_skills = "#douzhen-target";
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==CardsMoveOneTime || triggerEvent==EventPhaseStart)
			return 6;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart || !player->hasSkill(objectName()))return false;
			int n = player->getChangeSkillState("douzhen");
			foreach(int id,player->handCards()){
				const Card*c = Sanguosha->getEngineCard(id);
				if(!c->isKindOf("BasicCard"))continue;
				if(c->isBlack()&& n <= 1){
					Duel*duel = new Duel(c->getSuit(),c->getNumber());
					duel->setSkillName("douzhen");
					WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
					card->takeOver(duel);
					room->notifyUpdateCard(player,id,card);
				} else if(c->isRed()&& n >= 2){
					Slash*slash = new Slash(c->getSuit(),c->getNumber());
					slash->setSkillName("douzhen");
					WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
					card->takeOver(slash);
					room->notifyUpdateCard(player,id,card);
				}
			}
		} else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to != Player::NotActive)return false;
			room->filterCards(player,player->getCards("he"),true);
		} else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&& move.to_place==Player::PlaceHand&& player->hasSkill(objectName())
				&& player->hasFlag("CurrentPlayer")){
				int n = player->getChangeSkillState("douzhen");
				foreach(int id,player->handCards()){
					const Card*c = Sanguosha->getEngineCard(id);
					if(!c->isKindOf("BasicCard"))continue;
					if(c->isBlack()&& n <= 1){
						Duel*duel = new Duel(c->getSuit(),c->getNumber());
						duel->setSkillName("douzhen");
						WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
						card->takeOver(duel);
						room->notifyUpdateCard(player,id,card);
					} else if(c->isRed()&& n >= 2){
						Slash*slash = new Slash(c->getSuit(),c->getNumber());
						slash->setSkillName("douzhen");
						WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
						card->takeOver(slash);
						room->notifyUpdateCard(player,id,card);
					}
				}
			}
		} else if(event==EventAcquireSkill){
			if(data.toString()!= objectName()|| !player->hasSkill(objectName())|| player->getPhase()== Player::NotActive)return false;
			int n = player->getChangeSkillState("douzhen");
			foreach(int id,player->handCards()){
				const Card*c = Sanguosha->getEngineCard(id);
				if(!c->isKindOf("BasicCard"))continue;
				if(c->isBlack()&& n <= 1){
					Duel*duel = new Duel(c->getSuit(),c->getNumber());
					duel->setSkillName("douzhen");
					WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
					card->takeOver(duel);
					room->notifyUpdateCard(player,id,card);
				} else if(c->isRed()&& n >= 2){
					Slash*slash = new Slash(c->getSuit(),c->getNumber());
					slash->setSkillName("douzhen");
					WrappedCard*card = Sanguosha->getWrappedCard(c->getId());
					card->takeOver(slash);
					room->notifyUpdateCard(player,id,card);
				}
			}
		} else if(event==EventLoseSkill){
			if(data.toString()!= objectName()|| player->hasSkill(objectName()))return false;
			room->filterCards(player,player->getCards("he"),true);
		} else {
			if(!player->hasFlag("CurrentPlayer"))return false;

			const Card*c = nullptr;
			if(event==CardUsed)
				c = data.value<CardUseStruct>().card;
			else
				c = data.value<CardResponseStruct>().m_card;
			if(!c || c->isKindOf("SkillCard"))return false;

			if(c->isKindOf("Duel")&& c->getSkillNames().contains(objectName())&& event==CardUsed){
				room->setChangeSkillState(player,objectName(),2);
				foreach(int id,player->handCards()){
					const Card*c = Sanguosha->getEngineCard(id);
					if(!c->isKindOf("BasicCard"))continue;
					if(c->isRed()){
						Slash*slash = new Slash(c->getSuit(),c->getNumber());
						slash->setSkillName("douzhen");
						WrappedCard*card = Sanguosha->getWrappedCard(id);
						card->takeOver(slash);
						room->notifyUpdateCard(player,id,card);
					} else if(c->isBlack()){
						QList<const Card*> cards;
						cards << c;
						room->filterCards(player,cards,true);
					}
				}
				foreach(ServerPlayer*p,data.value<CardUseStruct>().to){
					if(player->isDead())return false;
					if(p->isNude())continue;
					int id = room->askForCardChosen(player,p,"he",objectName());
					room->obtainCard(player,id,false);
				}
			} else if(c->isKindOf("Slash")&& c->getSkillNames().contains(objectName())){
				room->setChangeSkillState(player,objectName(),1);

				if(event==CardUsed){
					CardUseStruct use = data.value<CardUseStruct>();
					use.m_addHistory = false;
					data = QVariant::fromValue(use);
				}

				foreach(int id,player->handCards()){
					const Card*c = Sanguosha->getEngineCard(id);
					if(!c->isKindOf("BasicCard"))continue;
					if(c->isBlack()){
						Duel*duel = new Duel(c->getSuit(),c->getNumber());
						duel->setSkillName("douzhen");
						WrappedCard*card = Sanguosha->getWrappedCard(id);
						card->takeOver(duel);
						room->notifyUpdateCard(player,id,card);
					} else if(c->isRed()){
						QList<const Card*> cards;
						cards << c;
						room->filterCards(player,cards,true);
					}
				}
			}
		}
		return false;
	}
};

class DouzhenTargetMod : public TargetModSkill
{
public:
	DouzhenTargetMod(): TargetModSkill("#douzhen-target")
	{
	}

	int getResidueNum(const Player*from,const Card*c,const Player*)const
	{
		if(c->getSkillName()== "douzhen")
			return 999;
		if(from->getMark("&dufeng-PlayClear")>1)
			return from->getMark("&dufeng-PlayClear")-1;
		return 0;
	}
};

CuichuanCard::CuichuanCard()
{
}

bool CuichuanCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void CuichuanCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	QList<int> drawpile = room->getDrawPile();
	QList<const Card*> equips;
	foreach(int id,drawpile){
		const Card*c = Sanguosha->getCard(id);
		if(!c->isKindOf("EquipCard"))continue;
		const EquipCard*equip = qobject_cast<const EquipCard*>(c->getRealCard());
		if(!equip)continue;
		if(to->getEquip(equip->location())|| !to->hasEquipArea(equip->location()))continue;
		equips << c;
	}

	try {
		if(!equips.isEmpty()){
			const Card*equip = equips.at(qrand()% equips.length());

			LogMessage log;
			log.type = "$ZhijianEquip";
			log.from = to;
			log.card_str = equip->toString();
			room->sendLog(log);

			room->moveCardTo(equip,nullptr,to,Player::PlaceEquip,
				CardMoveReason(CardMoveReason::S_REASON_PUT,from->objectName(),"cuichuan",""));
		}

		from->drawCards(to->getEquips().length(),"cuichuan");
	}
	catch(TriggerEvent triggerEvent){
		if(triggerEvent==TurnBroken || triggerEvent==StageChange)
			room->setPlayerMark(to,"cuichuan_equip_4-PlayClear",0);
		throw triggerEvent;
	}

	if(to->getMark("cuichuan_equip_4-PlayClear")<= 0)return;
	room->setPlayerMark(to,"cuichuan_equip_4-PlayClear",0);

	room->handleAcquireDetachSkills(from,"-cuichuan|zuojian");
	if(to->isAlive()&& room->hasCurrent())
		room->addPlayerMark(to,"&cuichuan_extra_turn");
}

class CuichuanVS : public OneCardViewAsSkill
{
public:
	CuichuanVS(): OneCardViewAsSkill("cuichuan")
	{
	}

	bool viewFilter(const Card*to_select)const
	{
		return !to_select->isEquipped()&& !Self->isJilei(to_select);
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("CuichuanCard");
	}

	const Card*viewAs(const Card*originalCard)const
	{
		CuichuanCard*c = new CuichuanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Cuichuan : public TriggerSkill
{
public:
	Cuichuan(): TriggerSkill("cuichuan")
	{
		events << CardsMoveOneTime << BeforeCardsMove << EventPhaseStart;
		view_as_skill = new CuichuanVS;
		waked_skills = "zuojian";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent triggerEvent)const
	{
		if(triggerEvent==CardsMoveOneTime)
			return 5;
		else if(triggerEvent==BeforeCardsMove)
			return 0;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				while(p->isAlive()){
					if(p->getMark("&cuichuan_extra_turn")<= 0)break;
					room->removePlayerMark(p,"&cuichuan_extra_turn");
					p->gainAnExtraTurn();
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(!move.to || move.to_place != Player::PlaceEquip)return false;
			CardMoveReason reason = move.reason;
			if(reason.m_skillName != objectName()|| reason.m_playerId != player->objectName())return false;

			ServerPlayer*to =(ServerPlayer*)move.to;

			if(event==BeforeCardsMove){
				if(to->getEquips().length()>= 4)return false;
				room->setPlayerMark(to,"cuichuan_ok-PlayClear",1);
			} else {
				if(to->getMark("cuichuan_ok-PlayClear")<= 0)return false;
				room->setPlayerMark(to,"cuichuan_ok-PlayClear",0);
				if(to->getEquips().length()>= 4)
					room->addPlayerMark(to,"cuichuan_equip_4-PlayClear");
			}
		}
		return false;
	}
};

class Zhengxu : public TriggerSkill
{
public:
	Zhengxu(): TriggerSkill("zhengxu")
	{
		events << CardsMoveOneTime << DamageInflicted << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;

		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player)return false;
			if(!move.from_places.contains(Player::PlaceHand)&& !move.from_places.contains(Player::PlaceEquip))return false;

			if(player->getMark("zhengxu_move-Clear")<= 0&& player->hasSkill(objectName())){
				room->setPlayerMark(player,"zhengxu_move-Clear",1);
				room->addPlayerMark(player,"&zhengxu_damage-Clear");
			}

			int mark = player->getMark("&zhengxu_draw-Clear");
			room->setPlayerMark(player,"&zhengxu_draw-Clear",0);

			int draw = 0;
			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)== Player::PlaceHand || move.from_places.at(i)== Player::PlaceEquip)
					draw++;
			}
			if(draw <= 0)return false;

			for(int i = 0; i < mark; i++){
				if(!player->askForSkillInvoke(this,"draw:" + QString::number(draw)))break;
				player->peiyin(this);
				player->drawCards(draw,objectName());
			}
		} else if(event==Damaged){
			if(player->getMark("zhengxu_damaged-Clear")<= 0&& player->hasSkill(objectName())){
				room->setPlayerMark(player,"zhengxu_damaged-Clear",1);
				room->addPlayerMark(player,"&zhengxu_draw-Clear");
			}
		} else {
			int mark = player->getMark("&zhengxu_damage-Clear");
			room->setPlayerMark(player,"&zhengxu_damage-Clear",0);
			if(mark <= 0)return false;

			DamageStruct damage = data.value<DamageStruct>();
			if(!player->askForSkillInvoke(this,"damage:" + QString::number(damage.damage)))return false;
			player->peiyin(this);

			LogMessage log;
			log.from = player;
			log.arg = QString::number(damage.damage);
			log.type = damage.from ? "#ZhengxuPrevent" : "#ZhengxuPrevent2";
			if(damage.from)
				log.to << damage.from;
			room->sendLog(log);
			return true;
		}
		return false;
	}
};

class Zuojian : public TriggerSkill
{
public:
	Zuojian(): TriggerSkill("zuojian")
	{
		events << EventPhaseEnd << CardUsed << CardResponded;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive()
		&& target->getPhase()== Player::Play;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseEnd){
			if(player->getMark("zuojian_num-PlayClear")< player->getHp()|| !player->hasSkill(objectName()))return false;
			QStringList choices;
			int equip_num = player->getEquips().length();

			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				int p_num = p->getEquips().length();
				if(p_num > equip_num&& !choices.contains("draw"))
					choices << "draw";
				else if(p_num < equip_num&& player->canDiscard(p,"h")&& !choices.contains("discard"))
					choices << "discard";
				if(choices.contains("draw")&& choices.contains("discard"))
					break;
			}
			if(choices.isEmpty())return false;
			choices << "cancel";

			QString choice = room->askForChoice(player,objectName(),choices.join("+"));
			if(choice=="cancel")return false;

			LogMessage log;
			log.from = player;
			log.type = "#InvokeSkill";
			log.arg = objectName();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());

			equip_num = player->getEquips().length();
			if(choice=="draw"){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getEquips().length()> equip_num)
						p->drawCards(1,objectName());
				}
			} else {
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getEquips().length()< equip_num&& player->canDiscard(p,"h")){
						int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodDiscard);
						room->throwCard(id,objectName(),p,player);
					}
				}
			}
		}else{
			const Card*c = nullptr;
			if(event==CardUsed)
				c = data.value<CardUseStruct>().card;
			else if(data.value<CardResponseStruct>().m_isUse)
				c = data.value<CardResponseStruct>().m_card;
			if(c&& c->getTypeId()>0)
				player->addMark("zuojian_num-PlayClear");
		}
		return false;
	}
};

class Tingxian : public TriggerSkill
{
public:
	Tingxian(): TriggerSkill("tingxian")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("tingxian_used-Clear")> 0)return false;

		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash")|| use.to.isEmpty())return false;

		int num = player->getEquips().length();
		if(num <= 0)return false;

		if(!player->askForSkillInvoke(this,"tingxian:" + QString::number((num))))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"tingxian_used-Clear");

		num = player->getEquips().length();
		player->drawCards(num,objectName());
		if(player->isDead()|| num <= 0)return false;

		LogMessage log;
		log.to = room->askForPlayersChosen(player,use.to,objectName(),0,num,"@tingxian-target:" + QString::number(num));
		if(log.to.isEmpty())return false;

		log.from = player;
		log.type = "#TingxianWuxiao";
		room->sendLog(log);

		foreach(ServerPlayer*p,log.to){
			room->doAnimate(1,player->objectName(),p->objectName());
			use.nullified_list << p->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Benshi : public TriggerSkill
{
public:
	Benshi(): TriggerSkill("benshi")
	{
		events << CardUsed;
		frequency = Compulsory;
		waked_skills = "#benshi";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;

		LogMessage log;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(use.to.contains(p)|| !player->inMyAttackRange(p)|| !player->canSlash(p,use.card))continue;
			room->doAnimate(1,player->objectName(),p->objectName());
			log.to << p;
		}
		if(log.to.isEmpty())return false;

		log.type = "#BenshiExtra";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player,objectName());
		use.to << log.to;
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		return false;
	}
};

class BenshiRange : public AttackRangeSkill
{
public:
	BenshiRange(): AttackRangeSkill("#benshi")
	{
	}

	int getExtra(const Player*target,bool)const
	{
		if(target->hasSkill("benshi"))
			return 1;
		return 0;
	}
};

LibangCard::LibangCard()
{
}

bool LibangCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.length()< 2&& !to_select->isNude()&& to_select != Self;
}

bool LibangCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	return targets.length()== 2;
}

void LibangCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	QList<const Card*> gets;
	foreach(ServerPlayer*p,targets){
		if(source->isDead())return;
		if(p->isNude())continue;
		int id = room->askForCardChosen(source,p,"he","libang");
		room->obtainCard(source,id,false);

		gets << Sanguosha->getCard(id);

		if(source->isAlive()&& source->hasCard(id))
			room->showCard(source,id);
	}

	if(source->isDead())return;

	JudgeStruct judge;
	judge.pattern = ".";
	judge.reason = "libang";
	judge.who = source;
	judge.play_animation = false;
	room->judge(judge);

	if(source->isDead())return;

	int same = 0;
	foreach(const Card*c,gets){
		if(c->getColorString()== judge.card->getColorString())
			same++;
	}

	if(same==0){
		if(source->getCardCount()< 2){
			room->loseHp(HpLostStruct(source,1,"libang",source));
			return;
		}

		QList<ServerPlayer*> new_targets;
		foreach(ServerPlayer*p,targets){
			if(p->isAlive())
				new_targets << p;
		}
		if(new_targets.isEmpty()){
			room->loseHp(HpLostStruct(source,1,"libang",source));
			return;
		}

		int n = 0;
		ServerPlayer*to;
		QList<int> card_ids = source->handCards()+ source->getEquipsId(),give;
		while(n < 2){
			if(card_ids.isEmpty())break;
			CardsMoveStruct move = room->askForYijiStruct(source,card_ids,"libang",false,false,
				n==0,2 - n,new_targets,CardMoveReason(),"@libang-give",false,false);
			if(move.card_ids.isEmpty()|| !move.to)
				break;
			n+= move.card_ids.length();
			new_targets.clear();
			to =(ServerPlayer*)move.to;
			new_targets << to;
			foreach(int id,move.card_ids){
				card_ids.removeOne(id);
				give << id;
			}
		}
		if(n < 2)
			room->loseHp(HpLostStruct(source,1,"libang",source));
		else
			room->giveCard(source,to,give,"libang");
	} else {
		if(room->getCardPlace(judge.card->getEffectiveId())== Player::DiscardPile)
			source->obtainCard(judge.card);
		if(source->isDead())return;

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_libang");
		slash->deleteLater();

		QList<ServerPlayer*> new_targets;
		foreach(ServerPlayer*p,targets){
			if(p->isAlive()&& source->canSlash(p,slash,false))
				new_targets << p;
		}
		if(new_targets.isEmpty())return;
		room->setPlayerFlag(source,"slashTargetFix");
		foreach(ServerPlayer*p,targets)
			room->setPlayerFlag(p,"SlashAssignee");
		if(!room->askForUseCard(source,"@@libang!","@libang")){
			room->setPlayerFlag(source,"-slashTargetFix");
			foreach(ServerPlayer*p,targets)
				room->setPlayerFlag(p,"-SlashAssignee");
			ServerPlayer*to = new_targets.at(qrand()% new_targets.length());
			room->useCard(CardUseStruct(slash,source,to),true);
		}
	}
}

class Libang : public ViewAsSkill
{
public:
	Libang(): ViewAsSkill("libang")
	{
		response_pattern = "@@libang!";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return selected.length()< 1&& !Self->isJilei(to_select);
		return false;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("LibangCard");
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			if(cards.isEmpty())return nullptr;
			LibangCard*c = new LibangCard;
			c->addSubcards(cards);
			return c;
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@libang!"){
				Slash*slash = new Slash(Card::NoSuit,0);
				slash->setSkillName("_libang");
				return slash;
			}
		}
		return nullptr;
	}
};

class Wujie : public TriggerSkill
{
public:
	Wujie(): TriggerSkill("wujie")
	{
		events << CardUsed << BuryVictim;
		frequency = Compulsory;
		waked_skills = "#wujie";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==BuryVictim){
			room->sendCompulsoryTriggerLog(player,this);
			player->setMark("wujieNoRewardAndPunish-Keep",1);
			DeathStruct death = data.value<DeathStruct>();
			ServerPlayer*killer = death.damage ? death.damage->from : nullptr;
			if(killer){
				LogMessage log;
				log.type = "#WujieNoRewardAndPunish";
				log.from = player;
				log.to << killer;
				log.arg = "wujie";
				room->sendLog(log);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard")|| use.card->isRed()|| use.card->isBlack())return false;
			LogMessage log;
			log.type = "#WujieNoSuit";
			log.from = player;
			log.arg = objectName();
			log.arg2 = use.card->objectName();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());
			use.m_addHistory = false;
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class WujieTargetMod : public TargetModSkill
{
public:
	WujieTargetMod(): TargetModSkill("#wujie")
	{
		pattern = ".";
	}

	int getResidueNum(const Player*from,const Card*card,const Player*)const
	{
		if(card->hasTip("shouhu"))
			return 999;
		if(from->hasSkill("wujie")&& !card->isBlack()&& !card->isRed())
			return 1;
		return 0;
	}

	int getDistanceLimit(const Player*from,const Card*card,const Player*)const
	{
		if(from->hasSkill("wujie")&& !card->isBlack()&& !card->isRed())
			return 999;
		return 0;
	}
};

class Piaoping : public TriggerSkill
{
public:
	Piaoping(): TriggerSkill("piaoping")
	{
		events << CardUsed << CardResponded;
		frequency = Compulsory;
		change_skill = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("piaoping_wuxiao-Clear")> 0)return false;

		const Card*card = nullptr;
		if(event==CardUsed)
			card = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(!res.m_isUse)return false;
			card = res.m_card;
		}

		if(!card || card->isKindOf("SkillCard"))return false;

		room->sendCompulsoryTriggerLog(player,this);
		room->addPlayerMark(player,"&piaoping_trigger-Clear");

		int x = player->getMark("&piaoping_trigger-Clear");
		x = qMin(x,player->getHp());
		if(x <= 0)return false;

		int n = player->getChangeSkillState(objectName());
		if(n <= 1){
			room->setChangeSkillState(player,objectName(),2);
			player->drawCards(x,objectName());
		} else if(n >= 2){
			room->setChangeSkillState(player,objectName(),1);
			room->askForDiscard(player,objectName(),x,x,false,true);
		}
		return false;
	}
};

TuoxianCard::TuoxianCard()
{
	mute = true;
	target_fixed = true;
}

void TuoxianCard::onUse(Room*room,CardUseStruct&use)const
{
	CardMoveReason reason(CardMoveReason::S_REASON_THROW,use.from->objectName(),"tuoxian","");
	room->throwCard(this,reason,use.from);
}

class TuoxianVS : public ViewAsSkill
{
public:
	TuoxianVS(): ViewAsSkill("tuoxian")
	{
		expand_pile = "#tuoxian";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return !Self->isJilei(to_select)&& selected.length()< Self->getMark("tuoxian_discard");
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()!= Self->getMark("tuoxian_discard"))return nullptr;
		TuoxianCard*c = new TuoxianCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@tuoxian";
	}
};

class Tuoxian : public TriggerSkill
{
public:
	Tuoxian(): TriggerSkill("tuoxian")
	{
		events << BeforeCardsMove;
		view_as_skill = new TuoxianVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getMark("&tuoxian_num")<= 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			if(move.reason.m_skillName != "piaoping")return false;

			room->fillAG(move.card_ids,player);
			ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@tuoxian-target",true,true);
			room->clearAG(player);
			if(!t)return false;
			player->peiyin(this);

			room->removePlayerMark(player,"&tuoxian_num");
			int x = move.card_ids.length();
			room->giveCard(player,t,move.card_ids,objectName());
			move.card_ids.clear();
			data = QVariant::fromValue(move);

			if(t->isDead())return false;

			QList<int> judge_ids = t->getJudgingAreaID();

			try {
				room->setPlayerMark(t,"tuoxian_discard",x);
				if(!judge_ids.isEmpty())
					room->notifyMoveToPile(t,judge_ids,objectName(),Player::PlaceDelayedTrick,true);
				const Card*c = room->askForUseCard(t,"@@tuoxian",QString("@tuoxian:%1::%2").arg(player->objectName()).arg(x),
								-1,Card::MethodDiscard);
				if(!judge_ids.isEmpty())
					room->notifyMoveToPile(t,judge_ids,objectName(),Player::PlaceDelayedTrick,false);

				if(!c){
					LogMessage log;
					log.type = "#TuoxianWuxiao";
					log.from = player;
					log.arg = "piaoping";
					room->sendLog(log);
					if(room->hasCurrent())
						room->addPlayerMark(player,"piaoping_wuxiao-Clear");
				}
			} catch(TriggerEvent triggerEvent){
				if(triggerEvent==TurnBroken || triggerEvent==StageChange){
					if(!judge_ids.isEmpty())
						room->notifyMoveToPile(t,judge_ids,objectName(),Player::PlaceDelayedTrick,false);
				}
				throw triggerEvent;
			}
		}
		return false;
	}
};

class Zhuili : public TriggerSkill
{
public:
	Zhuili(): TriggerSkill("zhuili")
	{
		events << TargetConfirming;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isBlack()|| use.card->isKindOf("SkillCard")|| use.from==player || !use.from || !use.to.contains(player)
			|| !player->hasSkill("piaoping",true)|| player->getMark("zhuili_wuxiao-Clear")> 0)return false;
		int x = player->getChangeSkillState("piaoping");
		player->peiyin(this);
		room->notifySkillInvoked(player,objectName());
		LogMessage log;
		log.from = player;
		log.arg = objectName();
		if(x <= 1){
			room->addPlayerMark(player,"&tuoxian_num");
			if(room->hasCurrent()){
				log.type = "#ZhuiliOne";
				log.arg2 = "tuoxian";
				room->sendLog(log);
				room->addPlayerMark(player,"zhuili_wuxiao-Clear");
			}
		} else {
			log.type = "#ZhuiliTwo";
			log.arg2 = "piaoping";
			room->sendLog(log);
			room->setChangeSkillState(player,"piaoping",1);
		}
		return false;
	}
};

class Dunxi : public TriggerSkill
{
public:
	Dunxi(): TriggerSkill("dunxi")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isDamageCard()|| use.card->isKindOf("SkillCard")|| use.card->isKindOf("DelayedTrick"))return false;
		QList<ServerPlayer*> targets = use.to;
		foreach(ServerPlayer*p,targets){
			if(p->isDead()|| p==player)
				targets.removeOne(p);
		}
		ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@dunxi-target",true,true);
		if(!t)return false;
		player->peiyin(this);
		t->gainMark("&bxdxdun");
		return false;
	}
};

class DunxiEffect : public TriggerSkill
{
public:
	DunxiEffect(): TriggerSkill("#dunxi")
	{
		events << TargetSpecifying;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->getMark("&bxdxdun")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("BasicCard")&& !use.card->isKindOf("TrickCard"))return false;
		if(use.to.length()!= 1)return false;

		player->loseMark("&bxdxdun");

		LogMessage log;
		log.arg = "dunxi";
		log.arg2 = use.card->objectName();
		log.from = player;

		QList<ServerPlayer*> targets = room->getCardTargets(player,use.card);

		if(targets.isEmpty()){
			log.type = "#DunxiNoTarget";
			room->sendLog(log);
			use.to.clear();
			data = QVariant::fromValue(use);
			return false;
		}

		ServerPlayer*old = use.to.first();
		ServerPlayer*neww = targets.at(qrand()% targets.length());
		room->doAnimate(1,player->objectName(),neww->objectName());

		log.type = "#DunxiTarget";
		log.to << neww;
		room->sendLog(log);
		use.to.clear();
		use.to << neww;
		data = QVariant::fromValue(use);

		if(use.card->isKindOf("Collateral")){/*
			QList<ServerPlayer*> victims;
			foreach(ServerPlayer*p,room->getOtherPlayers(neww)){
				if(neww->canSlash(p))
					victims << p;
			}
			ServerPlayer*victim = victims.at(qrand()% victims.length());

			log.type = "#DunxiTarget2";
			log.arg = use.card->objectName();
			log.arg2 = "slash";
			log.to.clear();
			log.to << victim;
			room->sendLog(log);

			room->doAnimate(1,neww->objectName(),victim->objectName());
			neww->setTag("attachTarget", QVariant::fromValue((victim)));*/
			neww->setTag("attachTarget", old->getTag("attachTarget"));
		}

		if(old==neww){
			player->loseAllMarks("&bxdxdun");
			room->loseHp(HpLostStruct(player,1,"dunxi",player));
			player->endPlayPhase();
		}
		return false;
	}
};

class Chongyi : public TriggerSkill
{
public:
	Chongyi(): TriggerSkill("chongyi")
	{
		events << CardUsed << EventPhaseEnd;
		waked_skills = "#chongyi";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1)return false;
			player->addMark("chongyi-PlayClear");
			player->setTag("chongyiCard", QVariant::fromValue(use.card));
			if(!use.card->isKindOf("Slash")||player->getMark("chongyi-PlayClear")>1)return false;

			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(!p->askForSkillInvoke(this,"slash:" + player->objectName()))continue;
				p->peiyin(this);
				player->drawCards(2,objectName());
				room->addPlayerMark(player,"chongyiSlash-PlayClear");
			}
		} else {
			const Card*c = player->getTag("chongyiCard").value<const Card*>();
			if(!c||!c->isKindOf("Slash"))return false;
			player->removeTag("chongyiCard");

			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				if(!p->askForSkillInvoke(this,"maxcards:" + player->objectName()))continue;
				p->peiyin(this);
				room->addMaxCards(player,1);
			}
		}
		return false;
	}
};

class ChongyiTargetMod : public TargetModSkill
{
public:
	ChongyiTargetMod(): TargetModSkill("#chongyi")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player*from,const Card*,const Player*)const
	{
		if(from->getPhase()== Player::Play)
			return from->getMark("chongyiSlash-PlayClear");
		return 0;
	}
};

class Chaofeng : public TriggerSkill
{
public:
	Chaofeng(const QString&chaofeng): TriggerSkill(chaofeng),chaofeng(chaofeng)
	{
		events << DamageCaused;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play || player->getMark(chaofeng + "_used-PlayClear")> 0)return false;
		if(!player->canDiscard(player,"h"))return false;
		const Card*card = room->askForCard(player,".|.|.|hand","@" + chaofeng + "-discard",data,objectName());
		if(!card)return false;
		room->broadcastSkillInvoke(this);
		room->addPlayerMark(player,chaofeng + "_used-PlayClear");

		int x = 1;
		DamageStruct damage = data.value<DamageStruct>();

		if(!damage.card){
			player->drawCards(x,objectName());
			return false;
		}

		if(chaofeng=="chaofeng"){
			if(damage.card->getSuit()== card->getSuit())
				x++;
			player->drawCards(x,objectName());
			if(damage.card->getNumber()== card->getNumber()){
				damage.damage++;
				data = QVariant::fromValue(damage);
			}
		} else if(chaofeng=="secondchaofeng"){
			if(damage.card->sameColorWith(card))
				x++;
			player->drawCards(x,objectName());
			if(damage.card->getTypeId()== card->getTypeId()){
				damage.damage++;
				data = QVariant::fromValue(damage);
			}

		}
		return false;
	}
private:
	QString chaofeng;
};

class Chuanshu : public PhaseChangeSkill
{
public:
	Chuanshu(const QString&chuanshu): PhaseChangeSkill(chuanshu),chuanshu(chuanshu)
	{
		frequency = Limited;
		limit_mark = "@" + chuanshu + "Mark";
		waked_skills = "longdan,congjian,chuanyun";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start || player->getMark(limit_mark)<= 0)return false;
		if(chuanshu=="chuanshu"&& !player->isLowestHpPlayer())return false;
		if(chuanshu=="secondchuanshu"&& !player->isWounded())return false;

		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@chuanshu-invoke",true,true);
		if(!t)return false;

		room->broadcastSkillInvoke(this);
		room->removePlayerMark(player,limit_mark);
		room->doSuperLightbox(player,chuanshu);

		room->acquireSkill(t,chuanshu=="chuanshu" ? "chaofeng" : "secondchaofeng");
		if(chuanshu=="chuanshu")
			room->loseMaxHp(player,1,objectName());
		room->handleAcquireDetachSkills(player,"longdan|congjian|chuanyun");
		return false;
	}
private:
	QString chuanshu;
};

class ChuanshuDeath : public TriggerSkill
{
public:
	ChuanshuDeath(const QString&chuanshu): TriggerSkill("#" + chuanshu),chuanshu(chuanshu)
	{
		events << Death;
		frequency = Limited;
		limit_mark = "@" + chuanshu + "Mark";
		waked_skills = "longdan,congjian,chuanyun";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->hasSkill(chuanshu)&& target->getMark(limit_mark)> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who != player)return false;
		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),chuanshu,"@chuanshu-invoke",true,true);
		if(!t)return false;

		room->broadcastSkillInvoke(chuanshu);
		room->removePlayerMark(player,limit_mark);
		room->doSuperLightbox(player,chuanshu);

		room->acquireSkill(t,chuanshu=="chuanshu" ? "chaofeng" : "secondchaofeng");
		if(chuanshu=="chuanshu")
			room->loseMaxHp(player,1,chuanshu);
		room->handleAcquireDetachSkills(player,"longdan|congjian|chuanyun");
		return false;
	}
private:
	QString chuanshu;
};

class Chuanyun : public TriggerSkill
{
public:
	Chuanyun(): TriggerSkill("chuanyun")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;
		foreach(ServerPlayer*p,QSet<ServerPlayer*>(use.to.begin(),use.to.end())){
			if(player->isDead())return false;
			if(!p->canDiscard(p,"e"))continue;
			if(!player->askForSkillInvoke(this,p))continue;
			room->broadcastSkillInvoke(this);

			QList<int> equips = p->getEquipsId();
			foreach(int id,equips){
				if(!p->canDiscard(p,id))
					equips.removeOne(id);
			}
			if(equips.isEmpty())continue;

			int equip = equips.at(qrand()% equips.length());
			room->throwCard(equip,objectName(),p);
		}
		return false;
	}
};

class Tianze : public TriggerSkill
{
public:
	Tianze(): TriggerSkill("tianze")
	{
		events << FinishJudge << CardFinished;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==FinishJudge){
			JudgeStruct*judge = data.value<JudgeStruct*>();
			if(!judge->card->isBlack())return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				room->sendCompulsoryTriggerLog(p,this);
				p->drawCards(1,objectName());
			}
		} else {
			if(player->getPhase()!= Player::Play)return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isBlack()|| use.card->isKindOf("SkillCard")|| !use.m_isHandcard)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->isDead())return false;
				if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("tianze-Clear")> 0)continue;
				if(!p->canDiscard(p,"he"))continue;
				const Card*card = room->askForCard(p,".|black","@tianze-discard:" + player->objectName(),data,objectName());
				if(!card)continue;
				p->peiyin(this);
				room->addPlayerMark(p,"tianze-Clear");
				room->damage(DamageStruct("tianze",p,player));
			}
		}
		return false;
	}
};

class Difa : public TriggerSkill
{
public:
	Difa(): TriggerSkill("difa")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!player->hasFlag("CurrentPlayer")|| player->getMark("difa-Clear")> 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to != player || !move.from_places.contains(Player::DrawPile)|| move.to_place != Player::PlaceHand)return false;

		QStringList ids;
		for(int i = 0; i < move.card_ids.length(); i++){
			int id = move.card_ids.at(i);
			if(player->handCards().contains(id)&& player->canDiscard(player,id)){
				if(move.from_places.at(i)== Player::DrawPile&& Sanguosha->getCard(id)->isRed())
					ids << QString::number(id);
			}
		}
		if(ids.isEmpty())return false;

		if(room->askForCard(player,ids.join(","),"@difa",data,objectName())){
			player->peiyin(this);
			room->addPlayerMark(player,"difa-Clear");
			QList<int> tricks;
			foreach(int id,room->getDrawPile()+ room->getDiscardPile()){
				if(Sanguosha->getCard(id)->isKindOf("TrickCard"))
					tricks << id;
			}
			if(tricks.isEmpty())return false;
			room->fillAG(tricks,player);
			int trick = room->askForAG(player,tricks,false,"difa");
			room->clearAG(player);
			room->obtainCard(player,trick,true);
		}
		return false;
	}
};

HeqiaCard::HeqiaCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool HeqiaCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(!targets.isEmpty()|| to_select==Self)return false;
	if(subcards.isEmpty())
		return !to_select->isKongcheng();
	return true;
}

void HeqiaCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to,*geter = nullptr;
	Room*room = from->getRoom();

	int num = subcardsLength();

	if(subcards.isEmpty()){
		if(to->isNude())return;
		const Card*card = room->askForExchange(to,"heqia",99999,1,true,"@heqia-give:" + from->objectName());
		geter = from;
		num = card->subcardsLength();
		room->giveCard(to,from,card,"heqia");
	} else {
		geter = to;
		room->giveCard(from,to,this,"heqia");
	}
	if(!geter || geter->isDead()|| geter->isKongcheng()|| num <= 0)return;

	QList<int> cards = room->getAvailableCardList(geter,"basic","heqia");
	room->fillAG(cards,geter);
	int id = room->askForAG(geter,cards,true,"heqia");
	room->clearAG(geter);
	if(id < 0)return;

	const Card*card = Sanguosha->getEngineCard(id);
	QString name = card->objectName();

	int extra = qMax(num - 1,0);
	if(card->targetFixed())
		num = num - 1;

	room->setPlayerMark(geter,"heqia_get_card",num);
	room->setPlayerProperty(geter,"heqia_card_name",name);
	QString prompt = "@heqia2:" + name + "::" + QString::number(extra);
	room->askForUseCard(geter,"@@heqia2",prompt,2,Card::MethodUse,false);
	room->setPlayerMark(geter,"heqia_get_card",0);
}

HeqiaUseCard::HeqiaUseCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
	m_skillName = "heqia";
	mute = true;
}

bool HeqiaUseCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	QString name = Self->property("heqia_card_name").toString();
	if(name.isEmpty())return false;
	Card*card = Sanguosha->cloneCard(name);
	card->setSkillName("_heqia");
	card->addSubcard(this);
	card->deleteLater();
	int mark = Self->getMark("heqia_get_card");
	if(mark <= 0&& card->targetFixed())
		return to_select==Self&& !Self->isProhibited(Self,card);
	return targets.length()< mark
		&& card->targetFilter(targets,to_select,Self);
}

bool HeqiaUseCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	QString name = Self->property("heqia_card_name").toString();
	if(name.isEmpty())return false;
	Card*card = Sanguosha->cloneCard(name);
	card->setSkillName("_heqia");
	card->addSubcard(this);
	card->deleteLater();
	return card->targetFixed()||!targets.isEmpty();
}

void HeqiaUseCard::onUse(Room*room,CardUseStruct&use)const
{
	QString name = use.from->property("heqia_card_name").toString();
	if(name.isEmpty())return;
	Card*card = Sanguosha->cloneCard(name);
	card->setSkillName("_heqia");
	card->addSubcard(this);
	card->deleteLater();
	use.card = card;
	if(card->targetFixed()&&!use.to.contains(use.from))
		use.to.append(use.from);
	if(card->isAvailable(use.from))
		room->useCard(use,false);
}

class HeqiaVS : public ViewAsSkill
{
public:
	HeqiaVS(): ViewAsSkill("heqia")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUsePattern().endsWith("1"))
			return true;

		QString name = Self->property("heqia_card_name").toString();
		if(name.isEmpty())return false;
		Card*card = Sanguosha->cloneCard(name);
		card->addSubcard(to_select);
		card->setSkillName("_heqia");
		card->deleteLater();
		return selected.isEmpty()&& card->isAvailable(Self);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern().endsWith("1")){
			HeqiaCard*c = new HeqiaCard;
			if(!cards.isEmpty())
				c->addSubcards(cards);
			return c;
		} else {
			if(cards.isEmpty())return nullptr;
			HeqiaUseCard*c = new HeqiaUseCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@heqia");
	}
};

class Heqia : public PhaseChangeSkill
{
public:
	Heqia(): PhaseChangeSkill("heqia")
	{
		view_as_skill = new HeqiaVS;
	}

	/*QDialog*getDialog()const
	{
		//if(Sanguosha->getCurrentCardUsePattern()== "@@heqia1")return nullptr;
		return GuhuoDialog::getInstance("heqia",true,false,false);
	}*/

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		room->askForUseCard(player,"@@heqia1","@heqia1",1,Card::MethodNone);
		return false;
	}
};

class HeqiaTargetMod : public TargetModSkill
{
public:
	HeqiaTargetMod(): TargetModSkill("#heqia")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*)const
	{
		if(card->getSkillName()== "heqia")
			return 999;
		return 0;
	}

	int getExtraTargetNum(const Player*from,const Card*card)const
	{
		if(card->getSkillName()== "heqia")
			return qMax(0,from->getMark("heqia_get_card")-1);
		return 0;
	}
};

class Yinyi : public TriggerSkill
{
public:
	Yinyi(): TriggerSkill("yinyi")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent()|| player->getMark("yinyi-Clear")> 0)return false;
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from==player || damage.nature != DamageStruct::Normal)return false;
		if(damage.from->getHandcardNum()== player->getHandcardNum()
			|| damage.from->getHp()== player->getHp())return false;

		room->addPlayerMark(player,"yinyi-Clear");
		LogMessage log;
		log.type = "#RenshiPrevent";
		log.from = player;
		log.arg = objectName();
		log.to << damage.from;
		log.arg2 = QString::number(damage.damage);
		room->sendLog(log);
		room->broadcastSkillInvoke(this);
		room->notifySkillInvoked(player,objectName());
		return true;
	}
};

class JingongViewAsSkill : public OneCardViewAsSkill
{
public:
	JingongViewAsSkill(const QString&name): OneCardViewAsSkill(name),name(name)
	{
		response_or_use = true;
		filter_pattern = "EquipCard,Slash";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		const Card*c = Self->getTag(name).value<const Card*>();
		if(!c || !c->isAvailable(Self)|| Self->isCardLimited(c,Card::MethodUse))return nullptr;
		Card*card = Sanguosha->cloneCard(c->objectName());
		card->setCanRecast(false);
		card->addSubcard(originalCard);
		card->setSkillName(name);
		return card;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark(name + "-PlayClear")<= 0;
	}

private:
	QString name;
};

class JingongSkill : public TriggerSkill
{
public:
	JingongSkill(const QString&name): TriggerSkill(name),name(name)
	{
		events << EventPhaseStart << EventAcquireSkill << PreCardUsed;
		view_as_skill = new JingongViewAsSkill(name);
	}

	int getPriority(TriggerEvent event)const
	{
		if(event==PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance(name,false,true,true,false,true);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()== Player::Finish){
				if(name != "jingong")return false;
				if(player->getMark("damage_point_round")> 0)return false;
				int mark = player->getMark(name + "_used-Clear");
				if(mark <= 0)return false;
				for(int i = 0; i < mark; i++){
					if(player->isDead())break;

					LogMessage log;
					log.type = "#ZhenguEffect";
					log.from = player;
					log.arg = objectName();
					room->sendLog(log);

					room->loseHp(HpLostStruct(player,1,objectName(),player));
				}
			}
			if(!player->hasSkill(objectName(),true))return false;
			if(player->getPhase()!= Player::Play)
				return false;
		} else if(event==EventAcquireSkill){
			if(data.toString()!= name)
				return false;
		} else if(event==PreCardUsed){
			if(!player->hasSkill(objectName(),true))return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isNDTrick()&& use.card->getSkillNames().contains(name)){
				room->addPlayerMark(player,name + "-PlayClear");
				room->addPlayerMark(player,name + "_used-Clear");
				return false;
			}
		}
		QStringList card_names,tricks;
		static QList<const TrickCard*> cards = Sanguosha->findChildren<const TrickCard*>();
		foreach(const TrickCard*card,cards){
			QString name = card->objectName();
			if(name.startsWith("_"))continue;
			if(!Sanguosha->getBanPackages().contains(card->getPackage())&& card->isNDTrick()
				&& !tricks.contains(card->objectName())&& !card->isKindOf("Nullification"))
				tricks << name;
		}
		qShuffle(tricks);
		foreach(QString name,tricks){
			card_names.append(name);
			if(card_names.length()>= 2)break;
		}
		int n = qrand()% 2;
		if(n==0)card_names << "__meirenji";
		else card_names << "__xiaolicangdao";
		QString property_name = name + "_tricks";
		room->setPlayerProperty(player,property_name.toStdString().c_str(),card_names.join("+"));
		return false;
	}

private:
	QString name;
};

class TenyearRanshang : public TriggerSkill
{
public:
	TenyearRanshang(): TriggerSkill("tenyearranshang")
	{
		events << EventPhaseStart << Damaged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.nature != DamageStruct::Fire)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->gainMark("&rsran",damage.damage);
		} else {
			if(player->getPhase()!= Player::Finish)return false;
			int mark = player->getMark("&rsran");
			if(mark <= 0)return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			room->loseHp(HpLostStruct(player,mark,objectName(),player));
			if(player->isDead())return false;
			//int lose = qMin(2,player->getMaxHp());
			//if(lose <= 0)return false;
			if(mark <= 2)return false;
			room->loseMaxHp(player,2,"tenyearranshang");
			if(player->isDead())return false;
			player->drawCards(2,objectName());
		}
		return false;
	}
};

class TenyearHanyong : public TriggerSkill
{
public:
	TenyearHanyong(): TriggerSkill("tenyearhanyong")
	{
		events << CardUsed << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			if(!player->hasSkill(objectName()))return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("ArcheryAttack")&& !use.card->isKindOf("SavageAssault")&&
					!(use.card->objectName()== "slash"&& use.card->getSuit()== Card::Spade))return false;
			if(!player->isWounded())return false;
			if(!player->askForSkillInvoke(this,data))return false;
			room->broadcastSkillInvoke(objectName());
			room->setCardFlag(use.card,"tenyearhanyong");
			if(player->getHp()<= room->getTag("TurnLengthCount").toInt())return false;
			player->gainMark("&rsran");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->hasFlag(objectName())|| damage.to->isDead())return false;
			++damage.damage;
			data = QVariant::fromValue(damage);
		}
		return false;
}
};

class Lvli : public TriggerSkill
{
public:
	Lvli(): TriggerSkill("lvli")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		ServerPlayer*current = room->getCurrent();
		if(!current || current->getPhase()== Player::NotActive)return false;
		int marks = player->getMark("lvli-Clear");
		int choujue = player->getMark("choujue");

		int max = 1;
		if(choujue > 0&& current==player)
			max = 2;
		if(marks >= max)return false;

		if(event==Damaged){
			if(player->getMark("beishui")<= 0)
				return false;
		}

		if((player->getHandcardNum()< player->getHp())||(player->getHandcardNum()> player->getHp()&& player->getLostHp()> 0)){
			if(!player->askForSkillInvoke(this))return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player,"lvli-Clear");
			if(player->getHandcardNum()< player->getHp()){
				int draw = player->getHp()- player->getHandcardNum();
				if(draw <= 0)return false;
				player->drawCards(draw,objectName());
			} else if(player->getHandcardNum()> player->getHp()&& player->getLostHp()> 0){
				int recover = player->getHandcardNum()- player->getHp();
				recover = qMin(recover,player->getMaxHp()- player->getHp());
				if(recover <= 0)return false;
				room->recover(player,RecoverStruct(player,nullptr,recover,"lvli"));
			}
		}
		return false;
	}
};

class Choujue : public TriggerSkill
{
public:
	Choujue(): TriggerSkill("choujue")
	{
		events << EventPhaseChanging;
		frequency = Wake;
		waked_skills = "beishui";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if(change.to != Player::NotActive)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| p->getMark(objectName())> 0 || !p->hasSkill(objectName()))continue;
			if(p->canWake("choujue")|| qAbs(p->getHandcardNum()- p->getHp())>= 3){
				room->sendCompulsoryTriggerLog(p,this);
				room->doSuperLightbox(p,"choujue");
				room->setPlayerMark(p,"choujue",1);
				if(room->changeMaxHpForAwakenSkill(p,-1,objectName())){
					if(!p->hasSkill("beishui",true))
						room->acquireSkill(p,"beishui");
					if(p->hasSkill("lvli"),true){
						LogMessage log;
						log.type = "#JiexunChange";
						log.from = p;
						log.arg = "lvli";
						room->sendLog(log);
					}
					QString translate;
					if(p->getMark("beishui")> 0)
						translate = Sanguosha->translate(":lvli4");
					else
						translate = Sanguosha->translate(":lvli2");
					Sanguosha->addTranslationEntry(":lvli",translate.toStdString().c_str());
					room->doNotify(p,QSanProtocol::S_COMMAND_UPDATE_SKILL,QVariant("lvli"));
				}
			}
		}
		return false;
	}
};

class Beishui : public PhaseChangeSkill
{
public:
	Beishui(): PhaseChangeSkill("beishui")
	{
		frequency = Wake;
		waked_skills = "qingjiao";
	}

	bool triggerable(const ServerPlayer*player)const
	{
		return player&& player->isAlive()&&player->getPhase()== Player::Start
		&& player->getMark(objectName())<1&& player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getHandcardNum()>=2&&player->getHp()>=2&&!player->canWake(objectName()))
			return false;
		room->sendCompulsoryTriggerLog(player,this);
		room->doSuperLightbox(player,"beishui");
		room->setPlayerMark(player,"beishui",1);
		if(room->changeMaxHpForAwakenSkill(player,-1,objectName())){
			room->acquireSkill(player,"qingjiao");
			if(player->hasSkill("lvli"),true){
				LogMessage log;
				log.type = "#JiexunChange";
				log.from = player;
				log.arg = "lvli";
				room->sendLog(log);
			}
			QString translate;
			if(player->getMark("choujue")> 0)
				translate = Sanguosha->translate(":lvli4");
			else
				translate = Sanguosha->translate(":lvli3");
			Sanguosha->addTranslationEntry(":lvli",translate.toStdString().c_str());
			room->doNotify(player,QSanProtocol::S_COMMAND_UPDATE_SKILL,QVariant("lvli"));
		}
		return false;
	}
};

class Qingjiao : public PhaseChangeSkill
{
public:
	Qingjiao(): PhaseChangeSkill("qingjiao")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::Play&& player->hasSkill(objectName())){
			if(!player->canDiscard(player,"h"))return false;
			if(!player->askForSkillInvoke(this))return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player,"qingjiao-Clear");
			player->throwAllHandCards(objectName());

			if(player->isDead())return false;

			QList<int> ids = room->getDrawPile()+ room->getDiscardPile();
			qShuffle(ids);
			QStringList names;
			DummyCard*dummy = new DummyCard();
			foreach(int id,ids){
				const Card*card = Sanguosha->getCard(id);
				QString name = card->objectName();
				if(card->isKindOf("Weapon"))name = "Weapon";
				else if(card->isKindOf("Armor"))name = "Armor";
				else if(card->isKindOf("DefensiveHorse"))name = "DefensiveHorse";
				else if(card->isKindOf("OffensiveHorse"))name = "OffensiveHorse";
				else if(card->isKindOf("Treasure"))name = "Treasure";
				if(!names.contains(name)){
					dummy->addSubcard(id);
					names << name;
					if(names.length()>=8)break;
				}
			}
			room->obtainCard(player,dummy,true);
			delete dummy;
		} else if(player->getPhase()== Player::Finish){
			if(player->getMark("qingjiao-Clear")<= 0)return false;
			room->setPlayerMark(player,"qingjiao-Clear",0);
			if(player->isNude())return false;
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);
			player->throwAllHandCardsAndEquips(objectName());
		}
		return false;
	}
};

class TenyearBaobian : public TriggerSkill
{
public:
	TenyearBaobian(): TriggerSkill("tenyearbaobian")
	{
		events << Damaged;
		frequency = Compulsory;
		waked_skills = "tiaoxin,tenyearpaoxiao,tenyearshensu";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		foreach(QString sk,waked_skills.split(",")){
			if(!player->hasSkill(sk,true)){
				room->sendCompulsoryTriggerLog(player,this);
				room->acquireSkill(player,sk);
				break;
			}
		}
		return false;
	}
};

FenyueCard::FenyueCard()
{
}

bool FenyueCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return Self->canPindian(to_select)&& targets.isEmpty();
}

void FenyueCard::onEffect(CardEffectStruct&effect)const
{
	if(!effect.from->canPindian(effect.to,false))return;
	PindianStruct*pindian = effect.from->PinDian(effect.to,"fenyue");
	if(!pindian)return;
	if(pindian->from_number <= pindian->to_number)return;

	int num = pindian->from_card->getNumber();
	Room*room = effect.from->getRoom();

	if(num <= 5){
		if(!effect.to->isNude()){
			int card_id = room->askForCardChosen(effect.from,effect.to,"he","fenyue");
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName());
			room->obtainCard(effect.from,Sanguosha->getCard(card_id),reason,room->getCardPlace(card_id)!= Player::PlaceHand);
		}
	}

	if(num <= 9){
		QList<int> slashs;
		foreach(int id,room->getDrawPile()){
			if(Sanguosha->getCard(id)->isKindOf("Slash"))
			slashs << id;
		}
		if(!slashs.isEmpty()){
			int id = slashs.at(qrand()% slashs.length());
			effect.from->obtainCard(Sanguosha->getCard(id),true);
		}
	}

	if(num <= 13){
		ThunderSlash*thunder_slash = new ThunderSlash(Card::NoSuit,0);
		thunder_slash->setSkillName("_fenyue");
		thunder_slash->deleteLater();
		if(effect.from->canSlash(effect.to,thunder_slash,false))
			room->useCard(CardUseStruct(thunder_slash,effect.from,effect.to),false);
	}
}

class FenyueVS : public ZeroCardViewAsSkill
{
public:
	FenyueVS(): ZeroCardViewAsSkill("fenyue")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(!player->canPindian())return false;
		return player->usedTimes("FenyueCard")< player->getMark("fenyue-PlayClear");
	}

	const Card*viewAs()const
	{
		return new FenyueCard;
	}
};

class Fenyue : public TriggerSkill
{
public:
	Fenyue(): TriggerSkill("fenyue")
	{
		events << EventPhaseStart << CardFinished << EventAcquireSkill << Death;
		view_as_skill = new FenyueVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getPhase()!= Player::Play)return false;
		int n = 0;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(!player->isYourFriend(p))n++;
		}
		room->setPlayerMark(player,"fenyue-PlayClear",n);
		return false;
	}
};

class FenyueRevived : public TriggerSkill
{
public:
	FenyueRevived(): TriggerSkill("#fenyue-revived")
	{
		events << Revived;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&)const
	{
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(!p->hasSkill("fenyue")|| p->getPhase()!= Player::Play)return false;
			int n = 0;
			foreach(ServerPlayer*q,room->getOtherPlayers(p)){
				if(!p->isYourFriend(q))n++;
			}
			room->setPlayerMark(p,"fenyue-PlayClear",n);
		}
		return false;
	}
};

class Zhuilie : public TriggerSkill
{
public:
	Zhuilie(): TriggerSkill("zhuilie")
	{
		events << TargetSpecified << ConfirmDamage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash"))return false;
			foreach(ServerPlayer*p,use.to){
				if(!player->inMyAttackRange(p)){
					room->sendCompulsoryTriggerLog(player,objectName(),true,true);
					use.m_addHistory = false;
					data = QVariant::fromValue(use);
					JudgeStruct judge;
					judge.reason = objectName();
					judge.who = player;
					judge.pattern = "Weapon,OffensiveHorse,DefensiveHorse";
					judge.good = true;
					room->judge(judge);

					if(judge.isGood())
						room->setCardFlag(use.card,"zhuilie_" + p->objectName());
					else
						room->loseHp(HpLostStruct(player,1,objectName(),player));
				}
			}
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash")|| damage.to->isDead())return false;
			if(!damage.card->hasFlag("zhuilie_" + damage.to->objectName()))return false;
			room->setCardFlag(damage.card,"-zhuilie_" + damage.to->objectName());
			LogMessage log;
			log.type = damage.to->getHp()> 0 ? "#ZhuilieDamage" : "#ZhuiliePrevent";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(qMax(0,damage.to->getHp()));
			room->sendLog(log);
			if(damage.to->getHp()> 0){
				damage.damage = damage.to->getHp();
				data = QVariant::fromValue(damage);
			} else
				return true;
			return false;
		}
		return false;
	}
};

class ZhuilieSlash : public TargetModSkill
{
public:
	ZhuilieSlash(): TargetModSkill("#zhuilie-slash")
	{
	}

	int getDistanceLimit(const Player*from,const Card*,const Player*)const
	{
		if(from->hasSkill("zhuilie"))
			return 999;
		return 0;
	}
};

class TenyearFenyin : public TriggerSkill
{
public:
	TenyearFenyin(): TriggerSkill("tenyearfenyin")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::DiscardPile||!player->hasFlag("CurrentPlayer"))return false;

		QString mark = "";
		foreach(QString m,player->getMarkNames()){
			if(m.startsWith("&tenyearfenyin")&& player->getMark(m)> 0){
				mark = m;
				break;
			}
		}
		foreach(int id,move.card_ids){
			const Card*c = Sanguosha->getCard(id);
			if(mark=="" || !mark.contains(c->getSuitString()+ "_char")){
				int index = qrand()% 2 + 1;
				if(!player->getGeneralName().contains("liuzan")&& !player->getGeneral2Name().contains("liuzan")&&
						(player->getGeneralName().contains("wufan")|| player->getGeneral2Name().contains("wufan")))
					index += 2;

				room->sendCompulsoryTriggerLog(player,objectName(),true,true,index);

				if(mark != ""){
					room->setPlayerMark(player,mark,0);
					QString len = "-Clear";
					int length = len.length();
					mark.chop(length);
				} else
					mark = "&tenyearfenyin";

				mark = mark + "+" + c->getSuitString()+ "_char-Clear";
				room->addPlayerMark(player,mark);
				player->drawCards(1,objectName());
			}
		}
		return false;
	}
};

LijiCard::LijiCard()
{
}

void LijiCard::onEffect(CardEffectStruct&effect)const
{
	effect.from->getRoom()->damage(DamageStruct("liji",effect.from,effect.to));
}

class LijiVS : public OneCardViewAsSkill
{
public:
	LijiVS(): OneCardViewAsSkill("liji")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		int alive = player->getMark("liji_alive_num-Clear");
		if(alive <= 0)return false;
		int n = floor(player->getMark("&liji-Clear")/ alive);
		return player->usedTimes("LijiCard")< n;
	}

	const Card*viewAs(const Card*originalcard)const
	{
		LijiCard*c = new LijiCard;
		c->addSubcard(originalcard->getId());
		return c;
	}
};

class Liji : public TriggerSkill
{
public:
	Liji(): TriggerSkill("liji")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		view_as_skill = new LijiVS;
	}

	int getPriority(TriggerEvent event)const
	{
		if(event==EventPhaseStart)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			int alive = 8;
			if(room->alivePlayerCount()< 5)
				alive = 4;
			room->setPlayerMark(player,"liji_alive_num-Clear",alive);
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to || move.to_place != Player::DiscardPile||!player->hasFlag("CurrentPlayer"))return false;
			room->addPlayerMark(player,"&liji-Clear",move.card_ids.length());
		}
		return false;
	}
};

class TenyearJinggongVS : public OneCardViewAsSkill
{
public:
	TenyearJinggongVS(): OneCardViewAsSkill("tenyearjinggong")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		return(pattern.contains("slash")|| pattern.contains("Slash"));
	}

	bool viewFilter(const Card*to_select)const
	{
		if(to_select->getTypeId()!= Card::TypeEquip)return false;
		Slash*slash = new Slash(Card::SuitToBeDecided,-1);
		slash->addSubcard(to_select);
		slash->deleteLater();
		return slash->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard)const
	{
		Slash*slash = new Slash(originalCard->getSuit(),originalCard->getNumber());
		slash->addSubcard(originalCard);
		slash->setSkillName(objectName());
		return slash;
	}
};

class TenyearJinggong : public TriggerSkill
{
public:
	TenyearJinggong(): TriggerSkill("tenyearjinggong")
	{
		events << ConfirmDamage;
		view_as_skill = new TenyearJinggongVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.card || !damage.card->isKindOf("Slash"))return false;
		if(damage.card->getSkillNames().contains(objectName())){
			if(!damage.to || damage.to->isDead())return false;
			int distance = player->distanceTo(damage.to);
			distance = qMin(5,distance);
			LogMessage log;
			log.type = "#TenyearJinggongDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(distance);
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());
			if(distance <= 0)return true;
			damage.damage = distance;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class TenyearJinggongTargetMod : public TargetModSkill
{
public:
	TenyearJinggongTargetMod(): TargetModSkill("#tenyearjinggong")
	{
		frequency = NotFrequent;
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*)const
	{
		if(card->getSkillName()== "tenyearjinggong")
			return 999;
		return 0;
	}
};

class TenyearXiaojun : public TriggerSkill
{
public:
	TenyearXiaojun(): TriggerSkill("tenyearxiaojun")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card || use.card->isKindOf("SkillCard")|| use.to.length()!= 1)return false;
		ServerPlayer*to = use.to.first();
		if(to==player)return false;
		if(!player->canDiscard(to,"h"))return false;
		int dis = to->getHandcardNum()/ 2;
		if(dis <= 0 || !player->askForSkillInvoke(this,QString("tenyearxiaojun:%1::%2").arg(to->objectName()).arg(dis)))return false;
		player->peiyin(this);

		QList<int> cards;

		for(int i = 0; i < dis; ++i){
			if(to->getCardCount()<=i)break;
			int id = room->askForCardChosen(player,to,"h",objectName(),false,Card::MethodDiscard,cards);
			if(id<0)break;
			cards << id;
		}
		if(cards.isEmpty())return false;
		DummyCard dummy(cards);
		room->throwCard(&dummy,objectName(),to,player);

		bool same = false;
		foreach(int id,cards){
			if(Sanguosha->getCard(id)->getSuit()== use.card->getSuit()){
				same = true;
				break;
			}
		}
		if(!same || player->isKongcheng())return false;
		dis = player->getHandcardNum()/ 2;
		if(dis <= 0)return false;
		room->askForDiscard(player,objectName(),dis,dis);
		return false;
	}
};

class TenyearYingbing : public TriggerSkill
{
public:
	TenyearYingbing(): TriggerSkill("tenyearyingbing")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("SkillCard"))return false;
		foreach(ServerPlayer*p,use.to){
			if(p->getPile("incantation").isEmpty())continue;
			if(player->getMark("tenyearyingbing_" + p->objectName()+ "-Clear")> 0)continue;
			room->addPlayerMark(player,"tenyearyingbing_" + p->objectName()+ "-Clear");
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(2,objectName());
		}
		return false;
	}
};

class Lianhua : public PhaseChangeSkill
{
public:
	Lianhua(): PhaseChangeSkill("lianhua")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::Play){
			room->setPlayerMark(player,"danxue_red",0);
			room->setPlayerMark(player,"danxue_black",0);
			player->loseAllMarks("&danxue");
		} else if(player->getPhase()== Player::Start){
			int red = player->getMark("danxue_red");
			int black = player->getMark("danxue_black");
			if(red + black <= 3){
				room->acquireOneTurnSkills(player,"lianhua","yingzi");
				LianhuaCard(player,"peach");
			} else {
				if(red > black){
					room->acquireOneTurnSkills(player,"lianhua","tenyearguanxing");
					LianhuaCard(player,"ex_nihilo");
				} else if(black > red){
					room->acquireOneTurnSkills(player,"lianhua","zhiyan");
					LianhuaCard(player,"snatch");
				} else if(black==red){
					room->acquireOneTurnSkills(player,"lianhua","gongxin");
					QList<int> slash,duel,get;
					foreach(int id,room->getDiscardPile()+ room->getDrawPile()){
						const Card*card = Sanguosha->getCard(id);
						if(card->isKindOf("Slash"))
							slash << id;
						else if(card->objectName()== "duel")
							duel << id;
					}
					if(!slash.isEmpty())
						get << slash.at(qrand()% slash.length());
					if(!duel.isEmpty())
						get << duel.at(qrand()% duel.length());
					if(get.isEmpty())return false;
					DummyCard*dummy = new DummyCard(get);
					room->obtainCard(player,dummy,true);
					delete dummy;
				}
			}
		}
		return false;
	}

private:
	void LianhuaCard(ServerPlayer*player,const QString&name)const
	{
		QList<int> ids;
		Room*room = player->getRoom();
		foreach(int id,room->getDiscardPile()+ room->getDrawPile()){
			if(Sanguosha->getCard(id)->objectName()== name)
				ids << id;
		}
		if(ids.isEmpty())return;
		int id = ids.at(qrand()% ids.length());
		room->obtainCard(player,id,true);
	}
};

class LianhuaEffect : public TriggerSkill
{
public:
	LianhuaEffect(): TriggerSkill("#lianhua-effect")
	{
		events << Damaged << EventLoseSkill;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getOtherPlayers(damage.to)){
				if(damage.to->isDead())return false;
				if(p->isDead()|| !p->hasSkill("lianhua")|| p->hasFlag("CurrentPlayer"))continue;
				room->sendCompulsoryTriggerLog(p,"lianhua",true,true);
				if(damage.to->isYourFriend(p))
					room->addPlayerMark(p,"danxue_red");
				else
					room->addPlayerMark(p,"danxue_black");
				p->gainMark("&danxue");
			}
		} else {
			if(data.toString()!= "lianhua")return false;
			room->setPlayerMark(player,"danxue_red",0);
			room->setPlayerMark(player,"danxue_black",0);
			room->setPlayerMark(player,"&danxue",0);
		}
		return false;
	}
};

ZhafuCard::ZhafuCard()
{
}

void ZhafuCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	room->doSuperLightbox(effect.from,"zhafu");
	room->removePlayerMark(effect.from,"@zhafuMark");

	QStringList names = effect.to->property("zhafu_from").toStringList();
	if(names.contains(effect.from->objectName()))return;
	names << effect.from->objectName();
	room->setPlayerProperty(effect.to,"zhafu_from",names);
}

class ZhafuVS : public ZeroCardViewAsSkill
{
public:
	ZhafuVS(): ZeroCardViewAsSkill("zhafu")
	{
		frequency = Limited;
		limit_mark = "@zhafuMark";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("@zhafuMark")> 0;
	}

	const Card*viewAs()const
	{
		return new ZhafuCard;
	}
};

class Zhafu : public PhaseChangeSkill
{
public:
	Zhafu(): PhaseChangeSkill("zhafu")
	{
		frequency = Limited;
		limit_mark = "@zhafuMark";
		view_as_skill = new ZhafuVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Discard)return false;
		QStringList names = player->property("zhafu_from").toStringList();
		if(names.isEmpty())return false;
		room->setPlayerProperty(player,"zhafu_from",QStringList());

		LogMessage log;
		log.type =(player->getHandcardNum()> 1)? "#ZhafuEffect" :(!player->isKongcheng()? "#ZhafuOne" : "#ZhafuZero");
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		if(player->getHandcardNum()<= 1)return false;

		QList<ServerPlayer*> players;
		foreach(QString name,names){
			ServerPlayer*from = room->findPlayerByObjectName(name);
			if(from&& from->isAlive())
				players << from;
		}
		if(players.isEmpty())return false;
		room->sortByActionOrder(players);

		foreach(ServerPlayer*p,players){
			if(p->isDead()||player->getHandcardNum()<=1)continue;
			int id = -1;
			const Card*card = room->askForCard(player,".!","zhafu-keep:" + p->objectName(),QVariant::fromValue(p),Card::MethodNone,p);
			if(card)id = card->getEffectiveId();
			else id = player->getRandomHandCardId();
			DummyCard*dummy = new DummyCard;
			foreach(int hid,player->handCards()){
				if(hid != id)dummy->addSubcard(hid);
			}
			if(dummy->subcardsLength()> 0){
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE,player->objectName(),p->objectName(),objectName());
				room->obtainCard(p,dummy,reason,false);
			}
			delete dummy;
		}
		return false;
	}
};

class Tuiyan : public PhaseChangeSkill
{
public:
	Tuiyan(const QString&tuiyan): PhaseChangeSkill(tuiyan),tuiyan(tuiyan)
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
	if(player->getPhase()!= Player::Play)return false;
	if(!player->askForSkillInvoke(this))return false;
	room->broadcastSkillInvoke(objectName());

	int n = 2;
	if(objectName()== "tenyeartuiyan")
		n = 3;

	QList<int> ids = room->getNCards(n);
	LogMessage log;
	log.type = "$ViewDrawPile";
	log.from = player;
	log.arg = QString::number(n);
	log.card_str = ListI2S(ids).join("+");
	room->sendLog(log,player);

	log.type = "#ViewDrawPile";
	room->sendLog(log,room->getOtherPlayers(player,true));

	room->fillAG(ids,player);
	room->askForAG(player,ids,true,objectName());
	room->clearAG(player);
	room->returnToTopDrawPile(ids);
	return false;
	}
private:
	QString tuiyan;
};

BusuanCard::BusuanCard()
{
}

void BusuanCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	QStringList alllist;
	QList<int> ids;
	foreach(int id,Sanguosha->getRandomCards()){
		const Card*c = Sanguosha->getEngineCard(id);
		if(c->isKindOf("EquipCard"))continue;
		if(alllist.contains(c->objectName()))continue;
		alllist << c->objectName();
		ids << id;
	}
	if(ids.isEmpty())return;
	room->fillAG(ids,effect.from);
	int id = -1,id2 = -1;
	id = room->askForAG(effect.from,ids,false,"busuan");
	room->clearAG(effect.from);
	ids.removeOne(id);

	const Card*first_card = Sanguosha->getEngineCard(id);
	if(first_card->isKindOf("Slash")){
		foreach(int id,ids){
			if(Sanguosha->getEngineCard(id)->isKindOf("Slash"))
				ids.removeOne(id);
		}
	}

	if(!ids.isEmpty()){
		room->fillAG(ids,effect.from);
		id2 = room->askForAG(effect.from,ids,false,"busuan");
		room->clearAG(effect.from);
	}

	QStringList list;
	QString name = first_card->objectName();
	list << name;
	QString name2 = "";
	if(id2 >= 0){
		name2 = Sanguosha->getEngineCard(id2)->objectName();
		list << name2;
	}
	LogMessage log;
	log.type = id2 >= 0 ? "#Busuantwo" : "#Busuanone";
	log.from = effect.from;
	log.arg = name;
	log.arg2 = name2;
	room->sendLog(log);

	if(list.isEmpty())return;
	room->setPlayerProperty(effect.to,"busuan_names",list);
}

class BusuanVS : public ZeroCardViewAsSkill
{
public:
	BusuanVS(): ZeroCardViewAsSkill("busuan")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("BusuanCard");
	}

	const Card*viewAs()const
	{
		return new BusuanCard;
	}
};

class Busuan : public DrawCardsSkill
{
public:
	Busuan(): DrawCardsSkill("busuan")
	{
		view_as_skill = new BusuanVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	int getPriority(TriggerEvent)const
	{
		return 0;
	}

	int getDrawNum(ServerPlayer*player,int n)const
	{
		QStringList list = player->property("busuan_names").toStringList();
		if(list.isEmpty())return n;

		Room*room = player->getRoom();
		room->setPlayerProperty(player,"busuan_names",QStringList());
		LogMessage log;
		log.type = "#BusuanEffect";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);

		DummyCard*dummy = new DummyCard();
		QList<int> all = room->getDrawPile()+ room->getDiscardPile();
		foreach(QString str,list){
			QList<int> ids;
			foreach(int id,all){
				if(Sanguosha->getCard(id)->objectName()== str)
					ids << id;
			}
			if(ids.isEmpty())continue;
			int id = ids.at(qrand()% ids.length());
			dummy->addSubcard(id);
		}

		if(dummy->subcardsLength()> 0){
			room->obtainCard(player,dummy,true);
		}
		delete dummy;
		return 0;
	}
};

class Mingjie : public TriggerSkill
{
public:
	Mingjie(const QString&mingjie): TriggerSkill(mingjie),mingjie(mingjie)
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Finish)return false;
			while(player->isAlive()){
				if(player->isDead())break;
				if(player->getMark(mingjie + "-Clear")> 0)break;
				if(player->getMark(mingjie + "_num-Clear")> 2)break;
				if(!player->askForSkillInvoke(this))return false;
				room->broadcastSkillInvoke(objectName());
				player->drawCards(1,objectName(),true,true);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName==objectName()&& move.to&& move.to==player){
				if(move.card_ids.length()<= 0)return false;
				room->addPlayerMark(player,mingjie + "_num-Clear",move.card_ids.length());
				foreach(int id,move.card_ids){
					if(!Sanguosha->getCard(id)->isBlack())continue;
					room->addPlayerMark(player,mingjie + "-Clear");
					if(mingjie=="mingjie" ||(mingjie=="tenyearmingjie"&& player->getHp()> 1))
						room->loseHp(HpLostStruct(player,1,objectName(),player));
					break;
				}
			}
		}
		return false;
	}
private:
	QString mingjie;
};

TianjiangCard::TianjiangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TianjiangCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	const Card*card = Sanguosha->getCard(subcards.first());
	const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
	if(!equip)return false;
	return targets.isEmpty()&& to_select != Self&& to_select->hasEquipArea(equip->location());
}

void TianjiangCard::onEffect(CardEffectStruct&effect)const
{
	const Card*card = Sanguosha->getCard(subcards.first());
	const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
	if(!equip)return;

	if(!effect.to->hasEquipArea(equip->location()))return;

	QList<CardsMoveStruct> exchangeMove;
	CardsMoveStruct move1(subcards.first(),effect.to,Player::PlaceEquip,CardMoveReason(CardMoveReason::S_REASON_PUT,effect.from->objectName(),
						effect.to->objectName(),"tianjiang",""));
	exchangeMove.append(move1);

	if(effect.to->getEquip(equip->location())){
		CardsMoveStruct move2(effect.to->getEquip(equip->location())->getEffectiveId(),nullptr,Player::DiscardPile,
			CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,effect.to->objectName()));
		exchangeMove.append(move2);
	}
	Room*room = effect.from->getRoom();
	room->moveCardsAtomic(exchangeMove,true);

	if(!effect.from->isAlive())return;
	QString name = card->objectName();
	if(name=="_hongduanqiang" || name=="_liecuidao" || name=="_shuibojian" || name=="_hunduwanbi" || name=="_tianleiren")
		effect.from->drawCards(2,"tianjiang");
}

class TianjiangVS : public OneCardViewAsSkill
{
public:
	TianjiangVS():OneCardViewAsSkill("tianjiang")
	{
		filter_pattern = "EquipCard|.|.|equipped";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		TianjiangCard*c = new TianjiangCard();
		c->addSubcard(originalCard);
		return c;
	}
};

class Tianjiang : public GameStartSkill
{
public:
	Tianjiang(): GameStartSkill("tianjiang")
	{
		view_as_skill = new TianjiangVS;
	}

	void onGameStart(ServerPlayer*player)const
	{
		QList<const EquipCard*> equips;
		Room*room = player->getRoom();

		foreach(int id,room->getDrawPile()){
			const Card*card = Sanguosha->getCard(id);
			if(!card->isKindOf("EquipCard"))continue;
			const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
			if(!player->hasEquipArea(equip->location())|| player->getEquip(equip->location()))continue;
			equips << equip;
		}
		if(equips.isEmpty())return;

		QList<int> get_equips;
		const EquipCard*equip1 = equips.at(qrand()% equips.length());
		get_equips << equip1->getEffectiveId();

		foreach(const EquipCard*equip,equips){
			if(equip->location()== equip1->location())
				equips.removeOne(equip);
		}
		if(!equips.isEmpty()){
			const EquipCard*equip2 = equips.at(qrand()% equips.length());
			get_equips << equip2->getEffectiveId();
		}

		if(get_equips.isEmpty())return;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		CardMoveReason reason(CardMoveReason::S_REASON_PUT,player->objectName(),objectName(),"");
		CardsMoveStruct move(get_equips,nullptr,player,Player::DrawPile,Player::PlaceEquip,reason);
		room->moveCardsAtomic(move,true);
	}
};

ZhurenCard::ZhurenCard()
{
	target_fixed = true;
	handling_method = Card::MethodDiscard;
}

void ZhurenCard::ZhurenGetSlash(ServerPlayer*source)const
{
	QList<int> slashs;
	Room*room = source->getRoom();
	foreach(int id,room->getDrawPile()){
		if(!Sanguosha->getCard(id)->isKindOf("Slash"))continue;
		slashs << id;
	}
	if(slashs.isEmpty())return;
	int id = slashs.at(qrand()% slashs.length());
	room->obtainCard(source,id);
}

void ZhurenCard::use(Room*,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	const Card*card = Sanguosha->getCard(subcards.first());
	int number = card->getNumber();
	if(card->isKindOf("Lightning"))
		number = 13;
	int max_number = 85;
	if(number >= 5&& number <= 8)
		max_number = 90;
	else if(number >= 9&& number <= 12)
		max_number = 95;
	else if(number > 12)
		max_number = 100;

	int probability = qrand()% 100 + 1;
	if(probability > max_number)
		ZhurenGetSlash(source);
	else {
		QString name = "_hongduanqiang";
		if(card->isKindOf("Lightning"))
			name = "_tianleiren";
		else {
			if(card->getSuit()== Card::Club)
				name = "_shuibojian";
			else if(card->getSuit()== Card::Diamond)
				name = "_liecuidao";
			else if(card->getSuit()== Card::Spade)
				name = "_hunduwanbi";
		}

		int id = source->getDerivativeCard(name,Player::PlaceHand);
		if(id < 0)
			ZhurenGetSlash(source);
	}
}

class Zhuren : public OneCardViewAsSkill
{
public:
	Zhuren():OneCardViewAsSkill("zhuren")
	{
		filter_pattern = ".|.|.|hand!";
		waked_skills = "_hongduanqiang,_tianleiren,_shuibojian,_liecuidao,_hunduwanbi";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		ZhurenCard*c = new ZhurenCard();
		c->addSubcard(originalCard);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("ZhurenCard");
	}
};

class Tianyun : public PhaseChangeSkill
{
public:
	Tianyun(): PhaseChangeSkill("tianyun")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::RoundStart;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		int seat = player->getPlayerSeat(),turn = room->getTag("TurnLengthCount").toInt();
		if(seat != turn)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;

			QList<int> suits;
			foreach(const Card*c,p->getCards("h")){
				int suit =(int)c->getSuit();
				if(suits.contains(suit))continue;
				suits << suit;
			}
			int num = qMax(suits.length(),1);

			if(!p->askForSkillInvoke(this,"tianyun:" + QString::number(num)))continue;
			p->peiyin(this);

			QList<int> puts = room->askForGuanxing(p,room->getNCards(num));
			if(!puts.isEmpty()|| p->isDead())continue;

			ServerPlayer*t = room->askForPlayerChosen(p,room->getAlivePlayers(),objectName(),"@tianyun-draw:" + QString::number(num),true);
			if(!t)continue;
			room->doAnimate(1,p->objectName(),t->objectName());
			t->drawCards(num,objectName());
			room->loseHp(HpLostStruct(p,1,objectName(),p));
		}
		return false;
	}
};

class TianyunInitial : public TriggerSkill
{
public:
	TianyunInitial(): TriggerSkill("#tianyun")
	{
		events  << AfterDrawNCards;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="InitialHandCards")return false;
		QStringList suits;
		foreach(const Card*c,player->getCards("h"))
			suits << c->getSuitString();
		QList<const Card*> cards;
		foreach(int id,room->getDrawPile()){
			const Card*c = Sanguosha->getCard(id);
			if(suits.contains(c->getSuitString()))continue;
			cards << c;
		}
		DummyCard*dummy = new DummyCard();
		dummy->deleteLater();
		while(cards.length()>0){
			const Card*card = cards.at(qrand()% cards.length());
			dummy->addSubcard(card);
			foreach(const Card*c,cards){
				if(c->getSuit()== card->getSuit())
					cards.removeOne(c);
			}
		}
		if(dummy->subcardsLength()> 0){
			room->sendCompulsoryTriggerLog(player,"tianyun",true,true);
			room->obtainCard(player,dummy,false);
		}
		return false;
	}
};

class Yuyan : public TriggerSkill
{
public:
	Yuyan(): TriggerSkill("yuyan")
	{
		events << EnterDying << RoundStart << Damage << EventPhaseChanging;
		waked_skills = "tenyearfenyin";
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EnterDying){
			if(room->getTag("YuyanFirstDying").toBool())return false;
			room->setTag("YuyanFirstDying",true);
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->getMark("&yuyan+#"+p->objectName()+"_lun")<1)continue;
				if(p->isAlive()){
					room->sendCompulsoryTriggerLog(p,objectName());
					if(!p->hasSkill("tenyearfenyin",true)){
						p->addMark("YuyanFenyin-SelfClear");
						room->handleAcquireDetachSkills(p,"tenyearfenyin");
					}
				}
			}
		}else if(event==Damage){
			if(room->getTag("YuyanFirstDamage").toBool())return false;
			room->setTag("YuyanFirstDamage",true);
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->getMark("&yuyan+#"+p->objectName()+"_lun")<1)continue;
				if(p->isAlive()){
					room->sendCompulsoryTriggerLog(p,objectName());
					p->drawCards(2,"yuyan");
				}
			}
		} else if(event==RoundStart){
			room->removeTag("YuyanFirstDying");
			room->removeTag("YuyanFirstDamage");
			if(player->isAlive()&&player->hasSkill(objectName())){
				ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"@yuyan-target",false);
				player->peiyin(this);

				LogMessage log;
				log.type = "#ChoosePlayerWithSkill";
				log.from = player;
				log.to << t;
				log.arg = objectName();
				room->sendLog(log,player);

				log.type = "#InvokeSkill";
				room->sendLog(log,room->getOtherPlayers(player,true));

				room->doAnimate(1,player->objectName(),t->objectName(),QList<ServerPlayer*>()<< player);
				room->notifySkillInvoked(player,objectName());
				room->setPlayerMark(t,"&yuyan+#"+player->objectName()+"_lun",1,QList<ServerPlayer*>()<< player);
			}
		} else {
			if(data.value<PhaseChangeStruct>().to==Player::NotActive&&player->getMark("YuyanFenyin-SelfClear")>0)
				room->handleAcquireDetachSkills(player,"-tenyearfenyin");
		}
		return false;
	}
};

class FanyinVS : public ZeroCardViewAsSkill
{
public:
	FanyinVS(): ZeroCardViewAsSkill("fanyin")
	{
		response_pattern = "@@fanyin";
	}

	const Card*viewAs()const
	{
		int id = Self->getMark("fanyin_id");
		const Card*c = Sanguosha->getCard(id);
		c->setFlags("fanyin_use_card");
		return c;
	}
};

class Fanyin : public PhaseChangeSkill
{
public:
	Fanyin(): PhaseChangeSkill("fanyin")
	{
		view_as_skill = new FanyinVS;
		waked_skills = "#fanyin";
	}

	int getCardId(Room*room,int number)const
	{
		int id = -1;
		QList<int> ids,pile = room->getDrawPile();
		if(pile.isEmpty())return id;

		if(number >= 0){
			foreach(int id,pile){
				if(Sanguosha->getCard(id)->getNumber()== number)
					ids << id;
			}
			if(!ids.isEmpty())
				id = ids.at(qrand()% ids.length());
		} else {
			int min = Sanguosha->getCard(pile.first())->getNumber();
			foreach(int id,pile){
				int num = Sanguosha->getCard(id)->getNumber();
				if(num < min)
					min = num;
			}
			foreach(int id,pile){
				if(Sanguosha->getCard(id)->getNumber()== min)
					ids << id;
			}
			if(!ids.isEmpty())
				id = ids.at(qrand()% ids.length());
		}
		return id;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		if(room->getDrawPile().isEmpty())return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);

		int number = -1;
		while(player->isAlive()){
			int id = getCardId(room,number);
			if(id < 0)break;
			const Card*card = Sanguosha->getCard(id);
			number = 2* card->getNumber();
			CardsMoveStruct move(id,nullptr,Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_TURNOVER,player->objectName(),objectName(),""));
			room->moveCardsAtomic(move,true);

			room->setPlayerMark(player,"fanyin_id",id);
			try {
				if(card->targetFixed()){
					if(!player->canUse(card,player,true)
						|| !player->askForSkillInvoke("fanyin_targetfixed","fanyin_targetfixed:" + card->objectName(),false)){
						room->addPlayerMark(player,"&fanyin_buff-Clear");
						DummyCard*dummy = new DummyCard();
						dummy->addSubcard(id);
						CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),objectName(),"");
						room->throwCard(dummy,reason,nullptr);
						delete dummy;
					} else
						room->useCard(CardUseStruct(card,player));
				} else {
					if(!room->askForUseCard(player,"@@fanyin","@fanyin:" + card->objectName(),-1,Card::MethodUse,!card->isKindOf("Slash"))){
						room->addPlayerMark(player,"&fanyin_buff-Clear");
						DummyCard*dummy = new DummyCard();
						dummy->addSubcard(id);
						CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),objectName(),"");
						room->throwCard(dummy,reason,nullptr);
						delete dummy;
					}
				}
			}catch(TriggerEvent triggerEvent){
				if(triggerEvent==TurnBroken || triggerEvent==StageChange){
					DummyCard*dummy = new DummyCard();
					dummy->addSubcard(id);
					CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),objectName(),"");
					room->throwCard(dummy,reason,nullptr);
					delete dummy;
				}
				throw triggerEvent;
			}

			room->getThread()->delay();
		}
		return false;
	}
};

class FanyinEffect : public TriggerSkill
{
public:
	FanyinEffect(): TriggerSkill("#fanyin")
	{
		events << PreCardUsed;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->getMark("&fanyin_buff-Clear")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("BasicCard")||(use.card->isNDTrick()&& !use.card->isKindOf("Nullification"))){
			int mark = player->getMark("&fanyin_buff-Clear");
			room->setPlayerMark(player,"&fanyin_buff-Clear",0);
			if(mark <= 0)return false;
			QList<ServerPlayer*> targets = room->getCardTargets(player,use.card,use.to);
			if(targets.isEmpty())return false;
			player->setTag("fanyinData", data);
			LogMessage log;
			log.to = room->askForPlayersChosen(player,targets,"fanyin",0,
				mark,QString("@fanyin-extra:%1::%2").arg(use.card->objectName()).arg(mark),true);
			player->removeTag("fanyinData");
			if(log.to.isEmpty())return false;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.card_str = use.card->toString();
			log.arg = "fanyin";
			room->sendLog(log);
			use.to.append(log.to);
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Peiqi : public MasochismSkill
{
public:
	Peiqi(): MasochismSkill("peiqi")
	{
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		Room*room = player->getRoom();
		if(!room->canMoveField()|| !player->askForSkillInvoke(this))return;
		player->peiyin(this);
		room->moveField(player,objectName());

		foreach(ServerPlayer*p,room->getAllPlayers()){
			foreach(ServerPlayer*pp,room->getOtherPlayers(p)){
				if(!p->inMyAttackRange(pp)|| !pp->inMyAttackRange(p))
					return;
			}
		}
		room->moveField(player,objectName(),true);
	}
};

class Wumei : public TriggerSkill
{
public:
	Wumei(): TriggerSkill("wumei")
	{
		events << EventPhaseChanging << EventPhaseStart << TurnStarted;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				if(!room->getTag("WumeiExtraTurn_" + player->objectName()).toBool())return false;
				room->removeTag("WumeiExtraTurn_" + player->objectName());

				foreach(ServerPlayer*p,room->getAllPlayers()){
					int hp = p->getMark("wumeiHP-Clear");
					hp = qMin(p->getMaxHp(),hp);
					room->setPlayerProperty(p,"hp",hp);
				}
			}
		} else if(event==EventPhaseStart){
			if(!room->getTag("WumeiExtraTurn_" + player->objectName()).toBool())return false;
			if(player->getPhase()!= Player::RoundStart)return false;
			foreach(ServerPlayer*p,room->getAllPlayers())
				room->setPlayerMark(p,"wumeiHP-Clear",p->getHp());
		} else if(event==TurnStarted){
			//尝试过载change.to==Player::RoundStart时询问技能，结果兵和乐出了bug：如果对自己用，两个回合对应的阶段都跳过了
			if(player->isDead()|| !player->hasSkill(objectName())|| player->getMark("wumeiUsed_lun")> 0)return false;
			ServerPlayer*t = room->askForPlayerChosen(player,room->getAllPlayers(),objectName(),"@wumei-target",true,true);
			if(!t)return false;
			player->peiyin(this);
			room->addPlayerMark(player,"wumeiUsed_lun");
			room->setTag("WumeiExtraTurn_" + t->objectName(),true);
			t->gainAnExtraTurn();
		}
		return false;
	}
};

class Zhanmeng : public TriggerSkill
{
public:
	Zhanmeng(): TriggerSkill("zhanmeng")
	{
		events << CardUsed << CardResponded;
		waked_skills = "#zhanmeng";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;

		const Card*c = nullptr;
		if(event==CardUsed)
			c = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(!res.m_isUse)return false;
			c = res.m_card;
		}
		if(!c || c->isKindOf("SkillCard"))return false;

		int last_turn = room->getTag("Global_AllTurnsNum").toInt()- 1;
		QString turn = QString::number(last_turn),name = c->objectName();
		QStringList used_names = room->getTag("ZhanmengRecord_" + turn).toStringList(),choices;
		if(c->isKindOf("Slash"))
			name = "slash";

		if(!used_names.contains(name)&& last_turn > 0&& player->getMark("zhanmeng_last-Clear")<= 0)
			choices << "last";

		if(player->getMark("zhanmeng_next-Clear")<= 0)
			choices << "next";

		if(player->getMark("zhanmeng_discard-Clear")<= 0){
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isNude())continue;
				choices << "discard";
				break;
			}
		}

		if(choices.isEmpty())return false;
		choices << "cancel";

		QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
		if(choice=="cancel")return false;
		room->addPlayerMark(player,"zhanmeng_" + choice + "-Clear");

		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player,objectName());

		if(choice=="last"){
			QList<int> card_ids;
			foreach(int id,room->getDrawPile()){
				const Card*c = Sanguosha->getCard(id);
				if(!(c->isDamageCard()&& !c->isKindOf("DelayedTrick")))
					card_ids << id;
			}
			if(card_ids.isEmpty())return false;
			int id = card_ids.at(qrand()% card_ids.length());
			room->obtainCard(player,id,true);
		} else if(choice=="next"){
			QString now_turn = QString::number(last_turn + 1);
			room->addPlayerMark(player,"&zhanmeng_record+" + name + "+#" + now_turn);
		} else {
			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->canDiscard(p,"he"))targets << p;
			}
			if(targets.isEmpty())return false;

			ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@zhanmeng-target");
			room->doAnimate(1,player->objectName(),t->objectName());
			const Card*discard = room->askForDiscard(t,objectName(),2,2,false,true,"@zhanmeng-discard");
			if(!discard)return false;

			int number = 0;
			foreach(int id,discard->getSubcards())
				number += Sanguosha->getCard(id)->getNumber();
			if(number <= 10)return false;
			room->damage(DamageStruct(objectName(),player,t,1,DamageStruct::Fire));
		}
		return false;
	}
};

class ZhanmengEffect : public TriggerSkill
{
public:
	ZhanmengEffect(): TriggerSkill("#zhanmeng")
	{
		events << CardFinished << EventPhaseChanging << PreCardUsed << PreCardResponded;
		global = true;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			int last_turn = room->getTag("Global_AllTurnsNum").toInt()- 1;
			QString turn = QString::number(last_turn),name = use.card->objectName();
			if(use.card->isKindOf("Slash"))name = "slash";
			foreach(ServerPlayer*p,room->getAllPlayers()){
				int mark = p->getMark("&zhanmeng_record+" + name + "+#" + turn);
				if(mark <= 0)continue;
				room->setPlayerMark(p,"&zhanmeng_record+" + name + "+#" + turn,0);
				for(int i = 0; i < mark; i++){
					room->sendCompulsoryTriggerLog(p,"zhanmeng");

					QList<int> card_ids;
					foreach(int id,room->getDrawPile()){
						const Card*c = Sanguosha->getCard(id);
						if(c->isDamageCard()&& !c->isKindOf("DelayedTrick"))
							card_ids << id;
					}
					if(card_ids.isEmpty())break;
					int id = card_ids.at(qrand()% card_ids.length());
					room->obtainCard(p,id,true);
					if(p->isDead())break;
				}
			}
		}else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			int last_turn = room->getTag("Global_AllTurnsNum").toInt()- 1;
			QString turn = QString::number(last_turn);
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				foreach(QString mark,p->getMarkNames()){
					if(mark.contains("&zhanmeng_record+")&& mark.endsWith(turn))
						room->setPlayerMark(p,mark,0);
				}
			}
		}else{
			const Card*c = nullptr;
			if(event==PreCardUsed)
				c = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if(!res.m_isUse)return false;
				c = res.m_card;
			}
			if(!c || c->isKindOf("SkillCard"))return false;

			QString turn = room->getTag("Global_AllTurnsNum").toString(),name = c->objectName();
			QStringList used_names = room->getTag("ZhanmengRecord_" + turn).toStringList();
			if(c->isKindOf("Slash"))name = "slash";
			used_names << name;
			room->setTag("ZhanmengRecord_" + turn,used_names);
		}
		return false;
	}
};

XiangmianCard::XiangmianCard()
{
}

bool XiangmianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select != Self&& to_select->getMark("xiangmianTarget-Keep")<= 0;
}

void XiangmianCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->addPlayerMark(to,"xiangmianTarget-Keep");

	JudgeStruct judge;
	judge.who = to;
	judge.reason = "xiangmian";
	judge.play_animation = false;
	judge.pattern = ".";
	room->judge(judge);

	if(to->isDead())return;

	QStringList patterns = judge.pattern.split("|");
	QString mark = QString("&xiangmianDebuff+%1+%2").arg(patterns[1] + "_char").arg(patterns[2]);
	room->addPlayerMark(to,mark);
}

class XiangmianVS : public ZeroCardViewAsSkill
{
public:
	XiangmianVS(): ZeroCardViewAsSkill("xiangmian")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("XiangmianCard");
	}

	const Card*viewAs()const
	{
		return new XiangmianCard;
	}
};

class Xiangmian : public TriggerSkill
{
public:
	Xiangmian(): TriggerSkill("xiangmian")
	{
		events << FinishJudge << CardFinished;
		view_as_skill = new XiangmianVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==FinishJudge){
			JudgeStruct*judge = data.value<JudgeStruct*>();
			if(judge->reason != objectName())return false;
			judge->pattern = QString(".|%1|%2").arg(judge->card->getSuitString()).arg(judge->card->getNumber());
		} else {
			if(player->isDead())return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card || use.card->isKindOf("SkillCard"))return false;
			room->addPlayerMark(player,"xiangmian_times");

			QString suit_str = use.card->getSuitString()+ "_char",number_str = QString::number(player->getMark("xiangmian_times"));
			foreach(QString mark,player->getMarkNames()){
				if(player->isDead())break;
				if(!mark.startsWith("&xiangmianDebuff+")|| player->getMark(mark)<= 0)continue;
				QStringList marks = mark.split("+");
				if(marks.length()!= 3)continue;
				if(marks[1] != suit_str&& marks.last()!= number_str)continue;

				int num = player->getMark(mark);
				room->setPlayerMark(player,mark,0);

				for(int i = 0; i < num; i++){
					if(player->isDead()|| player->getHp()<= 0)break;
					LogMessage log;
					log.type = "#ZhenguEffect";
					log.from = player;
					log.arg = objectName();
					room->sendLog(log);
					room->loseHp(HpLostStruct(player,player->getHp(),objectName(),player));
				}
			}
		}
		return false;
	}
};

class Tianji : public TriggerSkill
{
public:
	Tianji(): TriggerSkill("tianji")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::DiscardPile)return false;
		if(move.reason.m_reason != CardMoveReason::S_REASON_JUDGEDONE)return false;
		CardUseStruct use = move.reason.m_useStruct;
		if(!use.card)return false;

		QList<int> type_ids,suit_ids,number_ids;

		foreach(int id,room->getDrawPile()){
			const Card*c = Sanguosha->getCard(id);
			if(c->getTypeId()== use.card->getTypeId())
				type_ids << id;
			if(c->getSuit()== use.card->getSuit())
				suit_ids << id;
			if(c->getNumber()== use.card->getNumber())
				number_ids << id;
		}

		room->sendCompulsoryTriggerLog(player,this);

		DummyCard*dummy = new DummyCard;
		dummy->deleteLater();

		if(!type_ids.isEmpty()){
			int id = type_ids.at(qrand()% type_ids.length());
			dummy->addSubcard(id);
			suit_ids.removeOne(id);
			number_ids.removeOne(id);
		}
		if(!suit_ids.isEmpty()){
			int id = suit_ids.at(qrand()% suit_ids.length());
			dummy->addSubcard(id);
			number_ids.removeOne(id);
		}
		if(!number_ids.isEmpty()){
			int id = number_ids.at(qrand()% number_ids.length());
			dummy->addSubcard(id);
		}
		if(dummy->subcardsLength()> 0)
			room->obtainCard(player,dummy);
		return false;
	}
};

class Cansi : public PhaseChangeSkill
{
public:
	Cansi(): PhaseChangeSkill("cansi")
	{
		waked_skills = "#cansi";
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@cansi-target",false,true);
		player->peiyin(this);
		QList<ServerPlayer*> players;
		players << player << t;
		room->sortByActionOrder(players);
		foreach(ServerPlayer*p,players){
			if(p->isDead())continue;
			room->recover(p,RecoverStruct(objectName(),player));
		}

		if(player->isDead()|| t->isDead())return false;

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_cansi");
		slash->deleteLater();
		room->setCardFlag(slash,"YUANBEN");
		room->setCardFlag(slash,"cansiTarget_" + t->objectName());
		room->setCardFlag(slash,"cansiUser_" + player->objectName());
		if(player->canSlash(t,slash,false))
			room->useCard(CardUseStruct(slash,player,t));

		if(player->isDead()|| t->isDead())return false;

		Duel*duel = new Duel(Card::NoSuit,0);
		duel->setSkillName("_cansi");
		duel->deleteLater();
		room->setCardFlag(duel,"YUANBEN");
		room->setCardFlag(duel,"cansiTarget_" + t->objectName());
		room->setCardFlag(duel,"cansiUser_" + player->objectName());
		if(player->canUse(duel,t,true))
			room->useCard(CardUseStruct(duel,player,t));

		if(player->isDead()|| t->isDead())return false;

		FireAttack*fa = new FireAttack(Card::NoSuit,0);
		fa->setSkillName("_cansi");
		fa->deleteLater();
		room->setCardFlag(fa,"YUANBEN");
		room->setCardFlag(fa,"cansiTarget_" + t->objectName());
		room->setCardFlag(fa,"cansiUser_" + player->objectName());
		if(player->canUse(fa,t,true))
			room->useCard(CardUseStruct(fa,player,t));
		return false;
	}
};

class CansiDraw : public MasochismSkill
{
public:
	CansiDraw(): MasochismSkill("#cansi")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&damage)const
	{
		if(!damage.card || !damage.card->hasFlag("cansiTarget_" + player->objectName()))return;
		if(!damage.card->getSkillNames().contains("cansi"))return;
		QString name;
		foreach(QString flag,damage.card->getFlags()){
			if(!flag.startsWith("cansiUser_"))continue;
			QStringList flags = flag.split("_");
			if(flags.length()!= 2)continue;
			name = flags.last();
			break;
		}
		if(name.isEmpty())return;
		Room*room = player->getRoom();
		ServerPlayer*p = room->findChild<ServerPlayer*>(name);
		if(!p || p->isDead()|| !p->hasSkill("cansi"))return;
		room->sendCompulsoryTriggerLog(p,"cansi",true,true);
		p->drawCards(2* damage.damage,"cansi");
	}
};

class Fozong : public PhaseChangeSkill
{
public:
	Fozong(): PhaseChangeSkill("fozong")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		int put = player->getHandcardNum()- 7;
		if(put <= 0)return false;

		room->sendCompulsoryTriggerLog(player,this);

		const Card*c = room->askForExchange(player,objectName(),put,put,false,"@fozong-put:" + QString::number(put));
		player->addToPile(objectName(),c);

		QList<int> pile = player->getPile(objectName());
		if(pile.length()< 7)return false;

		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(pile.isEmpty())break;
			if(p->isDead())continue;
			room->fillAG(pile,p);
			p->setTag("FozongTarget", QVariant::fromValue(player));
			int id = room->askForAG(p,pile,true,objectName(),"@fozong-ag:" + player->objectName());
			p->removeTag("FozongTarget");
			room->clearAG(p);
			if(id < 0)
				room->loseHp(HpLostStruct(player,1,objectName(),p));
			else {
				pile.removeOne(id);
				room->obtainCard(p,id);
				room->recover(player,RecoverStruct(objectName(),p));
			}
		}
		return false;
	}
};

TenyearLianjiCard::TenyearLianjiCard()
{
}

void TenyearLianjiCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	QList<const Card*> weapons;
	foreach(int id,room->getDrawPile()){
		const Card*card = Sanguosha->getCard(id);
		if(card->isKindOf("Weapon")&&card->isAvailable(effect.to))
			weapons << card;
	}
	if(weapons.isEmpty())return;

	const Card*weapon = weapons.at(qrand()% weapons.length());
	room->useCard(CardUseStruct(weapon,effect.to));

	if(effect.to->isDead())return;

	effect.to->setTag("tenyearlianji_weapon", QVariant::fromValue(weapon)); //FOR AI
	CardUseStruct use = room->askForUseSlashToStruct(effect.to,room->getOtherPlayers(effect.from),
												"@tenyearlianji-slash:" + effect.from->objectName());
	if(use.card){
		room->addPlayerMark(effect.from,"tenyearlianji_choice_1");
		if(effect.to->isDead()|| !effect.to->hasCard(weapon))return;
		foreach(ServerPlayer*p,use.to){
			if(p->isDead())use.to.removeOne(p);
		}
		ServerPlayer*to = room->askForPlayerChosen(effect.to,use.to,"tenyearlianji","@tenyearlianji-give:" + weapon->objectName());
		if(to)room->giveCard(effect.to,to,weapon,"tenyearlianji",true);
	} else {
		room->addPlayerMark(effect.from,"tenyearlianji_choice_2");
		if(effect.from->isDead()|| effect.to->isDead())return;
		Card*slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("_tenyearlianji");
		if(effect.from->canSlash(effect.to,slash,false))
			room->useCard(CardUseStruct(slash,effect.from,effect.to));
		if(effect.to->hasCard(weapon)&& effect.from->isAlive())
			room->giveCard(effect.to,effect.from,weapon,"tenyearlianji",true);
		slash->deleteLater();
	}
}

class TenyearLianji : public OneCardViewAsSkill
{
public:
	TenyearLianji(): OneCardViewAsSkill("tenyearlianji")
	{
		filter_pattern = ".|.|.|hand!";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("TenyearLianjiCard");
	}

	const Card*viewAs(const Card*originalcard)const
	{
		TenyearLianjiCard*card = new TenyearLianjiCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class TenyearMoucheng : public PhaseChangeSkill
{
public:
	TenyearMoucheng(): PhaseChangeSkill("tenyearmoucheng")
	{
		frequency = Wake;
		waked_skills = "tenyearjingong";
	}

	bool triggerable(const ServerPlayer*player)const
	{
		return player&& player->isAlive()&&player->getPhase()== Player::Start
		&& player->getMark(objectName())<1&& player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getMark("tenyearlianji_choice_1")>0&& player->getMark("tenyearlianji_choice_2")> 0){}
		else if(!player->canWake(objectName()))return false;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		room->doSuperLightbox(player,"tenyearmoucheng");
		room->setPlayerMark(player,"tenyearmoucheng",1);

		if(room->changeMaxHpForAwakenSkill(player,0,objectName()))
			room->handleAcquireDetachSkills(player,"-tenyearlianji|tenyearjingong");
		return false;
	}
};

class Weicheng : public TriggerSkill
{
public:
	Weicheng(): TriggerSkill("weicheng")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(!move.from || move.from != player || !move.to || move.to==player || player->isDead())return false;
		if(!move.from_places.contains(Player::PlaceHand)|| move.to_place != Player::PlaceHand)return false;
		if(player->getHandcardNum()>= player->getHp())return false;
		if(!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1,objectName());
		return false;
	}
};

DaoshuCard::DaoshuCard()
{
}

bool DaoshuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*)const
{
	return targets.isEmpty()&& !to_select->isKongcheng();
}

void DaoshuCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	Card::Suit suit = room->askForSuit(effect.from,"daoshu");

	LogMessage log;
	log.type = "#ChooseSuit";
	log.from = effect.from;
	log.arg = Card::Suit2String(suit);
	room->sendLog(log);

	if(effect.to->isKongcheng())return;
	int id = room->askForCardChosen(effect.from,effect.to,"h","daoshu");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName());
	const Card*card = Sanguosha->getCard(id);
	room->obtainCard(effect.from,card,reason,true);

	if(effect.from->isDead()|| effect.to->isDead())return;
	if(card->getSuit()== suit){
		room->damage(DamageStruct("daoshu",effect.from,effect.to));
		int times = effect.from->usedTimes("DaoshuCard");
		if(times > 0)
			room->addPlayerHistory(effect.from,"DaoshuCard",-times);
	} else {
		QList<const Card*> cards;
		foreach(const Card*c,effect.from->getCards("h")){
			if(c->getSuit()!= card->getSuit())
				cards << c;
		}
		if(cards.isEmpty())
			room->showAllCards(effect.from);
		else {
			QStringList data;
			data << effect.to->objectName()<< card->getSuitString();
			const Card*give = room->askForCard(effect.from,".|^" + card->getSuitString()+ "|.|hand!",
				"daoshu-give:" + effect.to->objectName(),data,Card::MethodNone);

			if(!give)give = cards.at(qrand()% cards.length());
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE,effect.from->objectName(),effect.to->objectName(),"daoshu","");
			room->obtainCard(effect.to,give,reason,true);
		}
	}
}

class Daoshu : public ZeroCardViewAsSkill
{
public:
	Daoshu(): ZeroCardViewAsSkill("daoshu")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("DaoshuCard");
	}

	const Card*viewAs()const
	{
		DaoshuCard*card = new DaoshuCard;
		return card;
	}
};

class Zhongjie : public TriggerSkill
{
public:
	Zhongjie(): TriggerSkill("zhongjie")
	{
		events << Dying;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if(!dying.who || !dying.hplost)return false;
		if(player->getMark("zhongjie_used_lun")> 0)return false;
		if(!player->askForSkillInvoke(this,dying.who))return false;
		player->peiyin(this);
		room->addPlayerMark(player,"zhongjie_used_lun");
		room->recover(dying.who,RecoverStruct("zhongjie",player));
		dying.who->drawCards(1,objectName());
		return false;
	}
};

SushouCard::SushouCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void SushouCard::onUse(Room*,CardUseStruct&)const
{
}

class SushouVS : public ViewAsSkill
{
public:
	SushouVS(): ViewAsSkill("sushou")
	{
		response_pattern = "@@sushou";
		expand_pile = "#sushou";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(selected.length()< 2* Self->getPile("#sushou").length()&& selected.length()< 2* Self->getLostHp())
			return !to_select->isEquipped();
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int hand = 0,pile = 0;
		foreach(const Card*card,cards){
			if(Self->getHandcards().contains(card))
				hand++;
			else if(Self->getPile("#sushou").contains(card->getEffectiveId()))
				pile++;
		}

		if(hand==pile&& hand <= Self->getLostHp()){
			SushouCard*c = new SushouCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}
};

class Sushou : public PhaseChangeSkill
{
public:
	Sushou(): PhaseChangeSkill("sushou")
	{
		view_as_skill = new SushouVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Play;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getAllPlayers()){
			int hand = player->getHandcardNum();
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->getHandcardNum()>= hand)
					return false;
			}

			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(!p->askForSkillInvoke(this,player))continue;
			p->peiyin(this);

			room->loseHp(HpLostStruct(p,1,objectName(),p));
			p->drawCards(p->getLostHp(),objectName());

			if(p->isDead()|| p->hasFlag("CurrentPlayer"))continue;

			ServerPlayer*current = room->getCurrent();
			if(!current || current->isDead())continue;
			hand = current->getHandcardNum()/ 2;
			if(hand <= 0)continue;

			QList<int> cards;
			for(int i = 0; i < hand; ++i){
				int id = room->askForCardChosen(p,current,"h",objectName(),false,Card::MethodNone,cards);
				if(id<0)break;
				cards << id;
			}

			hand = cards.length();

			LogMessage log;
			log.type = "$SushouWatch";
			log.from = p;
			log.to << current;
			log.arg = QString::number(hand);
			log.card_str = ListI2S(cards).join("+");
			room->sendLog(log,p);

			log.type = "#SushouWatch";
			room->sendLog(log,room->getOtherPlayers(p,true));

			hand = qMin(hand,p->getLostHp());
			room->notifyMoveToPile(p,cards,objectName(),Player::PlaceHand,true);
			const Card*c = room->askForUseCard(p,"@@sushou","@sushou:" + QString::number(hand));
			room->notifyMoveToPile(p,cards,objectName(),Player::PlaceHand,false);
			if(!c || c->subcardsLength()<= 0)continue;

			QList<int> other,self;
			foreach(int id,c->getSubcards()){
				if(cards.contains(id))
					other << id;
				else
					self << id;
			}
			QList<CardsMoveStruct> exchangeMove;
			CardsMoveStruct move1(self,p,current,Player::PlaceHand,Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SWAP,p->objectName(),current->objectName(),objectName(),""));
			CardsMoveStruct move2(other,current,p,Player::PlaceHand,Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SWAP,current->objectName(),p->objectName(),objectName(),""));
			exchangeMove.push_back(move1);
			exchangeMove.push_back(move2);
			room->moveCardsAtomic(exchangeMove,false);

			LogMessage log2;
			log2.type = "#SushouExchange";
			log2.from = p;
			log2.to << current;
			log2.arg = QString::number(self.length());
			room->sendLog(log2);
		}
		return false;
	}
};

class TenyearPoyuan : public TriggerSkill
{
public:
	TenyearPoyuan(): TriggerSkill("tenyearpoyuan")
	{
		events << GameStart << EventPhaseStart;
		waked_skills = "_tenyearpiliche";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart&& player->getPhase()!= Player::RoundStart)return false;

		if(!player->getTreasure()|| player->getTreasure()->objectName()!= "_tenyearpiliche"){
			if(!player->canDiscard(player,"he")|| !room->askForCard(player,"..","@tenyearpoyuan-discard",data,objectName()))return false;
			player->peiyin(this);

			int id = player->getDerivativeCard("_tenyearpiliche",Player::PlaceHand);
			if(id < 0 || player->isDead())return false;

			QList<CardsMoveStruct> exchangeMove;
			CardsMoveStruct move1(id,player,Player::PlaceEquip,
				CardMoveReason(CardMoveReason::S_REASON_PUT,player->objectName()));
			exchangeMove.push_back(move1);
			if(player->getTreasure()){
				CardsMoveStruct move2(player->getTreasure()->getEffectiveId(),nullptr,Player::DiscardPile,
					CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,player->objectName()));
				exchangeMove.push_back(move2);
			}
			room->moveCardsAtomic(exchangeMove,true);
		} else {
			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->canDiscard(p,"e"))
					targets << p;
			}

			ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@tenyearpoyuan-invoke",true,true);
			if(!t)return false;
			player->peiyin(this);

			QList<int> cards;
			for(int i = 0; i < 2; ++i){
				if(t->getEquips().length()<=i)break;
				int id = room->askForCardChosen(player,t,"e",objectName(),false,Card::MethodDiscard,cards);
				if(id<0)break;
				cards << id;
			}
			DummyCard dummy(cards);
			room->throwCard(&dummy,objectName(),t,player);
		}
		return false;
	}
};

class TenyearHuaceVS : public OneCardViewAsSkill
{
public:
	TenyearHuaceVS(): OneCardViewAsSkill("tenyearhuace")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card*to_select)const
	{
		if(to_select->isEquipped())return false;
		const Card*c = Self->getTag("tenyearhuace").value<const Card*>();
		if(c){
			Card*dc = Sanguosha->cloneCard(c);
			if(dc){
				dc->addSubcard(to_select);
				dc->setCanRecast(false);
				dc->deleteLater();
				return dc->isAvailable(Self);
			}
		}
		return false;
	}

	const Card*viewAs(const Card*card)const
	{
		const Card*c = Self->getTag("tenyearhuace").value<const Card*>();
		if(c){
			Card*dc = Sanguosha->cloneCard(c->objectName());
			dc->setSkillName(objectName());
			dc->setFlags(objectName());
			dc->addSubcard(card);
			return dc;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("tenyearhuaceUse-PlayClear")<1;
	}
};

class TenyearHuace : public TriggerSkill
{
public:
	TenyearHuace(): TriggerSkill("tenyearhuace")
	{
		events << RoundStart << PreCardUsed;
		view_as_skill = new TenyearHuaceVS;
		global = true;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance("tenyearhuace",false);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==RoundStart){
			foreach(QString mark,player->getMarkNames()){
				if(mark.contains("tenyearhuace_guhuo_remove_"))
					room->setPlayerMark(player,mark,0);
			}
			QStringList strlist = player->getTag("TenyearHuaceRecord").toStringList();
			if(strlist.isEmpty())return false;
			player->removeTag("TenyearHuaceRecord");
			foreach(QString name,strlist)
				room->addPlayerMark(player,"tenyearhuace_guhuo_remove_" + name);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isNDTrick())return false;
			QString name = use.card->objectName();
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				QStringList strlist = p->getTag("TenyearHuaceRecord").toStringList();
				if(strlist.contains(name))continue;
				strlist << name;
				p->setTag("TenyearHuaceRecord", strlist);
			}
			if(use.card->getSkillNames().contains(objectName())||use.card->hasFlag(objectName()))
				room->addPlayerMark(player,"tenyearhuaceUse-PlayClear");
		}
		return false;
	}
};

JianjiYHCard::JianjiYHCard()
{
}

bool JianjiYHCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(targets.length()>= Self->getAttackRange())return false;
	if(targets.isEmpty())return true;
	foreach(const Player*p,targets){
		if(p->isAdjacentTo(to_select))
			return true;
	}
	return false;
}

void JianjiYHCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		if(p->isDead()|| p->isNude())continue;
		room->askForDiscard(p,"jianjiyh",1,1,false,true);
	}
	if(source->isDead()|| targets.length()== 1)return;

	int hand = targets.first()->getHandcardNum();
	QList<ServerPlayer*> maxcards;
	foreach(ServerPlayer*p,targets)
		hand = qMax(hand,p->getHandcardNum());
	foreach(ServerPlayer*p,targets){
		if(p->getHandcardNum()== hand)
			maxcards << p;
	}
	if(maxcards.isEmpty())return;

	ServerPlayer*maxcard = room->askForPlayerChosen(source,maxcards,"jianjiyh","@jianjiyh-target");
	LogMessage log;
	log.type = "#JianjiYHSlash";
	log.from = maxcard;
	room->sendLog(log);

	Slash*slash = new Slash(Card::NoSuit,0);
	slash->setSkillName("_jianjiyh");
	slash->deleteLater();
	bool ask = false;
	foreach(ServerPlayer*p,targets){
		if(p==maxcard || p->isDead())continue;
		if(!maxcard->canSlash(p,slash,false))continue;
		ask = true;
		break;
	}
	if(!ask)return;

	try {
		room->setPlayerFlag(maxcard,"slashTargetFix");
		foreach(ServerPlayer*p,targets){
			room->setPlayerFlag(p,"SlashAssignee");
			room->setPlayerFlag(p,"jianjiyhTarget");
		}
		room->askForUseCard(maxcard,"@@jianjiyh","@jianjiyh");
		room->setPlayerFlag(maxcard,"-slashTargetFix");
		foreach(ServerPlayer*p,targets){
			room->setPlayerFlag(p,"-jianjiyhTarget");
			room->setPlayerFlag(p,"-SlashAssignee");
		}
	}catch(TriggerEvent triggerEvent){
		if(triggerEvent==TurnBroken || triggerEvent==StageChange){
			foreach(ServerPlayer*p,targets)
				room->setPlayerFlag(p,"-jianjiyhTarget");
		}
	}
}

class JianjiYH : public ZeroCardViewAsSkill
{
public:
	JianjiYH(): ZeroCardViewAsSkill("jianjiyh")
	{
		waked_skills = "#jianjiyh";
		response_pattern = "@@jianjiyh";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("JianjiYHCard");
	}

	const Card*viewAs()const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY)
			return new JianjiYHCard;
		else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@jianjiyh"){
				Slash*slash = new Slash(Card::NoSuit,0);
				slash->setSkillName("_jianjiyh");
				return slash;
			}
		}
		return nullptr;
	}
};

class JianjiYHTargetMod : public TargetModSkill
{
public:
	JianjiYHTargetMod(): TargetModSkill("#jianjiyh")
	{
		frequency = NotFrequent;
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*to)const
	{
		if(card->getSkillName()== "jianjiyh"&& to&& to->hasFlag("jianjiyhTarget"))
			return 999;
		return 0;
	}
};

class Yuanmo : public TriggerSkill
{
public:
	Yuanmo(): TriggerSkill("yuanmo")
	{
		events << EventPhaseStart << Damaged;
		waked_skills = "#yuanmo";
	}

	void changeMark(ServerPlayer*player,bool add)const
	{
		Room*room = player->getRoom();
		int jia = player->getMark("&yuanmo_add"),jian = player->getMark("&yuanmo_reduce");
		if(add){
			if(jian > 0)
				room->removePlayerMark(player,"&yuanmo_reduce");
			else
				room->addPlayerMark(player,"&yuanmo_add");
		} else {
			if(jia > 0)
				room->removePlayerMark(player,"&yuanmo_add");
			else
				room->addPlayerMark(player,"&yuanmo_reduce");
		}
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==Damaged ||(event==EventPhaseStart&& player->getPhase()== Player::Start)){
			QString choice = room->askForChoice(player,objectName(),"add+reduce+cancel");
			if(choice=="cancel")return false;

			LogMessage log;
			log.type = "#YuanmoInvoke";
			log.from = player;
			log.arg = objectName();
			log.arg2 = "yuanmo:" + choice;
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player,objectName());

			if(choice=="reduce"){
				changeMark(player,false);
				player->drawCards(2,objectName());
			} else {
				QList<ServerPlayer*> targets,targets2;
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(player->inMyAttackRange(p))
						targets << p;
				}
				changeMark(player,true);
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(player->inMyAttackRange(p)&& !targets.contains(p)&& !p->isNude())
						targets2 << p;
				}
				if(targets2.isEmpty())return false;
				targets = room->askForPlayersChosen(player,targets2,objectName(),0,targets2.length(),"@yuanmo-obtain");
				foreach(ServerPlayer*p,targets){
					if(player->isDead())break;
					if(p->isNude())continue;
					int id = room->askForCardChosen(player,p,"he",objectName());
					room->obtainCard(player,id,false);
				}
			}
		} else if(event==EventPhaseStart&& player->getPhase()== Player::Finish){
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->inMyAttackRange(p))
					return false;
			}
			if(!player->askForSkillInvoke(this,"yuanmo"))return false;
			player->peiyin(this);
			changeMark(player,true);
		}
		return false;
	}
};

class YuanmoRange : public AttackRangeSkill
{
public:
	YuanmoRange(): AttackRangeSkill("#yuanmo")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player*target,bool)const
	{
		return target->getMark("&yuanmo_add")- target->getMark("&yuanmo_reduce");
	}
};

class TenyearYuhua :public TriggerSkill
{
public:
	TenyearYuhua(): TriggerSkill("tenyearyuhua")
	{
		events << EventPhaseProceeding << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseProceeding){
			if(player->getPhase()!=Player::Discard||player->isKongcheng())return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach(const Card*c,player->getHandcards()){
				if(!c->isKindOf("BasicCard"))
					room->ignoreCards(player,c);
			}
		} else {
			if(player->getPhase()!= Player::Finish || player->getHandcardNum()<= player->getMaxHp())return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->askForGuanxing(player,room->getNCards(1),Room::GuanxingBothSides);
		}
		return false;
	}
};

class TenyearQirang : public TriggerSkill
{
public:
	TenyearQirang(): TriggerSkill("tenyearqirang")
	{
		events << CardUsed;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!data.value<CardUseStruct>().card->isKindOf("EquipCard"))return false;
		if(!player->askForSkillInvoke(this,data))return false;
		room->broadcastSkillInvoke(this);

		QList<int> trickIds;
		foreach(int id,room->getDrawPile()){
			if(Sanguosha->getCard(id)->isKindOf("TrickCard"))
				trickIds.append(id);
		}

		if(trickIds.isEmpty()){
			LogMessage msg;
			msg.type = "#olqirang-failed";
			room->sendLog(msg);
			return false;
		}
		int trick_id = trickIds.at(qrand()% trickIds.length());
		if(trick_id >= 0){
			room->obtainCard(player,trick_id,true);
			if(player->isAlive()&& Sanguosha->getCard(trick_id)->isNDTrick()){
				QVariantList ids = player->getTag("tenyearqirang_tricks").toList();
				ids << trick_id;
				player->setTag("tenyearqirang_tricks", ids);
			}
		}
		return false;
	}
};

class TenyearQirangEffect : public TriggerSkill
{
public:
	TenyearQirangEffect(): TriggerSkill("#tenyearqirang")
	{
		events << EventPhaseChanging << TargetSpecifying;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& !target->getTag("tenyearqirang_tricks").toList().isEmpty();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			player->removeTag("tenyearqirang_tricks");
		} else {
			if(player->isDead())return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.to.length()!= 1 || !use.card->isNDTrick()|| use.card->isVirtualCard()|| !use.card->getSkillName().isEmpty())return false;
			int id = use.card->getEffectiveId();
			QVariantList ids = player->getTag("tenyearqirang_tricks").toList();
			if(!ids.contains(id))return false;
			QList<ServerPlayer*> targets = room->getCardTargets(player,use.card,use.to);
			if(targets.isEmpty())return false;
			ServerPlayer*target = room->askForPlayerChosen(player,targets,"tenyearqirang","@tenyearqirang-target:" + use.card->objectName(),true);
			if(!target)return false;
			room->broadcastSkillInvoke("tenyearqirang");
			room->doAnimate(1,player->objectName(),target->objectName());
			LogMessage log;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.to << target;
			log.card_str = use.card->toString();
			log.arg = "tenyearqirang";
			room->sendLog(log);
			use.to << target;
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class SpManyi : public TriggerSkill
{
public:
	SpManyi(): TriggerSkill("spmanyi")
	{
		events << CardEffected;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if(effect.card->isKindOf("SavageAssault")){
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#SkillNullify";
			log.from = player;
			log.arg = objectName();
			log.arg2 = "savage_assault";
			room->sendLog(log);
			room->notifySkillInvoked(player,objectName());
			return true;
		}
		return false;
	}
};

class Mansi : public TriggerSkill
{
public:
	Mansi(): TriggerSkill("mansi")
	{
		events << CardFinished << DamageDone;
		frequency = Frequent;
		global = true;//
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			QStringList records = player->getTag("MTGuquRecord").toStringList();
			records << use.card->getSuitString();
			player->setTag("MTGuquRecord", records);
			room->setTag("ChongwangLastUser",QVariant::fromValue(player));
			foreach(ServerPlayer*p,use.to)
				p->addMark("mobiledanshou_num" + player->objectName()+ "-Clear");
			if(use.card->isKindOf("Slash")){
				player->addMark("JueshengSlashNum");
				if(player->getPhase()== Player::Play)
					room->addPlayerMark(player,"tenyearpaoxiao-PlayClear");
			}
			if(use.card->isKindOf("SavageAssault")){
				QStringList names = room->getTag("MansiDamage" + use.card->toString()).toStringList();
				if(names.isEmpty())return false;
				room->removeTag("MansiDamage" + use.card->toString());
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->isDead()|| !p->hasSkill(objectName()))continue;
					if(!p->askForSkillInvoke(objectName()))continue;
					room->broadcastSkillInvoke(objectName());
					p->drawCards(names.length(),objectName());
				}
			}
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("SavageAssault")){
				QStringList names = room->getTag("MansiDamage" + damage.card->toString()).toStringList();
				if(!names.contains(player->objectName())){
					names << player->objectName();
					room->setTag("MansiDamage" + damage.card->toString(),names);
				}
				if(damage.from)
					room->setCardFlag(damage.card,"MobileYongXiangzhen_SavageAssault_DamageFrom_" + damage.from->objectName());
			}
			if(damage.from){
				damage.from->addMark("mtfuzhanDamage_" + player->objectName()+ "-Clear");
				player->addMark("mtfuzhanDamage_" + damage.from->objectName()+ "-Clear");
				damage.from->setTag("InvokeKuanggu", (damage.from->distanceTo(player)<= 1));
				if(damage.from->isAlive()){
					damage.from->addMark("zhongzuo_damage-Clear");
					if(damage.from->hasSkills("luanzhan|olluanzhan",true))
						room->addPlayerMark(damage.from,"&luanzhanMark");
					else
						room->addPlayerMark(damage.from,"luanzhanMark");
				}
				damage.from->setTag("kuimang_damage_"+player->objectName(), true);
			}
			if(player->isAlive()){
				if(player->hasSkill("olluanzhan",true))
					room->addPlayerMark(player,"&luanzhanMark");
				else
					room->addPlayerMark(player,"olluanzhanMark");
				player->addMark("zhongzuo_damaged-Clear");
			}
			int hujia = player->getHujia();
			if(hujia > damage.damage || damage.damage <= 0 || hujia <= 0)return false;
			foreach(QString tip,damage.tips){
				if(tip.startsWith("MobileMouDuojiangDamage_"))
					damage.tips.removeOne(tip);
			}
			damage.tips << "MobileMouDuojiangDamage_" + QString::number(hujia);
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Souying : public TriggerSkill
{
public:
	Souying(): TriggerSkill("souying")
	{
		events << DamageCaused;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from->isDead()|| damage.to->isDead())return false;
		damage.from->addMark("souying_damage_ " + damage.to->objectName()+ "-Clear");
		QList<ServerPlayer*> players;
		if(damage.to->isMale()&&damage.from->getMark("souying_damage_ "+damage.to->objectName()+"-Clear")==2&&damage.from->hasSkill(objectName()))
			players << damage.from;
		if(damage.from->isMale()&& damage.from->getMark("souying_damage_ " + damage.to->objectName()+ "-Clear")== 2&& damage.to->hasSkill(objectName())&& !players.contains(damage.to))
			players << damage.to;
		if(players.isEmpty())return false;
		room->sortByActionOrder(players);

		foreach(ServerPlayer*p,players){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(!p->canDiscard(p,"h")|| p->getMark("souying_used-Clear")> 0)continue;
			if(!room->askForCard(p,".","souying-invoke:" + damage.to->objectName(),data,objectName()))continue;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(p,"souying_used-Clear");
			if(p==damage.from){
				LogMessage log;
				log.type = "#SouyingAdd";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(++damage.damage);
				room->sendLog(log);
			} else if(p==damage.to){
				LogMessage log;
				log.type =(damage.damage > 1)? "#SouyingReduce" : "#SouyingPrevent";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(--damage.damage);
				room->sendLog(log);
				if(damage.damage <= 0)
					return true;
			}
		}
		data = QVariant::fromValue(damage);
		return false;
	}
};

class Zhanyuan : public PhaseChangeSkill
{
public:
	Zhanyuan(): PhaseChangeSkill("zhanyuan")
	{
		frequency = Wake;
		waked_skills = "xili";
	}

	bool triggerable(const ServerPlayer*player)const
	{
		return player&& player->isAlive()&&player->getPhase()== Player::Start
		&& player->getMark(objectName())<1&& player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		int mark = player->getMark("&zhanyuan_num")+ player->getMark("zhanyuan_num");
		if(mark > 7){
			LogMessage log;
			log.type = "#ZhanyuanWake";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(mark);
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player,objectName());
		room->doSuperLightbox(player,objectName());
		room->addPlayerMark(player,objectName());
		room->setPlayerMark(player,"&zhanyuan_num",0);
		if(room->changeMaxHpForAwakenSkill(player,1,objectName())){
			QList<ServerPlayer*> males;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->isMale())males << p;
			}
			ServerPlayer*male = room->askForPlayerChosen(player,males,objectName(),"@zhanyuan-invoke",true);
			if(!male)return false;
			room->doAnimate(1,player->objectName(),male->objectName());
			QList<ServerPlayer*> players;
			players << player;
			if(!players.contains(male))
				players << male;
			if(players.isEmpty())return false;
			room->sortByActionOrder(players);
			foreach(ServerPlayer*p,players){
				if(p->hasSkill("xili",true))continue;
				room->handleAcquireDetachSkills(p,"xili");
			}
			if(player->hasSkill("mansi",true))
				room->handleAcquireDetachSkills(player,"-mansi");
		}
		return false;
	}
};

class ZhanyuanRecord : public TriggerSkill
{
public:
	ZhanyuanRecord(const QString&zhanyuan): TriggerSkill("#" + zhanyuan),zhanyuan(zhanyuan)
	{
		events << CardsMoveOneTime;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		QString mansi = "mansi";
		if(zhanyuan=="secondzhanyuan")mansi = "secondmansi";
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to==player&& move.to_place==Player::PlaceHand&& move.reason.m_skillName==mansi){
			if(player->hasSkill(zhanyuan,true))
				room->addPlayerMark(player,"&" + zhanyuan + "_num",move.card_ids.length());
			else
				player->addMark(zhanyuan + "_num",move.card_ids.length());
		}
		return false;
	}
private:
	QString zhanyuan;
};

class Xili : public TriggerSkill
{
public:
	Xili(): TriggerSkill("xili")
	{
		events << TargetSpecified << DamageCaused << CardFinished;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.from->isDead()|| !use.from->hasSkill(objectName(),true)|| !use.from->hasFlag("CurrentPlayer")|| !use.card->isKindOf("Slash"))return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(use.from)){
				if(p->isDead()|| !p->hasSkill(objectName(),true)|| !p->canDiscard(p,"h")|| p->hasFlag("CurrentPlayer"))continue;
				if(!room->askForCard(p,".","xili-invoke",data,objectName()))continue;
				room->broadcastSkillInvoke(objectName());
				//int n = use.card->getTag("XiliDamage").toInt();
				//use.card->setTag("XiliDamage", n + 1);
				int n = room->getTag("XiliDamage" + use.card->toString()).toInt();
				room->setTag("XiliDamage" + use.card->toString(),n + 1);
			}
		} else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			//use.card->removeTag("XiliDamage");
			room->removeTag("XiliDamage" + use.card->toString());
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card || !damage.card->isKindOf("Slash")|| damage.to->isDead())return false;
			//int n = damage.card->getTag("XiliDamage").toInt(); //ai运行时无法获取这个tag
			int n = room->getTag("XiliDamage" + damage.card->toString()).toInt();
			if(n <= 0)return false;
			damage.damage += n;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

TenyearZhongjianCard::TenyearZhongjianCard()
{
}

bool TenyearZhongjianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(Self->getMark("tenyearzhongjian-PlayClear")> 0)
		return targets.isEmpty()&& to_select->getMark("tenyearzhongjian-PlayClear")<= 0;
	return targets.isEmpty();
}

void TenyearZhongjianCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.from->getRoom();
	room->addPlayerMark(effect.to,"tenyearzhongjian-PlayClear");
	QString choice = room->askForChoice(effect.from,"tenyearzhongjian","draw+discard",QVariant::fromValue(effect.to));

	QStringList names = effect.from->property("tenyearzhongjian_targets").toStringList();
	//if(names.contains(effect.to->objectName()))return;
	names << effect.to->objectName();
	room->setPlayerProperty(effect.from,"tenyearzhongjian_targets",names);
	QStringList choices = effect.from->getTag("tenyearzhongjian_choices" + effect.to->objectName()).toStringList();
	//if(choices.contains(choice))return;
	choices << choice;
	effect.from->setTag("tenyearzhongjian_choices" + effect.to->objectName(), choices);

	room->addPlayerMark(effect.to,"&tenyearzhongjian+" + choice);
}

class TenyearZhongjianVS : public ZeroCardViewAsSkill
{
public:
	TenyearZhongjianVS(): ZeroCardViewAsSkill("tenyearzhongjian")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("TenyearZhongjianCard")< 1 + player->getMark("tenyearcaishi_extra-PlayClear");
	}

	const Card*viewAs()const
	{
		return new TenyearZhongjianCard;
	}
};

class TenyearZhongjian : public TriggerSkill
{
public:
	TenyearZhongjian(): TriggerSkill("tenyearzhongjian")
	{
		events << EventPhaseStart << Damage << Damaged << Death;
		view_as_skill = new TenyearZhongjianVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::RoundStart)return false;
			QStringList names = player->property("tenyearzhongjian_targets").toStringList();
			if(names.isEmpty())return false;
			room->setPlayerProperty(player,"tenyearzhongjian_targets",QStringList());
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(!names.contains(p->objectName()))continue;
				QStringList choices = player->getTag("tenyearzhongjian_choices" + p->objectName()).toStringList();
				player->removeTag("tenyearzhongjian_choices" + p->objectName());
				foreach(QString choice,choices)
					room->removePlayerMark(p,"&tenyearzhongjian+" + choice);
			}
		} else if(event==Death){
			if(!data.value<DeathStruct>().who->hasSkill(objectName(),true))return false;
			QStringList names = player->property("tenyearzhongjian_targets").toStringList();
			if(names.isEmpty())return false;
			room->setPlayerProperty(player,"tenyearzhongjian_targets",QStringList());
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(!names.contains(p->objectName()))continue;
				QStringList choices = player->getTag("tenyearzhongjian_choices" + p->objectName()).toStringList();
				player->removeTag("tenyearzhongjian_choices" + p->objectName());
				foreach(QString choice,choices)
					room->removePlayerMark(p,"&tenyearzhongjian+" + choice);
			}
		} else if(event==Damage){
			if(player->isDead()|| player->getMark("&tenyearzhongjian+discard")<= 0)return false;
			QList<ServerPlayer*> xinxianyings;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				QStringList names = p->property("tenyearzhongjian_targets").toStringList();
				if(names.isEmpty())continue;
				xinxianyings << p;
				QStringList choices = p->getTag("tenyearzhongjian_choices" + player->objectName()).toStringList();
				choices.removeAll("discard");
				if(choices.isEmpty()){
					room->setPlayerProperty(p,"tenyearzhongjian_targets",QStringList());
					p->removeTag("tenyearzhongjian_choices" + player->objectName());
					continue;
				}
				p->setTag("tenyearzhongjian_choices" + player->objectName(), choices);
			}
			int n = player->getMark("&tenyearzhongjian+discard");
			room->setPlayerMark(player,"&tenyearzhongjian+discard",0);
			if(!player->canDiscard(player,"he"))return false;
			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->askForDiscard(player,objectName(),2* n,2* n,false,true);
			if(xinxianyings.isEmpty())return false;
			room->sortByActionOrder(xinxianyings);
			room->drawCards(xinxianyings,1,objectName());
		} else if(event==Damaged){
			if(player->isDead()|| player->getMark("&tenyearzhongjian+draw")<= 0)return false;
			QList<ServerPlayer*> xinxianyings;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				QStringList names = p->property("tenyearzhongjian_targets").toStringList();
				if(names.isEmpty())continue;
				xinxianyings << p;
				QStringList choices = p->getTag("tenyearzhongjian_choices" + player->objectName()).toStringList();
				choices.removeAll("draw");
				if(choices.isEmpty()){
					room->setPlayerProperty(p,"tenyearzhongjian_targets",QStringList());
					p->removeTag("tenyearzhongjian_choices" + player->objectName());
					continue;
				}
				p->setTag("tenyearzhongjian_choices" + player->objectName(), choices);
			}
			int n = player->getMark("&tenyearzhongjian+draw");
			room->setPlayerMark(player,"&tenyearzhongjian+draw",0);
			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			player->drawCards(2* n,objectName());
			if(xinxianyings.isEmpty())return false;
			room->sortByActionOrder(xinxianyings);
			room->drawCards(xinxianyings,1,objectName());
		}
		return false;
	}
};

class TenyearCaishi : public TriggerSkill
{
public:
	TenyearCaishi(): TriggerSkill("tenyearcaishi")
	{
		events << EventPhaseEnd << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Draw)return false;
		if(event==EventPhaseEnd){
			QVariantList vids = player->getTag("Tenyearcaishi_ids").toList();
			if(vids.isEmpty())return false;

			QList<int> ids = ListV2I(vids);
			player->removeTag("Tenyearcaishi_ids");
			bool same = true;
			Card::Suit suit = Sanguosha->getCard(ids.first())->getSuit();
			foreach(int id,ids){
				if(Sanguosha->getCard(id)->getSuit()!= suit){
					same = false;
					break;
				}
			}

			if(same){
				room->addPlayerMark(player,"tenyearcaishi_extra-PlayClear");
				if(!player->hasSkill("tenyearzhongjian",true))return false;
				LogMessage log;
				log.type = "#TenyearCaishiSame";
				log.from = player;
				log.arg = objectName();
				log.arg2 = "tenyearzhongjian";
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player,objectName());
			} else {
				if(player->getLostHp()<= 0)return false;
				if(!player->askForSkillInvoke(this,"tenyearcaishi"))return false;
				room->broadcastSkillInvoke(objectName());
				room->recover(player,RecoverStruct("tenyearcaishi",player));
				room->addPlayerMark(player,"tenyearcaishi_pro-Clear");
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&& move.to_place==Player::PlaceHand&& move.from_places.contains(Player::DrawPile)){
				QVariantList vids = player->getTag("Tenyearcaishi_ids").toList();
				for(int i = 0; i < move.card_ids.length(); i++){
					if(move.from_places.at(i)== Player::DrawPile&& !vids.contains(move.card_ids.at(i)))
						vids << move.card_ids.at(i);
				}
				player->setTag("Tenyearcaishi_ids", vids);
			}
		}
		return false;
	}
};

class TenyearCaishiPro : public ProhibitSkill
{
public:
	TenyearCaishiPro(): ProhibitSkill("#tenyearcaishi-pro")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		return from==to&& from->getMark("tenyearcaishi_pro-Clear")> 0&& !card->isKindOf("SkillCard");
	}
};

class Xialei : public TriggerSkill
{
public:
	Xialei(): TriggerSkill("xialei")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place != Player::DiscardPile)return false;

		int watch = 3 - player->getMark("&xialei_watch-Clear");
		if(watch <= 0)return false;

		bool red = false;
		for(int i = 0; i < move.card_ids.length(); i++){
			if(move.from_places.at(i)== Player::PlaceTable){
				if(move.reason.m_playerId != player->objectName())
					continue;
                if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
					&&move.reason.m_reason!=CardMoveReason::S_REASON_RESPONSE)
					continue;
			} else {
				if(move.from_places.at(i)!= Player::PlaceHand&& move.from_places.at(i)!= Player::PlaceEquip)
					continue;
				if(move.from != player)
					continue;
			}
			red = Sanguosha->getCard(move.card_ids.at(i))->isRed();
			if(red)break;
		}
		if(!red)return false;

		if(!player->askForSkillInvoke(objectName()+"$-1"))return false;

		QList<int> watch_cards = room->getNCards(watch);
		try {
			LogMessage log;
			log.type = "$ViewDrawPile";
			log.from = player;
			log.card_str = ListI2S(watch_cards).join("+");
			room->sendLog(log,player);

			log.type = "#ViewDrawPile";
			log.arg = QString::number(watch_cards.length());
			room->sendLog(log,room->getOtherPlayers(player,true));

			room->fillAG(watch_cards,player);
			int id = room->askForAG(player,watch_cards,false,objectName());
			room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
			watch_cards.removeOne(id);
			room->obtainCard(player,id,false);
			if(room->hasCurrent())
				room->addPlayerMark(player,"&xialei_watch-Clear");

			if(!watch_cards.isEmpty()){
				if(player->askForSkillInvoke("xialei_put","xialei_put",false)){
					room->clearAG(player);
					log.type = "$XialeiPut";
					log.card_str = ListI2S(watch_cards).join("+");
					room->sendLog(log,player);
					log.type = "#XialeiPut";
					log.arg = QString::number(watch_cards.length());
					room->sendLog(log,room->getOtherPlayers(player,true));
					room->returnToEndDrawPile(watch_cards);
					return false;
				}
			}
			room->clearAG(player);
			room->returnToTopDrawPile(watch_cards);
		}
		catch(TriggerEvent triggerEvent){
			if(triggerEvent==TurnBroken || triggerEvent==StageChange){
				room->returnToTopDrawPile(watch_cards);
			}
		}
		return false;
	}
};

AnzhiCard::AnzhiCard()
{
	target_fixed = true;
}

void AnzhiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	JudgeStruct judge;
	judge.pattern = ".";
	judge.play_animation = false;
	judge.reason = "anzhi";
	judge.who = source;
	room->judge(judge);

	if(source->isDead())return;

	if(judge.card->getColorString()== "red")
		room->setPlayerMark(source,"&xialei_watch-Clear",0);
	else if(judge.card->getColorString()== "black"){
		QList<int> ids;
		foreach(int id,room->getDiscardPile()){
			if(source->getMark(QString::number(id)+"AnzhiRecord-Clear")>0)
				ids << id;
		}
		if(ids.isEmpty())return;

		QList<ServerPlayer*> targets = room->getAlivePlayers();
		ServerPlayer*p = room->getCurrent();
		if(p)targets.removeOne(p);

		ServerPlayer*t = room->askForPlayerChosen(source,targets,"anzhi","@anzhi-target",true);
		if(!t)return;
		room->addPlayerMark(source,"anzhi_wuxiao-Clear");

		DummyCard*dummy = new DummyCard();
		for(int i = 0; i < 2; i++){
			if(ids.isEmpty()|| source->isDead())break;
			room->fillAG(ids,source);
			int id = room->askForAG(source,ids,false,"anzhi",QString("@anzhi-get:%1:%2").arg(t->objectName()).arg(i + 1));
			room->clearAG(source);
			ids.removeOne(id);
			dummy->addSubcard(id);
		}
		if(dummy->subcardsLength()> 0&& t->isAlive())
			room->obtainCard(t,dummy);
		delete dummy;
	}
}

class AnzhiVS : public ZeroCardViewAsSkill
{
public:
	AnzhiVS(): ZeroCardViewAsSkill("anzhi")
	{
	}

	const Card*viewAs()const
	{
		return new AnzhiCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("anzhi_wuxiao-Clear")<= 0;
	}
};

class Anzhi : public TriggerSkill
{
public:
	Anzhi(): TriggerSkill("anzhi")
	{
		events << Damaged;
		view_as_skill = new AnzhiVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getMark("anzhi_wuxiao-Clear")<1&&player->askForSkillInvoke(this)){
			player->peiyin(this);
			AnzhiCard*az = new AnzhiCard;
			QList<ServerPlayer*> targets;
			az->use(room,player,targets);
			delete az;
		}
		return false;
	}
};

class TenyearWangyuan : public TriggerSkill
{
public:
	TenyearWangyuan(): TriggerSkill("tenyearwangyuan")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	static void addWang(ServerPlayer*player)
	{
		if(player->isDead())return;
		Room*room = player->getRoom();
		QStringList names;
		QList<int> pile = player->getPile("thrjwywang"),drawpile = room->getDrawPile(),ids;
		foreach(int id,pile){
			const Card*c = Sanguosha->getCard(id);
			QString name = c->objectName();
			if(c->isKindOf("Slash"))
				name = "slash";
			if(!names.contains(name))
				names << name;
		}
		foreach(int id,drawpile){
			const Card*c = Sanguosha->getCard(id);
			if(!c->isKindOf("BasicCard")&& !c->isKindOf("TrickCard"))continue;
			QString name = c->objectName();
			if(c->isKindOf("Slash"))
				name = "slash";
			if(names.contains(name))continue;
			ids << id;
		}
		if(ids.isEmpty())return;
		int id = ids.at(qrand()% ids.length());
		player->addToPile("thrjwywang",id);
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.from != player || player->hasFlag("CurrentPlayer"))return false;
		if(!move.from_places.contains(Player::PlaceHand)&& !move.from_places.contains(Player::PlaceEquip))return false;
		if(player->getPile("thrjwywang").length()> Sanguosha->getPlayerCount(room->getMode()))return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);
		addWang(player);
		return false;
	}
};

class TenyearLiying : public TriggerSkill
{
public:
	TenyearLiying(): TriggerSkill("tenyearliying")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(room->getTag("FirstRound").toBool())return false;
		if(!room->hasCurrent()|| player->getPhase()== Player::Draw || player->getMark("tenyearliyingUsed-Clear")> 0)return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to != player || move.to_place != Player::PlaceHand)return false;

		QList<int> move_ids = move.card_ids,ids;
		foreach(int id,move_ids){
			if(!player->hasCard(id))continue;
			ids << id;
		}
		if(ids.isEmpty())return false;

		room->fillAG(ids,player);
		ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@tenyearliying-give",true,true);
		room->clearAG(player);
		if(!t)return false;
		room->addPlayerMark(player,"tenyearliyingUsed-Clear");
		player->peiyin(this);

		room->giveCard(player,t,ids,objectName());
		player->drawCards(1,objectName());

		if(player->getPhase()== Player::NotActive)return false;
		if(player->getPile("thrjwywang").length()> Sanguosha->getPlayerCount(room->getMode()))return false;
		TenyearWangyuan::addWang(player);
		return false;
	}
};

TenyearLingyinCard::TenyearLingyinCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearLingyinCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	LogMessage log;
	log.type = "$KuangbiGet";
	log.from = source;
	log.arg = "thrjwywang";
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);
	room->obtainCard(source,this,true);

	QList<int> pile = source->getPile("thrjwywang");
	if(pile.isEmpty())return;
	Card::Color color = Sanguosha->getCard(pile.first())->getColor();
	foreach(int id,pile){
		if(Sanguosha->getCard(id)->getColor()!= color)
			return;
	}
	room->addPlayerMark(source,"&tenyearlingyinBuff-Clear");
	room->setPlayerMark(source,"ViewAsSkill_tenyearlingyinEffect",1);
}

class TenyearLingyinVS : public ViewAsSkill
{
public:
	TenyearLingyinVS(): ViewAsSkill("tenyearlingyin")
	{
		expand_pile = "thrjwywang";
		response_or_use = true;
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			if(!selected.isEmpty()|| Self->getPile("thrjwywang").contains(to_select->getEffectiveId()))return false;
			if(!to_select->isKindOf("Armor")&& !to_select->isKindOf("Weapon"))return false;
			Duel*duel = new Duel(Card::SuitToBeDecided,-1);
			duel->setSkillName(objectName());
			duel->addSubcard(to_select);
			duel->deleteLater();
			return duel->isAvailable(Self);
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@tenyearlingyin"){
				int lun = Self->getMark("tenyearlingyin_lun-PalyClear");
				return Self->getPile("thrjwywang").contains(to_select->getEffectiveId())&& selected.length()< lun;
			}
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_PLAY){
			Duel*duel = new Duel(Card::SuitToBeDecided,-1);
			duel->setSkillName(objectName());
			duel->addSubcards(cards);
			return duel;
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if(pattern=="@@tenyearlingyin"){
				TenyearLingyinCard*c = new TenyearLingyinCard;
				c->addSubcards(cards);
				return c;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("&tenyearlingyinBuff-Clear")> 0;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern=="@@tenyearlingyin";
	}
};

class TenyearLingyin : public PhaseChangeSkill
{
public:
	TenyearLingyin(): PhaseChangeSkill("tenyearlingyin")
	{
		view_as_skill = new TenyearLingyinVS;
		waked_skills = "#tenyearlingyin";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPile("thrjwywang").isEmpty()|| player->getPhase()!= Player::Play)return false;
		int lun = room->getTag("TurnLengthCount").toInt();
		if(lun <= 0)return false;
		room->setPlayerMark(player,"tenyearlingyin_lun-PalyClear",lun);
		room->askForUseCard(player,"@@tenyearlingyin","@tenyearlingyin:" + QString::number(lun),-1,Card::MethodNone);
		return false;
	}
};

class TenyearLingyinEffect : public TriggerSkill
{
public:
	TenyearLingyinEffect(): TriggerSkill("#tenyearlingyin")
	{
		events << ConfirmDamage << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(player==damage.to)return false;
			int mark = player->getMark("&tenyearlingyinBuff-Clear");
			if(mark <= 0)return false;

			damage.damage += mark;
			data = QVariant::fromValue(damage);

			LogMessage log;
			log.type = "#XionghuoDamage";
			log.from = player;
			log.arg = "tenyearlingyin";
			log.to << damage.to;
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			player->peiyin("tenyearlingyin");
			room->notifySkillInvoked(player,"tenyearlingyin");
		} else {
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			room->setPlayerMark(player,"ViewAsSkill_tenyearlingyinEffect",0);
		}
		return false;
	}
};

CaizhuangCard::CaizhuangCard()
{
	target_fixed = true;
}

int CaizhuangCard::getSuitsNum(ServerPlayer*source)const
{
	QList<Card::Suit> suits;
	foreach(const Card*c,source->getHandcards()){
		Card::Suit suit = c->getSuit();
		if(suits.contains(suit))continue;
		suits << suit;
	}
	return suits.length();
}

void CaizhuangCard::use(Room*,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QList<Card::Suit> suits;
	foreach(int id,subcards){
		Card::Suit suit = Sanguosha->getCard(id)->getSuit();
		if(suits.contains(suit))continue;
		suits << suit;
	}
	while(getSuitsNum(source)< suits.length()){
		if(source->isDead())break;
		source->drawCards(1,"caizhuang");
	}
}

class Caizhuang : public ViewAsSkill
{
public:
	Caizhuang(): ViewAsSkill("caizhuang")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Self->isJilei(to_select))return false;
		foreach(const Card*c,selected){
			if(to_select->getSuit()== c->getSuit())
				return false;
		}
		return true;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;
		CaizhuangCard*c = new CaizhuangCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("CaizhuangCard");
	}
};

class Huayi : public PhaseChangeSkill
{
public:
	Huayi(): PhaseChangeSkill("huayi")
	{
		frequency = Frequent;
		waked_skills = "#huayi";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		if(!player->askForSkillInvoke(this))return false;
		player->peiyin(this);

		JudgeStruct judge;
		judge.pattern = ".";
		judge.reason = objectName();
		judge.who = player;
		judge.play_animation = false;
		room->judge(judge);

		QString phase = QString::number((int)Player::RoundStart);
		if(judge.card->isBlack())
			room->addPlayerMark(player,"&huayi+black-Self" + phase + "Clear");
		else if(judge.card->isRed())
			room->addPlayerMark(player,"&huayi+red-Self" + phase + "Clear");
		return false;
	}
};

class HuayiEffect : public TriggerSkill
{
public:
	HuayiEffect(): TriggerSkill("#huayi")
	{
		events << EventPhaseChanging << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		QString phase = QString::number((int)Player::RoundStart);
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isDead())continue;
				int mark = p->getMark("&huayi+red-Self" + phase + "Clear");
				for(int i = 0; i < mark; i++){
					if(p->isDead())break;
					room->sendCompulsoryTriggerLog(p,"huayi",true,true);
					p->drawCards(1,"huayi");
				}
			}
		} else {
			if(player->isDead())return false;
			int mark = player->getMark("&huayi+black-Self" + phase + "Clear");
			for(int i = 0; i < mark; i++){
				if(player->isDead())break;
				room->sendCompulsoryTriggerLog(player,"huayi",true,true);
				player->drawCards(2,"huayi");
			}
		}
		return false;
	}
};

class Tenyearbiluan : public DistanceSkill
{
public:
	Tenyearbiluan(): DistanceSkill("tenyearbiluan")
	{
		frequency = NotFrequent;
	}

	int getCorrect(const Player*,const Player*to)const
	{
		int n = to->property("tenyearbiluan_distance").toInt();
		if(n>0&&to->hasSkill(objectName()))
			return n;
		return 0;
	}
};

class TenyearbiluanTrigger : public PhaseChangeSkill
{
public:
	TenyearbiluanTrigger(): PhaseChangeSkill("#tenyearbiluan-trigger")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(!player->hasSkill("tenyearbiluan")|| player->getPhase()!= Player::Finish || !player->canDiscard(player,"he"))return false;
		bool can_invoke = false;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->distanceTo(player)== 1){
				can_invoke = true;
				break;
			}
		}
		if(!can_invoke)return false;
		if(!room->askForCard(player,"..","@tenyearbiluan-discard",QVariant(),"tenyearbiluan"))return false;
		room->broadcastSkillInvoke("tenyearbiluan");
		//int distance = qMin(4,room->alivePlayerCount());
		//room->addDistance(player,distance,false,false);
		int distance = player->property("tenyearbiluan_distance").toInt();
		distance += qMin(4,room->alivePlayerCount());
		room->setPlayerProperty(player,"tenyearbiluan_distance",distance);
		return false;
	}
};

class TenyearLixia : public DistanceSkill
{
public:
	TenyearLixia(): DistanceSkill("tenyearlixia")
	{
	}

	int getCorrect(const Player*,const Player*to)const
	{
		int n = to->property("tenyearlixia_distance").toInt();
		if(n>0&&to->hasSkill(objectName()))
			return -n;
		return 0;
	}
};

class TenyearLixiaTrigger : public PhaseChangeSkill
{
public:
	TenyearLixiaTrigger(): PhaseChangeSkill("#tenyearlixia-trigger")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getPhase()== Player::Finish;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isAlive()&& p->hasSkill("tenyearlixia")){
				if(player->inMyAttackRange(p))continue;
				room->sendCompulsoryTriggerLog(p,"tenyearlixia",true,true);
				QString choice = room->askForChoice(p,"tenyearlixia","self+other",QVariant::fromValue(player));
				if(choice=="self")
					p->drawCards(1,"tenyearlixia");
				else
					player->drawCards(2,"tenyearlixia");
				int distance = p->property("tenyearlixia_distance").toInt();
				distance++;
				room->setPlayerProperty(p,"tenyearlixia_distance",distance);
			}
		}
		return false;
	}
};

class TenyearCanshi : public TriggerSkill
{
public:
	TenyearCanshi(): TriggerSkill("tenyearcanshi")
	{
		events << DrawNCards << CardUsed;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(triggerEvent==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason=="draw_phase"){
				int n = 0;
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->isWounded())
						++n;
				}
				if(n>0&&player->askForSkillInvoke(this)){
					room->broadcastSkillInvoke(objectName());
					player->setFlags(objectName());
					draw.num += n;
					data = QVariant::fromValue(draw);
				}
			}
		} else {
			if(player->hasFlag(objectName())){
				const Card*card = data.value<CardUseStruct>().card;
				if(card&&(card->isKindOf("Slash")|| card->isNDTrick())&& player->canDiscard(player,"he")){
					room->sendCompulsoryTriggerLog(player,objectName());
					room->askForDiscard(player,objectName(),1,1,false,true,"@tenyearcanshi-discard");
				}
			}
		}
		return false;
	}
};

class TenyearChouhai : public TriggerSkill
{
public:
	TenyearChouhai(): TriggerSkill("tenyearchouhai")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.card || !damage.card->isKindOf("Slash"))return false;
		if(player->isKongcheng()){
			room->sendCompulsoryTriggerLog(player,objectName(),true,true);

			++damage.damage;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Shiyuan : public TriggerSkill
{
public:
	Shiyuan(): TriggerSkill("shiyuan")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		ServerPlayer*p = room->getCurrent();
		if(p==nullptr)return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("SkillCard"))return false;
		if(!use.from || use.from->isDead()|| use.from==player || !use.to.contains(player))return false;
		int n = 1;
		if(p->getKingdom()== "qun"&& player->hasLordSkill("yuwei"))
			n++;

		int from_hp = use.from->getHp();
		int player_hp = player->getHp();
		int draw_num = 3;

		if(from_hp > player_hp){
			if(player->getMark("shiyuan_dayu-Clear")>= n)return false;
			room->addPlayerMark(player,"shiyuan_dayu-Clear");
		} else if(from_hp==player_hp){
			if(player->getMark("shiyuan_dengyu-Clear")>= n)return false;
			room->addPlayerMark(player,"shiyuan_dengyu-Clear");
			draw_num = 2;
		} else {
			if(player->getMark("shiyuan_xiaoyu-Clear")>= n)return false;
			room->addPlayerMark(player,"shiyuan_xiaoyu-Clear");
			draw_num = 1;
		}

		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		player->drawCards(draw_num,objectName());
		return false;
	}
};

class SpDushi : public TriggerSkill
{
public:
	SpDushi(): TriggerSkill("spdushi")
	{
		events << Death << EnterDying << QuitDying;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EnterDying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who==player&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					room->addPlayerMark(p,"Global_PreventPeach");
					p->addMark("spdushi");
				}
			}
		} else if(event==QuitDying){
			DyingStruct dying = data.value<DyingStruct>();
			foreach(ServerPlayer*p,room->getOtherPlayers(dying.who)){
				if(p->getMark("spdushi")>0){
					room->removePlayerMark(p,"Global_PreventPeach",p->getMark("spdushi"));
					p->removeMark("spdushi");
				}
			}
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != player || !player->hasSkill(objectName()))return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(death.who)){
				if(p->getMark("spdushi")>0){
					room->removePlayerMark(p,"Global_PreventPeach",p->getMark("spdushi"));
					p->removeMark("spdushi");
				}
			}
			room->sendCompulsoryTriggerLog(player,this);
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@spdushi-invoke",false,true);
			room->handleAcquireDetachSkills(target,"spdushi");
		}
		return false;
	}
};

class SpDushiPro : public ProhibitSkill
{
public:
	SpDushiPro(): ProhibitSkill("#spdushi-pro")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		return from != to&& card->isKindOf("Peach")&& to->hasFlag("Global_Dying")&& to->hasSkill("spdushi");
	}
};

QianlongCard::QianlongCard()
{
	mute = true;
	handling_method = Card::MethodNone;
	will_throw = false;
	target_fixed = true;
}

void QianlongCard::onUse(Room*,CardUseStruct&)const
{
}

class QianlongVS : public ViewAsSkill
{
public:
	QianlongVS(): ViewAsSkill("qianlong")
	{
		expand_pile = "#qianlong";
		response_pattern = "@@qianlong";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		int losehp = Self->getLostHp();
		return selected.length()< losehp&& Self->getPile("#qianlong").contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		QianlongCard*c = new QianlongCard;
		c->addSubcards(cards);
		return c;
	}
};

class Qianlong : public MasochismSkill
{
public:
	Qianlong(): MasochismSkill("qianlong")
	{
		view_as_skill = new QianlongVS;
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		if(!player->askForSkillInvoke(this))return;
		player->peiyin(this);

		Room*room = player->getRoom();
		//QList<int> shows = room->showDrawPile(player,3,objectName(),false);
		//这个函数会把牌return回牌堆，和下面的guanxing连用，会让牌在牌堆顶和牌堆底各有一张，从而卡住牌
		QList<int> shows = room->getNCards(3,false);

		LogMessage log;
		log.type = "$TurnOver";
		log.from = player;
		log.card_str = ListI2S(shows).join("+");
		room->sendLog(log);

		room->fillAG(shows);

		int losehp = player->getLostHp();
		if(losehp>0){
			room->notifyMoveToPile(player,shows,objectName(),Player::DrawPile,true);
			const Card*c = room->askForUseCard(player,"@@qianlong","@qianlong:" + QString::number(losehp));
			room->notifyMoveToPile(player,shows,objectName(),Player::DrawPile,false);

			if(c){
				room->obtainCard(player,c,true);
				foreach(int id,c->getSubcards())
					shows.removeOne(id);
			}
		}
		room->clearAG();
		if(!shows.isEmpty()){
			if(player->isAlive())
				room->askForGuanxing(player,shows,Room::GuanxingDownOnly);
			else
				room->returnToTopDrawPile(shows);
		}
	}
};

class Fensi : public PhaseChangeSkill
{
public:
	Fensi(): PhaseChangeSkill("fensi")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;

		QList<ServerPlayer*> targets;
		int hp = player->getHp();
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getHp()>= hp)
				targets << p;
		}
		if(targets.isEmpty())return false;

		ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),"@fensi-damage",false,true);
		player->peiyin(this);
		room->damage(DamageStruct(objectName(),player,t));

		if(t->isAlive()&& player->isAlive()&& t != player){
			Slash*slash = new Slash(Card::NoSuit,0);
			slash->deleteLater();
			room->setCardFlag(slash,"YUANBEN");
			slash->setSkillName("_fensi");
			if(t->canSlash(player,slash,false))
				room->useCard(CardUseStruct(slash,t,player),true);
		}
		return false;
	}
};

class JuetaoVS : public ZeroCardViewAsSkill
{
public:
	JuetaoVS(): ZeroCardViewAsSkill("juetao")
	{
		response_pattern = "@@juetao!";
	}

	const Card*viewAs()const
	{
		return Sanguosha->getCard(Self->getMark("juetao_card_id"));
	}
};

class Juetao : public PhaseChangeSkill
{
public:
	Juetao(): PhaseChangeSkill("juetao")
	{
		frequency = Limited;
		limit_mark = "@juetaoMark";
		view_as_skill = new JuetaoVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play || player->getMark("@juetaoMark")<= 0
		|| player->getHp()!= 1 || room->getDrawPile().isEmpty())return false;

		ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"@juetao-use",true,true);
		if(!t)return false;
		player->peiyin(this);

		room->doSuperLightbox(player,objectName());
		room->removePlayerMark(player,"@juetaoMark");

		room->setPlayerMark(player,"juetao_buff-PalyClear",1);
		room->setPlayerMark(t,"juetao_buff-PalyClear",1);

		try {
			while(player->isAlive()){
				QList<int> drawpile = room->getDrawPile();
				if(drawpile.isEmpty())break;

				int id = drawpile.last();
				QList<int> list;
				list << id;
				room->fillAG(list,player);

				const Card*card = Sanguosha->getCard(id);
				room->setCardFlag(card,"juetao_card");

				room->getThread()->delay();
				if(!player->canUse(card)){
					room->clearAG(player);
					break;
				}

				if(card->targetFixed()){
					room->clearAG(player);
					room->useCard(CardUseStruct(card,player));
				} else {
					room->setPlayerMark(player,"juetao_card_id",id);
					if(room->askForUseCard(player,"@@juetao!","@juetao:" + card->objectName(),-1,Card::MethodUse,false))
						room->clearAG(player);
					else{
						room->clearAG(player);
						QList<ServerPlayer*> players = player->getRandomTargets(card);
						room->setCardFlag(card,"-juetao_card");
						if(players.isEmpty())break;
						room->useCard(CardUseStruct(card,player,players));
					}
				}
				room->setCardFlag(card,"-juetao_card");
			}
		}
		catch(TriggerEvent triggerEvent){
			if(triggerEvent==TurnBroken || triggerEvent==StageChange){
				room->setPlayerMark(player,"juetao_buff-PalyClear",0);
				room->setPlayerMark(t,"juetao_buff-PalyClear",0);
			}
			throw triggerEvent;
		}
		room->setPlayerMark(player,"juetao_buff-PalyClear",0);
		room->setPlayerMark(t,"juetao_buff-PalyClear",0);
		return false;
	}
};

class JuetaoPro : public ProhibitSkill
{
public:
	JuetaoPro(): ProhibitSkill("#juetao")
	{
	}

	bool isProhibited(const Player*,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		return card->hasFlag("juetao_card")&& to->getMark("juetao_buff-PalyClear")<1;
	}
};

class Zhushi : public TriggerSkill
{
public:
	Zhushi(): TriggerSkill("zhushi$")
	{
		events << HpRecover;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getKingdom()== "wei";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(!room->hasCurrent()|| player != room->getCurrent())return false;
		QList<ServerPlayer*> caomaos;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(!p->hasLordSkill(this)||p->getMark("zhushi_used_" + player->objectName()+ "-Clear")> 0)continue;
			caomaos << p;
		}

		QList<ServerPlayer*> drawers = room->askForPlayersChosen(player,caomaos,objectName(),0,999,"@zhushi-draw",false,false);
		if(drawers.isEmpty())return false;

		foreach(ServerPlayer*p,drawers){
			QString name = objectName();
			if(p->isWeidi())
				name = "weidi";
			LogMessage log;
			log.type = "#InvokeOthersSkill";
			log.from = player;
			log.arg = name;
			log.to << p;
			room->sendLog(log);
			p->peiyin(name);
			room->doAnimate(1,player->objectName(),p->objectName());
			room->notifySkillInvoked(p,name);
			room->addPlayerMark(p,"zhushi_used_" + player->objectName()+ "-Clear");
		}
		room->drawCards(drawers,1,objectName());
		return false;
	}
};

class TenyearSuifu : public PhaseChangeSkill
{
public:
	TenyearSuifu(): PhaseChangeSkill("tenyearsuifu")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;

		ServerPlayer*current = room->getCurrent();
		if(!current)return false;

		int one_damage = 0;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getPlayerSeat()== 1){
				one_damage = p->getMark("tenyearsuifu_damage-Clear");
				break;
			}
		}

		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(current->isKongcheng())break;
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(one_damage+p->getMark("tenyearsuifu_damage-Clear")>= 2){
				if(!p->askForSkillInvoke(this,current))continue;
				p->peiyin(this);
				CardMoveReason reason(CardMoveReason::S_REASON_PUT,current->objectName(),objectName(),"");
				DummyCard*handcards = current->wholeHandCards();
				room->moveCardTo(handcards,nullptr,Player::DrawPile,reason);

				if(p->isDead())continue;

				AmazingGrace*am = new AmazingGrace(Card::NoSuit,0);
				am->setSkillName("_tenyearsuifu");
				am->deleteLater();
				if(am->isAvailable(p))
					room->useCard(CardUseStruct(am,p));
			}
		}
		return false;
	}
};

class TenyearSuifuRecord : public TriggerSkill
{
public:
	TenyearSuifuRecord(): TriggerSkill("#tenyearsuifu")
	{
		events << DamageDone;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;
		int d = data.value<DamageStruct>().damage;
		room->addPlayerMark(player,"tenyearsuifu_damage-Clear",d);
		return false;
	}
};

class TenyearPijing : public PhaseChangeSkill
{
public:
	TenyearPijing(): PhaseChangeSkill("tenyearpijing")
	{
		waked_skills = "tenyearzimu";
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		QList<ServerPlayer*> targets = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,999,"@tenyearpijing-target",true);
		if(targets.isEmpty())return false;
		player->peiyin(this);
		QStringList names = player->getTag("TenyearPijingTargets").toStringList();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(names.contains(p->objectName()))
				room->handleAcquireDetachSkills(p,"-tenyearzimu");
		}
		if(!targets.contains(player)){
			targets << player;
			room->sortByActionOrder(targets);
		}
		names.clear();
		foreach(ServerPlayer*p,targets){
			if(p->isDead()|| p->hasSkill("tenyearzimu",true))continue;
			room->handleAcquireDetachSkills(p,"tenyearzimu");
			names << p->objectName();
		}
		player->setTag("TenyearPijingTargets", names);
		return false;
	}
};

FengyanCard::FengyanCard()
{
}

bool FengyanCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	if(user_string=="hp")
		return targets.isEmpty()&& to_select != Self&& to_select->getHp()<= Self->getHp()&& !to_select->isKongcheng();
	else {
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_fengyan");
		slash->deleteLater();
		return targets.isEmpty()&& to_select != Self&& to_select->getHandcardNum()<= Self->getHandcardNum()
		&& !Self->isProhibited(to_select,slash);
	}
	return false;
}

void FengyanCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->addPlayerMark(from,"fengyan_tiansuan_remove_" + user_string + "-PlayClear");

	if(user_string=="hp"){
		if(to->isKongcheng())return;
		const Card*c = room->askForExchange(to,"fengyan",1,1,false,"@fengyan-give");
		room->giveCard(to,from,c,"fengyan");
	} else {
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("_fengyan");
		slash->setFlags("YUANBEN");
		slash->deleteLater();
		if(from->canSlash(to,slash,false))
			room->useCard(CardUseStruct(slash,from,to));
	}
}

class Fengyan : public ZeroCardViewAsSkill
{
public:
	Fengyan(): ZeroCardViewAsSkill("fengyan")
	{
	}

	QDialog*getDialog()const
	{
		return TiansuanDialog::getInstance("fengyan","hp,hand");
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("fengyan_tiansuan_remove_hp-PlayClear")<= 0
			|| player->getMark("fengyan_tiansuan_remove_hand-PlayClear")<= 0;
	}

	const Card*viewAs()const
	{
		FengyanCard*c = new FengyanCard;
		c->setUserString(Self->getTag("fengyan").toString());
		return c;
	}
};

class Fudao : public TriggerSkill
{
public:
	Fudao(): TriggerSkill("fudao")
	{
		events << GameStart << TargetSpecified << Death << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart&&player->hasSkill(objectName())){
			ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"",false,true);
			if(to)room->setPlayerMark(to,"&fudao+#"+player->objectName(),1);
		} else if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1)return false;
			foreach(ServerPlayer*p,use.to){
				if(p->hasSkill(objectName())){
					if(use.from->getMark("&fudao+#"+p->objectName())>0&&use.from->getMark(p->objectName()+"fudao-Clear")<1){
						room->addPlayerMark(use.from,p->objectName()+"fudao-Clear");
						use.from->drawCards(2,objectName());
						p->drawCards(2,objectName());
					}
					if(use.card->isBlack()&&use.from->getMark("&fdjuelie+#"+p->objectName())>0)
						room->setPlayerCardLimitation(use.from,"use","..",true);
				}
				if(p->getMark("&fudao+#"+use.from->objectName())>0&&use.from->hasSkill(objectName())
					&&p->getMark(use.from->objectName()+"fudao-Clear")<1){
					room->addPlayerMark(p,use.from->objectName()+"fudao-Clear");
					p->drawCards(2,objectName());
					use.from->drawCards(2,objectName());
				}
			}
		} else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(death.who->hasSkill(objectName())&&p->getMark("&fudao+#"+death.who->objectName())>0)
						player->gainMark("&fdjuelie+#"+p->objectName());
					if(death.who->getMark("&fudao+#"+p->objectName())>0&&p->hasSkill(objectName()))
						player->gainMark("&fdjuelie+#"+p->objectName());
				}
			}
		} else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.to->getMark("&fdjuelie+#"+damage.from->objectName())>0){
				damage.damage++;
				data.setValue(damage);
			}
		}
		return false;
	}
};

TenyearNewShichouCard::TenyearNewShichouCard()
{
}

bool TenyearNewShichouCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.length()< Self->getLostHp()&& to_select->hasFlag("tenyearnewshichou_canchoose");
}

void TenyearNewShichouCard::use(Room*room,ServerPlayer*,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets)
		room->setPlayerFlag(p,"tenyearnewshichou_extra_target");
}

class TenyearNewShichouVS : public ZeroCardViewAsSkill
{
public:
	TenyearNewShichouVS(): ZeroCardViewAsSkill("tenyearnewshichou")
	{
		response_pattern = "@@tenyearnewshichou";
	}

	const Card*viewAs()const
	{
		return new TenyearNewShichouCard;
	}
};

class TenyearNewShichou : public TriggerSkill
{
public:
	TenyearNewShichou(): TriggerSkill("tenyearnewshichou")
	{
		events << CardUsed << CardFinished;
		view_as_skill = new TenyearNewShichouVS;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("Slash")|| player->getLostHp()<= 0)return false;
			bool can_invoke = false;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(use.to.contains(p)|| !use.card->targetFilter(QList<const Player*>(),p,player))continue;
				room->setPlayerFlag(p,"tenyearnewshichou_canchoose");
				can_invoke = true;
			}
			if(!can_invoke)return false;

			player->setTag("tenyearnewshichou_data", data);
			if(room->askForUseCard(player,"@@tenyearnewshichou","@tenyearnewshichou:" + use.card->objectName())){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->hasFlag("tenyearnewshichou_extra_target")){
						room->setPlayerFlag(p,"-tenyearnewshichou_extra_target");
						use.to.append(p);
					}
				}
				room->setCardFlag(use.card,"tenyearnewshichou_effect");
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
			foreach(ServerPlayer*p,room->getAlivePlayers())
				room->setPlayerFlag(p,"-tenyearnewshichou_canchoose");
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->hasFlag("tenyearnewshichou_effect")
				|| use.card->hasFlag("DamageDone"))return false;
			if(!room->CardInPlace(use.card,Player::DiscardPile))return false;
			room->obtainCard(player,use.card);
		}
		return false;
	}
};

class Pianchong : public DrawCardsSkill
{
public:
	Pianchong(): DrawCardsSkill("pianchong")
	{
		frequency = Frequent;
	}

	int getDrawNum(ServerPlayer*player,int n)const
	{
		Room*room = player->getRoom();
		if(room->askForSkillInvoke(player,objectName())){
			room->broadcastSkillInvoke("pianchong");
			QList<int> red,black;
			foreach(int id,room->getDrawPile()){
				if(Sanguosha->getCard(id)->isBlack())
					black << id;
				else if(Sanguosha->getCard(id)->isRed())
					red << id;
			}
			DummyCard*dummy = new DummyCard;
			if(!red.isEmpty())
				dummy->addSubcard(red.at(qrand()% red.length()));
			if(!black.isEmpty())
				dummy->addSubcard(black.at(qrand()% black.length()));
			if(dummy->subcardsLength()> 0)
				room->obtainCard(player,dummy);
			delete dummy;
			QString choice = room->askForChoice(player,objectName(),"red+black");
			if(choice=="red")
				room->addPlayerMark(player,"&pianchong+red");
			else
				room->addPlayerMark(player,"&pianchong+black");
			return -n;
		}
		return n;
	}
};

class PianchongEffect : public TriggerSkill
{
public:
	PianchongEffect(): TriggerSkill("#pianchong-effect")
	{
		events << CardsMoveOneTime << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()== Player::RoundStart){
				room->setPlayerMark(player,"&pianchong+red",0);
				room->setPlayerMark(player,"&pianchong+black",0);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(player != move.from)return false;
			if(!move.from_places.contains(Player::PlaceHand)&& !move.from_places.contains(Player::PlaceEquip))return false;
			QList<int> red,black;
			foreach(int id,room->getDrawPile()){
				if(Sanguosha->getCard(id)->isBlack())
					black << id;
				else if(Sanguosha->getCard(id)->isRed())
					red << id;
			}
			DummyCard*dummy = new DummyCard;
			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)== Player::PlaceHand || move.from_places.at(i)== Player::PlaceEquip){
					const Card*card = Sanguosha->getCard(move.card_ids.at(i));
					if(card->isRed()){
						int mark = move.from->getMark("&pianchong+red");
						for(int j = 0; j < mark; j++){
							if(black.isEmpty())break;
							int id = black.at(qrand()% black.length());
							black.removeOne(id);
							dummy->addSubcard(id);
						}
					} else if(card->isBlack()){
						int mark = move.from->getMark("&pianchong+black");
						for(int j = 0; j < mark; j++){
							if(red.isEmpty())break;
							int id = red.at(qrand()% red.length());
							red.removeOne(id);
							dummy->addSubcard(id);
						}
					}
				}
			}
			if(dummy->subcardsLength()> 0)
				room->obtainCard(player,dummy);
			delete dummy;
		}
		return false;
	}
};

ZunweiCard::ZunweiCard()
{
}

bool ZunweiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	QList<const Player*> all;
	foreach(const Player*p,Self->getAliveSiblings()){
		if(!Self->property("zunwei_draw").toBool()&& p->getHandcardNum()> Self->getHandcardNum())
			all << p;
		else if(!Self->property("zunwei_equip").toBool()&& p->getEquips().length()> Self->getEquips().length()&& Self->hasEquipArea())
			all << p;
		else if(!Self->property("zunwei_recover").toBool()&& p->getHp()> Self->getHp()&& Self->getLostHp()> 0)
			all << p;
	}
	return targets.isEmpty()&& all.contains(to_select);
}

void ZunweiCard::onEffect(CardEffectStruct&effect)const
{
	QStringList choices;
	if(!effect.from->property("zunwei_draw").toBool()&& effect.to->getHandcardNum()> effect.from->getHandcardNum())
		choices << "draw";
	if(!effect.from->property("zunwei_equip").toBool()&& effect.to->getEquips().length()> effect.from->getEquips().length()&& effect.from->hasEquipArea())
		choices << "equip";
	if(!effect.from->property("zunwei_recover").toBool()&& effect.to->getHp()> effect.from->getHp()&& effect.from->getLostHp()> 0)
		choices << "recover";
	if(choices.isEmpty())return;

	Room*room = effect.from->getRoom();
	QString choice = room->askForChoice(effect.from,"zunwei",choices.join("+"),QVariant::fromValue(effect.to));
	if(choice=="draw"){
		room->setPlayerProperty(effect.from,"zunwei_draw",true);
		int num = effect.to->getHandcardNum()- effect.from->getHandcardNum();
		num = qMin(num,5);
		effect.from->drawCards(num,"zunwei");
	} else if(choice=="recover"){
		room->setPlayerProperty(effect.from,"zunwei_recover",true);
		int recover = effect.to->getHp()- effect.from->getHp();
		recover = qMin(recover,effect.from->getMaxHp()- effect.from->getHp());
		room->recover(effect.from,RecoverStruct(effect.from,nullptr,recover,"zunwei"));
	} else {
	room->setPlayerProperty(effect.from,"zunwei_equip",true);
		QList<const Card*> equips;
		foreach(int id,room->getDrawPile()){
			const Card*card = Sanguosha->getCard(id);
			if(card->isKindOf("EquipCard")&& effect.from->canUse(card))
				equips << card;
		}
		if(equips.isEmpty())return;
		while(effect.to->getEquips().length()> effect.from->getEquips().length()){
			if(effect.from->isDead()||equips.isEmpty())break;
			const Card*equip = equips.at(qrand()% equips.length());
			equips.removeOne(equip);
			if(effect.from->canUse(equip))
				room->useCard(CardUseStruct(equip,effect.from));
		}
	}
}

class Zunwei : public ZeroCardViewAsSkill
{
public:
	Zunwei(): ZeroCardViewAsSkill("zunwei")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->hasUsed("ZunweiCard"))return false;
		return !player->property("zunwei_draw").toBool()||(!player->property("zunwei_recover").toBool()&& player->getLostHp()> 0)
			||(!player->property("zunwei_equip").toBool()&& player->hasEquipArea());
	}

	const Card*viewAs()const
	{
		return new ZunweiCard;
	}
};

BazhanCard::BazhanCard(QString bazhan): bazhan(bazhan)
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool BazhanCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	int n = Self->getChangeSkillState(bazhan);
	if(n==1)
		return targets.isEmpty()&& to_select != Self;
	else if(n==2)
		return targets.isEmpty()&& to_select != Self&& !to_select->isKongcheng();
	return false;
}

void BazhanCard::BazhanEffect(ServerPlayer*from,ServerPlayer*to)const
{
	if(from->isDead()|| to->isDead())return;
	QStringList choices;
	if(to->getLostHp()> 0)
		choices << "bazhan_recover=" + to->objectName();
	choices << "bazhan_reset=" + to->objectName()<< "cancel";
	Room*room = from->getRoom();
	QString choice = room->askForChoice(from,bazhan,choices.join("+"),QVariant::fromValue(to));
	if(choice=="cancel")return;
	if(choice.contains("recover"))
		room->recover(to,RecoverStruct(bazhan,from));
	else {
		if(to->isChained())
			room->setPlayerChained(to);

		if(!to->faceUp())
			to->turnOver();
	}
}

void BazhanCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from= effect.from;
	ServerPlayer*to = effect.to;
	Room*room = from->getRoom();
	int n = from->getChangeSkillState(bazhan);
	bool caneffect = false;

	if(n==1){
		room->setChangeSkillState(from,bazhan,2);
		room->giveCard(from,to,subcards,bazhan);
		foreach(int id,subcards){
			const Card*card = Sanguosha->getCard(id);
			if(card->isKindOf("Analeptic")|| card->getSuit()== Card::Heart){
				caneffect = true;
				break;
			}
		}
		if(caneffect)
			BazhanEffect(from,to);
	} else if(n==2){
		room->setChangeSkillState(from,bazhan,1);
		if(to->isKongcheng())return;

		int num = 1;
		if(bazhan=="secondbazhan"){
			QStringList choices;
			choices << "1";
			if(to->getHandcardNum()>= 2)
				choices << "2";
			num = room->askForChoice(from,bazhan,choices.join("+"),QVariant::fromValue(to)).toInt();
		};
		QList<int> cards;
		if(bazhan=="bazhan")
			cards << room->askForCardChosen(from,to,"h",bazhan);
		else if(bazhan=="secondbazhan"){

			for(int i = 0; i < num; i++){
				if(to->getHandcardNum()<=i)break;
				int id = room->askForCardChosen(from,to,"h",bazhan,false,Card::MethodNone,cards);
				if(id<0)break;
				cards << id;
			}
		}

		if(cards.isEmpty())return;

		DummyCard dummy(cards);
		room->obtainCard(from,&dummy);

		foreach(int id,cards){
			const Card*card = Sanguosha->getCard(id);
			if(card->isKindOf("Analeptic")|| card->getSuit()== Card::Heart){
				caneffect = true;
				break;
			}
		}
		if(caneffect)
			BazhanEffect(from,from);
	}
}

SecondBazhanCard::SecondBazhanCard(): BazhanCard("secondbazhan")
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

class Bazhan : public ViewAsSkill
{
public:
	Bazhan(const QString&bazhan): ViewAsSkill(bazhan),bazhan(bazhan)
	{
		change_skill = true;
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		int n = Self->getChangeSkillState(bazhan);
		int num = 1;
		if(bazhan=="secondbazhan")num = 2;
		if(n==1)
			return selected.length()< num&& !to_select->isEquipped();
		return false;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(bazhan=="bazhan")
			return !player->hasUsed("BazhanCard");
		else if(bazhan=="secondbazhan")
			return !player->hasUsed("SecondBazhanCard");
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int n = Self->getChangeSkillState(bazhan);
		if(n != 1&& n != 2)return nullptr;
		if(n==2&& cards.length()!= 0)return nullptr;
		if(n==1){
			if(bazhan=="bazhan"){
				if(cards.length()!= 1)
					return nullptr;
			} else if(bazhan=="secondbazhan"){
				if(cards.isEmpty()|| cards.length()> 2)
					return nullptr;
			}
		}

		if(bazhan=="bazhan"){
			BazhanCard*card = new BazhanCard;
			if(n==1)
				card->addSubcards(cards);
			return card;
		} else if(bazhan=="secondbazhan"){
			SecondBazhanCard*card = new SecondBazhanCard;
			if(n==1)
				card->addSubcards(cards);
			return card;
		}
		return nullptr;
	}
private:
	QString bazhan;
};

class Jiaoying : public TriggerSkill
{
public:
	Jiaoying(const QString&jiaoying): TriggerSkill(jiaoying),jiaoying(jiaoying)
	{
		events << EventPhaseChanging << CardUsed << CardResponded;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			QString property = jiaoying + "_targets";
			foreach(ServerPlayer*fanyufeng,room->getAllPlayers()){
				QStringList names = fanyufeng->property(property.toStdString().c_str()).toStringList();
				room->setPlayerProperty(fanyufeng,property.toStdString().c_str(),QStringList());
				if(fanyufeng->isDead()|| !fanyufeng->hasSkill(objectName()))continue;

				QList<ServerPlayer*> targets;
				foreach(QString name,names){
					ServerPlayer*target = room->findChild<ServerPlayer*>(name);
					if(!target || target->isDead()|| targets.contains(target))continue;
					targets << target;
				}
				if(targets.isEmpty())continue;
				room->sortByActionOrder(targets);

				foreach(ServerPlayer*p,targets){
					if(p->getMark(jiaoying + "_card-Clear")> 0)continue;
					ServerPlayer*drawer = room->askForPlayerChosen(fanyufeng,room->getAlivePlayers(),objectName(),"@" + jiaoying  + "-invoke",false,true);
					room->broadcastSkillInvoke(objectName());
					int num = qMin(5,drawer->getMaxHp())- drawer->getHandcardNum();
					if(jiaoying=="secondjiaoying")num = 5 - drawer->getHandcardNum();
					if(num > 0)
						drawer->drawCards(num,objectName());
				}
			}
			foreach(ServerPlayer*p,room->getAllPlayers(true)){
				QString property = jiaoying + "_colors";
				QStringList colors = p->property(property.toStdString().c_str()).toStringList();
				room->setPlayerProperty(p,property.toStdString().c_str(),QStringList());
				if(colors.isEmpty())continue;
				foreach(QString color,colors)
					room->removePlayerCardLimitation(p,"use,response",".|" + color + "|.|.$1");
			}
		} else {
			if(!room->hasCurrent())return false;
			const Card*card = nullptr;
			if(event==CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if(!card || card->isKindOf("SkillCard"))return false;
			if(player->getMark(jiaoying + "_effect-Clear")<= 0)return false;
			room->addPlayerMark(player,jiaoying + "_card-Clear");
		}
		return false;
	}
private:
	QString jiaoying;
};

class JiaoyingMove : public TriggerSkill
{
public:
	JiaoyingMove(const QString&jiaoying): TriggerSkill("#" + jiaoying + "-move"),jiaoying(jiaoying)
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(!move.to || !move.from || move.from != player || move.to==player || !move.from_places.contains(Player::PlaceHand)||
				move.to_place != Player::PlaceHand)return false;

		room->sendCompulsoryTriggerLog(player,jiaoying,true,true);

		QString property_t = jiaoying + "_targets",property_c = jiaoying + "_colors";
		QStringList names = player->property(property_t.toStdString().c_str()).toStringList();
		if(!names.contains(move.to->objectName())){
			names << move.to->objectName();
			room->setPlayerProperty(player,property_t.toStdString().c_str(),names);
			room->addPlayerMark((ServerPlayer*)move.to,jiaoying + "_effect-Clear");
		}

		for(int i = 0; i < move.card_ids.length(); i++){
			const Card*card = Sanguosha->getCard(move.card_ids.at(i));
			QString color;
			if(card->isRed())
				color = "red";
			else if(card->isBlack())
				color = "black";
			else
				continue;
			QStringList colors = move.to->property(property_c.toStdString().c_str()).toStringList();
			if(colors.contains(color))continue;
			colors << color;
			room->setPlayerProperty((ServerPlayer*)move.to,property_c.toStdString().c_str(),colors);
			room->setPlayerCardLimitation((ServerPlayer*)move.to,"use,response",".|" + color + "|.|.",true);
		}
		return false;
	}
private:
	QString jiaoying;
};

XingzuoCard::XingzuoCard()
{
	target_fixed = true;
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
}

void XingzuoCard::onUse(Room*,CardUseStruct&)const
{
}

class XingzuoVS : public ViewAsSkill
{
public:
	XingzuoVS(): ViewAsSkill("xingzuo")
	{
		expand_pile = "#xingzuo";
		response_pattern = "@@xingzuo";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(selected.length()< 2* Self->getPile("#xingzuo").length())
			return !to_select->isEquipped();

		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int hand = 0;
		int pile = 0;
		foreach(const Card*card,cards){
			if(Self->getHandcards().contains(card))
				hand++;
			else if(Self->getPile("#xingzuo").contains(card->getEffectiveId()))
				pile++;
		}

		if(hand==pile&& hand > 0){
			XingzuoCard*c = new XingzuoCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}
};

class Xingzuo : public PhaseChangeSkill
{
public:
	Xingzuo(): PhaseChangeSkill("xingzuo")
	{
		view_as_skill = new XingzuoVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Play)return false;
		if(!player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(objectName());

		QList<int> views = room->getNCards(3,false,false);

		LogMessage log;
		log.type = "$ViewEndDrawPile";
		log.from = player;
		log.arg = "3";
		log.card_str = ListI2S(views).join("+");
		room->sendLog(log,player);

		room->notifyMoveToPile(player,views,objectName(),Player::DrawPile,true);
		const Card*card = room->askForUseCard(player,"@@xingzuo","@xingzuo",-1,Card::MethodNone);
		room->notifyMoveToPile(player,views,objectName(),Player::DrawPile,false);
		room->returnToEndDrawPile(views);
		if(card){
			QList<int> get,hand;
			room->addPlayerMark(player,"xingzuo-Clear");
			foreach(int id,card->getSubcards()){
				if(views.contains(id))get << id;
				else hand << id;
			}
			if(!hand.isEmpty())
				room->moveCardsToEndOfDrawpile(player,hand,objectName(),false,true);
			if(!get.isEmpty()&& player->isAlive()){
				DummyCard dc(get);
				room->obtainCard(player,&dc,false);
			};
		}
		return false;
	}
};

class XingzuoFinish : public PhaseChangeSkill
{
public:
	XingzuoFinish(): PhaseChangeSkill("#xingzuo-finish")
	{
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive()&& target->getMark("xingzuo-Clear")> 0&& target->getPhase()== Player::Finish;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		int mark = player->getMark("xingzuo-Clear");
		for(int i = 0; i < mark; i++){
			if(player->isDead())return false;

			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->isKongcheng())continue;
				targets << p;
			}
			ServerPlayer*target = room->askForPlayerChosen(player,targets,"xingzuo","@xingzuo-invoke",true,true);
			if(!target)return false;
			room->broadcastSkillInvoke("xingzuo");

			QList<int> hands,cards = target->handCards();
			for(int i = 0; i < target->getHandcardNum(); i++){
				if(cards.isEmpty())break;
				int id = cards.at(qrand()% cards.length());
				cards.removeOne(id);
				hands << id;
			}

			if(hands.isEmpty())continue;

			cards = room->getNCards(3,false,false);
			room->returnToEndDrawPile(cards);
			room->moveCardsToEndOfDrawpile(target,hands,"xingzuo");
			if(target->isAlive()){
				DummyCard gett(cards);
				room->obtainCard(target,&gett,false);
			}
			if(hands.length()> 3&& player->isAlive())
				room->loseHp(HpLostStruct(player,1,"xingzuo",player));
		}
		return false;
	}
};

MiaoxianCard::MiaoxianCard()
{
	handling_method = Card::MethodUse;
}

bool MiaoxianCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string);
	card->setCanRecast(false);
	card->addSubcards(subcards);
	card->deleteLater();
	return card->targetFilter(targets,to_select,Self);
}

bool MiaoxianCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE){
		return true;
	}
	Card*card = Sanguosha->cloneCard(user_string);
	if(card){
		card->setCanRecast(false);
		card->setSkillName("miaoxian");
		card->addSubcards(subcards);
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool MiaoxianCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string);
	card->setSkillName("miaoxian");
	card->addSubcards(subcards);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetsFeasible(targets,Self);
}

const Card*MiaoxianCard::validate(CardUseStruct&card_use)const
{
	card_use.from->getRoom()->addPlayerMark(card_use.from,"miaoxian-Clear");
	Card*use_card = Sanguosha->cloneCard(user_string);
	use_card->setSkillName("miaoxian");
	use_card->addSubcards(subcards);
	use_card->deleteLater();
	return use_card;
}

const Card*MiaoxianCard::validateInResponse(ServerPlayer*source)const
{
	source->getRoom()->addPlayerMark(source,"miaoxian-Clear");
	Card*use_card = Sanguosha->cloneCard(user_string);
	use_card->setSkillName("miaoxian");
	use_card->addSubcards(subcards);
	use_card->deleteLater();
	return use_card;
}

class MiaoxianVS : public OneCardViewAsSkill
{
public:
	MiaoxianVS(): OneCardViewAsSkill("miaoxian")
	{
		filter_pattern = ".|black|.|hand";
        response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("miaoxian-Clear")> 0)return false;
		int black = 0;
		foreach(const Player*p,player->getAliveSiblings(true)){
			if(p->hasFlag("CurrentPlayer")){
				black = 1;
				break;
			}
		}
		if(black!=1)return false;
		foreach(const Card*card,player->getHandcards()){
			if(card->isBlack())black++;
		}
		return black==2;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(player->getMark("miaoxian-Clear")> 0)return false;
		int black = 0;
		foreach(const Player*p,player->getAliveSiblings(true)){
			if(p->hasFlag("CurrentPlayer")){
				black = 1;
				break;
			}
		}
		if(black!=1)return false;
		foreach(QString srt,pattern.split("+")){
			Card*c = Sanguosha->cloneCard(srt);
			if(!c)continue;
			c->deleteLater();
			if(c->isNDTrick()){
				black = 2;
				break;
			}
		}
		if(black!=2)return false;
		foreach(const Card*card,player->getHandcards()){
			if(card->isBlack())black++;
		}
		return black==3;
	}

	const Card*viewAs(const Card*originalCard)const
	{
		if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			MiaoxianCard*card = new MiaoxianCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			card->addSubcard(originalCard);
			return card;
		}

		const Card*c = Self->getTag("miaoxian").value<const Card*>();
		if(c&& c->isAvailable(Self)){
			MiaoxianCard*card = new MiaoxianCard;
			card->setUserString(c->objectName());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}
};

class Miaoxian : public TriggerSkill
{
public:
	Miaoxian(): TriggerSkill("miaoxian")
	{
		events << CardUsed << CardResponded;
		view_as_skill = new MiaoxianVS;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance("miaoxian",false);
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		const Card*card = nullptr;
		if(event==CardUsed)
			card = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(!res.m_isUse)return false;
			card = res.m_card;
		}
		if(!card || card->isKindOf("SkillCard")|| !card->isRed())return false;

		bool red = false;
		foreach(const Card*c,player->getHandcards()){
			if(!c->isRed())continue;
			red = true;
			break;
		}
		if(red)return false;
		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		player->drawCards(1,objectName());
		return false;
	}
};

class Youyan : public TriggerSkill
{
public:
	Youyan(): TriggerSkill("youyan")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getPhase()!= Player::Play&& player->getPhase()!= Player::Discard)return false;
		QString phase = QString::number(int(player->getPhase()));
		if(player->getMark("youyan-" + phase + "Clear")> 0)return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			if(move.from != player || !(move.from_places.contains(Player::PlaceHand)|| move.from_places.contains(Player::PlaceEquip)))
				return false;
			if(move.to_place != Player::DiscardPile)return false;

			QList<Card::Suit> suits;
			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)== Player::PlaceHand || move.from_places.at(i)== Player::PlaceEquip){
					const Card*card = Sanguosha->getCard(move.card_ids.at(i));
					if(!suits.contains(card->getSuit()))
						suits << card->getSuit();
				}
			}
			if(suits.length()>= 4)return false;

			if(!player->askForSkillInvoke(this,data))return false;
			room->broadcastSkillInvoke(this);
			room->addPlayerMark(player,"youyan-" + phase + "Clear");

			QList<const Card*> cards;
			foreach(int id,room->getDrawPile()){
				const Card*card = Sanguosha->getCard(id);
				if(suits.contains(card->getSuit()))continue;
				cards << card;
			}
			if(cards.isEmpty())return false;

			DummyCard*dummy = new DummyCard;
			dummy->deleteLater();

			while(!cards.empty()){
				const Card*card = cards.first();
				Card::Suit suit = card->getSuit();
				QList<const Card*> new_cards;
				foreach(const Card*c,cards){
					if(c->getSuit()!= suit)continue;
					new_cards << c;
					cards.removeOne(c);
				}
				const Card*cd = new_cards.at(qrand()% new_cards.length());
				dummy->addSubcard(cd);
			}

			if(dummy->subcardsLength()== 0)return false;
			room->obtainCard(player,dummy,true);
		}
		return false;
	}
};

class Zhuihuan : public PhaseChangeSkill
{
public:
	Zhuihuan(): PhaseChangeSkill("zhuihuan")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		ServerPlayer*target = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"",true,true);
		if(!target)return false;
		room->broadcastSkillInvoke(this);
		room->addPlayerMark(target,"&zhuihuan");
		return false;
	}
};

class ZhuihuanEffect : public TriggerSkill
{
public:
	ZhuihuanEffect(): TriggerSkill("#zhuihuan")
	{
		events << DamageDone << EventPhaseStart << Death;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to->isDead()|| damage.to->getMark("&zhuihuan")<= 0 || !damage.from)return false;
			QStringList names = damage.to->getTag("zhuihuan_damage_from").toStringList();
			if(names.contains(damage.from->objectName()))return false;
			names << damage.from->objectName();
			damage.to->setTag("zhuihuan_damage_from", names);
		} else if(event==EventPhaseStart){
			if(player->isDead()|| player->getPhase()!= Player::Start)return false;
			room->setPlayerMark(player,"&zhuihuan",0);
			QStringList names = player->getTag("zhuihuan_damage_from").toStringList();
			player->removeTag("zhuihuan_damage_from");

			bool effect = false;

			foreach(QString name,names){
				ServerPlayer*p = room->findChild<ServerPlayer*>(name);
				if(!p || p->isDead())continue;

				if(!effect){
					effect = true;
					LogMessage log;
					log.type = "#ZhenguEffect";
					log.arg = "zhuihuan";
					log.from = player;
					room->sendLog(log);
					room->broadcastSkillInvoke("zhuihuan");
				}

				if(p->getHp()> player->getHp())
					room->damage(DamageStruct("zhuihuan",player,p,2));
				else {
					DummyCard*dummy = new DummyCard();
					dummy->deleteLater();

					QList<int> discards;
					foreach(int id,p->handCards()){
						if(p->canDiscard(p,id))
							discards << id;
					}
					for(int i = 0; i < 2; i++){
						if(discards.isEmpty())break;
						int id = discards.at(qrand()% discards.length());
						discards.removeOne(id);
						dummy->addSubcard(id);
					}
					if(dummy->subcardsLength()== 0){
						LogMessage log;
						log.type = "#ZhuihuanCantDiscard";
						log.from = p;
						room->sendLog(log);
					} else
						room->throwCard(dummy,"zhuihuan",p);
				}
			}
		} else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != player)return false;
			death.who->removeTag("zhuihuan_damage_from");
		}
		return false;
	}
};

YuqiCard::YuqiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void YuqiCard::onUse(Room*,CardUseStruct&)const
{
}

class YuqiVS : public ViewAsSkill
{
public:
	YuqiVS(): ViewAsSkill("yuqi")
	{
		expand_pile = "#yuqi";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(!Self->getPile("#yuqi").contains(to_select->getEffectiveId()))return false;
		int num = Self->getMark("yuqi_help");
		return selected.length()< num;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;

		int num = Self->getMark("yuqi_help");
		if(cards.length()> num)return nullptr;

		YuqiCard*c = new YuqiCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*)const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.startsWith("@@yuqi");
	}
};

class Yuqi : public MasochismSkill
{
public:
	Yuqi(): MasochismSkill("yuqi")
	{
		view_as_skill = new YuqiVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		Room*room = player->getRoom();
		if(!room->hasCurrent())return;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead())return;
			if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("yuqi-Clear")>= 2)continue;

			int juli = p->getMark("SkillDescriptionArg1_yuqi");
			juli = qMax(0,juli);
			if(p->distanceTo(player)> juli)continue;

			if(!p->askForSkillInvoke(this,player))continue;
			room->broadcastSkillInvoke(this);

			room->addPlayerMark(p,"yuqi-Clear");

			int guankan = p->getMark("SkillDescriptionArg2_yuqi");
			guankan = guankan==0 ? 3 : guankan;
			guankan = qMin(guankan,5);
			guankan = qMax(3,guankan);

			int jiaogei = p->getMark("SkillDescriptionArg3_yuqi");
			jiaogei = jiaogei==0 ? 1 : jiaogei;
			jiaogei = qMin(jiaogei,5);
			jiaogei = qMax(1,jiaogei);

			//QList<int> views = room->showDrawPile(p,guankan,objectName(),false);
			QList<int> views = room->getNCards(guankan);
			LogMessage log;
			log.type = "$TurnOver";
			log.from = player;
			log.card_str = ListI2S(views).join("+");
			room->sendLog(log,p);

			room->setPlayerMark(p,"yuqi_help",jiaogei);
			room->notifyMoveToPile(p,views,objectName(),Player::DrawPile,true);
			const Card*card = room->askForUseCard(p,"@@yuqi1","@yuqi1:" + player->objectName()+ "::" + QString::number(jiaogei),1,Card::MethodNone);
			room->notifyMoveToPile(p,views,objectName(),Player::DrawPile,false);
			room->returnToTopDrawPile(views);

			QList<int> gives;
			if(card)
				gives = card->getSubcards();

			if(!gives.isEmpty()){
				foreach(int id,gives)
					views.removeOne(id);
				room->giveCard(p,player,gives,objectName());
			}

			if(p->isDead()|| views.isEmpty())continue;

			int huode = p->getMark("SkillDescriptionArg4_yuqi");
			huode = huode==0 ? 1 : huode;
			huode = qMin(huode,5);
			huode = qMax(1,huode);

			room->setPlayerMark(p,"yuqi_help",huode);
			room->notifyMoveToPile(p,views,objectName(),Player::DrawPile,true);
			const Card*card2 = room->askForUseCard(p,"@@yuqi2","@yuqi2:" + QString::number(huode),2,Card::MethodNone);
			room->notifyMoveToPile(p,views,objectName(),Player::DrawPile,false);

			if(!card2)continue;
			room->obtainCard(p,card2,false);
		}
	}
};

class Shanshen : public TriggerSkill
{
public:
	Shanshen(): TriggerSkill("shanshen")
	{
		events << Death;
		frequency = Frequent;
	}

	static QStringList YuqiAddNumChoices(ServerPlayer*p,int num)
	{
		QStringList choices;

		int juli = qMin(p->getMark("SkillDescriptionArg1_yuqi"),5);
		juli = qMax(0,juli);

		int guankan = p->getMark("SkillDescriptionArg2_yuqi");
		guankan = guankan==0 ? 3 : guankan;
		guankan = qMin(guankan,5);
		guankan = qMax(3,guankan);

		int jiaogei = p->getMark("SkillDescriptionArg3_yuqi");
		jiaogei = jiaogei==0 ? 1 : jiaogei;
		jiaogei = qMin(jiaogei,5);
		jiaogei = qMax(1,jiaogei);

		int huode = p->getMark("SkillDescriptionArg4_yuqi");
		huode = huode==0 ? 1 : huode;
		huode = qMin(huode,5);
		huode = qMax(1,huode);

		if(juli < 5){
			juli += num;
			juli = qMin(5,juli);
			choices << "juli=" + QString::number(juli);
		}
		if(guankan < 5){
			guankan += num;
			guankan = qMin(5,guankan);
			choices << "guankan=" + QString::number(guankan);
		}
		if(jiaogei < 5){
			jiaogei += num;
			jiaogei = qMin(5,jiaogei);
			choices << "jiaogei=" + QString::number(jiaogei);
		}
		if(huode < 5){
			huode += num;
			huode = qMin(5,huode);
			choices << "huode=" + QString::number(huode);
		}

		return choices;
	}

	static void YuqiAddNum(ServerPlayer*p,int num,const QString&skill)
	{
		Room*room = p->getRoom();

		QStringList choices = YuqiAddNumChoices(p,num);
		if(choices.isEmpty())return;

		QString choice = room->askForChoice(p,skill,choices.join("+"));

		int number = 1,least = 0;
		if(choice.startsWith("guankan")){
			least = 3;
			number = 2;
		} else if(choice.startsWith("jiaogei")){
			least = 1;
			number = 3;
		} else if(choice.startsWith("huode")){
			least = 1;
			number = 4;
		}
		p->setSkillDescriptionSwap("yuqi","%arg1",QString::number(p->getMark("SkillDescriptionArg1_yuqi")));

		if(p->getMark("SkillDescriptionArg2_yuqi")< 1){
			room->setPlayerMark(p,"SkillDescriptionArg2_yuqi",3);
			p->setSkillDescriptionSwap("yuqi","%arg2","3");
		}
		if(p->getMark("SkillDescriptionArg3_yuqi")< 1){
			room->setPlayerMark(p,"SkillDescriptionArg3_yuqi",1);
			p->setSkillDescriptionSwap("yuqi","%arg3","1");
		}
		if(p->getMark("SkillDescriptionArg4_yuqi")< 1){
			room->setPlayerMark(p,"SkillDescriptionArg4_yuqi",1);
			p->setSkillDescriptionSwap("yuqi","%arg4","1");
		}

		QString mark_name = "SkillDescriptionArg" + QString::number(number)+ "_yuqi";
		int mark = p->getMark(mark_name);
		mark = qMax(mark,least);
		mark += num;
		mark = qMin(5,mark);
		room->setPlayerMark(p,mark_name,mark);
		p->setSkillDescriptionSwap("yuqi","%arg"+QString::number(number),QString::number(mark));

		room->changeTranslation(p,"yuqi",2);
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(!death.who || death.who==player || YuqiAddNumChoices(player,2).isEmpty()|| !player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(this);

		YuqiAddNum(player,2,objectName());

		if(player->isAlive()&& !player->getTag("kuimang_damage_" + death.who->objectName()).toBool())
			room->recover(player,RecoverStruct("shanshen",player));
		return false;
	}
};

class Xianjing : public PhaseChangeSkill
{
public:
	Xianjing(): PhaseChangeSkill("xianjing")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start || Shanshen::YuqiAddNumChoices(player,1).isEmpty()|| !player->askForSkillInvoke(this))return false;
		room->broadcastSkillInvoke(this);
		Shanshen::YuqiAddNum(player,1,objectName());
		if(player->getLostHp()> 0)return false;
		Shanshen::YuqiAddNum(player,1,objectName());
		return false;
	}
};

JiqiaosyCard::JiqiaosyCard()
{
	will_throw = false;
	target_fixed = true;
	m_skillName = "jiqiaosy";
	mute = true;
}

void JiqiaosyCard::onUse(Room*,CardUseStruct&)const
{
}

class JiqiaosyVS : public OneCardViewAsSkill
{
public:
	JiqiaosyVS(): OneCardViewAsSkill("jiqiaosy")
	{
		expand_pile = "jiqiaosy";
		filter_pattern = ".|.|.|jiqiaosy";
		response_pattern = "@@jiqiaosy!";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		JiqiaosyCard*card = new JiqiaosyCard();
		card->addSubcard(originalCard);
		return card;
	}
};

class Jiqiaosy : public TriggerSkill
{
public:
	Jiqiaosy(): TriggerSkill("jiqiaosy")
	{
		events << EventPhaseStart << CardFinished;
		view_as_skill = new JiqiaosyVS;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!= Player::Play)return false;
			int maxhp = player->getMaxHp();
			if(maxhp <= 0 || !player->askForSkillInvoke(this,"jiqiaosy:" + QString::number(maxhp)))return false;
			room->broadcastSkillInvoke(this);
			player->addToPile(objectName(),room->getNCards(maxhp));
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard"))return false;
			QList<int> jiqiaosy = player->getPile(objectName());
			if(jiqiaosy.isEmpty())return false;

			room->sendCompulsoryTriggerLog(player,this);

			int id = -1;
			if(jiqiaosy.length()== 1)
				id = jiqiaosy.first();
			else {
				const Card*card = room->askForUseCard(player,"@@jiqiaosy!","@jiqiaosy",-1,Card::MethodNone);
				if(card)id = card->getSubcards().first();
				else id = jiqiaosy.at(qrand()% jiqiaosy.length());
			}
			if(id < 0)return false;

			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = player;
			log.arg = "jiqiaosy";
			log.card_str = QString::number(id);
			room->sendLog(log);
			room->obtainCard(player,id);

			if(player->isDead())return false;

			int red = 0,black = 0;
			foreach(int id,player->getPile(objectName())){
				const Card*card = Sanguosha->getCard(id);
				if(card->isRed())
					red++;
				else if(card->isBlack())
					black++;
			}

			if(red==black)
				room->recover(player,RecoverStruct(objectName(),player));
			else
				room->loseHp(HpLostStruct(player,1,objectName(),player));
		}
		return false;
	}
};

class JiqiaosyEnter : public TriggerSkill
{
public:
	JiqiaosyEnter(): TriggerSkill("#jiqiaosy")
	{
		events << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& !target->getPile("jiqiaosy").isEmpty()&& target->getPhase()== Player::Play;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		room->sendCompulsoryTriggerLog(player,"jiqiaosy",true,true);
		player->clearOnePrivatePile("jiqiaosy");
		return false;
	}
};

class Xiongyisy : public TriggerSkill
{
public:
	Xiongyisy(): TriggerSkill("xiongyisy")
	{
		events << AskForPeaches;
		frequency = Limited;
		limit_mark = "@xiongyisyMark";
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dy = data.value<DyingStruct>();
		if(dy.who != player)return false;
		if(player->getMark("@xiongyisyMark")<= 0)return false;
		if(!player->askForSkillInvoke(this,data))return false;

		player->peiyin(this);
		room->doSuperLightbox(player,"xiongyisy");
		room->removePlayerMark(player,"@xiongyisyMark");

		bool xushi = false;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(Sanguosha->translate(p->getGeneralName()).contains("徐氏")||
					(p->getGeneral2()&& Sanguosha->translate(p->getGeneral2Name()).contains("徐氏"))){
				xushi = true;
				break;
			}
		}

		int n = -1;
		if(!xushi){
			n = qMin(3,player->getMaxHp())- player->getHp();
			if(n > 0)
				room->recover(player,RecoverStruct(player,nullptr,n,objectName()));
			room->setPlayerProperty(player,"ChangeHeroMaxHp",player->getMaxHp()+ 1);
			room->changeHero(player,"xushi",false,false);
			if(player->getPile("jiqiaosy").isEmpty())return false;
			player->clearOnePrivatePile("jiqiaosy");
		} else {
			n = qMin(1,player->getMaxHp())- player->getHp();
			if(n > 0)
				room->recover(player,RecoverStruct(player,nullptr,n,objectName()));
			room->acquireSkill(player,"hunzi");
		}
		return false;
	}
};

class Tiqi : public TriggerSkill
{
public:
	Tiqi(): TriggerSkill("tiqi")
	{
		events  << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(data.value<PhaseChangeStruct>().to != Player::Play || player->isSkipped(Player::Play))return false;
		int mark = qAbs(2 - player->getMark("tiqi_record-Clear"));
		if(mark==0)return false;

		QString _mark = QString::number(mark);
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;
			if(!p->askForSkillInvoke(this,"tiqi:" + _mark))continue;
			p->peiyin(objectName());
			p->drawCards(mark,objectName());
			if(player->isAlive()&& p->isAlive()){
				QStringList choices;
				choices << "zeng=" + player->objectName()+ "=" + _mark << "jian=" + player->objectName()+ "=" + _mark << "cancel";
				QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(player));
				if(choice=="cancel")continue;

				LogMessage log;
				log.type = "#TiqiMaxCards" + choice.split("=").first();
				log.from = p;
				log.to << player;
				log.arg = _mark;
				room->sendLog(log);

				int num = mark;
				if(choice.startsWith("jian"))
					num = -num;
				room->addMaxCards(player,num);
			}
		}
		return false;
	}
};

class TiqiRecord : public TriggerSkill
{
public:
	TiqiRecord(): TriggerSkill("#tiqi")
	{
		events  << EventPhaseStart << CardsMoveOneTime;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Draw)
				player->setMark("tiqi_record-Clear",0);
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&player->getPhase()==Player::Draw&&move.reason.m_reason==CardMoveReason::S_REASON_DRAW){
				for(int i = 0; i < move.card_ids.length(); i++){
					if(move.from_places.at(i)== Player::DrawPile)
						player->addMark("tiqi_record-Clear");
				}
			}
		}
		return false;
	}
};

BaoshuCard::BaoshuCard()
{
}

bool BaoshuCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*Self)const
{
	return targets.length()< Self->getMaxHp();
}

void BaoshuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	int y = targets.length();
	room->setCardFlag(this,"baoshu_num_y_" + QString::number(y));
	foreach(ServerPlayer*p,targets){
		if(p->isAlive())
			room->cardEffect(this,source,p);
	}
}

void BaoshuCard::onEffect(CardEffectStruct&effect)const
{
	int y = 0;
	foreach(QString flag,getFlags()){
		if(!flag.startsWith("baoshu_num_y_"))continue;
		QStringList flags = flag.split("_");
		if(flags.length()!= 4)continue;
		y = flags.last().toInt();
		break;
	}
	y = qMax(y,1);
	int num = effect.from->getMaxHp()- y + 1;
	num = qMax(1,num);
	effect.to->getRoom()->addPlayerMark(effect.to,"&fybsshu",num);
}

class BaoshuVS : public ZeroCardViewAsSkill
{
public:
	BaoshuVS(): ZeroCardViewAsSkill("baoshu")
	{
		response_pattern = "@@baoshu";
	}

	const Card*viewAs()const
	{
		return new BaoshuCard;
	}
};

class Baoshu : public PhaseChangeSkill
{
public:
	Baoshu(): PhaseChangeSkill("baoshu")
	{
		view_as_skill = new BaoshuVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		if(player->getMaxHp()<= 0)return false;
		room->askForUseCard(player,"@@baoshu","@baoshu",-1,Card::MethodNone);
		return false;
	}
};

class BaoshuDraw : public TriggerSkill
{
public:
	BaoshuDraw(): TriggerSkill("#baoshu")
	{
		events  << DrawNCards << AfterDrawNCards;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->getMark("&fybsshu")> 0;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="draw_phase")return false;
		if(event==DrawNCards){
			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = "baoshu";
			room->sendLog(log);
			draw.num += player->getMark("&fybsshu");
			data = QVariant::fromValue(draw);
		} else
			player->loseAllMarks("&fybsshu");
		return false;
	}
};

XiaowuCard::XiaowuCard()
{
	target_fixed = true;
	//mute = true;
}

void XiaowuCard::onUse(Room*room,CardUseStruct&use)const
{
	ServerPlayer*source = use.from;

	QStringList choices;
	ServerPlayer*next = source->getNextAlive(),*last = source->getNextAlive(room->alivePlayerCount()- 1),*start,*end;
	choices << "shangjia";
	if(next != last)
		choices << "xiajia";
	QString choice = room->askForChoice(source,"xiaowu",choices.join("+"));
	start = choice=="shangjia" ? last : next;
	end = room->askForPlayerChosen(source,room->getOtherPlayers(start),"xiaowu","@xiaowu-end",false,false);

	QList<ServerPlayer*> players;
	players << start;
	while(start != end){
		ServerPlayer*next = start->getNextAlive();
		players << next;
		start = next;
	}
	if(players.isEmpty())return;
	//room->sortByActionOrder(players);

	RoomThread*thread = room->getThread();

	use.to = players;
	QVariant data = QVariant::fromValue(use);

	thread->trigger(PreCardUsed,room,source,data);
	use = data.value<CardUseStruct>();

	//source->peiyin("xiaowu");

	LogMessage log;
	log.from = source;
	log.to << use.to;
	log.type = "#UseCard";
	log.card_str = toString();
	room->sendLog(log);

	thread->trigger(CardUsed,room,use.from,data);
	use = data.value<CardUseStruct>();
	thread->trigger(CardFinished,room,use.from,data);
}

void XiaowuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	int you = 0,self = 0;
	QStringList choices;
	choices << "draw=" + source->objectName()<< "selfdraw";
	QList<ServerPlayer*> players;

	foreach(ServerPlayer*p,targets){
		if(p->isDead())continue;
		if(source->isDead()){
			choices.clear();
			choices << "selfdraw";
		}
		QStringList tips;
		tips << "tip=" + source->objectName()+ "=" + QString::number(you)+ "=" + QString::number(self);

		QString choice = room->askForChoice(p,"xiaowu",choices.join("+"),QVariant::fromValue(source),tips.join("+"));
		if(choice=="selfdraw"){
			p->drawCards(1,"xiaowu");
			self++;
			players << p;
		} else {
			source->drawCards(1,"xiaowu");
			you++;
		}
	}
	if(source->isDead()|| you==self)return;

	if(you > self)
		source->gainMark("&lyexwsha");
	else {
		foreach(ServerPlayer*p,players)
			room->damage(DamageStruct("xiaowu",source,p));
	}
}

class Xiaowu : public ZeroCardViewAsSkill
{
public:
	Xiaowu(): ZeroCardViewAsSkill("xiaowu")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("XiaowuCard");
	}

	const Card*viewAs()const
	{
		return new XiaowuCard;
	}
};

class Huaping : public TriggerSkill
{
public:
	Huaping(): TriggerSkill("huaping")
	{
		events << Death;
		frequency = Limited;
		limit_mark = "@huapingMark";
		waked_skills = "shawu";
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->hasSkill(objectName())&& target->getMark("@huapingMark")> 0;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(!death.who)return false;

		if(death.who != player){
			if(!player->askForSkillInvoke(this,death.who))return false;
			player->peiyin(this);
			room->doSuperLightbox(player,"huaping");
			room->removePlayerMark(player,"@huapingMark");

			QStringList skills;
			foreach(const Skill*sk,death.who->getVisibleSkillList()){
				if(!player->hasSkill(sk,true)&& !sk->inherits("SPConvertSkill")&& !sk->isAttachedLordSkill()&&
						!skills.contains(sk->objectName()))
					skills << sk->objectName();
			}
			room->handleAcquireDetachSkills(player,skills);
			room->detachSkillFromPlayer(player,"xiaowu");
			int mark = player->getMark("&lyexwsha");
			if(mark > 0){
				player->loseAllMarks("&lyexwsha");
				player->drawCards(mark,objectName());
			}
		} else {
			if(player->getTag("HuapingInvoke").toBool())return false;
			ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@huaping-target",true,true);
			if(!t)return false;
			player->peiyin(this);
			room->doSuperLightbox(player,"huaping");
			room->removePlayerMark(player,"@huapingMark");
			room->acquireSkill(t,"shawu");
			int mark = player->getMark("&lyexwsha");
			if(mark > 0)
				t->gainMark("&lyexwsha",mark);
		}
		return false;
	}
};

class HuapingInvoke : public TriggerSkill
{
public:
	HuapingInvoke(): TriggerSkill("#huaping")
	{
		events << ChoiceMade;
		frequency = Limited;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*,ServerPlayer*player,QVariant&data)const
	{
		if(data.toString().isEmpty())return false;
		QStringList invoke = data.toString().split(":");
		if(invoke[1]=="skillInvoke"){
			if(invoke[2]=="huaping"&& invoke.last()== "yes")
				player->setTag("HuapingInvoke", true);
		} else if(invoke[1]=="playerChosen"){
			if(invoke[2]=="huaping")
				player->setTag("HuapingInvoke", true);
		}
		return false;
	}
};

ShawuCard::ShawuCard()
{
	target_fixed = true;
}

void ShawuCard::use(Room*,ServerPlayer*,QList<ServerPlayer*>&)const
{
}

class ShawuVS : public ViewAsSkill
{
public:
	ShawuVS(): ViewAsSkill("shawu")
	{
		response_pattern = "@@shawu";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(to_select->isEquipped())return false;
		return selected.length()< 2&& !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Self->getMark("&lyexwsha")<= 0&& cards.isEmpty())return nullptr;
		if(cards.isEmpty())
			return new ShawuCard;

		if(cards.length()!= 2)return nullptr;
		ShawuCard*card = new ShawuCard;
		card->addSubcards(cards);
		return card;
	}
};

class Shawu : public TriggerSkill
{
public:
	Shawu(): TriggerSkill("shawu")
	{
		events << TargetSpecified;
		view_as_skill = new ShawuVS;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isKindOf("Slash"))return false;

		foreach(ServerPlayer*p,use.to){
			if(player->isDead())return false;
			if(player->getHandcardNum()< 2&& player->getMark("&lyexwsha")<= 0)return false;
			if(p->isDead())continue;

			player->setTag("ShawuTarget", QVariant::fromValue(p));
			const Card*c = room->askForUseCard(player,"@@shawu","@shawu:" + p->objectName());
			player->removeTag("ShawuTarget");

			if(!c)continue;

			if(c->subcardsLength()== 0)
				player->loseMark("&lyexwsha");
			room->damage(DamageStruct(objectName(),player->isAlive()? player : nullptr,p));
			if(c->subcardsLength()== 0)
				player->drawCards(2,objectName());
		}
		return false;
	}
};

class Caiyi : public PhaseChangeSkill
{
public:
	Caiyi(): PhaseChangeSkill("caiyi")
	{
		change_skill = true;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		int n = player->getChangeSkillState(objectName());

		if(n==1){
			QString remove = player->property("SkillDescriptionRecord_caiyi").toString();
			QStringList removes;
			if(!remove.isEmpty())removes = remove.split("+");
			if(removes.length()> 3)return false;

			ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"@caiyi-target",true,true);
			if(!t)return false;
			player->peiyin(this);

			int n = 4 - removes.length();
			QString num = QString::number(n);

			QStringList all_choices,choices,removes_copy = removes;
			all_choices << "recover" << "draw" << "fuyuan" << "random1";
			foreach(QString cho,all_choices){
				if(removes.contains("caiyi_" + cho))continue;
				if(cho=="recover"&& !t->isWounded())continue;
				if(cho=="random1"&& removes.isEmpty())continue;
				choices << cho + "=" + num;
			}
			if(choices.isEmpty()){
				room->setChangeSkillState(player,objectName(),2);
				return false;
			}

			QString choice = room->askForChoice(t,objectName(),choices.join("+"));
			QString choice_copy = choice.split("=").first();

			if(choice.startsWith("random1")){
				if(!t->isWounded())
					removes_copy.removeOne("caiyi_recover");
				if(removes_copy.isEmpty())
					choice = "";
				else
					choice = removes_copy.at(qrand()% removes_copy.length());
			}

			if(choice.startsWith("recover"))
				room->recover(t,RecoverStruct(player,nullptr,qMin(n,t->getMaxHp()- t->getHp()),"caiyi"));
			else if(choice.startsWith("draw"))
				t->drawCards(n,objectName());
			else if(choice.startsWith("fuyuan")){
				room->setPlayerChained(t,false);
				if(!t->faceUp())
					t->turnOver();
			}

			removes << "caiyi_" + choice_copy;
			room->setPlayerProperty(player,"SkillDescriptionRecord_caiyi",removes.join("+"));
			all_choices.clear();
			foreach(QString cho,removes){
				all_choices << cho << "|";
			}
			player->setSkillDescriptionSwap(objectName(),"%arg11",all_choices.join("+"));
			room->setChangeSkillState(player,objectName(),2);
		} else {
			QString remove = player->property("SkillDescriptionChoiceRecord1_caiyi").toString();
			QStringList removes;
			if(!remove.isEmpty())
				removes = remove.split("+");
			if(removes.length()> 3)return false;

			ServerPlayer*t = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"@caiyi-target",true,true);
			if(!t)return false;
			player->peiyin(this);

			int n = 4 - removes.length();
			QString num = QString::number(n);

			QStringList all_choices,choices,removes_copy = removes;
			all_choices << "damage" << "discard" << "turn" << "random2";
			foreach(QString cho,all_choices){
				if(removes.contains(cho))continue;
				if(cho=="discard"&& !t->canDiscard(t,"he"))continue;
				if(cho=="random2"&& removes.isEmpty())continue;
				choices << cho + "=" + num;
			}
			if(choices.isEmpty()){
				room->setChangeSkillState(player,objectName(),1);
				return false;
			}

			QString choice = room->askForChoice(t,objectName(),choices.join("+"));
			QString choice_copy = choice.split("=").first();

			if(choice.startsWith("random2")){
				if(!t->canDiscard(t,"he"))
					removes_copy.removeOne("discard");
				if(removes_copy.isEmpty())
					choice = "";
				else
					choice = removes_copy.at(qrand()% removes_copy.length());
			}

			if(choice.startsWith("damage"))
				room->damage(DamageStruct(objectName(),nullptr,t,n));
			else if(choice.startsWith("discard"))
				room->askForDiscard(t,objectName(),n,n,false,true);
			else if(choice.startsWith("turn")){
				t->turnOver();
				room->setPlayerChained(t,true);
			}

			removes << choice_copy;
			room->setPlayerProperty(player,"SkillDescriptionChoiceRecord1_caiyi",removes.join("+"));
			all_choices.clear();
			foreach(QString cho,removes){
				all_choices << cho << "|";
			}
			player->setSkillDescriptionSwap(objectName(),"%arg21",all_choices.join("+"));
			room->setChangeSkillState(player,objectName(),1);
		}
		return false;
	}
};

class Guili : public PhaseChangeSkill
{
public:
	Guili(): PhaseChangeSkill("guili")
	{
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()== Player::RoundStart){
			player->addMark("guiliRound-Keep");
			if(player->getMark("guiliRound-Keep")==1&&player->hasSkill(objectName())){
				ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"@guili-target",false,true);
				player->peiyin(this);
				room->addPlayerMark(t,"&guili_target+#" + player->objectName());
			}
		}else if(player->getPhase()==Player::NotActive&&player->getMark("guili_first_turn_lun")<1){
			room->addPlayerMark(player,"guili_first_turn_lun");
			if(player->getMark("damage_point_round")> 0)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				for(int i = 0; i < player->getMark("&guili_target+#" + p->objectName()); i++){
					if(p->isDead()||!p->hasSkill(objectName(),true))continue;
					room->sendCompulsoryTriggerLog(p,objectName());
					p->gainAnExtraTurn();
				}
			}
		}
		return false;
	}
};

class TenyearAocaiViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearAocaiViewAsSkill(): ZeroCardViewAsSkill("tenyearaocai")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasFlag("CurrentPlayer");
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(player->hasFlag("CurrentPlayer")|| player->hasFlag("Global_TenyearAocaiFailed"))return false;/*
		if(pattern.contains("slash")|| pattern.contains("Slash"))
			return Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		else if(pattern=="peach")
			return player->getMark("Global_PreventPeach")== 0;
		else if(pattern.contains("analeptic"))
			return true;*/
		if(pattern=="@@tenyearaocai")return true;
		foreach(QString cn,pattern.split("+")){
			Card*c = Sanguosha->cloneCard(cn);
			if(c){
				c->deleteLater();
				if(c->getTypeId()==1)
					return true;
			}
		}
		return false;
	}

	const Card*viewAs()const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@tenyearaocai"){
			int id = Self->getMark("aocaiId");
			return Sanguosha->getCard(id);
		}
		TenyearAocaiCard*aocai_card = new TenyearAocaiCard;
		aocai_card->setUserString(pattern);
		return aocai_card;
	}
};

class TenyearAocai : public TriggerSkill
{
public:
	TenyearAocai(): TriggerSkill("tenyearaocai")
	{
		//events << CardAsked;
		events << CardUsed;
		view_as_skill = new TenyearAocaiViewAsSkill;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(triggerEvent==CardUsed){
			foreach(ServerPlayer*p,room->getPlayers()){
				room->setPlayerFlag(p,"-Global_TenyearAocaiFailed");
			}
			return false;
		}
		QString pattern = data.toStringList().first();
		if(player!=room->getCurrent()
			&&(pattern.contains("slash")|| pattern.contains("Slash")|| pattern=="jink")
			&& room->askForSkillInvoke(player,objectName(),data)){
			int num = 2;
			if(player->isKongcheng())
				num = 4;
			QList<int> ids = room->getNCards(num);
			QList<int> enabled,disabled;
			foreach(int id,ids){
				if(Sanguosha->getCard(id)->objectName().contains(pattern))
					enabled << id;
				else
					disabled << id;
			}
			int id = TenyearAocai::view(room,player,ids,enabled,disabled);
			if(id > -1){
				const Card*card = Sanguosha->getCard(id);
				room->provide(card);
				return true;
			}
		}
		return false;
	}

	static int view(Room*room,ServerPlayer*player,QList<int>&ids,QList<int>&enabled,QList<int>&disabled)
	{
		int result = -1,index = -1;
		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.card_str = ListI2S(ids).join("+");
		room->sendLog(log,player);

		room->broadcastSkillInvoke("tenyearaocai");
		room->notifySkillInvoked(player,"tenyearaocai");
		if(enabled.isEmpty()){
			JsonArray arg;
			arg << "." << false << JsonUtils::toJsonArray(ids);
			room->doNotify(player,QSanProtocol::S_COMMAND_SHOW_ALL_CARDS,arg);
		} else {
			room->fillAG(ids,player,disabled);
			int id = room->askForAG(player,enabled,true,"tenyearaocai");
			if(id > -1){
				index = ids.indexOf(id);
				ids.removeOne(id);
				result = id;
			}
			room->clearAG(player);
		}

		room->returnToTopDrawPile(ids);
		if(result==-1)
			room->setPlayerFlag(player,"Global_TenyearAocaiFailed");
		else {
			LogMessage log;
			log.type = "#AocaiUse";
			log.from = player;
			log.arg = "tenyearaocai";
			log.arg2 = QString::number(index + 1);
			room->sendLog(log);
		}
		return result;
	}
};

TenyearAocaiCard::TenyearAocaiCard()
{
}

bool TenyearAocaiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets,to_select,Self);
	}
	return false;
}

bool TenyearAocaiCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;

	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool TenyearAocaiCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets,Self);
	}
	return false;
}

const Card*TenyearAocaiCard::validateInResponse(ServerPlayer*user)const
{
	Room*room = user->getRoom();
	int num = 2;
	if(user->isKongcheng())
		num = 4;
	QList<int> ids = room->getNCards(num);
	QStringList names = user_string.split("+");
	//if(names.contains("slash"))names << "fire_slash" << "thunder_slash";

	QList<int> enabled,disabled;
	foreach(int id,ids){
		const Card*c = Sanguosha->getCard(id);
		if(user->isCardLimited(c,Card::MethodResponse)){
			disabled << id;
			continue;
		}
		foreach(QString cn,names){
			if(c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if(enabled.contains(id))continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = user;
	log.arg = "tenyearaocai";
	room->sendLog(log);

	int id = TenyearAocai::view(room,user,ids,enabled,disabled);
	return Sanguosha->getCard(id);
}

const Card*TenyearAocaiCard::validate(CardUseStruct&cardUse)const
{
	cardUse.m_isOwnerUse = false;
	Room*room = cardUse.from->getRoom();
	int num = 2;
	if(cardUse.from->isKongcheng())
		num = 4;
	QList<int> ids = room->getNCards(num);
	QStringList names = user_string.split("+");
	//if(names.contains("slash"))names << "fire_slash" << "thunder_slash";

	QList<int> enabled,disabled;
	foreach(int id,ids){
		const Card*c = Sanguosha->getCard(id);
		if(cardUse.from->isLocked(c)){
			disabled << id;
			continue;
		}
		foreach(QString cn,names){
			if(user_string.isEmpty()){
				if(c->getTypeId()==1&&c->isAvailable(cardUse.from)){
					enabled << id;
					break;
				}
			}else if(c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if(enabled.contains(id))continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = cardUse.from;
	log.arg = "tenyearaocai";
	room->sendLog(log);

	int id = TenyearAocai::view(room,cardUse.from,ids,enabled,disabled);
	if(user_string.isEmpty()&&id>=0){
		room->setPlayerMark(cardUse.from,"aocaiId",id);
		room->askForUseCard(cardUse.from,"@@tenyearaocai","aocai0:"+Sanguosha->getCard(id)->objectName());
		return nullptr;
	}
	return Sanguosha->getCard(id);
}

TenyearDuwuCard::TenyearDuwuCard()
{
	mute = true;
}

bool TenyearDuwuCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& qMax(0,to_select->getHp())== subcardsLength()&& Self->inMyAttackRange(to_select,subcards);
}

void TenyearDuwuCard::onEffect(CardEffectStruct&effect)const
{
	Room*room = effect.to->getRoom();

	if(subcards.length()<= 1)
		room->broadcastSkillInvoke("tenyearduwu",2);
	else
		room->broadcastSkillInvoke("tenyearduwu",1);

	room->damage(DamageStruct("tenyearduwu",effect.from,effect.to));
}

class TenyearDuwuViewAsSkill : public ViewAsSkill
{
public:
	TenyearDuwuViewAsSkill(): ViewAsSkill("tenyearduwu")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->canDiscard(player,"he")&& !player->hasFlag("TenyearDuwuEnterDying");
	}

	bool viewFilter(const QList<const Card*>&,const Card*to_select)const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		TenyearDuwuCard*duwu = new TenyearDuwuCard;
		if(!cards.isEmpty())
			duwu->addSubcards(cards);
		return duwu;
	}
};

class TenyearDuwu : public TriggerSkill
{
public:
	TenyearDuwu(): TriggerSkill("tenyearduwu")
	{
		events << QuitDying;
		view_as_skill = new TenyearDuwuViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if(dying.damage&& dying.damage->getReason()== "tenyearduwu"&& !dying.damage->chain&& !dying.damage->transfer){
			ServerPlayer*from = dying.damage->from;
			if(from&& from->isAlive()){
				room->setPlayerFlag(from,"TenyearDuwuEnterDying");
				room->loseHp(HpLostStruct(from,1,objectName(),from));
			}
		}
		return false;
	}
};

TenyearSongciCard::TenyearSongciCard()
{
	mute = true;
}

bool TenyearSongciCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select->getMark("tenyearsongci" + Self->objectName())== 0;
}

void TenyearSongciCard::onEffect(CardEffectStruct&effect)const
{
	int handcard_num = effect.to->getHandcardNum();
	int hp = effect.to->getHp();
	Room*room = effect.from->getRoom();
	room->setPlayerMark(effect.to,"@songci",1);
	room->addPlayerMark(effect.to,"tenyearsongci" + effect.from->objectName());
	if(handcard_num > hp){
		room->broadcastSkillInvoke("tenyearsongci",2);
		room->askForDiscard(effect.to,"tenyearsongci",2,2,false,true);
	} else if(handcard_num <= hp){
		room->broadcastSkillInvoke("tenyearsongci",1);
		effect.to->drawCards(2,"tenyearsongci");
	}
}

class TenyearSongciViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearSongciViewAsSkill(): ZeroCardViewAsSkill("tenyearsongci")
	{
	}

	const Card*viewAs()const
	{
		return new TenyearSongciCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("tenyearsongci" + player->objectName())== 0)return true;
		foreach(const Player*sib,player->getAliveSiblings())
			if(sib->getMark("tenyearsongci" + player->objectName())== 0)
				return true;
		return false;
	}
};

class TenyearSongci : public TriggerSkill
{
public:
	TenyearSongci(): TriggerSkill("tenyearsongci")
	{
		events << CardUsed;
		view_as_skill = new TenyearSongciViewAsSkill;
	}


	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!data.value<CardUseStruct>().card->isKindOf("BifaCard"))return false;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->getMark("tenyearsongci" + player->objectName())<= 0)
				return false;
		}

		room->sendCompulsoryTriggerLog(player,objectName(),true,true);
		player->drawCards(1,objectName());
		return false;
	}
};

class TenyearDanlao : public TriggerSkill
{
public:
	TenyearDanlao(): TriggerSkill("tenyeardanlao")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.to.length()<= 1 || !use.to.contains(player)
			||(!use.card->isKindOf("TrickCard")&& !use.card->isKindOf("Slash"))
			|| !room->askForSkillInvoke(player,objectName(),data))
			return false;

		room->broadcastSkillInvoke(objectName());
		player->setFlags("-TenyearDanlaoTarget");
		player->setFlags("TenyearDanlaoTarget");
		player->drawCards(1,objectName());
		if(player->isAlive()&& player->hasFlag("TenyearDanlaoTarget")){
			player->setFlags("-TenyearDanlaoTarget");
			use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class TenyearJilei : public TriggerSkill
{
public:
	TenyearJilei(): TriggerSkill("tenyearjilei")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*yangxiu,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(!damage.from || damage.from->isDead())return false;

		if(room->askForSkillInvoke(yangxiu,objectName(),data)){
			QString choice = room->askForChoice(yangxiu,objectName(),"BasicCard+EquipCard+TrickCard");
			room->broadcastSkillInvoke(objectName());

			LogMessage log;
			log.type = "#Jilei";
			log.from = damage.from;
			log.arg = choice;
			room->sendLog(log);

			QStringList jilei_list = damage.from->getTag(objectName()).toStringList();
			if(jilei_list.contains(choice))return false;
			jilei_list.append(choice);
			damage.from->setTag(objectName(), QVariant::fromValue(jilei_list));
			QString _type = choice + "|.|.|hand"; // Handcards only
			room->setPlayerCardLimitation(damage.from,"use,response,discard",_type,false);

			if(damage.from->getMark("&tenyearjilei+" + choice)== 0)
				room->setPlayerMark(damage.from,"&tenyearjilei+" + choice,1);
		}
		return false;
	}
};

class TenyearJileiClear : public TriggerSkill
{
public:
	TenyearJileiClear(): TriggerSkill("#tenyearjilei-clear")
	{
		events << EventPhaseChanging << Death;
	}

	int getPriority(TriggerEvent)const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent,Room*room,ServerPlayer*target,QVariant&data)const
	{
		if(triggerEvent==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to != Player::RoundStart)return false;
		} else if(triggerEvent==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who != target)return false;
		}
		QStringList jilei_list = target->getTag("tenyearjilei").toStringList();
		if(!jilei_list.isEmpty()){
			LogMessage log;
			log.type = "#JileiClear";
			log.from = target;
			room->sendLog(log);

			foreach(QString jilei_type,jilei_list){
				room->removePlayerCardLimitation(target,"use,response,discard",jilei_type + "|.|.|hand$0");
				room->setPlayerMark(target,"&tenyearjilei+" + jilei_type,0);
			}
			target->removeTag("tenyearjilei");
		}
		return false;
	}
};

class Jinjian : public TriggerSkill
{
public:
	Jinjian(): TriggerSkill("jinjian")
	{
		events << DamageCaused << DamageInflicted;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if(player->getMark("&jinjianadd-Clear")> 0 || player->getMark("&jinjianreduce-Clear")> 0){
			if(!damage.tips.contains("jinjian_invoke")) //处理对自己造成伤害的问题，可以先+1再-1
				return false;
		}

		int change = 1;
		QString mark = "&jinjianreduce-Clear";
		bool invoke = true;
		player->setTag("JinjianDamage", data);

		if(event==DamageCaused)
			invoke = player->askForSkillInvoke(this,"add");
		else {
			invoke = player->askForSkillInvoke(this,"reduce");
			change = -1;
			mark = "&jinjianadd-Clear";
		}

		player->removeTag("JinjianDamage");

		if(!invoke)return false;
		player->peiyin(this);

		damage.tips << "jinjian_invoke";
		damage.damage += change;
		data = QVariant::fromValue(damage);

		if(room->hasCurrent())
			room->addPlayerMark(player,mark);

		if(damage.damage <= 0)
			return true;
		return false;
	}
};

class JinjianEffect : public TriggerSkill
{
public:
	JinjianEffect(): TriggerSkill("#jinjian")
	{
		events << DamageCaused << DamageInflicted;
	}

	int getPriority(TriggerEvent)const
	{
		return 0;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(!room->hasCurrent())return false;

		DamageStruct damage = data.value<DamageStruct>();
		if(damage.tips.contains("jinjian_invoke")){
			//damage.tips.removeOne("jinjian_invoke");
			//data = QVariant::fromValue(damage);
			return false;
		}
		int d = damage.damage;

		if(event==DamageInflicted){
			int mark = player->getMark("&jinjianadd-Clear");
			if(mark <= 0)return false;
			room->setPlayerMark(player,"&jinjianadd-Clear",0);

			damage.damage += mark;
			data = QVariant::fromValue(damage);

			LogMessage log;
			log.type = "#JinjianDamage";
			log.from = player;
			log.arg = "jinjian";
			log.arg2 = QString::number(d);
			log.arg3 = QString::number(damage.damage);
			room->sendLog(log);
			player->peiyin("jinjian");
			room->notifySkillInvoked(player,"jinjian");
		} else {
			int mark = player->getMark("&jinjianreduce-Clear");
			if(mark <= 0)return false;
			room->setPlayerMark(player,"&jinjianreduce-Clear",0);

			damage.damage -= mark;
			data = QVariant::fromValue(damage);

			player->peiyin("jinjian");
			room->notifySkillInvoked(player,"jinjian");

			LogMessage log;
			log.type = "#JinjianDamage";
			log.from = player;
			log.arg = "jinjian";
			log.arg2 = QString::number(d);

			if(damage.damage <= 0){
				log.type = "#JinjianPreventDamage";
				room->sendLog(log);
				return true;
			}

			log.arg3 = QString::number(damage.damage);
			room->sendLog(log);
		}
		return false;
	}
};

class Renzheng : public TriggerSkill
{
public:
	Renzheng(): TriggerSkill("renzheng")
	{
		events << DamageComplete;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();

		int start_damage = 0;
		foreach(QString tip,damage.tips){
			if(!tip.startsWith("STARTDAMAGE:"))continue;
			QStringList tips = tip.split(":");
			if(tips.length()!= 2)continue;
			start_damage = tips.last().toInt();
			if(start_damage >= 0)break;
		}

		if(damage.prevented || damage.damage <= 0 || damage.damage < start_damage){
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName()))continue;
				room->sendCompulsoryTriggerLog(p,this);
				p->drawCards(2,objectName());
			}
		}
		return false;
	}
};

KaijiCard::KaijiCard()
{
	mute = true;
	target_fixed = true;
}

void KaijiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	int n = source->getChangeSkillState("kaiji");
	if(n <= 1){
		room->broadcastSkillInvoke("kaiji",1);
		source->drawCards(source->getMaxHp(),"kaiji");
		room->setChangeSkillState(source,"kaiji",2);
	} else{
		room->broadcastSkillInvoke("kaiji",2);
		room->setChangeSkillState(source,"kaiji",1);
	}
}

class Kaiji : public ViewAsSkill
{
public:
	Kaiji(): ViewAsSkill("kaiji")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		int n = Self->getChangeSkillState("kaiji");
		if(n <= 1)return false;
		return !Self->isJilei(to_select)&& selected.length()< qMax(1,Self->getMaxHp());
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int n = Self->getChangeSkillState("kaiji");
		if(cards.isEmpty()&& n > 1)return nullptr;

		KaijiCard*c = new KaijiCard;
		if(!cards.isEmpty())
			c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return !player->hasUsed("KaijiCard");
	}
};

class Pingxi : public PhaseChangeSkill
{
public:
	Pingxi(): PhaseChangeSkill("pingxi")
	{
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		int mark = player->getMark("pingxi_discard-Clear");
		if(mark <= 0)return false;
		QList<ServerPlayer*> targets = room->askForPlayersChosen(player,room->getOtherPlayers(player),objectName(),0,mark,
								"@pingxi-targets:" + QString::number(mark),true);
		if(targets.isEmpty())return false;
		player->peiyin("pingxi");

		foreach(ServerPlayer*p,targets){
			if(player->isDead())return false;
			if(p->isDead()|| !player->canDiscard(p,"he"))continue;
			int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard);
			room->throwCard(id,objectName(),p,player);
		}

		Slash*slash = new Slash(Card::NoSuit,0);
		slash->deleteLater();
		slash->setSkillName("_pingxi");

		foreach(ServerPlayer*p,targets){
			if(player->isDead())return false;
			if(p->isDead()|| !player->canSlash(p,slash,false))continue;
			room->useCard(CardUseStruct(slash,player,p));
		}
		return false;
	}
};

class PingxiDiscard : public TriggerSkill
{
public:
	PingxiDiscard(): TriggerSkill("#pingxi_discard")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
			if(room->hasCurrent()&& move.to_place==Player::DiscardPile)
				room->addPlayerMark(player,"pingxi_discard-Clear",move.card_ids.length());
		}
		return false;
	}
};

JingzaoCard::JingzaoCard()
{
}

bool JingzaoCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select != Self&& to_select->getMark("jingzao_target-PlayClear")<= 0;
}

void JingzaoCard::getCards(ServerPlayer*player,QList<int> card_ids)const
{
	Room*room = player->getRoom();
	QList<int> ids = card_ids,dummy_ids;

	while(!card_ids.isEmpty()){
		int id = card_ids.at(qrand()% card_ids.length());
		dummy_ids << id;
		card_ids.removeOne(id);

		const Card*c = Sanguosha->getCard(id);
		foreach(int id,card_ids){
			if(Sanguosha->getCard(id)->sameNameWith(c))
				card_ids.removeOne(id);
		}
	}
	if(!dummy_ids.isEmpty()){
		DummyCard*dummy = new DummyCard(dummy_ids);
		room->obtainCard(player,dummy);
		delete dummy;
	}

	if(room->hasCurrent())
		room->addPlayerMark(player,"jingzao_wuxiao-Clear");

	DummyCard*to_throw = new DummyCard;
	foreach(int id,ids){
		if(dummy_ids.contains(id))continue;
		to_throw->addSubcard(id);
	}
	if(to_throw->subcardsLength()> 0){
		CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,player->objectName(),"jingzao","");
		room->throwCard(to_throw,reason,nullptr);
	}
	delete to_throw;
}

void JingzaoCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;

	Room*room = from->getRoom();
	room->addPlayerMark(to,"jingzao_target-PlayClear");

	int num = qMax(3,3 + from->getMark("&jingzao_show-Clear"));
	QList<int> shows = room->showDrawPile(from,num,"jingzao");
	if(shows.isEmpty())return;

	QStringList patterns;
	foreach(int id,shows){
		QString classname = Sanguosha->getCard(id)->getClassName();
		if(!classname.isEmpty()&& !patterns.contains(classname))
			patterns << classname;
	}
	if(patterns.isEmpty()){
		getCards(from,shows);
		return;
	}

	to->setTag("JingzaoPatternForAI", patterns);
	if(to->isAlive()&& room->askForDiscard(to,"jingzao",1,1,true,true,"@jingzao-discard",patterns.join(","))){
		if(room->hasCurrent())
			room->addPlayerMark(from,"&jingzao_show-Clear");

		DummyCard*dummy = new DummyCard(shows);
		CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER,from->objectName(),"jingzao","");
		room->throwCard(dummy,reason,nullptr);
		delete dummy;
	} else
		getCards(from,shows);
}

class Jingzao : public ZeroCardViewAsSkill
{
public:
	Jingzao(): ZeroCardViewAsSkill("jingzao")
	{
	}

	const Card*viewAs()const
	{
		return new JingzaoCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("jingzao_wuxiao-Clear")<= 0;
	}
};

class Enyu : public TriggerSkill
{
public:
	Enyu(): TriggerSkill("enyu")
	{
		events << TargetConfirmed;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.to.contains(player))return false;

		if(use.card->isKindOf("BasicCard")|| use.card->isNDTrick()){
			QString name = use.card->objectName();
			if(use.card->isKindOf("Slash"))
				name = "slash";

			if(player->getMark("enyu_target_" + name + "-Clear")> 0){
				if(use.from != player&& player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					use.nullified_list << player->objectName();
					data = QVariant::fromValue(use);
				}
			} else
				player->setMark("enyu_target_" + name + "-Clear",1);
		}
		return false;
	}
};

TenyearZhaohanCard::TenyearZhaohanCard()
{
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
}

bool TenyearZhaohanCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	return targets.isEmpty()&& to_select->isKongcheng()&& to_select != Self;
}

void TenyearZhaohanCard::onUse(Room*room,CardUseStruct&card_use)const
{
	room->giveCard(card_use.from,card_use.to.first(),this,"tenyearzhaohan");
}

class TenyearZhaohanVS : public ViewAsSkill
{
public:
	TenyearZhaohanVS(): ViewAsSkill("tenyearzhaohan")
	{
		response_pattern = "@@tenyearzhaohan!";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return selected.length()< 2&& !to_select->isEquipped();
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()!= 2)return nullptr;

		TenyearZhaohanCard*c = new TenyearZhaohanCard;
		c->addSubcards(cards);
		return c;
	}
};

class TenyearZhaohan : public TriggerSkill
{
public:
	TenyearZhaohan(): TriggerSkill("tenyearzhaohan")
	{
		events << AfterDrawNCards << DrawNCards;
		view_as_skill = new TenyearZhaohanVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="draw_phase")return false;
		if(event==AfterDrawNCards){
			if(!player->hasFlag("tenyearzhaohan"))return false;
			player->setFlags("-tenyearzhaohan");
			if(player->getHandcardNum()< 2)return false;

			QStringList choices;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isKongcheng()){
					choices << "give";
					break;
				}
			}

			int can_dis = 0;
			foreach(int id,player->handCards()){
				if(player->canDiscard(player,id))
					can_dis++;
				if(can_dis >= 2)
					break;
			}
			if(can_dis >= 2)
				choices << "discard";

			QString choice = room->askForChoice(player,objectName(),choices.join("+"));
			if(choice=="give"){
				if(!room->askForUseCard(player,"@@tenyearzhaohan!","@tenyearzhaohan",-1,Card::MethodNone)){
					QList<int> give,hands = player->handCards();
					int id = hands.at(qrand()% hands.length());
					hands.removeOne(id);
					give << id;
					if(!hands.isEmpty()){
						id = hands.at(qrand()% hands.length());
						give << id;
					}
					if(!give.isEmpty()){
						QList<ServerPlayer*> targets = room->getOtherPlayers(player);
						ServerPlayer*t = targets.at(qrand()% targets.length());
						room->giveCard(player,t,give,objectName());
					}
				}
			} else
				room->askForDiscard(player,objectName(),2,2);
		} else {
			if(!TriggerSkill::triggerable(player))return false;
			if(!player->askForSkillInvoke(this))return false;
			player->peiyin(this);
			player->setFlags("tenyearzhaohan");
			draw.num += 2;
			data = QVariant::fromValue(draw);
		}
		return false;
	}
};

class TenyearJinjie : public TriggerSkill
{
public:
	TenyearJinjie(): TriggerSkill("tenyearjinjie")
	{
		events << Dying;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DyingStruct dy = data.value<DyingStruct>();
		ServerPlayer*who = dy.who;
		if(player->getMark("tenyearjinjieTurn_lun")> 0){
			if(!player->askForSkillInvoke(this,"draw:" + who->objectName()))return false;
			player->peiyin(this);
			room->addPlayerMark(player,"&tenyearjinjieUsed_lun");
			who->drawCards(1,objectName());
		} else {
			int mark = player->getMark("&tenyearjinjieUsed_lun");
			if(mark <= 0){
				if(!player->askForSkillInvoke(this,"recover:" + who->objectName()))return false;
				player->peiyin(this);
				room->addPlayerMark(player,"&tenyearjinjieUsed_lun");
				room->recover(who,RecoverStruct(objectName(),player));
			} else {
				if(player->getHandcardNum()< mark)return false;
				if(!room->askForDiscard(player,objectName(),mark,mark,true,false,
						QString("@tenyearjinjie-discard:%1:%2").arg(who->objectName()).arg(mark),".",objectName()))return false;
				room->addPlayerMark(player,"&tenyearjinjieUsed_lun");
				room->recover(who,RecoverStruct(objectName(),player));
			}
		}
		return false;
	}
};

TenyearJueCard::TenyearJueCard()
{
}

bool TenyearJueCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Slash*slash = new Slash(Card::NoSuit,0);
	slash->setSkillName("tenyearjue");
	slash->deleteLater();
	return to_select->getMaxHp()== to_select->getHp()&& slash->targetFilter(targets,to_select,Self);
}

void TenyearJueCard::onUse(Room*room,CardUseStruct&card_use)const
{
	Slash*slash = new Slash(Card::NoSuit,0);
	slash->setSkillName("tenyearjue");
	slash->deleteLater();
	room->useCard(CardUseStruct(slash,card_use.from,card_use.to));
}

class TenyearJueVS : public ZeroCardViewAsSkill
{
public:
	TenyearJueVS(): ZeroCardViewAsSkill("tenyearjue")
	{
		response_pattern = "@@tenyearjue";
	}

	const Card*viewAs()const
	{
		return new TenyearJueCard;
	}
};

class TenyearJue : public PhaseChangeSkill
{
public:
	TenyearJue(): PhaseChangeSkill("tenyearjue")
	{
		view_as_skill = new TenyearJueVS;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Start)return false;
		Slash*slash = new Slash(Card::NoSuit,0);
		slash->setSkillName("tenyearjue");
		slash->deleteLater();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->canSlash(p,slash)&& p->getMaxHp()== p->getHp()){
				room->askForUseCard(player,"@@tenyearjue","@tenyearjue");
				return false;
			}
		}
		return false;
	}
};

class Lianzhi : public TriggerSkill
{
public:
	Lianzhi(): TriggerSkill("lianzhi")
	{
		events << Dying << GameStart << Death;
		waked_skills = "shouze";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Dying){
			if(!room->hasCurrent())return false;
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who != player || player->getMark("lianzhi_invoked-Clear")> 0)return false;

			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isDead()|| p->getMark("&lianzhi+#" + player->objectName())<= 0)continue;

				room->sendCompulsoryTriggerLog(player,this);
				room->addPlayerMark(player,"lianzhi_invoked-Clear");

				room->recover(player,RecoverStruct(objectName(),player));
				QList<ServerPlayer*> drawers;
				drawers << player << p;
				room->sortByActionOrder(drawers);
				room->drawCards(drawers,1,objectName());

				break;
			}
		}else if(event==GameStart){
			ServerPlayer*target = room->askForPlayerChosen(player,room->getOtherPlayers(player),"lianzhi","@lianzhi-choose",false);

			player->peiyin(this);

			LogMessage log;
			log.from = player;
			log.to << target;
			log.arg = "lianzhi";
			log.type = "#ChoosePlayerWithSkill";
			room->sendLog(log,player);

			log.type = "#InvokeSkill";
			room->sendLog(log,room->getOtherPlayers(player,true));

			room->doAnimate(1,player->objectName(),target->objectName(),QList<ServerPlayer*>()<< player);
			room->notifySkillInvoked(player,"lianzhi");

			room->addPlayerMark(target,"&lianzhi+#" + player->objectName(),1,QList<ServerPlayer*>()<< player);
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if(death.who->getMark("&lianzhi+#" + player->objectName())<= 0)return false;

			ServerPlayer*t = room->askForPlayerChosen(player,room->getOtherPlayers(player),"lianzhi_shouze","@lianzhi-shouze",true);
			if(!t)return false;

			LogMessage log;
			log.from = player;
			log.to << t;
			log.arg = "lianzhi";
			log.type = "#ChoosePlayerWithSkill";
			room->sendLog(log);
			player->peiyin("lianzhi");

			room->doAnimate(1,player->objectName(),t->objectName());

			QList<ServerPlayer*> targets;
			targets << player << t;
			room->sortByActionOrder(targets);
			room->notifySkillInvoked(player,"lianzhi");
			room->handleAcquireDetachSkills(targets.first(),"shouze");
			room->handleAcquireDetachSkills(targets.last(),"shouze");

			int mark = qMax(1,player->getMark("&dgrlzjiao"));
			t->gainMark("&dgrlzjiao",mark);
		}
		return false;
	}
};

class Lingfang : public TriggerSkill
{
public:
	Lingfang(): TriggerSkill("lingfang")
	{
		events << CardFinished << CardOnEffect;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==CardOnEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			room->addPlayerMark(effect.to,"lingfangTo"+effect.card->toString());
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->isBlack()|| use.card->isKindOf("SkillCard"))return false;
		foreach(ServerPlayer*player,room->getAllPlayers()){
			if(player->isDead()|| !player->hasSkill(objectName()))continue;

			if(player==use.from){
				foreach(ServerPlayer*p,use.to){
					if(p==use.from || use.nullified_list.contains(p->objectName())
						|| p->getMark("lingfangTo"+use.card->toString())<1)continue;
					room->sendCompulsoryTriggerLog(player,this);
					player->gainMark("&dgrlzjiao");
					break;
				}
				foreach(ServerPlayer*p,use.to)
					room->setPlayerMark(p,"lingfangTo"+use.card->toString(),0);
			} else if(use.to.contains(player)){
				if(player->getMark("lingfangTo"+use.card->toString())<1)continue;
				room->setPlayerMark(player,"lingfangTo"+use.card->toString(),0);
				if(use.nullified_list.contains(player->objectName()))continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&dgrlzjiao");
			}
		}
		return false;
	}
};

FengyingCard::FengyingCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FengyingCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("fengying");
		card->deleteLater();
		return card->targetFilter(targets,to_select,Self);
	}
	return false;
}

bool FengyingCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()!= CardUseStruct::CARD_USE_REASON_RESPONSE){
		Card*card = Sanguosha->cloneCard(user_string.split("+").first());
		if(card){
			card->addSubcards(subcards);
			card->setSkillName("fengying");
			card->deleteLater();
			return card->targetFixed();
		}
	}
	return true;
}

bool FengyingCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("fengying");
		card->deleteLater();
		return card->targetsFeasible(targets,Self);
	}
	return false;
}

const Card*FengyingCard::validate(CardUseStruct&card_use)const
{
	ServerPlayer*player = card_use.from;
	if(Sanguosha->getCard(subcards.first())->getNumber()> player->getMark("&dgrlzjiao"))return nullptr;

	QString record = player->property("SkillDescriptionRecord_fengying").toString();
	if(record.isEmpty())return nullptr;
	QStringList records = record.split("+");

	Room*room = player->getRoom();
	QString to_yizan = user_string;
	if(room->hasCurrent())
		room->addPlayerMark(player,"fengyingUsed-Clear");

	if((user_string.contains("slash")|| user_string.contains("Slash"))&&
			Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		QStringList guhuo_list;
		foreach(QString name,records){
			Card*c = Sanguosha->cloneCard(name);
			if(c&& c->isKindOf("Slash")&& !guhuo_list.contains(name))
				guhuo_list << name;
			if(c)
				delete c;
		}
		if(guhuo_list.isEmpty())return nullptr;
		to_yizan = room->askForChoice(player,"fengying_slash",guhuo_list.join("+"));
	}

	if(to_yizan=="normal_slash")
		to_yizan = "slash";

	Card*use_card = Sanguosha->cloneCard(to_yizan);
	use_card->setSkillName("fengying");
	use_card->addSubcards(getSubcards());
	use_card->deleteLater();
	return use_card;
}

const Card*FengyingCard::validateInResponse(ServerPlayer*player)const
{
	if(Sanguosha->getCard(subcards.first())->getNumber()> player->getMark("&dgrlzjiao"))return nullptr;

	QString record = player->property("SkillDescriptionRecord_fengying").toString();
	if(record.isEmpty())return nullptr;
	QStringList records = record.split("+");

	Room*room = player->getRoom();
	QString to_yizan;
	if(room->hasCurrent())
		room->addPlayerMark(player,"fengyingUsed-Clear");

	if(user_string=="peach+analeptic"){
		QStringList guhuo_list;
		foreach(QString name,records){
			Card*c = Sanguosha->cloneCard(name);
			if(!c)continue;
			if(c->isKindOf("Peach")&& !guhuo_list.contains(name))
				guhuo_list << name;
			else if(c->isKindOf("Analeptic")&& !guhuo_list.contains(name))
				guhuo_list << name;
			delete c;
		}
		if(guhuo_list.isEmpty())return nullptr;
		to_yizan = room->askForChoice(player,"fengying_saveself",guhuo_list.join("+"));
	} else if(user_string.contains("slash")|| user_string.contains("Slash")){
		QStringList guhuo_list;
		foreach(QString name,records){
			Card*c = Sanguosha->cloneCard(name);
			if(c&& c->isKindOf("Slash")&& !guhuo_list.contains(name))
				guhuo_list << name;
			if(c)
				delete c;
		}
		if(guhuo_list.isEmpty())return nullptr;
		to_yizan = room->askForChoice(player,"fengying_slash",guhuo_list.join("+"));
	} else
		to_yizan = user_string;

	if(to_yizan=="normal_slash")
		to_yizan = "slash";

	Card*use_card = Sanguosha->cloneCard(to_yizan);
	use_card->setSkillName("fengying");
	use_card->addSubcards(getSubcards());
	use_card->deleteLater();
	return use_card;
}

class FengyingVS : public OneCardViewAsSkill
{
public:
	FengyingVS(): OneCardViewAsSkill("fengying")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		if(player->getMark("fengyingUsed-Clear")> 0||player->getMark("&dgrlzjiao")<1)return false;
		QString record = player->property("SkillDescriptionRecord_fengying").toString();
		return !record.isEmpty();
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		if(player->getMark("fengyingUsed-Clear")> 0||player->getMark("&dgrlzjiao")<1)return false;

		if(pattern.startsWith(".")|| pattern.startsWith("@"))return false;
		if(pattern=="peach"&& player->getMark("Global_PreventPeach")> 0)return false;
		QString record = player->property("SkillDescriptionRecord_fengying").toString();
		if(record.isEmpty())return false;
		bool current = false;
		foreach(const Player*p,player->getAliveSiblings(true)){
			if(p->getPhase()!= Player::NotActive){
				current = true;
				break;
			}
		}
		if(!current)return false;

		QStringList records = record.split("+");

		foreach(QString pat,pattern.split("+")){
			foreach(QString name,pat.split(",")){
				if(records.contains(name.toLower()))
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtNullification(const ServerPlayer*player)const
	{
		if(player->getMark("fengyingUsed-Clear")> 0)return false;
		QString record = player->property("SkillDescriptionRecord_fengying").toString();
		if(record.isEmpty())return false;
		QStringList records = record.split("+");

		foreach(QString name,records){
			Card*c = Sanguosha->cloneCard(name);
			if(!c)continue;
			c->deleteLater();
			if(c->isKindOf("Nullification"))
				return true;
		}
		return false;
	}

	bool viewFilter(const Card*to_select)const
	{
		if(to_select->isEquipped())return false;
		return to_select->getNumber()<= Self->getMark("&dgrlzjiao");
	}

	const Card*viewAs(const Card*originalCard)const
	{
		if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			FengyingCard*card = new FengyingCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			card->addSubcard(originalCard);
			return card;
		}

		const Card*c = Self->getTag("fengying").value<const Card*>();
		if(c&& c->isAvailable(Self)){
			FengyingCard*card = new FengyingCard;
			card->setUserString(c->objectName());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}
};

class Fengying : public PhaseChangeSkill
{
public:
	Fengying(): PhaseChangeSkill("fengying")
	{
		view_as_skill = new FengyingVS;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance(objectName(),true,true,true,false,false,true);
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr&& target->getPhase()== Player::RoundStart;
	}

	bool onPhaseChange(ServerPlayer*,Room*room)const
	{
		QStringList names;
		foreach(int id,room->getDiscardPile()){
			const Card*c = Sanguosha->getCard(id);
			if(!c->isBlack()|| names.contains(c->objectName()))continue;
			if(c->isKindOf("BasicCard")|| c->isNDTrick())
				names << c->objectName();
		}
		if(names.isEmpty())return false;

		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill(objectName()))continue;

			LogMessage log;
			log.type = "#FengyingRecord";
			log.from = p;
			log.arg = objectName();
			room->sendLog(log);
			//p->peiyin(this);女BB机~~~
			room->notifySkillInvoked(p,objectName());
			QStringList _names;
			foreach(QString str,names)
				_names << str << "|";
			room->setPlayerProperty(p,"SkillDescriptionRecord_fengying",names.join("+"));
			p->setSkillDescriptionSwap(objectName(),"%arg11",_names.join("+"));
			room->changeTranslation(p,objectName(),1);
		}
		return false;
	}
};

class Shouze : public PhaseChangeSkill
{
public:
	Shouze(): PhaseChangeSkill("shouze")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player,Room*room)const
	{
		if(player->getPhase()!= Player::Finish)return false;
		if(player->getMark("&dgrlzjiao")<= 0)return false;

		room->sendCompulsoryTriggerLog(player,this);
		player->loseMark("&dgrlzjiao");

		QList<int> discardpile = room->getDiscardPile(),blacks;
		foreach(int id,discardpile){
			if(Sanguosha->getCard(id)->isBlack())
				blacks << id;
		}
		if(!blacks.isEmpty()){
			int id = blacks.at(qrand()% blacks.length());
			room->obtainCard(player,id);
		}

		room->loseHp(HpLostStruct(player,1,objectName(),player));
		return false;
	}
};

class TenyearLuochong : public TriggerSkill
{
public:
	TenyearLuochong(): TriggerSkill("tenyearluochong")
	{
		events << RoundStart;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		int all_num = 4 - player->getMark("&tenyearluochong_debuff");
		if(all_num <= 0)return false;

		bool three = false,invoke = false;
		QList<ServerPlayer*> chosen;

		while(all_num > 0){
			if(player->isDead())break;

			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->canDiscard(p,"hej")&& !chosen.contains(p))
					targets << p;
			}
			if(targets.isEmpty())break;
			ServerPlayer*t = room->askForPlayerChosen(player,targets,objectName(),
				"@tenyearluochong-invoke:" + QString::number(all_num),true);
			if(!t)break;
			chosen << t;

			if(!invoke){
				invoke = true;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player,objectName());
			}

			room->doAnimate(1,player->objectName(),t->objectName());

			QList<int> cards;
			for(int i = 0; i < all_num; ++i){
				if(t->getCardCount(true,true)<=i)break;
				int id = room->askForCardChosen(player,t,"hej",objectName(),false,Card::MethodDiscard,cards,i != 0);
				if(id < 0)break;
				cards << id;
			}

			if(!cards.isEmpty()){
				if(cards.length()> 2)
					three = true;
				all_num -= cards.length();
				room->throwCard(cards,objectName(),t,player);
			}
		}

		if(three&& player->isAlive())
			room->addPlayerMark(player,"&tenyearluochong_debuff");
		return false;
	}
};

class TenyearAichen : public TriggerSkill
{
public:
	TenyearAichen(): TriggerSkill("tenyearaichen")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		frequency = Compulsory;
		waked_skills = "#tenyearaichen";
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		int num = room->getDrawPile().length();
		if(event==CardsMoveOneTime){
			if(num <= 80)return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName != "tenyearluochong" || move.from != player || move.reason.m_playerId != player->objectName())return false;
			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				for(int i = 0; i < move.card_ids.length(); i++){
					Player::Place place = move.from_places.at(i);
					if(place==Player::PlaceHand || place==Player::PlaceEquip || place==Player::PlaceDelayedTrick){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(2,objectName());
						return false;
					}
				}
			}
		} else {
			if(num <= 40)return false;
			if(player->isSkipped(Player::Discard))return false;
			if(data.value<PhaseChangeStruct>().to != Player::Discard)return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->skip(Player::Discard);
		}
		return false;
	}
};

class TenyearAichenSpade : public TriggerSkill
{
public:
	TenyearAichenSpade(): TriggerSkill("#tenyearaichen")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(room->getDrawPile().length()>= 40)return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card || use.card->isKindOf("SkillCard")|| use.card->getSuit()!= Card::Spade)return false;
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->isDead()|| !p->hasSkill("tenyearaichen"))continue;
			LogMessage log;
			log.from = p;
			log.arg = "tenyearaichen";
			log.type = "#TenyearAichen";
			log.arg2 = use.card->objectName();
			room->sendLog(log);
			p->peiyin("tenyearaichen");
			room->notifySkillInvoked(p,"tenyearaichen");
			use.no_respond_list << p->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Huizhi : public TriggerSkill
{
public:
	Huizhi(): TriggerSkill("huizhi")
	{
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getPhase()!= Player::Draw || player->isKongcheng())return false;
		if(!room->askForDiscard(player,objectName(),99999,1,true,false,"@huizhi-discard",".|.|.|hand",objectName()))return false;

		int hand = player->getHandcardNum();
		foreach(ServerPlayer*p,room->getOtherPlayers(player))
			hand = qMax(hand,p->getHandcardNum());
		hand -= player->getHandcardNum();
		hand = qMax(hand,1);
		hand = qMin(hand,5);

		player->drawCards(hand,objectName());
		return false;
	}
};

JijiaoCard::JijiaoCard()
{
}

bool JijiaoCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void JijiaoCard::onEffect(CardEffectStruct&effect)const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	room->removePlayerMark(from,"@jijiaoMark");
	room->doSuperLightbox(from,"jijiao");

	QList<int> dis = room->getDiscardPile(),tricks = ListV2I(from->getTag("JijiaoRecord").toList()),give;
	foreach(int id,tricks){
		if(dis.contains(id))
			give << id;
	}
	if(give.isEmpty())return;

	room->giveCard(from,to,give,"jijiao",true);

	QVariantList _tricks = room->getTag("JijiaoRecord").toList();
	foreach(int id,give){
		if(room->getCardPlace(id)== Player::PlaceHand){
			_tricks << id;
			room->setCardTip(id,"jijiao");
		}
	}
	room->setTag("JijiaoRecord",_tricks);
}

class JijiaoVS : public ZeroCardViewAsSkill
{
public:
	JijiaoVS(): ZeroCardViewAsSkill("jijiao")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("@jijiaoMark")> 0;
	}

	const Card*viewAs()const
	{
		return new JijiaoCard;
	}
};

class Jijiao : public TriggerSkill
{
public:
	Jijiao(): TriggerSkill("jijiao")
	{
		events << TrickCardCanceling << EventPhaseChanging << Death << SwappedPile;
		frequency = Limited;
		limit_mark = "@jijiaoMark";
		view_as_skill = new JijiaoVS;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data)const
	{
		if(event==TrickCardCanceling){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			return effect.card->hasFlag("jijiaoTrick");
		} else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			if(!room->getTag("JijiaoMark").toBool())return false;
			room->removeTag("JijiaoMark");
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isDead()|| !p->hasSkill(objectName())|| p->getMark("@jijiaoMark")> 0)continue;
				LogMessage log;
				log.type = "#JijiaoMark";
				log.arg = "jijiao";
				log.from = p;
				room->sendLog(log);
				p->peiyin(this);
				room->notifySkillInvoked(p,objectName());
				room->setPlayerMark(p,"@jijiaoMark",1);
			}
		} else if(event==SwappedPile){
			if(!room->hasCurrent())return false;
			room->setTag("JijiaoMark",true);
		} else {
			if(!room->hasCurrent())return false;
			DeathStruct death = data.value<DeathStruct>();
			if(!death.who)return false;
			room->setTag("JijiaoMark",true);
		}
		return false;
	}
};

class JijiaoRecord : public TriggerSkill
{
public:
	JijiaoRecord(): TriggerSkill("#jijiao")
	{
		events << PreCardUsed << CardsMoveOneTime;
		global = true;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		QVariantList tricks = player->getTag("JijiaoRecord").toList();
		QVariantList _tricks = room->getTag("JijiaoRecord").toList();

		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isVirtualCard()|| !use.card->getSkillName().isEmpty()|| !use.card->isNDTrick())return false;
			int id = use.card->getEffectiveId();
			if(!tricks.contains(QVariant(id))){
				tricks << id;
				player->setTag("JijiaoRecord", tricks);
			}
			if(_tricks.contains(QVariant(id))){
				room->setCardFlag(id,"jijiaoTrick");
				LogMessage log;
				log.type = "#JijiaoEffect";
				log.arg = "jijiao";
				log.arg2 = use.card->objectName();
				room->sendLog(log);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from != player || !move.from_places.contains(Player::PlaceHand))return false;
			if((move.reason.m_reason& CardMoveReason::S_MASK_BASIC_REASON)== CardMoveReason::S_REASON_DISCARD){
				for(int i = 0; i < move.card_ids.length(); i++){
					if(move.from_places.at(i)!= Player::PlaceHand)continue;
					int id = move.card_ids.at(i);
					if(!Sanguosha->getCard(id)->isNDTrick())continue;
					if(tricks.contains(QVariant(id)))continue;
					tricks << id;
				}
				player->setTag("JijiaoRecord", tricks);
			}

			for(int i = 0; i < move.card_ids.length(); i++){
				if(move.from_places.at(i)!= Player::PlaceHand)continue;
				int id = move.card_ids.at(i);
				if(!_tricks.contains(QVariant(id)))continue;
				_tricks.removeOne(id);
			}
			room->setTag("JijiaoRecord",_tricks);
		}
		return false;
	}
};

class TenyearZimu : public MasochismSkill
{
public:
	TenyearZimu(): MasochismSkill("tenyearzimu")
	{
		frequency = Compulsory;
	}

	void onDamaged(ServerPlayer*player,const DamageStruct&)const
	{
		Room*room = player->getRoom();
		room->sendCompulsoryTriggerLog(player,this);
		QList<ServerPlayer*> players = room->findPlayersBySkillName(objectName());
		players.removeOne(player);
		room->sortByActionOrder(players);
		room->drawCards(players,1,objectName());
		room->handleAcquireDetachSkills(player,"-tenyearzimu");
	}
};

class Liangjue : public TriggerSkill
{
public:
	Liangjue(): TriggerSkill("liangjue")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&(move.to_place==Player::PlaceDelayedTrick||move.to_place==Player::PlaceEquip)){
				for(int i = 0; i < move.card_ids.length(); i++){
					if(Sanguosha->getCard(move.card_ids[i])->isBlack()&&player->isAlive()){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(2,objectName());
						if(player->getHp()>1)
							room->loseHp(player,1,true,player,objectName());
					}
				}
			}else if(move.from==player&&(move.from_places.contains(Player::PlaceDelayedTrick)||move.from_places.contains(Player::PlaceEquip))){
				for(int i = 0; i < move.card_ids.length(); i++){
					if(move.from_places[i]==Player::PlaceDelayedTrick||move.from_places[i]==Player::PlaceEquip){
						if(Sanguosha->getCard(move.card_ids[i])->isBlack()&&player->isAlive()){
							room->sendCompulsoryTriggerLog(player,this);
							player->drawCards(2,objectName());
							if(player->getHp()>1)
								room->loseHp(player,1,true,player,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class Dangzhai : public TriggerSkill
{
public:
	Dangzhai(): TriggerSkill("dangzhai")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getPhase()!= Player::Play)return false;
		QList<ServerPlayer*>tps;
		foreach(ServerPlayer*p,room->getOtherPlayers(player)){
			foreach(const Card*j,p->getJudgingArea()){
				if(p->isProhibited(player,j))continue;
				tps << p;
				break;
			}
		}
		ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"dangzhai0",true,true);
		if(tp){
			player->peiyin(this);
			QList<int>ids;
			foreach(const Card*j,tp->getJudgingArea()){
				if(tp->isProhibited(player,j))ids << j->getId();
			}
			Card*dc = dummyCard();
			for(int i = 0; i < tp->getJudgingArea().length()-ids.length(); i++){
				int id = room->askForCardChosen(player,tp,"j",objectName(),false,Card::MethodNone,ids,i>0);
				if(id<0)break;
				dc->addSubcard(id);
				ids << id;
			}
			room->moveCardTo(dc,player,Player::PlaceDelayedTrick,true);
		}
		return false;
	}
};

class Zhantao : public TriggerSkill
{
public:
	Zhantao(): TriggerSkill("zhantao")
	{
		events << Damaged;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		DamageStruct damage = data.value<DamageStruct>();
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(player->isDead()||p->isDead()||damage.from==p||!p->hasSkill(objectName()))continue;
			if(damage.card&&(p==player||p->inMyAttackRange(player))&&p->askForSkillInvoke(this,data)){
				p->peiyin(this);
				JudgeStruct judge;
				judge.pattern = ".|.|1~"+QString::number(damage.card->getNumber());
				judge.reason = objectName();
				judge.who = player;
				judge.good = false;
				room->judge(judge);
				if(judge.isGood()&&damage.from&&damage.from->isAlive()){
					Card*dc = Sanguosha->cloneCard("slash");
					dc->setSkillName("_zhantao");
					if(p->canSlash(damage.from,dc,false))
						room->useCard(CardUseStruct(dc,p,damage.from));
					dc->deleteLater();
				}
			}
		}
		return false;
	}
};

class Anjing : public TriggerSkill
{
public:
	Anjing(): TriggerSkill("anjing")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(player->getMark("anjingUse-Clear")>0||!player->hasTurn())return false;
		QList<ServerPlayer*>tps;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->isWounded())tps << p;
		}
		int n = player->getMark("&anjing")+1;
		tps = room->askForPlayersChosen(player,tps,objectName()+"$-1",0,n,"anjing0:"+QString::number(n),true);
		if(tps.length()>0){
			player->addMark("anjingUse-Clear");
			room->addPlayerMark(player,"&anjing");
			room->drawCards(tps,1,objectName());
			n = 998;
			foreach(ServerPlayer*p,tps){
				if(p->isAlive()&&p->getHp()<n)n = p->getHp();
			}
			foreach(ServerPlayer*p,tps){
				if(p->isAlive()&&p->getHp()<=n)
					room->recover(p,RecoverStruct(objectName(),player));
			}
		}
		return false;
	}
};

class Xidi : public TriggerSkill
{
public:
	Xidi():TriggerSkill("xidi")
	{
		events << EventPhaseStart << AfterDrawNCards;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start||player->getPhase()==Player::Finish){
				room->sendCompulsoryTriggerLog(player,this);
				int n = 0;
				foreach(const Card*h,player->getHandcards()){
					if(h->hasTip("xi_di"))n++;
				}
				n = qMin(qMax(1,n),8);
				room->askForGuanxing(player,room->getNCards(n));
			}
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards")return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach(int id,player->handCards()){
				room->setCardTip(id,"xi_di");
			}
		}
		return false;
	}
};

class Chengyan : public TriggerSkill
{
public:
	Chengyan(): TriggerSkill("chengyan")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")|| use.card->isNDTrick()){
				foreach(ServerPlayer*p,use.to){
					if(p!=player){
						if(player->askForSkillInvoke(objectName()+"$-1",data)){
							int id = room->showDrawPile(player,1,objectName()).first();
							const Card*c = Sanguosha->getCard(id);
							room->getThread()->delay();
							if(c->isKindOf("Slash")||c->isNDTrick()){
								Card*dc = Sanguosha->cloneCard(c->objectName());
								if(c->getTypeId()<2||dc->isAvailable(player)){
									if(!c->sameNameWith(use.card))player->broadcastSkillInvoke(c);
									dc->addSubcards(use.card->getSubcards());
									use.card = dc;
									data.setValue(use);
								}
								dc->deleteLater();
								room->throwCard(id,objectName(),nullptr);
							}else{
								player->obtainCard(c);
								if(player->handCards().contains(id))
									room->setCardTip(id,"xi_di");
							}
						}
						break;
					}
				}
			}
		}
		return false;
	}
};

ManhouCard::ManhouCard()
{
	target_fixed = true;
}

void ManhouCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	int n = room->askForChoice(source,"manhou","1+2+3+4").toInt();
	source->drawCards(n,"manhou");
	if(n>=1){
		room->detachSkillFromPlayer(source,"tanluan");
	}
	if(n>=2){
		room->askForDiscard(source,"manhou",1,1);
	}
	if(n>=3){
		room->loseHp(source,1,true,source,"manhou");
		if(source->isDead())return;
		QList<ServerPlayer*>tps;
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(source->canDiscard(p,"ej"))tps << p;
		}
		ServerPlayer*tp = room->askForPlayerChosen(source,tps,"manhou","manhou0");
		if(tp){
			room->doAnimate(1,source->objectName(),tp->objectName());
			int id = room->askForCardChosen(source,tp,"ej","manhou",false,Card::MethodDiscard);
			if(id>=0)room->throwCard(id,"manhou",tp,source);
		}
	}
	if(n>=4){
		room->askForDiscard(source,"manhou",1,1,false,true);
		room->acquireSkill(source,"tanluan");
	}
}

class Manhouvs : public ZeroCardViewAsSkill
{
public:
	Manhouvs(): ZeroCardViewAsSkill("manhou")
	{
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("ManhouCard")<1;
	}

	const Card*viewAs()const
	{
		return new ManhouCard;
	}
};

class Manhou : public TriggerSkill
{
public:
	Manhou(): TriggerSkill("manhou")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Manhouvs;
		waked_skills = "tanluan";
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
					room->addPlayerMark(player,"tanluanCan-PlayClear");
					foreach(int id,move.card_ids)
						player->addMark(QString::number(id)+"tanluanId-Clear");
				}else{
					foreach(int id,move.card_ids)
						player->setMark(QString::number(id)+"tanluanId-Clear",0);
				}
			}
		}
		return false;
	}
};

TanluanCard::TanluanCard()
{
	target_fixed = true;
}

void TanluanCard::onUse(Room*room,CardUseStruct&use)const
{
	QList<int>ids;
	foreach(int id,room->getDiscardPile()){
		if(use.from->getMark(QString::number(id)+"tanluanId-Clear")>0&&Sanguosha->getCard(id)->isAvailable(use.from))
			ids << id;
	}
	if(ids.length()>0){
		room->notifyMoveToPile(use.from,ids,"tanluan");
		const Card*sc = room->askForUseCard(use.from,"@@tanluan","tanluan0",-1,Card::MethodUse,true,nullptr,nullptr,"tanluanUse");
		room->notifyMoveToPile(use.from,ids,"tanluan",Player::PlaceUnknown,false);
		if(sc)return;
	}
	use.m_addHistory = false;
	room->setPlayerMark(use.from,"tanluanCan-PlayClear",0);
}

class Tanluanvs : public ViewAsSkill
{
public:
	Tanluanvs(): ViewAsSkill("tanluan")
	{
		response_pattern = "@@tanluan";
		expand_pile = "#tanluan";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@tanluan")
			return selected.isEmpty()&&Self->getPileName(to_select->getEffectiveId())==expand_pile
			&&to_select->isAvailable(Self);
		return false;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@tanluan"){
			if(cards.isEmpty())return nullptr;
			return cards.first();
		}
		return new TanluanCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("TanluanCard")<1&&player->getMark("tanluanCan-PlayClear")>0;
	}
};

class Tanluan : public TriggerSkill
{
public:
	Tanluan(): TriggerSkill("tanluan")
	{
		events << CardsMoveOneTime << PreCardUsed << CardFinished;
		view_as_skill = new Tanluanvs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
					room->addPlayerMark(player,"tanluanCan-PlayClear");
					foreach(int id,move.card_ids)
						player->addMark(QString::number(id)+"tanluanId-Clear");
				}else{
					foreach(int id,move.card_ids)
						player->setMark(QString::number(id)+"tanluanId-Clear",0);
				}
			}
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("tanluanUse")){
				player->skillInvoked(this);
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("tanluanUse")){
				foreach(ServerPlayer*p,room->getOtherPlayers(player,true)){
					if(use.card->hasFlag("DamageDone_"+p->objectName())){
						room->addPlayerHistory(player,"ManhouCard",0);
						break;
					}
				}
			}
		}
		return false;
	}
};

WeitiCard::WeitiCard()
{
}

bool WeitiCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void WeitiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		if(p->isWounded()&&room->askForChoice(p,"weiti","weiti1+weiti2")=="weiti2"){
			room->recover(p,RecoverStruct("weiti",source));
			if(p->getCardCount()<2)continue;
			const Card*sc = room->askForUseCard(p,"@@weiti!","weiti0",-1,Card::MethodDiscard);
			if(!sc){
				Card*dc = dummyCard();
				foreach(const Card*c,p->getCards("he")){
					if(dc->getNumber()!=c->getNumber()){
						if(p->canDiscard(p,c->getId())){
							dc->addSubcard(c);
							if(dc->subcardsLength()>1)
								break;
						}
					}
				}
				sc = dc;
			}
			room->throwCard(sc,"weiti",p);
		}else{
			room->damage(DamageStruct("weiti",nullptr,p));
			QList<int>ids;
			foreach(const Card*c,p->getCards("h"))
				ids << c->getNumber();
			Card*dc = dummyCard();
			foreach(int id,room->getDrawPile()){
				const Card*c = Sanguosha->getCard(id);
				if(ids.contains(c->getNumber()))continue;
				dc->addSubcard(id);
				if(dc->subcardsLength()>1){
					p->obtainCard(dc,false);
					break;
				}
			}
		}
	}
}

class Weiti : public ViewAsSkill
{
public:
	Weiti(): ViewAsSkill("weiti")
	{
		response_pattern = "@@weiti!";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		return Sanguosha->getCurrentCardUsePattern()=="@@weiti!"
		&&selected.length()< 2&& !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@weiti!"){
			if(cards.length()!= 2)return nullptr;
			Card*dc = new DummyCard();
			dc->addSubcards(cards);
			return dc;
		}
		return new WeitiCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("WeitiCard")<1;
	}
};

class Yuanrongvs : public OneCardViewAsSkill
{
public:
	Yuanrongvs(): OneCardViewAsSkill("yuanrong")
	{
		response_pattern = "@@yuanrong!";
		expand_pile = "#yuanrong";
		filter_pattern = ".|.|.|#yuanrong";
	}

	const Card*viewAs(const Card*originalCard)const
	{
		Card*dc = Sanguosha->cloneCard(Self->property("yuanrongCN").toString());
		dc->setSkillName("yuanrong");
		dc->addSubcard(originalCard);
		return dc;
	}
};

class Yuanrong : public TriggerSkill
{
public:
	Yuanrong(): TriggerSkill("yuanrong")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new Yuanrongvs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id,move.card_ids)
					player->addMark(QString::number(id)+"yuanrongId-Clear");
			}
		}else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive&&player->hasSkill(objectName())){
				QList<int>ids,ids2 = room->getAvailableCardList(player,"trick",objectName());
				foreach(int id,room->getDiscardPile()){
					if(player->getMark(QString::number(id)+"yuanrongId-Clear")>0){
						if(Sanguosha->getCard(id)->isBlack())ids << id;
					}
				}
				if(ids.length()>0&&ids2.length()>0&&player->askForSkillInvoke(this,data,false)){
					room->fillAG(ids2,player);
					int id2 = room->askForAG(player,ids2,ids2.length()<2,objectName(),"yuanrong1");
					if(id2<0)id2 = ids2.last();
					room->clearAG(player);
					const Card*c = Sanguosha->getCard(id2);
					room->notifyMoveToPile(player,ids,"yuanrong");
					room->setPlayerProperty(player,"yuanrongCN",c->objectName());
					room->askForUseCard(player,"@@yuanrong!","yuanrong0:"+c->objectName());

					ids.clear();
					foreach(int did,room->getDiscardPile()){
						if(player->getMark(QString::number(did)+"yuanrongId-Clear")>0){
							if(Sanguosha->getCard(did)->isRed())ids << did;
						}
					}
					ids2 = room->getAvailableCardList(player,"basic",objectName());
					if(player->isDead()||ids.isEmpty()||ids2.isEmpty())return false;
					room->fillAG(ids2,player);
					id2 = room->askForAG(player,ids2,ids2.length()<2,objectName(),"yuanrong1");
					if(id2<0)id2 = ids2.last();
					room->clearAG(player);
					c = Sanguosha->getCard(id2);
					room->notifyMoveToPile(player,ids,"yuanrong");
					room->setPlayerProperty(player,"yuanrongCN",c->objectName());
					room->askForUseCard(player,"@@yuanrong!","yuanrong2:"+c->objectName());
				}
			}
		}
		return false;
	}
};

class BoxuanVS : public OneCardViewAsSkill
{
public:
	BoxuanVS(): OneCardViewAsSkill("boxuan")
	{
		response_pattern = "@@boxuan";
		expand_pile = "#boxuan";
	}

	bool viewFilter(const Card*to_select)const
	{
		return Self->getPileName(to_select->getId())=="#boxuan"
		&&to_select->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard)const
	{
		return originalCard;
	}
};

class Boxuan : public TriggerSkill
{
public:
	Boxuan(): TriggerSkill("boxuan")
	{
		events << CardFinished;
		view_as_skill = new BoxuanVS;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()>0&&use.m_isHandcard){
				bool has = player->getTag("boxuan_revise").toBool();
				foreach(ServerPlayer*p,use.to){
					if(p!=player)has = true;
				}
				if(!has||!player->askForSkillInvoke(this,data))return false;
				player->peiyin(this);
				QList<int>ids = room->showDrawPile(player,3,objectName(),false,false);
				bool hasname = false,hassuit = false,hastype = false;
				foreach(int id,ids){
					const Card*c = Sanguosha->getCard(id);
					if(c->nameLength()==use.card->nameLength())
						hasname = true;
					if(c->getSuit()==use.card->getSuit())
						hassuit = true;
					if(c->getType()==use.card->getType())
						hastype = true;
				}
				room->getThread()->delay();
				if(hasname)
					player->drawCards(1,objectName());
				if(hassuit&&player->isAlive()){
					QList<ServerPlayer*>tps;
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						if(player->canDiscard(p,"he"))tps << p;
					}
					ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"boxuan0",true);
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						int id = room->askForCardChosen(player,tp,"he",objectName(),false,Card::MethodDiscard);
						if(id>-1)room->throwCard(id,objectName(),tp,player);
					}
				}
				if(hastype&&player->isAlive()){
					room->notifyMoveToPile(player,ids,"boxuan");
					room->askForUseCard(player,"@@boxuan","boxuan1");
				}/*
				if(player->getTag("boxuan_revise").toBool()&&room->getCardOwner(use.card->getEffectiveId())==nullptr
					&&player->askForSkillInvoke("boxuan_revise",data,false)){
					room->moveCardsToEndOfDrawpile(player,use.card->getSubcards(),objectName(),true);
				}*/
			}
		}
		return false;
	}
};

class ThYizheng : public TriggerSkill
{
public:
	ThYizheng(): TriggerSkill("thyizheng")
	{
		events << EventPhaseChanging;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().from==Player::NotActive&&player->getHandcardNum()>0){
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getHandcardNum()>0)tps << p;
				}
				tps = room->askForPlayersChosen(player,tps,objectName(),0,player->getMaxHp(),"thyizheng0:"+QString::number(player->getMaxHp()),true);
				if(tps.length()>0){
					player->peiyin(this);
					tps.prepend(player);
					int t = 0;
					Card*dc = dummyCard();
					foreach(ServerPlayer*p,tps){
						const Card*c = room->askForCardShow(p,player,objectName());
						if(c){
							dc->addSubcard(c);
							room->showCard(p,c->getId());
							if(t==0)t = c->getTypeId();
							else if(t!=c->getTypeId())t = -1;
						}
					}
					if(t>0){
						room->fillAG(dc->getSubcards(),player);
						ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"thyizheng1",true);
						room->clearAG(player);
						if(tp){
							room->doAnimate(1,player->objectName(),tp->objectName());
							tp->obtainCard(dc);
						}
					}else if(t<0){
						foreach(int id,dc->getSubcards())
							room->throwCard(id,objectName(),room->getCardOwner(id),player);
					}
				}
			}
		}
		return false;
	}
};

GuilinCard::GuilinCard()
{
	target_fixed = true;
}

void GuilinCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	room->removePlayerMark(source,"@guilin");
	room->doSuperLightbox(source,"guilin");
	int n = source->getMaxHp()-source->getHp();
	room->recover(source,RecoverStruct("guilin",source,n));
	//source->drawCards(n,"guilin");
	room->detachSkillFromPlayer(source,"thyizheng");
	source->setTag("boxuan_revise", true);
	room->changeTranslation(source,"boxuan",1);
}

class Guilinvs : public ZeroCardViewAsSkill
{
public:
	Guilinvs(): ZeroCardViewAsSkill("guilin")
	{
		response_pattern = "@@guilin";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("@guilin")>0;
	}

	const Card*viewAs()const
	{
		return new GuilinCard;
	}
};

class Guilin : public TriggerSkill
{
public:
	Guilin(): TriggerSkill("guilin")
	{
		events << Dying;
		limit_mark = "@guilin";
		frequency = Skill::Limited;
		view_as_skill = new Guilinvs;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Dying){
			DyingStruct dy = data.value<DyingStruct>();
			if(dy.who==player&&player->getHp()<1&&player->getMark("@guilin")>0){
				room->askForUseCard(player,"@@guilin","guilin0");
			}
		}
		return false;
	}
};

class ThGuying : public TriggerSkill
{
public:
	ThGuying(): TriggerSkill("thguying")
	{
		events << Damaged << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(player->getMark("&thguying+#"+p->objectName())>0){
					room->setPlayerMark(player,"&thguying+#"+p->objectName(),0);
					room->sendCompulsoryTriggerLog(p,objectName());
					int n = qMin(5,player->getMaxHp());
					player->drawCards(n,objectName());
					n = player->getHandcardNum()-player->getMaxHp();
					if(n<1||player->isDead()||p->isDead())continue;
					const Card*dc = room->askForExchange(player,objectName(),n,n);
					if(dc)p->obtainCard(dc,false);
				}
			}
		}else{
			if(player->getPhase()==Player::Finish){
				bool has = false;
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->getMark("&thguying+#"+player->objectName())>0){
						room->setPlayerMark(p,"&thguying+#"+player->objectName(),0);
						has = true;
					}
				}
				if(player->hasSkill(objectName())){
					int n = 1;
					if(has)n++;
					QList<ServerPlayer*>tps = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,n,"thguying0:"+QString::number(n),true);
					if(tps.length()>0){
						player->peiyin(this);
						foreach(ServerPlayer*p,tps)
							room->setPlayerMark(p,"&thguying+#"+player->objectName(),1);
					}
				}
			}
		}
		return false;
	}
};

ThMuzhenCard::ThMuzhenCard()
{
	will_throw = false;
}

bool ThMuzhenCard::targetFilter(const QList<const Player*>&targets,const Player*target,const Player*Self)const
{
	return targets.isEmpty()&&target!=Self;
}

void ThMuzhenCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	int n = subcardsLength();
	const Card*c = Sanguosha->getCard(getEffectiveId());
	foreach(ServerPlayer*p,targets){
		room->giveCard(source,p,this,"thmuzhen");
		QString choice = room->askForChoice(source,"thmuzhen","1+2+3",QVariant::fromValue(p));
		if(choice=="1"){
			const Card*dc = room->askForDiscard(p,"thmuzhen",n,n,false,"","^"+c->getType());
			if(!dc||dc->subcardsLength()<n)
				room->showAllCards(p);
		}
		if(choice=="2"){
			Card*dc = dummyCard(p->getCards("ej"));
			source->obtainCard(dc);
		}
		if(choice=="3"){
			Card*dc = dummyCard();
			foreach(int id,room->showDrawPile(p,n,"thmuzhen",false)){
				const Card*bc = Sanguosha->getCard(id);
				if(bc->getType()==c->getType())
					dc->addSubcard(id);
			}
			source->obtainCard(dc);
		}
	}
	foreach(QString m,source->getMarkNames()){
		if(m.contains("&thmuzhen+")){
			n = source->getMark(m)+1;
			room->setPlayerMark(source,m,0);
			m.remove("-PlayClear");
			QStringList ms = m.split("+");
			ms << c->getType();
			room->setPlayerMark(source,ms.join("+")+"-PlayClear",n);
			return;
		}
	}
	room->setPlayerMark(source,"&thmuzhen+"+c->getType()+"-PlayClear",1);
}

class ThMuzhen : public ViewAsSkill
{
public:
	ThMuzhen(): ViewAsSkill("thmuzhen")
	{
		response_pattern = "@@thmuzhen";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*to_select)const
	{
		int n = 0;
		foreach(QString m,Self->getMarkNames()){
			if(m.contains("&thmuzhen+")&&Self->getMark(m)>0){
				if(m.contains(to_select->getType()))return false;
				n = Self->getMark(m);
			}
		}
		if(n>0){
			if(selected.length()>0){
				if(selected.last()->getType()!=to_select->getType())
					return false;
			}
		}
		return selected.length()<=n;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		int n = 1;
		foreach(QString m,Self->getMarkNames()){
			if(m.contains("&thmuzhen+")&&Self->getMark(m)>0)
				n = Self->getMark(m);
		}
		if(cards.length()<n)return nullptr;
		Card*dc = new ThMuzhenCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getCardCount()>0;
	}
};

GengduCard::GengduCard()
{
	target_fixed = true;
}

void GengduCard::onUse(Room*room,CardUseStruct&use)const
{
	QList<int>ids = room->getAvailableCardList(use.from,"trick","gengdu");
	foreach(int id,ids){
		if(use.from->getMark(QString::number(id)+"tanluanId-Clear")>0&&Sanguosha->getCard(id)->isAvailable(use.from)){
			ids << id;
		}
	}
	if(ids.length()>0){
		room->fillAG(ids,use.from);
		int id = room->askForAG(use.from,ids,false,"gengdu","gengdu1");
		room->clearAG(use.from);
		if(id>-1){
			const Card*c = Sanguosha->getCard(id);
			room->setPlayerProperty(use.from,"gengduCN",c->objectName());
			room->askForUseCard(use.from,"@@gengdu","gengdu2:"+c->objectName());
		}
	}
}

class Gengduvs : public ViewAsSkill
{
public:
	Gengduvs(): ViewAsSkill("gengdu")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*card)const
	{
		return selected.isEmpty()&&card->isRed()
		&&Sanguosha->getCurrentCardUsePattern()=="@@gengdu";
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@gengdu"){
			if(cards.isEmpty())return nullptr;
			Card*dc = Sanguosha->cloneCard(Self->property("gengduCN").toString());
			dc->setSkillName("gengdu");
			dc->addSubcards(cards);
			return dc;
		}
		return new GengduCard;
	}

	bool isEnabledAtResponse(const Player*,const QString&pattern)const
	{
		return pattern.contains("@@gengdu");
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("&gengdu+red-PlayClear")>0&&player->getCardCount()>0;
	}
};

class Gengdu : public TriggerSkill
{
public:
	Gengdu(): TriggerSkill("gengdu")
	{
		events << EventPhaseStart << CardFinished << PreCardUsed;
		view_as_skill = new Gengduvs;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				QList<int>ids = room->showDrawPile(player,4,objectName());
				room->fillAG(ids,player);
				int cid = room->askForAG(player,ids,false,objectName(),"gengdu0");
				Card*dc = dummyCard();
				const Card*c = Sanguosha->getCard(cid);
				foreach(int id,ids){
					if(Sanguosha->getCard(id)->getColor()==c->getColor()){
						dc->addSubcard(id);
						ids.removeOne(id);
					}
				}
				room->setPlayerMark(player,"&gengdu+"+c->getColorString()+"-PlayClear",ids.length());
				player->obtainCard(dc);
				room->clearAG(player);
				room->throwCard(ids,objectName(),nullptr);
			}
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isNDTrick()){
				room->addPlayerMark(player,use.card->objectName()+"gengduUse-Clear");
				if(use.card->getSkillNames().contains(objectName()))
					room->removePlayerMark(player,"&gengdu+red-PlayClear");
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isBlack()&&use.card->getTypeId()>0&&player->getMark("&gengdu+black-PlayClear")>0){
				room->removePlayerMark(player,"&gengdu+black-PlayClear");
				foreach(int id,player->drawCardsList(2,objectName())){
					if(player->handCards().contains(id))
						room->setCardTip(id,"gengdu-Clear");
				}
			}
		}
		return false;
	}
};

class GengduLimit : public CardLimitSkill
{
public:
	GengduLimit(): CardLimitSkill("#GengduLimit")
	{
	}

	QString limitList(const Player*)const
	{
		return "ignore,use,response";
	}

	QString limitPattern(const Player*target,const Card*card)const
	{
		if(card->hasTip("gengdu")&&target->hasSkill("gengdu"))
			return card->toString();
		return "";
	}
};

class Gumai : public TriggerSkill
{
public:
	Gumai(): TriggerSkill("gumai")
	{
		events << DamageCaused << DamageInflicted;
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(player->getMark("gumaiUse_lun")<1&&player->getHandcardNum()>0&&player->askForSkillInvoke(this,data)){
			player->addMark("gumaiUse_lun");
			player->peiyin(this);
			room->showAllCards(player);
			DamageStruct damage = data.value<DamageStruct>();
			bool has = false;
			if(room->askForChoice(player,objectName(),"1+2",data)=="1"){
				has = player->damageRevises(data,1);
			}else{
				has = player->damageRevises(data,-1);
			}
			bool can = true;
			QList<int>ids = player->handCards();
			foreach(int id,ids){
				if(Sanguosha->getCard(id)->getColor()!=Sanguosha->getCard(ids.first())->getColor()){
					can = false;
					break;
				}
			}
			if(can&&room->askForCard(player,".","gumai0",data))
				player->removeMark("gumaiUse_lun");
			return has;
		}
		return false;
	}
};

class ThShefu : public TriggerSkill
{
public:
	ThShefu(): TriggerSkill("thshefu")
	{
		events << RoundEnd << CardsMoveOneTime << ConfirmDamage;
		frequency = Compulsory;
		global = true;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==RoundEnd){
			foreach(const Card*h,player->getHandcards())
				player->addMark("thshefuRound"+h->toString());
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getTypeId()>0){
				int n = player->getMark("thshefuRound"+damage.card->toString());
				CardUseStruct use = room->getUseStruct(damage.card);
				if(!use.m_isHandcard||use.from!=player)n = 0;
				if(player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					return player->damageRevises(data,n-damage.damage);
				}else if(damage.to->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(damage.to,this);
					return damage.to->damageRevises(data,n-damage.damage);
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player){
				foreach(const Card*h,player->getHandcards()){
					if(move.card_ids.contains(h->getId()))
						player->setMark("thshefuRound"+h->toString(),1);
				}
			}
		}
		return false;
	}
};

class Pigua : public TriggerSkill
{
public:
	Pigua(): TriggerSkill("pigua")
	{
		events << Damage;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>1&&damage.to->getHandcardNum()>0&&player->askForSkillInvoke(this,damage.to)){
				Card*dc = dummyCard();
				for(int i = 0; i < player->getMark("TurnLengthCount"); i++){
					int id = room->askForCardChosen(player,damage.to,"h",objectName(),false,Card::MethodNone,dc->getSubcards(),i>0);
					if(id<0)break;
					dc->addSubcard(id);
					if(dc->subcardsLength()>=damage.to->getHandcardNum())break;
				}
				player->obtainCard(dc,false);
				foreach(int id,player->handCards()){
					if(dc->getSubcards().contains(id))
						room->setCardTip(id,"pigua-Clear");
				}
			}
		}
		return false;
	}
};

class Dujun : public TriggerSkill
{
public:
	Dujun(): TriggerSkill("dujun")
	{
		events << GameStart << Damage << Damaged << CardUsed;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart){
			if(player->hasSkill(objectName())){
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"dujun0",false,true);
				if(tp){
					player->peiyin(this);
					room->setPlayerMark(tp,"&dujun+#"+player->objectName(),1);
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==1||use.card->isNDTrick()){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())&&player->getMark("&dujun+#"+p->objectName())>0){
						room->sendCompulsoryTriggerLog(p,objectName());
						use.no_respond_list << p->objectName();
						data.setValue(use);
					}
				}
			}
		}else {
			player->addMark("dujunDamage-Clear");
			if(player->getMark("dujunDamage-Clear")==1){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						if(player==p||player->getMark("&dujun+#"+p->objectName())>0){
							if(p->askForSkillInvoke(this)){
								p->peiyin(this);
								QList<int>ids = p->drawCardsList(2,objectName());
								foreach(int id,ids){
									if(p->handCards().contains(id))continue;
									ids.removeOne(id);
								}
								if(p->isDead()||ids.isEmpty())continue;
								ServerPlayer*tp = room->askForPlayerChosen(p,room->getOtherPlayers(p),"dujun1","dujun1",true);
								if(tp)tp->obtainCard(dummyCard(ids),false);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

JikunCard::JikunCard()
{
	will_throw = false;
}

bool JikunCard::targetFilter(const QList<const Player*>&targets,const Player*target,const Player*Self)const
{
	if(targets.isEmpty()){
		return target!=Self;
	}
	int n = 0;
	foreach(const Player*p,Self->getAliveSiblings(true)){
		n = qMax(n,p->getHandcardNum());
	}
	return targets.length()<2&&target->getHandcardNum()>=n;
}

bool JikunCard::targetsFeasible(const QList<const Player*>&targets,const Player*)const
{
	return targets.length()==2;
}

void JikunCard::onUse(Room*room,CardUseStruct&use)const
{
	room->setTag("jikunUse",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void JikunCard::use(Room*room,ServerPlayer*,QList<ServerPlayer*>&)const
{
	CardUseStruct use = room->getTag("jikunUse").value<CardUseStruct>();
	int id = use.to.last()->getRandomHandCardId();
	if(id>=0){
		room->obtainCard(use.to.first(),id,false);
	}
}

class Jikunvs : public ViewAsSkill
{
public:
	Jikunvs(): ViewAsSkill("jikun")
	{
		response_pattern = "@@jikun";
	}

	bool viewFilter(const QList<const Card*>&,const Card*)const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*>&)const
	{
		return new JikunCard;
	}
};

class Jikun : public TriggerSkill
{
public:
	Jikun(): TriggerSkill("jikun")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Jikunvs;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
				foreach(Player::Place p,move.from_places){
					if(p==Player::PlaceHand||p==Player::PlaceEquip){
						room->addPlayerMark(player,"&jikun");
						if(player->getMark("&jikun")>=5){
							room->setPlayerMark(player,"&jikun",0);
							room->askForUseCard(player,"@@jikun","jikun0");
						}
					}
				}
			}
		}
		return false;
	}
};

JieshiCard::JieshiCard()
{
	target_fixed = true;
}

void JieshiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList choices;
	QList<int>ids = room->getDiscardPile();
	ids << room->getDrawPile();
	qShuffle(ids);
	foreach(int id,ids){
		QString n = QString::number(Sanguosha->getCard(id)->nameLength());
		if(choices.contains(n))continue;
		choices << n;
	}
	QString cn = room->askForChoice(source,"jieshi",choices.join("+"));
	source->addMark(cn+"jieshiCn-PlayClear");
	foreach(int id,ids){
		if(cn==QString::number(Sanguosha->getCard(id)->nameLength())){
			room->obtainCard(source,id);
			break;
		}
	}
	if(source->getMark(cn+"jieshiCn-PlayClear")==1
	&&room->askForDiscard(source,"jieshi",cn.toInt(),cn.toInt(),true,true)){
		room->addPlayerHistory(source,"JieshiCard",0);
	}
}

class Jieshi : public ZeroCardViewAsSkill
{
public:
	Jieshi(): ZeroCardViewAsSkill("jieshi")
	{
		response_pattern = "@@jieshi";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("JieshiCard")<1;
	}

	const Card*viewAs()const
	{
		return new JieshiCard;
	}
};

class Zhidui : public TriggerSkill
{
public:
	Zhidui(): TriggerSkill("zhidui")
	{
		events << CardUsed;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				QStringList hs,hs2 = room->getTag("zhiduiHistory").toStringList();
				hs << QString::number(use.card->nameLength());
				hs << use.card->getType();
				room->setTag("zhiduiHistory",hs);
				if(hs2.length()>1&&player->getMark("zhiduiBan-Clear")<1&&player->hasSkill(objectName())){
					if(hs[0]==hs2[0]&&hs[1]==hs2[1]){
						if(player->askForSkillInvoke(objectName()+"$-1",data)){
							if(room->askForChoice(player,objectName(),"zhidui1+zhidui2",data)=="zhidui1"){
								player->drawCards(2,objectName());
							}else{
								use.m_addHistory = false;
								data.setValue(use);
							}
						}
					}else if(hs[0]!=hs2[0]&&hs[1]!=hs2[1]){
						player->addMark("zhiduiBan-Clear");
					}
				}
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->hasSkill(objectName(),true)){
						foreach(QString m,p->getMarkNames()){
							if(m.contains("&zhidui+"))
								room->setPlayerMark(p,m,0);
						}
						room->setPlayerMark(p,"&zhidui+"+hs[0]+"+"+hs[1]+"-Clear",1);
					}
				}
			}
		}
		return false;
	}
};

BizuCard::BizuCard()
{
	target_fixed = true;
}

void BizuCard::onUse(Room*room,CardUseStruct&use)const
{
	foreach(ServerPlayer*p,room->getAlivePlayers()){
		if(use.from->getHandcardNum()==p->getHandcardNum())
			use.to << p;
	}
	SkillCard::onUse(room,use);
}

void BizuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&tos)const
{
	QStringList choices;
	room->drawCards(tos,1,"bizu");
	choices << "bizuTos";
	foreach(ServerPlayer*p,tos){
		choices << p->objectName();
	}
	choices << "-Clear";
	source->addMark(choices.join("+"));
	if(source->getMark(choices.join("+"))>1){
		room->addPlayerMark(source,"bizuBan-Clear");
		room->recover(source,RecoverStruct("bizu",source));
	}
}

class Bizu : public ZeroCardViewAsSkill
{
public:
	Bizu(): ZeroCardViewAsSkill("bizu")
	{
		response_pattern = "@@bizu";
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getMark("bizuBan-Clear")<1;
	}

	const Card*viewAs()const
	{
		return new BizuCard;
	}
};

class Wuxie : public TriggerSkill
{
public:
	Wuxie(): TriggerSkill("wuxie")
	{
		events << EventPhaseChanging;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName()+"$-1","wuxie0",true,true);
			if(tp){
				QList<int>ids;
				room->swapCards(player,tp,"h",objectName());
				foreach(const Card*h,player->getHandcards()){
					if(h->isDamageCard())ids << h->getId();
				}
				room->moveCardsToEndOfDrawpile(player,ids,objectName());
			}
		}
		return false;
	}
};

class Shuhe : public TriggerSkill
{
public:
	Shuhe(): TriggerSkill("shuhe")
	{
		events << CardUsed;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getMark("shuheUse-Clear")<1&&p->hasTurn()&&p->hasSkill(objectName())){
						foreach(int id,room->getDrawPile()){
							const Card*c = Sanguosha->getCard(id);
							if(c->isKindOf("Analeptic")){
								if(p->askForSkillInvoke(objectName()+"$-1",data)){
									p->addMark("shuheUse-Clear");
									p->obtainCard(c);
									Card*dc = Sanguosha->cloneCard("analeptic");
									dc->setSkillName("_shuhe");
									QString choices = "shuhe2";
									if(dc->isAvailable(player))
										choices = "shuhe1+shuhe2";
									if(room->askForChoice(player,objectName(),choices,QVariant::fromValue(p))=="shuhe1"){
										room->useCard(CardUseStruct(dc,player));
										use.nullified_list << "_ALL_TARGETS";
										data.setValue(use);
									}else
										p->drawCards(1,objectName());
									dc->deleteLater();
								}
								break;
							}
						}
					}
				}
			}
		}
		return false;
	}
};

JilieCard::JilieCard()
{
	target_fixed = true;
}

void JilieCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QList<int> suits;
	foreach(int id,subcards){
		int n = Sanguosha->getCard(id)->getSuit();
		if(suits.contains(n))continue;
		suits << n;
	}
	for(int i = 0; i < suits.length()*2; i++){
		JudgeStruct judge;
		judge.reason = "jilie";
		judge.who = source;
		judge.pattern = "Slash";
		room->judge(judge);
		if(judge.isGood()&&source->isAlive()){
			room->setPlayerProperty(source,"jilieCn",judge.card->objectName());
			room->askForUseCard(source,"@@jilie","jilie0:"+judge.card->objectName());
		}
		if(source->isDead())break;
	}
	
}

class Jilievs : public ViewAsSkill
{
public:
	Jilievs(): ViewAsSkill("jilie")
	{
		response_pattern = "@@jilie";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*card)const
	{
		if(Sanguosha->getCurrentCardUsePattern()==response_pattern){
			return false;
		}
		return selected.length()<Self->getHp()&&!Self->isJilei(card);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(Sanguosha->getCurrentCardUsePattern()==response_pattern){
			Card*dc = Sanguosha->cloneCard(Self->property("jilieCn").toString());
			dc->setSkillName("_jilie");
			return dc;
		}
		if(cards.length()<Self->getHp())return nullptr;
		Card*dc = new JilieCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("JilieCard")<1;
	}
};

class Jilie : public TriggerSkill
{
public:
	Jilie(): TriggerSkill("jilie")
	{
		events << ConfirmDamage << PreCardUsed;
		view_as_skill = new Jilievs;
	}

	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				player->addMark("jilieNum-Clear");
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&damage.card->getSkillNames().contains(objectName())){
				player->damageRevises(data,player->getMark("jilieNum-Clear")-damage.damage);
			}
		}
		return false;
	}
};

class Qingleng : public TriggerSkill
{
public:
	Qingleng(): TriggerSkill("qingleng")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->isBlack()&&use.to.contains(player)){
				if(use.card->isVirtualCard()&&use.card->getEffectiveId()>=0)
					return false;
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(player->canDiscard(p,"he"))tps << p;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName()+"$-1","qingleng0",true,true);
				if(tp){
					int id = room->askForCardChosen(player,tp,"he",objectName(),false,Card::MethodDiscard);
					if(id<0)return false;
					room->throwCard(id,objectName(),tp,player);
					if(Sanguosha->getCard(id)->isKindOf("EquipCard")){
						use.nullified_list << player->objectName();
						data.setValue(use);
					}else if(!use.card->isKindOf("DelayedTrick")
						&&player->askForSkillInvoke("qingleng","1:"+use.card->objectName(),false)){
						QStringList cns = player->getTag("danduCards").toStringList();
						cns << use.card->objectName();
						player->setTag("danduCards", cns);
					}
				}
			}
		}
		return false;
	}
};

class Danduvs : public ZeroCardViewAsSkill
{
public:
	Danduvs(): ZeroCardViewAsSkill("dandu")
	{
		response_pattern = "@@dandu";
	}

	const Card*viewAs()const
	{
		Card*dc = Sanguosha->cloneCard(Self->property("danduCn").toString());
		dc->setSkillName("_dandu");
		return dc;
	}
};

class Dandu : public TriggerSkill
{
public:
	Dandu(): TriggerSkill("dandu")
	{
		events << EventPhaseChanging << EventPhaseEnd << HpRecover << Damaged;
		view_as_skill = new Danduvs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::RoundStart)return false;
			if(player->getMark("danduCards")>0){
				player->setMark("danduCards",0);
				QStringList cns = player->getTag("danduCards").toStringList();
				player->removeTag("danduCards");
				room->setPlayerMark(player,"danduUse",1);
				foreach(QString cn,cns){
					Card*dc = Sanguosha->cloneCard(cn);
					dc->setSkillName("_dandu");
					dc->deleteLater();
					if(!dc->isAvailable(player))break;
					room->setPlayerProperty(player,"danduCn",cn);
					if(!room->askForUseCard(player,"@@dandu","dandu1:"+cn)||player->isDead())
						break;
				}
				room->setPlayerMark(player,"danduUse",0);
			}
		}else if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->getHandcardNum()>0&&player->hasSkill(objectName())){
				const Card*dc = room->askForExchange(player,objectName(),5,1,false,"dandu0",true,"BasicCard,TrickCard+^DelayedTrick");
				if(dc){
					player->skillInvoked(objectName());
					room->showCard(player,dc->getSubcards());
					QStringList cns = player->getTag("danduCards").toStringList();
					foreach(int id,dc->getSubcards()){
						cns << Sanguosha->getCard(id)->objectName();
					}
					player->setTag("danduCards", cns);
					player->addMark("danduCards");
				}
			}
		}else{
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->getMark("danduUse")>0){
					room->sendCompulsoryTriggerLog(p,objectName());
					room->ignoreCards(p,room->drawCardsList(p,1,objectName()));
				}
			}
		}
		return false;
	}
};

class ThHcZhuiji : public TriggerSkill
{
public:
	ThHcZhuiji(): TriggerSkill("thhczhuiji")
	{
		events << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				bool has = true;
				foreach(ServerPlayer*p,use.to){
					if(player!=p){
						if(has){
							has = false;
							room->sendCompulsoryTriggerLog(player,this);
						}
						room->setPlayerMark(player,p->objectName()+"thhczhuijiBf-Clear",1);
					}
				}
			}
		}
		return false;
	}
};

class ThHcZhuijiDistance : public DistanceSkill
{
public:
	ThHcZhuijiDistance(): DistanceSkill("#ThHcZhuijiDistance")
	{
	}

	int getFixed(const Player*from,const Player*to)const
	{
		if(from->getMark(to->objectName()+"thhczhuijiBf-Clear")>0&&from->hasSkill("thhczhuiji"))
			return 1;
		return 0;
	}
};

QuxianCard::QuxianCard()
{
}

bool QuxianCard::targetFilter(const QList<const Player*>&targets,const Player*,const Player*)const
{
	return targets.isEmpty();
}

void QuxianCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(int id,room->getDrawPile()){
		if(Sanguosha->getCard(id)->isKindOf("Slash")){
			room->obtainCard(source,id);
			break;
		}
	}
	foreach(ServerPlayer*p,targets){
		QList<ServerPlayer*>has;
		foreach(ServerPlayer*q,room->getAllPlayers()){
			if(q->inMyAttackRange(p))has << q;
		}
		int n = 0;
		room->removeTag("quxianDamage"+p->objectName());
		foreach(ServerPlayer*q,has){
			if(q->isAlive()&&room->askForUseSlashTo(q,p,"quxian1:"+p->objectName(),true,false,false,nullptr,nullptr,"quxianUse")){
				source->drawCards(1,"quxian");
				has.removeOne(q);
				n++;
			}
			if(p->isDead())break;
		}
		if(room->getTag("quxianDamage"+p->objectName()).toBool()||n<1)continue;
		foreach(ServerPlayer*q,has)
			room->loseHp(q,n,true,source,"quxian");
	}
}

class Quxianvs : public ZeroCardViewAsSkill
{
public:
	Quxianvs(): ZeroCardViewAsSkill("quxian")
	{
		response_pattern = "@@quxian";
	}

	const Card*viewAs()const
	{
		return new QuxianCard;
	}
};

class Quxian : public TriggerSkill
{
public:
	Quxian(): TriggerSkill("quxian")
	{
		events << DamageDone << EventPhaseChanging;
		view_as_skill = new Quxianvs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("quxianUse"))
				room->setTag("quxianDamage"+player->objectName(),true);
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if((change.to==Player::NotActive||change.from==Player::NotActive)&&player->hasSkill(objectName()))
				room->askForUseCard(player,"@@quxian","quxian0");
		}
		return false;
	}
};

class Huashang : public TriggerSkill
{
public:
	Huashang(): TriggerSkill("huashang")
	{
		events << GameStart << CardFinished << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==GameStart){
			QStringList choices,has;
			for(int i = 0; i < 5; i++){
				if(player->hasEquipArea(i))
					choices << QString("EquipArea%1").arg(i);
			}
			if(choices.isEmpty())return false;
			QList<const Card*>cs;
			foreach(int id,room->getDrawPile()){
				const Card*c = Sanguosha->getCard(id);
				foreach(const Card*bc,cs){
					if(bc->getColor()==c->getColor()){
						c = nullptr;
						break;
					}
				}
				if(c)cs << c;
			}
			if(cs.isEmpty())return false;
			room->sendCompulsoryTriggerLog(player,this);
			for(int i = 0; i < cs.length(); i++){
				QString choice = room->askForChoice(player,"huashang",choices.join("+"));
				choices.removeOne(choice);
				has << choice;
				if(choices.isEmpty())break;
			}
			QList<CardsMoveStruct> exchangeMove;
			for(int i = 0; i < has.length(); i++){
				QString cn = has[i];
				cn.remove("EquipArea");
				int n = cn.toInt();
				if(cn.contains("0"))cn = "_zhizhe_weapon";
				else if(cn.contains("1"))cn = "_zhizhe_armor";
				else if(cn.contains("2"))cn = "_zhizhe_defensivehorse";
				else if(cn.contains("3"))cn = "_zhizhe_offensivehorse";
				else if(cn.contains("4"))cn = "_zhizhe_treasure";
				WrappedCard*card = Sanguosha->getWrappedCard(cs[i]->getId());
				card->takeOver(Sanguosha->cloneCard(cn,cs[i]->getSuit(),cs[i]->getNumber()));
				room->notifyUpdateCard(player,cs[i]->getId(),card);
				if(player->getEquips(n).length()>=player->getEquipArea(n)){
					CardsMoveStruct move2(player->getEquip(n)->getEffectiveId(),nullptr,Player::DiscardPile,
						CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,player->objectName(),"huashang","change equip"));
					exchangeMove.append(move2);
				}
				CardsMoveStruct move1(cs[i]->getId(),player,Player::PlaceEquip,
					CardMoveReason(CardMoveReason::S_REASON_USE,player->objectName(),"huashang",""));
				exchangeMove.append(move1);
				QStringList info;
				info << cn << cs[i]->getSuitString()<< QString::number(cs[i]->getNumber());
				room->setTag("ZhizheFilter_" + cs[i]->toString(),info.join("+"));
				info = room->getTag("huashangEquip").toStringList();
				info << cs[i]->toString();
				room->setTag("huashangEquip",info);
			}
			foreach(ServerPlayer*p,room->getAlivePlayers())
				room->acquireSkill(p,"#zhizhe");
			room->moveCardsAtomic(exchangeMove,true);
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getHandcardNum()>0){
				QStringList choices;
				for(int i = 0; i < 5; i++){
					if(player->hasEquipArea(i))
						choices << QString("EquipArea%1").arg(i);
				}
				if(choices.isEmpty())return false;
				const Card*dc = room->askForCard(player,"^EquipCard|"+use.card->getSuitString(),"huashang0:"+use.card->getSuitString(),data,Card::MethodNone);
				if(dc){
					player->skillInvoked(this);
					QString choice = room->askForChoice(player,"huashang",choices.join("+"));
					choice.remove("EquipArea");
					int n = choice.toInt();
					if(choice.contains("0"))choice = "_zhizhe_weapon";
					else if(choice.contains("1"))choice = "_zhizhe_armor";
					else if(choice.contains("2"))choice = "_zhizhe_defensivehorse";
					else if(choice.contains("3"))choice = "_zhizhe_offensivehorse";
					else if(choice.contains("4"))choice = "_zhizhe_treasure";
					WrappedCard*card = Sanguosha->getWrappedCard(dc->getId());
					card->takeOver(Sanguosha->cloneCard(choice,dc->getSuit(),dc->getNumber()));
					room->notifyUpdateCard(player,dc->getId(),card);
					QList<CardsMoveStruct> exchangeMove;
					if(player->getEquips(n).length()>=player->getEquipArea(n)){
						CardsMoveStruct move2(player->getEquip(n)->getId(),nullptr,Player::DiscardPile,
							CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,player->objectName(),"huashang","change equip"));
						exchangeMove.append(move2);
					}
					CardsMoveStruct move1(dc->getId(),player,Player::PlaceEquip,
						CardMoveReason(CardMoveReason::S_REASON_USE,player->objectName(),"huashang",""));
					exchangeMove.append(move1);
					QStringList info;
					info << choice << dc->getSuitString()<< QString::number(dc->getNumber());
					room->setTag("ZhizheFilter_" + dc->toString(),info.join("+"));
					info = room->getTag("huashangEquip").toStringList();
					info << dc->toString();
					room->setTag("huashangEquip",info);
					foreach(ServerPlayer*p,room->getAlivePlayers())
						room->acquireSkill(p,"#zhizhe");
					room->moveCardsAtomic(exchangeMove,true);
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player){
				if(move.from_places.contains(Player::PlaceEquip)&&move.to_place!=Player::PlaceEquip){
					QStringList info = room->getTag("huashangEquip").toStringList();
					foreach(int id,move.card_ids){
						if(info.contains(QString::number(id))){
							info.removeOne(QString::number(id));
							room->setTag("huashangEquip",info);
							room->removeTag("ZhizheFilter_"+QString::number(id));
						}
					}
				}
				if(move.from_places.contains(Player::PlaceHand)){
					QList<int> suits;
					foreach(const Card*e,player->getEquips()){
						if(suits.contains(e->getSuit()))continue;
						suits.append(e->getSuit());
					}
					int n = suits.length()-player->getHandcardNum();
					if(n>0){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(n,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Yuzhi : public TriggerSkill
{
public:
	Yuzhi(): TriggerSkill("yuzhi")
	{
		events << TargetConfirming << CardAsked << ConfirmDamage << EventPhaseStart;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				if(player->getMark("yuzhi1")<1&&player->canDiscard(player,"e")&&room->askForCard(player,".|.|.|equipped","yuzhi0",data)){
					player->addMark("yuzhi1");
					player->setTag("yuzhi1", use.card->toString());
				}else{
					room->setCardFlag(use.card,"yuzhi2");
				}
			}
		}else if(event==CardAsked){
			QStringList patterns = data.toStringList();
			if(patterns[0]=="jink"&&patterns.length()>3&&player->getTag("yuzhi1").toString()==patterns[3]){
				player->removeTag("yuzhi1");
				Card*dc = Sanguosha->cloneCard("jink");
				dc->setSkillName("_yuzhi");
				room->provide(dc);
				dc->deleteLater();
				return true;
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("yuzhi2"))
				player->damageRevises(data,1);
		}else if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart)
				player->setMark("yuzhi1",0);
		}
		return false;
	}
};

ZouyiCard::ZouyiCard()
{
	target_fixed = true;
}

void ZouyiCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&)const
{
	QStringList choices;
	choices << "zouyi1";
	if(source->canDiscard("he"))
		choices << "zouyi2";
	for(int i = 0; i < 2; i++){
		QString choice = room->askForChoice(source,"zouyi",choices.join("+"));
		if(choice=="cancel")break;
		if(i==0)choices << "cancel";
		choices.removeOne(choice);
        source->setTag("zouyi", choice);
		if(choice=="zouyi1"){
			source->drawCards(2,"zouyi");
			QList<ServerPlayer*>tps;
			foreach(ServerPlayer*p,room->getOtherPlayers(source)){
				if(source->canDiscard(p,"he"))tps << p;
			}
			ServerPlayer*tp = room->askForPlayerChosen(source,tps,"zouyi","zouyi10",true);
			if(tp){
				room->doAnimate(1,source->objectName(),tp->objectName());
				int id = room->askForCardChosen(source,tp,"he",objectName(),false,Card::MethodDiscard);
				if(id>0)room->throwCard(id,"zouyi",tp,source);
			}
		}else{
			room->askForDiscard(source,"zouyi",1,1,false,true);
			ServerPlayer*tp = room->askForPlayerChosen(source,room->getOtherPlayers(source),"zouyi","zouyi20",true);
			if(tp){
				room->doAnimate(1,source->objectName(),tp->objectName());
				room->drawCards(tp,2,"zouyi");
			}
		}
		if(source->isDead())return;
	}
	int n = 0;
	foreach(ServerPlayer*p,room->getAlivePlayers()){
		if(p->getHandcardNum()==source->getHandcardNum())
			n++;
	}
	source->addMark("yanxiNum",n);
	source->setSkillDescriptionSwap("yanxi","%arg1",QString::number(source->getMark("yanxiNum")));
	room->changeTranslation(source,"yanxi",1);
	room->recover(source,RecoverStruct("zouyi",source,n));
}

class Zouyi : public ZeroCardViewAsSkill
{
public:
	Zouyi(): ZeroCardViewAsSkill("zouyi")
	{
	}
	const Card*viewAs()const
	{
		return new ZouyiCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("ZouyiCard")<1;
	}
};

class Yanxi : public TriggerSkill
{
public:
	Yanxi(): TriggerSkill("yanxi")
	{
		events << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(!p->hasSkill(objectName()))continue;
				while(p->getMark("yanxiNum")>0&&p->isAlive()&&player->isAlive()){
					Card*dc = Sanguosha->cloneCard("slash");
					dc->setSkillName(objectName());
					dc->deleteLater();
					if(p->canSlash(player,dc,false)&&p->askForSkillInvoke(objectName(),player,false)){
						p->removeMark("yanxiNum");
						p->setSkillDescriptionSwap("yanxi","%arg1",QString::number(p->getMark("yanxiNum")));
						room->changeTranslation(p,"yanxi",1);
						room->useCard(CardUseStruct(dc,p,player));
					}else
						break;
				}
			}
		}
		return false;
	}
};

class Yuxian : public TriggerSkill
{
public:
	Yuxian(): TriggerSkill("yuxian")
	{
		events << CardUsed << RoundStart;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				player->addMark("yuxianUse-Clear");
				int n = player->getMark("yuxianUse-Clear");
				if(n>4)return false;
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->isAlive()&&player->isAlive()&&p->hasSkill(objectName(),true)){
						foreach(QString m,p->getMarkNames()){
							if(m.contains("&yuxian+")){
								QStringList suits = m.split("+");
								if(suits.length()>n&&suits[n].startsWith(use.card->getSuitString())
									&&p->askForSkillInvoke(objectName()+"$-1",player)){
									QList<ServerPlayer*>aps;
									aps << player << p,
									room->drawCards(aps,1,objectName());
								}
								break;
							}
						}
					}
				}
			}
		}else if(event==RoundStart){
			if(player->hasSkill(objectName())&&player->getHandcardNum()>0){
				const Card*dc = room->askForExchange(player,objectName(),4,1,false,"yuxian0",true);
				if(dc){
					player->skillInvoked(objectName());
					room->showCard(player,dc->getSubcards());
					QStringList suits;
					foreach(int id,dc->getSubcards()){
						suits << Sanguosha->getCard(id)->getSuitString()+"_char";
					}
					room->setPlayerMark(player,"&yuxian+"+suits.join("+")+"_lun",1);
				}
			}
		}
		return false;
	}
};

class Minshan : public TriggerSkill
{
public:
	Minshan(): TriggerSkill("minshan")
	{
		events << Damaged;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for(int i = 0; i < damage.damage; i++){
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"minshan0",true,true);
				if(tp){
					QStringList suits;
					foreach(QString m,player->getMarkNames()){
						if(m.contains("&yuxian+"))suits = m.split("+");
					}
					Card*dc = dummyCard();
					foreach(int id,room->getDrawPile()){
						const Card*c = Sanguosha->getCard(id);
						foreach(QString s,suits){
							if(s.startsWith(c->getSuitString())){
								dc->addSubcard(id);
								break;
							}
						}
						if(dc->subcardsLength()>1)break;
					}
					foreach(int id,room->getDrawPile()){
						if(dc->subcardsLength()>1)break;
						if(dc->getSubcards().contains(id))continue;
						dc->addSubcard(id);
					}
					tp->obtainCard(dc);
					if(player->isDead())break;
				}else
					break;
			}
		}
		return false;
	}
};

FuhuiCard::FuhuiCard()
{
}

bool FuhuiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("fuhui");
		card->deleteLater();
		return card->targetFilter(targets,to_select,Self);
	}
	return false;
}

bool FuhuiCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;

	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("fuhui");
		card->deleteLater();
		return card->targetFixed();
	}
	return false;
}

bool FuhuiCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("fuhui");
		card->deleteLater();
		return card->targetsFeasible(targets,Self);
	}
	return false;
}

const Card*FuhuiCard::validateInResponse(ServerPlayer*user)const
{
	QStringList names = user_string.split("+");
	foreach(QString cn,names){
		if(user->getMark("fuhui_guhuo_remove_"+cn+"-Clear")>0)
			names.removeOne(cn);
	}
	Room*room = user->getRoom();
	QString cn = room->askForChoice(user,"fuhui",names.join("+"));
	room->addPlayerMark(user,"fuhui_guhuo_remove_"+cn+"-Clear");
	Card*dc = Sanguosha->cloneCard(cn);
	dc->addSubcards(subcards);
	dc->setSkillName("fuhui");
	dc->deleteLater();
	return dc;
}

const Card*FuhuiCard::validate(CardUseStruct&use)const
{
	QStringList names = user_string.split("+");
	foreach(QString cn,names){
		if(use.from->getMark("fuhui_guhuo_remove_"+cn+"-Clear")>0)
			names.removeOne(cn);
	}
	Room*room = use.from->getRoom();
	QString cn = room->askForChoice(use.from,"fuhui",names.join("+"));
	room->addPlayerMark(use.from,"fuhui_guhuo_remove_"+cn+"-Clear");
	Card*dc = Sanguosha->cloneCard(cn);
	dc->addSubcards(subcards);
	dc->setSkillName("fuhui");
	dc->deleteLater();
	return dc;
}

class Fuhuivs : public ViewAsSkill
{
public:
	Fuhuivs(): ViewAsSkill("fuhui")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*card)const
	{
		if(card->isEquipped())return false;
		if(selected.length()==1)return selected[0]->getSuit()!=card->getSuit();
		return selected.length()<2;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.length()<2)return nullptr;
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*dc = Self->getTag(objectName()).value<const Card*>();
			if(!dc)return nullptr;
			pattern = dc->objectName();
		}
		FuhuiCard*sc = new FuhuiCard;
		sc->setUserString(pattern);
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern)const
	{
		if(player->getHandcardNum()<2||pattern.contains("@@")||pattern.length()<3||!player->hasTurn()
		||Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE)return false;
		foreach(QString cn,pattern.split("+")){
			if(player->getMark("fuhui_guhuo_remove_"+cn+"-Clear")>0)continue;
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard")||dc->isNDTrick())
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->getHandcardNum()>1;
	}
};

class Fuhui : public TriggerSkill
{
public:
	Fuhui(): TriggerSkill("fuhui")
	{
		events << CardUsed;
		view_as_skill = new Fuhuivs;
	}

	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance(objectName(),true,true);
	}

	bool trigger(TriggerEvent triggerEvent,Room*,ServerPlayer*player,QVariant&data)const
	{
		if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				QStringList suits;
				foreach(int id,use.card->getSubcards()){
					suits << Sanguosha->getCard(id)->getSuitString();
				}
				player->addMark(suits.first()+suits.last()+"fuhuiSuit-Clear");
				if(player->getMark(suits.first()+suits.last()+"fuhuiSuit-Clear")<2
				&&player->getMark(suits.last()+suits.first()+"fuhuiSuit-Clear")<1
				&&player->askForSkillInvoke(objectName(),"fuhui0",false)){
					player->drawCards(1,objectName());
				}
			}
		}
		return false;
	}
};

MohuaCard::MohuaCard()
{
}

bool MohuaCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self)const
{
	const Card*card = Sanguosha->getCard(getEffectiveId());
	QString c2p = Self->property("mohuaCP").toString();
	foreach(const Player*p,Self->getAliveSiblings()){
		if(c2p.endsWith(p->objectName()))
			return card->targetFilter(targets,to_select,p);
	}
	return false;
}

bool MohuaCard::targetFixed()const
{
	if(Sanguosha->getCurrentCardUseReason()== CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	const Card*card = Sanguosha->getCard(getEffectiveId());
	return card->targetFixed();
}

bool MohuaCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self)const
{
	const Card*card = Sanguosha->getCard(getEffectiveId());
	QString c2p = Self->property("mohuaCP").toString();
	foreach(const Player*p,Self->getAliveSiblings()){
		if(c2p.endsWith(p->objectName()))
			return card->targetsFeasible(targets,p);
	}
	return false;
}

const Card*MohuaCard::validateInResponse(ServerPlayer*)const
{
	return Sanguosha->getCard(getEffectiveId());
}

const Card*MohuaCard::validate(CardUseStruct&)const
{
	return Sanguosha->getCard(getEffectiveId());
}

class Mohuavs : public ViewAsSkill
{
public:
	Mohuavs(): ViewAsSkill("mohua")
	{
		response_pattern = "@@mohua";
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*card)const
	{
		QStringList c2p = Self->property("mohuaCP").toString().split(":");
		return selected.isEmpty()&&card->sameNameWith(c2p[0]);
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;
		Card*dc = new MohuaCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Mohua : public TriggerSkill
{
public:
	Mohua(): TriggerSkill("mohua")
	{
		events << PreCardUsed << EventPhaseChanging;
		waked_skills = "#MohuaPro";
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play){
				QStringList pcs,c2t = player->getTag("mohuaC2T").toStringList();
				pcs << use.card->objectName();
				foreach(ServerPlayer*p,use.to){
					pcs << p->objectName();
				}
				c2t << pcs.join("+");
				player->setTag("mohuaC2T", c2t);
			}
			if(use.card->hasFlag("mohuaUse")){
				room->setPlayerMark(player,"guowu_two-PlayClear",0);
				QString c2p = player->property("mohuaCP").toString();
				foreach(ServerPlayer*q,room->getAlivePlayers()){
					room->setPlayerMark(q,"mohua_"+use.card->objectName(),0);
					if(c2p.endsWith(q->objectName())){
						use.from = q;
						data.setValue(use);
					}
				}
				if(player->getMark("mohuaUse_lun")<1){
					player->addMark("mohuaUse_lun");
					player->skillInvoked(objectName());
				}
			}
		}else{
			if(data.value<PhaseChangeStruct>().to != Player::NotActive)return false;
			QStringList c2t = player->getTag("mohuaC2T").toStringList();
			if(c2t.isEmpty())return false;
			player->removeTag("mohuaC2T");
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(p->isAlive()&&p->getMark("mohuaUse_lun")<1&&p->hasSkill(objectName())){
					foreach(QString p2t,c2t){
						if(player->isDead())return false;
						QStringList p2ts = p2t.split("+");
						Card*dc = Sanguosha->cloneCard(p2ts[0]);
						if(!dc->targetFixed()){
							room->setPlayerMark(p,"guowu_two-PlayClear",1);
							foreach(ServerPlayer*q,room->getAlivePlayers()){
								if(dc->targetFilter(QList<const Player*>(),q,player))continue;
								room->setPlayerMark(q,"mohua_"+p2ts[0],1);
							}
						}
						room->setPlayerProperty(p,"mohuaCP",p2ts[0]+":"+player->objectName());
						CardUseStruct use = room->askForUseCardStruct(p,p2ts[0],"mohua0:"+player->objectName()+"::"+p2ts[0],-1,Card::MethodUse,true,nullptr,nullptr,"mohuaUse");
						if(!dc->targetFixed()){
							room->setPlayerMark(p,"guowu_two-PlayClear",0);
							foreach(ServerPlayer*q,room->getAlivePlayers())
								room->setPlayerMark(q,"mohua_"+p2ts[0],0);
						}
						dc->deleteLater();
						if(p->isDead()||use.card==nullptr)break;
						int n = 0;
						foreach(ServerPlayer*t,use.to){
							if(p2ts.contains(t->objectName()))
								n++;
						}
						p->drawCards(n,objectName());
					}
				}
			}
		}
		return false;
	}
};

class MohuaPro : public ProhibitSkill
{
public:
	MohuaPro(): ProhibitSkill("#MohuaPro")
	{
	}

	bool isProhibited(const Player*,const Player*to,const Card*card,const QList<const Player*>&)const
	{
		return to->getMark("mohua_"+card->objectName())> 0;
	}
};

ShouhuCard::ShouhuCard()
{
}

bool ShouhuCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	return targets.isEmpty()&&to!=Self&&Self->canSlash(to);
}

void ShouhuCard::onUse(Room*room,CardUseStruct&use)const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("shouhu");
	dc->deleteLater();
	use.card = dc;
	QStringList tos;
	foreach(ServerPlayer*p,room->getAlivePlayers()){
		if(use.from->inMyAttackRange(p))tos << p->objectName();
	}
	use.from->setTag("shouhuTos", tos);
	SkillCard::onUse(room,use);
	foreach(ServerPlayer*p,use.to){
		if(p->isDead())continue;
		room->addPlayerMark(use.from,p->objectName()+"shouhuBf-Clear");
	}
}

class Shouhuvs : public ZeroCardViewAsSkill
{
public:
	Shouhuvs(): ZeroCardViewAsSkill("shouhu")
	{
	}

	const Card*viewAs()const
	{
		return new ShouhuCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("ShouhuCard")<1&&Slash::IsAvailable(player);
	}
};

class Shouhu : public TriggerSkill
{
public:
	Shouhu(): TriggerSkill("shouhu")
	{
		events << Death << CardFinished;
		view_as_skill = new Shouhuvs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive()&&target->getPhase()==Player::Play;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player&&death.damage->card->getSkillNames().contains(objectName())){
				foreach(int id,player->drawCardsList(player->getMaxHp()-player->getHandcardNum(),objectName())){
					if(player->hasCard(id))room->setCardTip(id,"shouhu");
				}
			}
		}else{
			if(player->usedTimes("ShouhuCard")>0){
				QStringList tos2,tos = player->getTag("shouhuTos").toStringList();
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(player->inMyAttackRange(p)){
						tos2 << p->objectName();
						if(tos.contains(p->objectName()))continue;
						room->addPlayerHistory(player,"ShouhuCard",0);
					}
				}
				player->setTag("shouhuTos", tos2);
			}
		}
		return false;
	}
};

class ShouhuBf : public DistanceSkill
{
public:
	ShouhuBf(): DistanceSkill("#ShouhuBf")
	{
	}

	int getCorrect(const Player*from,const Player*to)const
	{
		return from->getMark(to->objectName()+"shouhuBf-Clear")+to->getMark("&zijue+#num");
	}
};

class Ningzhun : public TriggerSkill
{
public:
	Ningzhun(): TriggerSkill("ningzhun")
	{
		events << CardFinished;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				player->addMark("sgsningzhunNum-Clear");
				if(player->getMark("sgsningzhunNum-Clear")%2==0&&player->hasSkill(objectName())){
					QStringList choices;
					if(player->getMark("ningzhun1-Clear")<1)
						choices << "ningzhun1";
					if(player->getMark("ningzhun2-Clear")<1)
						choices << "ningzhun2";
					if(player->getMark("ningzhun3-Clear")<1&&room->canMoveField())
						choices << "ningzhun3";
					if(choices.length()>0&&player->askForSkillInvoke(objectName()+"$-1",data)){
						QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
						player->addMark(choice+"-Clear");
						if(choice=="ningzhun1")
							room->addAttackRange(player,-1,false);
						else if(choice=="ningzhun2")
							room->addAttackRange(player,1,false);
						else
							room->moveField(player,objectName());
						if(choices.length()==1){
							foreach(QString m,player->getMarkNames()){
								if(m.startsWith("ningzhun"))
									player->setMark(m,0);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

ZijueCard::ZijueCard()
{
}

bool ZijueCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	return targets.isEmpty()&&to!=Self;
}

void ZijueCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		LogMessage log;
		log.type = "#ShouxiChoice";
		log.from = p;
		log.arg = room->askForChoice(p,"zijue","2+3+4",QVariant::fromValue(source));
		room->sendLog(log);
		int n = log.arg.toInt();
		Card*dc = room->askForExchange(source,"zijue",n,n,true,"zijue0:"+p->objectName(),true);
		if(dc){
			room->giveCard(source,p,dc,"zijue");
			room->recover(source,RecoverStruct("zijue",source));
			room->setPlayerMark(source,"&zijue+#num",n);
		}
	}
}

class Zijuevs : public ZeroCardViewAsSkill
{
public:
	Zijuevs(): ZeroCardViewAsSkill("zijue")
	{
	}

	const Card*viewAs()const
	{
		return new ZijueCard;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("ZijueCard")<1;
	}
};

class Zijue : public TriggerSkill
{
public:
	Zijue(): TriggerSkill("zijue")
	{
		events << PindianVerifying << Pindian << EventPhaseChanging;
		view_as_skill = new Zijuevs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==PindianVerifying){
			PindianStruct*pd = data.value<PindianStruct*>();
			LogMessage log;
			log.type = "#JiciUp";
			if(pd->from->getMark("&zijue+#num")>0){
				pd->from_number = qMin(13,pd->from_number+pd->from->getMark("&zijue+#num"));
				log.from = pd->from;
				log.arg = QString::number(pd->from_number);
				room->sendLog(log);
			}
			if(pd->to->getMark("&zijue+#num")>0){
				pd->to_number = qMin(13,pd->to_number+pd->from->getMark("&zijue+#num"));
				log.from = pd->to;
				log.arg = QString::number(pd->to_number);
				room->sendLog(log);
			}
		}else if(event==Pindian){
			PindianStruct*pd = data.value<PindianStruct*>();
			if(pd->from->getMark("&zijue+#num")>0){
				if(pd->from_number>pd->to_number)
					pd->from->drawCards(1,objectName());
				else
					pd->from->drawCards(pd->from->getMark("&zijue+#num"),objectName());
			}
			if(pd->to->getMark("&zijue+#num")>0){
				if(pd->to_number>pd->from_number)
					pd->to->drawCards(1,objectName());
				else
					pd->to->drawCards(pd->to->getMark("&zijue+#num"),objectName());
			}
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.from==Player::NotActive)
				room->setPlayerMark(player,"&zijue+#num",0);
		}
		return false;
	}
};

class Chibi : public TriggerSkill
{
public:
	Chibi(): TriggerSkill("chibi")
	{
		events << TargetSpecifying;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&player->distanceTo(use.to.last())>1){
				room->setTag("chibiData",data);
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())&&p->getMark("chibiBan-Clear")<1
						&&p->canPindian(player)&&p->askForSkillInvoke(objectName()+"$-1",player)){
						if(p->pindian(player,objectName())){
							use.nullified_list << "_ALL_TARGETS";
							if(p->isAlive()&&!room->getCardOwner(use.card->getEffectiveId()))
								p->obtainCard(use.card);
						}else{
							use.to << p;
							p->addMark("chibiBan-Clear");
						}
						data.setValue(use);
					}
				}
			}
		}
		return false;
	}
};

JianliCard::JianliCard()
{
	will_throw = false;
}

bool JianliCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	return targets.isEmpty()&&to!=Self;
}

void JianliCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	foreach(ServerPlayer*p,targets){
		room->giveCard(source,p,this,"jianli");
		p->setTag("jianliDamage", false);
		room->askForUseCard(p,"$.|.|.|hand","jianli0");
		if(p->getTag("jianliDamage").toBool()||p->isDead())continue;
		room->damage(DamageStruct("jianli",p,source));
	}
}

class Jianlivs : public ViewAsSkill
{
public:
	Jianlivs(): ViewAsSkill("jianli")
	{
	}

	bool viewFilter(const QList<const Card*>&selected,const Card*)const
	{
		return selected.length()<2;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;
		Card*dc = new JianliCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("JianliCard")<2;
	}
};

class Jianli : public TriggerSkill
{
public:
	Jianli(): TriggerSkill("jianli")
	{
		events << Damage;
		view_as_skill = new Jianlivs;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*,ServerPlayer*player,QVariant&)const
	{
		if(event==Damage){
			player->setTag("jianliDamage", true);
		}
		return false;
	}
};

class Qingzheng : public TriggerSkill
{
public:
	Qingzheng(): TriggerSkill("qingzheng")
	{
		events << HpChanged << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive()&&target->hasTurn();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		bool has = false;
		if(event==HpChanged){
			has = player->getHp()==1;
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand){
				has = move.to==player&&player->getHandcardNum()==1;
			}else if(move.to_place==Player::PlaceEquip){
				has = move.to==player&&player->getEquips().length()==1;
			}
			if(move.from_places.contains(Player::PlaceHand)){
				if(move.from==player&&player->getHandcardNum()==1)has = true;
			}else if(move.from_places.contains(Player::PlaceEquip)){
				if(move.from==player&&player->getEquips().length()==1)has = true;
			}
		}
		if(has&&player->hasSkill(objectName())){
			QStringList choices;
			int n = 0;
			if(player->getHp()==1)n++;
			if(player->getHandcardNum()==1)n++;
			if(player->getEquips().length()==1)n++;
			if(player->getMark("1qingzheng-Clear")<1)
				choices << QString("1qingzheng=%1").arg(n);
			if(player->getMark("2qingzheng-Clear")<1)
				choices << QString("2qingzheng=%1").arg(n);
			if(player->getMark("3qingzheng-Clear")<1)
				choices << QString("3qingzheng=%1").arg(n);
			if(choices.length()>0&&player->askForSkillInvoke(objectName()+"$-1",data)){
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				choices = choice.split("=");
				player->addMark(choices.first()+"-Clear");
				if(choice.contains("1"))
					player->drawCards(n,objectName());
				else if(choice.contains("2"))
					room->recover(player,RecoverStruct(objectName(),player,n));
				else{
					QList<int>ids;
					foreach(int id,room->getDrawPile()){
						Card*c = Sanguosha->getCard(id);
						if(c->isKindOf("EquipCard")){
							if(player->isProhibited(player,c))continue;
							ids << id;
						}
					}
					room->fillAG(ids,player);
					QList<ServerPlayer*>tps;
					tps << player;
					for(int i = 0; i < n; i++){
						if(ids.isEmpty()||player->isDead())break;
						int id = room->askForAG(player,ids,false,objectName());
						if(id<0)break;
						ids.removeAll(id);
						room->takeAG(player,id,false,tps);
						Card*c = Sanguosha->getCard(id);
						room->moveCardTo(c,nullptr,Player::PlaceTable,true);
						c->use(room,player,tps);
					}
					room->clearAG(player);
				}
				if(n>2){
					foreach(QString m,player->getMarkNames()){
						if(m.contains("qingzheng-Clear"))
							player->setMark(m,0);
					}
					foreach(int id,player->handCards()){
						room->setCardTip(id,"qingzheng");
					}
				}
			}
		}
		return false;
	}
};

class Gongtu : public TriggerSkill
{
public:
	Gongtu(): TriggerSkill("gongtu")
	{
		events << CardUsed << GameStart;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				QStringList cts = room->getTag("gongtuCts").toStringList();
				cts << use.card->getType();
				room->setTag("gongtuCts",cts);
				if(cts.length()<2)return false;
				foreach(ServerPlayer*p,room->getAllPlayers()){
					QStringList choices = p->getTag("gongtuCts").toStringList();
					if(choices.length()<2||!p->hasSkill(objectName()))continue;
					if(cts.last()==choices.last()&&cts.at(cts.length()-2)==choices.first()
						&&p->askForSkillInvoke(objectName()+"$-1")){
						QStringList choices2;
						choices = QString("A+2+3+4+5+6+7+8+9+10+J+Q+K").split("+");
						for(int i = 0; i < 2; i++){
							QString choice = room->askForChoice(player,objectName(),choices.join("+"));
							choices.removeOne(choice);
							choices2 << choice;
						}
						Card*dc = dummyCard();
						foreach(int id,room->getDrawPile()){
							Card*c = Sanguosha->getCard(id);
							if(choices2.contains(c->getNumberString())){
								choices2.removeOne(c->getNumberString());
								dc->addSubcard(id);
								if(choices2.isEmpty())break;
							}
						}
						player->obtainCard(dc);
					}
				}
			}
		}else if(player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1")){
			QStringList choices,choices2;
			choices << "basic" << "trick" << "equip";
			for(int i = 0; i < 2; i++){
				QString choice = room->askForChoice(player,objectName(),choices.join("+"));
				choices.removeOne(choice);
				choices2 << choice;
			}
			player->setTag("gongtuCts", choices2);
			room->setPlayerMark(player,"&gongtu+:+"+choices2.join("_char+")+"_char",1);
		}
		return false;
	}
};

HangzhuCard::HangzhuCard()
{
	will_throw = false;
}

bool HangzhuCard::targetFilter(const QList<const Player*>&targets,const Player*to,const Player*Self)const
{
	return targets.isEmpty()&&to!=Self;
}

void HangzhuCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&targets)const
{
	source->addToPile("hz_ji",subcards);
	QList<int>cns,ids,pids = source->getPile("hz_ji");
	foreach(int id1,pids){
		Card*c1 = Sanguosha->getCard(id1);
		foreach(int id2,pids){
			if(id1==id2||ids.contains(id2))continue;
			Card*c2 = Sanguosha->getCard(id2);
			if(c1->getNumber()==c2->getNumber())
				ids << id2;
		}
		if(cns.contains(c1->getNumber()))continue;
		cns.append(c1->getNumber());
	}
	pids.clear();
	foreach(int id1,subcards){
		Card*c1 = Sanguosha->getCard(id1);
		pids.append(c1->getNumber());
	}
	room->throwCard(ids,"hangzhu",nullptr);
	source->drawCards(ids.length(),"hangzhu");
	if(cns.length()==13&&source->isAlive()){
		ServerPlayer*tp = room->askForPlayerChosen(source,room->getAlivePlayers(),"hangzhu","hangzhu0",true);
		if(tp){
			room->giveCard(source,tp,source->getPile("hz_ji"),"hangzhu");
			room->recover(tp,RecoverStruct("hangzhu",source,tp->getMaxHp()-tp->getHp()));
		}
	}
	foreach(ServerPlayer*p,targets){
		ids.clear();
		foreach(const Card*c,p->getCards("he")){
			if(pids.contains(c->getNumber())&&p->canDiscard(c->getId()))
				ids << c->getId();
		}
		room->throwCard(ids,"hangzhu",p);
	}
}

class Hangzhu : public ViewAsSkill
{
public:
	Hangzhu(): ViewAsSkill("hangzhu")
	{
	}

	bool viewFilter(const QList<const Card*>&,const Card*)const
	{
		return true;
	}

	const Card*viewAs(const QList<const Card*>&cards)const
	{
		if(cards.isEmpty())return nullptr;
		Card*dc = new HangzhuCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player)const
	{
		return player->usedTimes("HangzhuCard")<1;
	}
};

class Zhangguan : public TriggerSkill
{
public:
	Zhangguan(): TriggerSkill("zhangguan")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->isAlive()&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1")){
						int n = p->getHp()-p->getHandcardNum();
						if(n>0)
							p->drawCards(n,objectName());
						else if(n==0){
							room->recover(p,RecoverStruct(objectName(),p));
						}else{
							room->askForDiscard(p,objectName(),-n,-n);
							if(p->isDead()||room->getCurrent()->getCardCount()<1)continue;
							n = room->askForCardChosen(p,room->getCurrent(),"he",objectName());
							if(n>=0)room->obtainCard(p,n,false);
						}
						if(player->isDead())break;
					}
				}
			}
		}
		return false;
	}
};

class Congfeng : public TriggerSkill
{
public:
	Congfeng(): TriggerSkill("congfeng")
	{
		events << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName==objectName())return false;
			if(move.to_place==Player::PlaceHand&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
				if((move.to!=player&&move.from==player)||(move.to==player&&move.from!=player)){
					move.card_ids = move.from->handCards()+move.from->getEquipsId();
					if(move.card_ids.length()>0){
						qShuffle(move.card_ids);
						move.reason.m_skillName==objectName();
						room->sendCompulsoryTriggerLog(player,this);
						room->moveCardTo(Sanguosha->getCard(move.card_ids.last()),(ServerPlayer*)move.to,move.to_place,move.reason,false);
					}
				}
			}
		}
		return false;
	}
};

class Yinbi : public TriggerSkill
{
public:
	Yinbi(): TriggerSkill("yinbi")
	{
		events << EventPhaseProceeding << HpChanged << CardUsed;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data)const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasSkill(objectName())){
				bool has = true;
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p!=player&&player->getHandcardNum()==p->getHandcardNum()){
						has = false;
						break;
					}
				}
				if(has)player->peiyin(objectName());
			}
		}else if(event==EventPhaseProceeding){
			if(player->getPhase()==Player::Discard&&player->hasSkill(objectName())){
				player->peiyin(objectName());
			}
		}
		foreach(ServerPlayer*p,room->getAlivePlayers()){
			if(p->hasSkill(objectName(),true)){
				int n = 0;
				foreach(ServerPlayer*q,room->getAlivePlayers()){
					if(p!=q)n = qMax(n,q->getMaxCards());
				}
				room->setPlayerMark(p,"yinbiMax",n);
			}
		}
		return false;
	}
};

class YinbiKeep : public MaxCardsSkill
{
public:
	YinbiKeep(): MaxCardsSkill("#YinbiKeep")
	{
	}
	int getFixed(const Player*target)const
	{
		if(target->hasSkill("yinbi"))
			return target->getMark("yinbiMax");
		return -1;
	}
};

class ThShuaiyan : public TriggerSkill
{
public:
	ThShuaiyan(): TriggerSkill("thshuaiyan")
	{
		events << EventPhaseEnd;
	}
	bool triggerable(const ServerPlayer*target)const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&)const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Draw||player->getPhase()==Player::Discard){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						if(p==player){
							if(p->askForSkillInvoke(objectName()+"$-1")){
								int n = 0;
								foreach(ServerPlayer*q,room->getAlivePlayers()){
									if(p->getHandcardNum()==q->getHandcardNum()) n++;
								}
								p->drawCards(n,objectName());
							}
						}else if(p->getHandcardNum()==player->getHandcardNum()){
							if(p->askForSkillInvoke(objectName()+"$-1")){
								if(p->canDiscard(player,"he")&&p->askForSkillInvoke("thshuaiyan0","0:"+player->objectName(),false)){
									int id = room->askForCardChosen(p,player,"he",objectName(),false,Card::MethodDiscard);
									if(id>=0) room->throwCard(id,objectName(),player,p);
								}else{
									p->drawCards(1,objectName());
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









TenyearHcPackage::TenyearHcPackage()
	: Package("tenyear_hc")
{








{//黄巾之乱
	General*sp_hansui = new General(this,"sp_hansui","qun",4);
	sp_hansui->addSkill(new SpNiluan);
	sp_hansui->addSkill(new Weiwu);
	addMetaObject<WeiwuCard>();

	General*zhujun = new General(this,"zhujun","qun",4);
	zhujun->addSkill(new Gongjian);
	zhujun->addSkill(new Kuimang);

	General*second_liuhong = new General(this,"second_liuhong","qun",4);
	second_liuhong->addSkill(new SecondYujue);
	second_liuhong->addSkill("tuxing");
	addMetaObject<SecondYujueCard>();
	skills << new Zhihu << new SecondZhihu;

	General*xushao = new General(this,"xushao","qun",4);
	xushao->addSkill(new Pingjian);
	addMetaObject<PingjianCard>();
}
{//诸侯伐董
	General*tenyear_quyi = new General(this,"tenyear_quyi","qun",4);
	tenyear_quyi->addSkill(new TenyearFuqi);
	tenyear_quyi->addSkill("jiaozi");

	General*hanfu = new General(this,"hanfu","qun",4);
	hanfu->addSkill(new Jieyingh);
	hanfu->addSkill(new Weipo);
	addMetaObject<JieyinghCard>();

	General*tenyear_dingyuan = new General(this,"tenyear_dingyuan*xh_tianzhu","qun",4);
	tenyear_dingyuan->addSkill(new Cixiao);
	tenyear_dingyuan->addSkill(new CixiaoSkill);
	tenyear_dingyuan->addSkill(new Xianshuai);
	tenyear_dingyuan->addSkill(new XianshuaiRecord);
	addMetaObject<CixiaoCard>();
	skills << new Panshi;

	General*wangrong = new General(this,"wangrong","qun",3,false);
	wangrong->addSkill(new Minsi);
	wangrong->addSkill(new MinsiTargetMod);
	wangrong->addSkill(new Jijing);
	wangrong->addSkill(new Zhuide);
	related_skills.insertMulti("minsi","#minsi-target");
	addMetaObject<MinsiCard>();
	addMetaObject<JijingCard>();

}
{//徐州风云
	General*tenyear_taoqian = new General(this,"tenyear_taoqian","qun",3);
	tenyear_taoqian->addSkill("zhaohuo");
	tenyear_taoqian->addSkill(new TenyearYixiang);
	tenyear_taoqian->addSkill(new TenyearYirang("tenyearyirang"));

	General*second_tenyear_taoqian = new General(this,"second_tenyear_taoqian","qun",3);
	second_tenyear_taoqian->addSkill("zhaohuo");
	second_tenyear_taoqian->addSkill("tenyearyixiang");
	second_tenyear_taoqian->addSkill(new TenyearYirang("secondtenyearyirang"));

	General*second_caosong = new General(this,"second_caosong","wei",4);
	second_caosong->addSkill("lilu");
	second_caosong->addSkill("thyizhengc");

	General*zhangmiao = new General(this,"zhangmiao","qun",4);
	zhangmiao->addSkill(new Mouni("mouni"));
	zhangmiao->addSkill(new MouniDying("mouni"));
	zhangmiao->addSkill(new Zongfan);
	zhangmiao->addRelateSkill("zhangu");
	related_skills.insertMulti("mouni","#mouni-dying");

	General*second_zhangmiao = new General(this,"second_zhangmiao","qun",4);
	second_zhangmiao->addSkill(new Mouni("secondmouni"));
	second_zhangmiao->addSkill(new MouniDying("secondmouni"));
	second_zhangmiao->addSkill("zongfan");
	second_zhangmiao->addRelateSkill("zhangu");
	related_skills.insertMulti("secondmouni","#secondmouni-dying");
	skills << new Zhangu;

	General*qiuliju = new General(this,"qiuliju","qun",6);
	qiuliju->setStartHp(4);
	qiuliju->addSkill(new Koulve("koulve"));
	qiuliju->addSkill(new Suirenq);

	General*second_qiuliju = new General(this,"second_qiuliju","qun",6);
	second_qiuliju->setStartHp(4);
	second_qiuliju->addSkill(new Koulve("secondkoulve"));
	second_qiuliju->addSkill("suirenq");

}
{//中原狼烟
	General*tenyear_dongcheng = new General(this,"tenyear_dongcheng","qun",4);
	tenyear_dongcheng->addSkill(new Xuezhao("xuezhao"));
	addMetaObject<XuezhaoCard>();

	General*second_tenyear_dongcheng = new General(this,"second_tenyear_dongcheng","qun",4);
	second_tenyear_dongcheng->addSkill(new Xuezhao("secondxuezhao"));
	addMetaObject<SecondXuezhaoCard>();

	General*tenyear_hucheer = new General(this,"tenyear_hucheer","qun",4);
	tenyear_hucheer->addSkill(new TenyearDaoji);
	tenyear_hucheer->addSkill(new TenyearDaojiLimit);
	tenyear_hucheer->addSkill(new Fuzhong);
	tenyear_hucheer->addSkill(new FuzhongMax);
	tenyear_hucheer->addSkill(new FuzhongDistance);
	related_skills.insertMulti("tenyeardaoji","#tenyeardaoji-limit");
	related_skills.insertMulti("fuzhong","#fuzhong-max");
	related_skills.insertMulti("fuzhong","#tenyeardaoji-distance");

	General*tenyear_zoushi = new General(this,"tenyear_zoushi","qun",3,false);
	tenyear_zoushi->addSkill(new TenyearHuoshui);
	tenyear_zoushi->addSkill(new TenyearHuoshuiClear);
	tenyear_zoushi->addSkill(new TenyearQingcheng);
	related_skills.insertMulti("tenyearhuoshui","#tenyearhuoshui-clear");
	addMetaObject<TenyearHuoshuiCard>();
	addMetaObject<TenyearQingchengCard>();
}
{//虓虎悲歌
	General*haomeng = new General(this,"haomeng","qun",7);
	haomeng->addSkill(new Xiongmang);
	addMetaObject<XiongmangCard>();

	General*yanfuren = new General(this,"yanfuren","qun",3,false);
	yanfuren->addSkill(new Channi);
	yanfuren->addSkill(new Nifu);
	addMetaObject<ChanniCard>();

	General*tenyear_zhuling = new General(this,"tenyear_zhuling","wei",4);
	tenyear_zhuling->addSkill(new TenyearZhanyi);
	tenyear_zhuling->addSkill(new TenyearZhanyiEffect);
	tenyear_zhuling->addSkill(new TenyearZhanyiTarget);
	related_skills.insertMulti("tenyearzhanyi","#tenyearzhanyi");
	related_skills.insertMulti("tenyearzhanyi","#tenyearzhanyi-mod");

	General*yanrou = new General(this,"yanrou*xh_huben","wei",4);
	yanrou->addSkill(new Choutao);
	yanrou->addSkill(new Xiangshu);
}
{//群雄伺动
	General*chezhou = new General(this,"chezhou","wei",4);
	chezhou->addSkill(new ThShefu);
	chezhou->addSkill(new Pigua);

	General*matie = new General(this,"matie","wei",4);
	matie->addSkill(new ThHcZhuiji);
	matie->addSkill(new ThHcZhuijiDistance);
	matie->addSkill(new Quxian);
	addMetaObject<QuxianCard>();
	
	General*hansong = new General(this,"hansong","qun",4);
	hansong->addSkill(new Yinbi);
	hansong->addSkill(new YinbiKeep);
	hansong->addSkill(new ThShuaiyan);
	
}
{//文和乱武
	General*lijue = new General(this,"lijue","qun",6,true,false,false,4);
	lijue->addSkill(new Langxi);
	lijue->addSkill(new Yisuan);

	General*guosi = new General(this,"guosi","qun",4);
	guosi->addSkill(new Tanbei);
	guosi->addSkill(new TanbeiTargetMod);
	guosi->addSkill(new TanbeiPro);
	guosi->addSkill(new Sidao);
	guosi->addSkill(new SidaoTargetMod);
	related_skills.insertMulti("tanbei","#tanbei-target");
	related_skills.insertMulti("tanbei","#tanbei-pro");
	related_skills.insertMulti("sidao","#sidao-target");
	addMetaObject<TanbeiCard>();
	addMetaObject<SidaoCard>();

	General*tenyear_fanchou = new General(this,"tenyear_fanchou","qun",4);
	tenyear_fanchou->addSkill("tenyearxingluan");

	General*liangxing = new General(this,"liangxing","qun",4);
	liangxing->addSkill(new Lulve);
	liangxing->addSkill(new Zhuixi);

	General*tangji = new General(this,"tangji","qun",3,false);
	tangji->addSkill(new Kangge);
	tangji->addSkill(new Jielie);

	General*duanwei = new General(this,"duanwei","qun",4);
	duanwei->addSkill(new Langmie);
	duanwei->addSkill("#dingpinbf");
	related_skills.insertMulti("langmie","#dingpinbf");

	General*second_duanwei = new General(this,"second_duanwei","qun",4);
	second_duanwei->addSkill(new SecondLangmie);
	second_duanwei->addSkill("#dingpinbf");
	related_skills.insertMulti("secondlangmie","#dingpinbf");

	General*zhangheng = new General(this,"zhangheng","qun",8);
	zhangheng->addSkill(new Liangjue);
	zhangheng->addSkill(new Dangzhai);

	General*niufu = new General(this,"niufu","qun",7);
	niufu->setStartHp(4);
	niufu->addSkill(new XiaoxiNF);
	niufu->addSkill(new Xiongrao);
	niufu->addSkill(new XiongraoClear);
	niufu->addSkill(new XiongraoInvalidity);
}
{//逐鹿天下
	General*weiwenzhugezhi = new General(this,"weiwenzhugezhi","wu",4);
	weiwenzhugezhi->addSkill(new Fuhai);
	addMetaObject<FuhaiCard>();

	General*zhanggong = new General(this,"zhanggong","wei",3);
	zhanggong->addSkill(new SpQianxin);
	zhanggong->addSkill(new Zhenxing);
	addMetaObject<SpQianxinCard>();
}
{//食禄尽忠
	General*shamoke = new General(this,"shamoke","shu",4);
	shamoke->addSkill(new Jili);

	General*mangyachang = new General(this,"mangyachang","qun",4);
	mangyachang->addSkill(new Jiedao);

	General*cuilie = new General(this,"cuilie","qun",3);
	cuilie->addSkill(new Zijue);
	cuilie->addSkill(new Chibi);
	addMetaObject<ZijueCard>();
}
{//戚宦之争
	General*sp_hejin = new General(this,"sp_hejin","qun",4);
	sp_hejin->addSkill(new SpMouzhu);
	sp_hejin->addSkill(new SpYanhuo);
	addMetaObject<SpMouzhuCard>();

	General*tenyear_zhangrang = new General(this,"tenyear_zhangrang","qun",3);
	tenyear_zhangrang->addSkill("tenyeartaoluan");

	General*zhaozhong = new General(this,"zhaozhong","qun",6);
	zhaozhong->addSkill(new Yangzhong);
	zhaozhong->addSkill(new Huangkong);

	General*fengfang = new General(this,"fengfang","qun",3);
	fengfang->addSkill(new Diting);
	fengfang->addSkill(new DitingEffect);
	fengfang->addSkill(new Bihuof);
	fengfang->addSkill(new BihuofDraw);

	General*mushun = new General(this,"mushun","qun",4);
	mushun->addSkill(new Jingjian);
	mushun->addSkill(new JingjianAttackRange);
	mushun->addSkill(new Shizhao);
	mushun->addSkill(new ShizhaoDamage);
}
{//上兵伐谋
	General*sp_yiji = new General(this,"sp_yiji","shu",3);
	sp_yiji->addSkill(new Jijie);
	sp_yiji->addSkill(new Jiyuan);
	addMetaObject<JijieCard>();

	General*lisu = new General(this,"lisu","qun",2);
	lisu->addSkill(new Lixun);
	lisu->addSkill(new SpKuizhu);
	addMetaObject<SpKuizhuCard>();

	General*zhangwen = new General(this,"zhangwen","wu",3);
	zhangwen->addSkill(new Songshu);
	zhangwen->addSkill(new Sibian);
	addMetaObject<SongshuCard>();
}
{//兵临城下
	General*sp_niujin = new General(this,"sp_niujin","wei",4);
	sp_niujin->addSkill(new SpCuorui);
	sp_niujin->addSkill(new SpLiewei);

	General*second_sp_niujin = new General(this,"second_sp_niujin","wei",4);
	second_sp_niujin->addSkill(new SecondSpCuorui);
	second_sp_niujin->addSkill(new SecondSpLiewei);
	addMetaObject<SpCuoruiCard>();
	addMetaObject<SecondSpCuoruiCard>();

	General*mifangfushiren = new General(this,"mifangfushiren","shu",4);
	mifangfushiren->addSkill(new FengshiMF);
	mifangfushiren->addSkill(new FengshiMFDamage);
	related_skills.insertMulti("fengshimf","#fengshimf");

	General*licaiwei = new General(this,"licaiwei","qun",3,false);
	licaiwei->addSkill(new Yijiao);
	licaiwei->addSkill(new Qibie);
	addMetaObject<YijiaoCard>();

	General*wangwei = new General(this,"wangwei","qun",4);
	wangwei->addSkill(new Ruizhan);
	wangwei->addSkill(new Shilie);
	addMetaObject<ShilieCard>();
	addMetaObject<ShilieGetCard>();

	General*tenyear_zhaoyanw = new General(this,"tenyear_zhaoyanw","wei",3);
	tenyear_zhaoyanw->addSkill(new TenyearFuning);
	tenyear_zhaoyanw->addSkill(new TenyearBingji);
	addMetaObject<TenyearBingjiCard>();

	General*liyixiejing = new General(this,"liyixiejing","wu",4);
	liyixiejing->addSkill(new Douzhen);
	liyixiejing->addSkill(new DouzhenTargetMod);

	General*shiyi = new General(this,"shiyi","wu",3);
	shiyi->addSkill(new Cuichuan);
	shiyi->addSkill(new Zhengxu);
	addMetaObject<CuichuanCard>();
	skills << new Zuojian;

	General*sunlang = new General(this,"sunlang","shu",4);
	sunlang->addSkill(new Tingxian);
	sunlang->addSkill(new Benshi);
	sunlang->addSkill(new BenshiRange);

	General*mengda = new General(this,"mengda","wei",4);
	mengda->addSkill(new Libang);
	mengda->addSkill(new Wujie);
	mengda->addSkill(new WujieTargetMod);
	addMetaObject<LibangCard>();

}
{//千里单骑
	General*qinyilu = new General(this,"qinyilu","qun",3);
	qinyilu->addSkill(new Piaoping);
	qinyilu->addSkill(new Tuoxian);
	qinyilu->addSkill(new Zhuili);
	addMetaObject<TuoxianCard>();

	General*bianxi = new General(this,"bianxi","wei",4);
	bianxi->addSkill(new Dunxi);
	bianxi->addSkill(new DunxiEffect);
	related_skills.insertMulti("dunxi","#dunxi");

	General*huban = new General(this,"huban","wei",4);
	huban->addSkill(new Chongyi);
	huban->addSkill(new ChongyiTargetMod);

	General*tenyear_hujinding = new General(this,"tenyear_hujinding","shu",6,false,false,false,3);
	tenyear_hujinding->addSkill("tenyeardeshi");
	tenyear_hujinding->addSkill("tenyearwuyuan");
	tenyear_hujinding->addSkill("huaizi");

}
{//烽火连天
	General*sp_tongyuan = new General(this,"sp_tongyuan","qun",4);
	sp_tongyuan->addSkill(new Chaofeng("chaofeng"));
	sp_tongyuan->addSkill(new Chuanshu("chuanshu"));
	sp_tongyuan->addSkill(new ChuanshuDeath("chuanshu"));
	related_skills.insertMulti("chuanshu","#chuanshu");
	skills << new JingongSkill("jingong")<< new JingongSkill("tenyearjingong")
	<< new Chuanyun;

	General*second_sp_tongyuan = new General(this,"second_sp_tongyuan","qun",4);
	second_sp_tongyuan->addSkill(new Chaofeng("secondchaofeng"));
	second_sp_tongyuan->addSkill(new Chuanshu("secondchuanshu"));
	second_sp_tongyuan->addSkill(new ChuanshuDeath("secondchuanshu"));
	related_skills.insertMulti("secondchuanshu","#secondchuanshu");

	General*zhangning = new General(this,"zhangning","qun",3,false);
	zhangning->addSkill(new Tianze);
	zhangning->addSkill(new Difa);

	General*tenyear_pangdegong = new General(this,"tenyear_pangdegong","qun",3);
	tenyear_pangdegong->addSkill(new Heqia);
	tenyear_pangdegong->addSkill(new HeqiaTargetMod);
	tenyear_pangdegong->addSkill(new Yinyi);
	related_skills.insertMulti("heqia","#heqia");
	addMetaObject<HeqiaCard>();
	addMetaObject<HeqiaUseCard>();
}
{//无双上将
	General*tenyear_panfeng = new General(this,"tenyear_panfeng","qun",4);
	tenyear_panfeng->addSkill(new TenyearKuangfu);
	tenyear_panfeng->addSkill(new SlashNoDistanceLimitSkill("tenyearkuangfu"));
	related_skills.insertMulti("tenyearkuangfu","#tenyearkuangfu-slash-ndl");
	addMetaObject<TenyearKuangfuCard>();

	General*xingdaorong = new General(this,"xingdaorong","qun",6,true,false,false,4);
	xingdaorong->addSkill(new Xuhe);

	General*caoxing = new General(this,"caoxing*xh_huben","qun",4);
	caoxing->addSkill(new Liushi);
	caoxing->addSkill(new Zhanwan);
	caoxing->addSkill(new ZhanwanMove);
	related_skills.insertMulti("zhanwan","#zhanwan-move");
	addMetaObject<LiushiCard>();

	General*chunyuqiong = new General(this,"chunyuqiong","qun",4);
	chunyuqiong->addSkill(new Cangchu);
	chunyuqiong->addSkill(new CangchuKeep);
	chunyuqiong->addSkill(new Liangying);
	chunyuqiong->addSkill(new Shishou);
	related_skills.insertMulti("cangchu","#cangchu-keep");

	General*xiahoujie = new General(this,"xiahoujie","wei",5);
	xiahoujie->addSkill(new Liedan);
	xiahoujie->addSkill(new Zhuangdan);

	General*caiyang = new General(this,"caiyang","wei",4);
	caiyang->addSkill(new Xunji);
	caiyang->addSkill(new XunjiLose);
	caiyang->addSkill(new Jiaofeng);
	addMetaObject<XunjiCard>();
}
{//才子佳人
	General*tenyear_sunluyu = new General(this,"tenyear_sunluyu","wu",3,false);
	tenyear_sunluyu->addSkill(new TenyearMeibu("tenyearmeibu"));
	tenyear_sunluyu->addSkill(new TenyearMumu);

	General*second_tenyear_sunluyu = new General(this,"second_tenyear_sunluyu","wu",3,false);
	second_tenyear_sunluyu->addSkill(new TenyearMeibu("secondtenyearmeibu"));
	second_tenyear_sunluyu->addSkill(new SecondTenyearMeibuGet);
	second_tenyear_sunluyu->addSkill(new SecondTenyearMumu);
	related_skills.insertMulti("secondtenyearmeibu","#secondtenyearmeibu-get");
	skills << new TenyearZhixi;

	General*tenyear_dongbai = new General(this,"tenyear_dongbai","qun",3,false);
	tenyear_dongbai->addSkill("tenyearlianzhu");
	tenyear_dongbai->addSkill("tenyearxiahui");
	tenyear_dongbai->addSkill("#tenyearxiahui-clear");
	tenyear_dongbai->addSkill(new TenyearXiahuiMove);
	related_skills.insertMulti("tenyearxiahui","#tenyearxiahui-move");

	General*heyan = new General(this,"heyan","wei",3);
	heyan->addSkill(new Yachai);
	heyan->addSkill(new YachaiLimit);
	heyan->addSkill(new Qingtan);
	related_skills.insertMulti("yachai","#yachai-limit");
	addMetaObject<QingtanCard>();

	General*wangtao = new General(this,"wangtao","shu",3,false);
	wangtao->addSkill(new Huguan);
	wangtao->addSkill(new HuguanIgnore);
	wangtao->addSkill(new Yaopei);
	related_skills.insertMulti("huguan","#huguan");

	General*wangyue = new General(this,"wangyue","shu",3,false);
	wangyue->addSkill("huguan");
	wangyue->addSkill(new Mingluan);

	General*zhaoyan = new General(this,"zhaoyan","wu",3,false);
	zhaoyan->addSkill(new Jinhui);
	zhaoyan->addSkill(new JinhuiTarget);
	zhaoyan->addSkill(new Qingman);
	related_skills.insertMulti("jinhui","#jinhui-target");
	addMetaObject<JinhuiCard>();
	addMetaObject<JinhuiUseCard>();

	General*tengyin = new General(this,"tengyin","wu",3);
	tengyin->addSkill(new Chenjian);
	tengyin->addSkill(new Xixiu);
	addMetaObject<ChenjianCard>();

	General*zhangyao = new General(this,"zhangyao","wu",3,false);
	zhangyao->addSkill(new Yuanyu);
	zhangyao->addSkill(new Xiyan);
	addMetaObject<YuanyuCard>();

	General*xiahoulingnv = new General(this,"xiahoulingnv","wei",4,false);
	xiahoulingnv->addSkill(new Fuping);
	xiahoulingnv->addSkill(new Weilie);
	addMetaObject<FupingCard>();
	addMetaObject<WeilieCard>();

	General*zhangxuan = new General(this,"zhangxuan","wu",4,false);
	zhangxuan->addSkill(new Tongli);
	zhangxuan->addSkill(new Shezang);
	zhangxuan->addSkill("#jingce-record");
	related_skills.insertMulti("tongli","#jingce-record");

	General*tenyear_sunru = new General(this,"tenyear_sunru","wu",3,false);
	tenyear_sunru->addSkill(new TenyearXiecui);
	tenyear_sunru->addSkill(new TenyearYouxu);

	General*th_xiahouxuan = new General(this,"th_xiahouxuan","wei",3);
	th_xiahouxuan->addSkill(new Boxuan);
	th_xiahouxuan->addSkill(new ThYizheng);
	th_xiahouxuan->addSkill(new Guilin);
	addMetaObject<GuilinCard>();

}
{//芝兰玉树
	General*zhanghu = new General(this,"zhanghu","wei",4);
	zhanghu->addSkill(new Cuijian);
	zhanghu->addSkill(new Tongyuan);
	zhanghu->addSkill(new TongyuanEffect);
	related_skills.insertMulti("tongyuan","#tongyuan");

	General*second_zhanghu = new General(this,"second_zhanghu","wei",4);
	second_zhanghu->addSkill(new SecondCuijian);
	second_zhanghu->addSkill(new SecondTongyuan);
	second_zhanghu->addSkill(new SecondTongyuanEffect);
	related_skills.insertMulti("secondtongyuan","#secondtongyuan");
	addMetaObject<CuijianCard>();
	addMetaObject<SecondCuijianCard>();

	General*wanniangongzhu = new General(this,"wanniangongzhu","qun",3,false);
	wanniangongzhu->addSkill(new Zhenge);
	wanniangongzhu->addSkill(new Xinghan);

	General*liuyong = new General(this,"liuyong","shu",3);
	liuyong->addSkill(new Zhuning);
	liuyong->addSkill(new Fengxiang);
	addMetaObject<ZhuningCard>();

	General*lvlingqi = new General(this,"lvlingqi","qun",4,false);
	lvlingqi->addSkill(new Guowu);
	lvlingqi->addSkill(new Zhuangrong);
	lvlingqi->addRelateSkill("shenwei");
	lvlingqi->addRelateSkill("wushuang");
	addMetaObject<GuowuCard>();

	General*tenggongzhu = new General(this,"tenggongzhu","wu",3,false);
	tenggongzhu->addSkill(new Xingchong);
	tenggongzhu->addSkill(new Liunian);

	General*panghui = new General(this,"panghui","wei",5);
	panghui->addSkill(new Yiyong);

	General*tenyear_zhaotongzhaoguang = new General(this,"tenyear_zhaotongzhaoguang","shu",4);
	tenyear_zhaotongzhaoguang->addSkill("yizan");
	tenyear_zhaotongzhaoguang->addSkill(new TenyearQingren);
	tenyear_zhaotongzhaoguang->addSkill(new TenyearLongyuan);

	General*huangwudie = new General(this,"huangwudie","shu",4,false);
	huangwudie->addSkill(new Shuangrui);
	huangwudie->addSkill(new Fuxie);
	addMetaObject<FuxieCard>();
	addMetaObject<ShouxingCard>();
	skills << new Shaxue << new Shouxing;

	General*panghong = new General(this,"panghong","shu",3);
	panghong->addSkill(new Pingzhi);
	panghong->addSkill(new Gangjian);
	addMetaObject<PingzhiCard>();

	General*erji = new General(this,"erji","qun",4);
	erji->addSkill(new Zouyi);
	erji->addSkill(new Yanxi);
	addMetaObject<ZouyiCard>();
}
{//天下归心
	General*tenyear_kanze = new General(this,"tenyear_kanze*xh_sibi","wu",3);
	tenyear_kanze->addSkill("xiashu");
	tenyear_kanze->addSkill("tenyearkuanshi");
	tenyear_kanze->addSkill("#tenyearkuanshi-mark");
	tenyear_kanze->addSkill(new TenyearKuanshiEffect);
	related_skills.insertMulti("tenyearkuanshi","#tenyearkuanshi-effect");

	General*tenyear_chendeng = new General(this,"tenyear_chendeng","qun",3);
	tenyear_chendeng->addSkill(new Wangzu);
	tenyear_chendeng->addSkill(new Yingrui);
	tenyear_chendeng->addSkill(new Fuyuan);
	addMetaObject<YingruiCard>();

	General*tenyear_gaolan = new General(this,"tenyear_gaolan","qun",4);
	tenyear_gaolan->addSkill(new TenyearXizhen);
	tenyear_gaolan->addSkill(new TenyearXizhenEffect);
	related_skills.insertMulti("tenyearxizhen","#tenyearxizhen");

	General*caimaozhangyun = new General(this,"caimaozhangyun","wei",4);
	caimaozhangyun->addSkill(new Lianzhou);
	caimaozhangyun->addSkill(new Jinglan);

	General*tenyear_lvkuanglvxiang = new General(this,"tenyear_lvkuanglvxiang","wei",4);
	tenyear_lvkuanglvxiang->addSkill(new TenyearShuhe);
	tenyear_lvkuanglvxiang->addSkill(new TenyearLiehou);
	addMetaObject<TenyearShuheCard>();

	General*yinfuren = new General(this,"yinfuren","wei",3,false);
	yinfuren->addSkill(new Yingyu);
	yinfuren->addSkill(new Yongbi);
	yinfuren->addSkill(new YongbiKeep);
	addMetaObject<YongbiCard>();

	General*chengui = new General(this,"chengui","qun",3);
	chengui->addSkill(new Yingtu);
	chengui->addSkill(new Congshi);

	General*chenjiao = new General(this,"chenjiao","wei",3);
	chenjiao->addSkill(new Xieshou);
	chenjiao->addSkill(new Qingyan);
	chenjiao->addSkill(new Qizi);

	General*tenyear_tangzi = new General(this,"tenyear_tangzi","wei",4);
	tenyear_tangzi->addSkill("tenyearxingzhao");
	tenyear_tangzi->addSkill("#tenyearxingzhao-xunxun");
	tenyear_tangzi->addRelateSkill("xunxun");

	General*qinlang = new General(this,"qinlang","wei",4);
	qinlang->addSkill(new Haochong);
	qinlang->addSkill(new Jinjin);

	General*mamiao = new General(this,"mamiao","shu",4);
	mamiao->addSkill(new Zhangguan);
	mamiao->addSkill(new Congfeng);


}
{//代汉涂高
	General*mamidi = new General(this,"mamidi*xh_sibi","qun",6);
	mamidi->setStartHp(4);
	mamidi->addSkill(new Bingjie);
	mamidi->addSkill(new BingjieEffect);
	mamidi->addSkill(new Zhengding);
	related_skills.insertMulti("bingjie","#bingjie");

	General*zhangxun = new General(this,"zhangxun","qun",4);
	zhangxun->addSkill(new Suizheng);
	zhangxun->addSkill(new SuizhengTargetMod);
	//由于新功能，关联的隐藏技能不需要单独加给related_skills了

	General*yuejiu = new General(this,"yuejiu","qun",4);
	yuejiu->addSkill(new Cuijin);

	General*qiaorui = new General(this,"qiaorui","qun",4);
	qiaorui->addSkill(new Aishou);
	qiaorui->addSkill(new Saowei);

	General*leibo = new General(this,"leibo","qun");
	leibo->addSkill(new Silve);
	leibo->addSkill(new ShuaijieVS);
	addMetaObject<ShuaijieCard>();
}
{//江湖之远
	General*guanning = new General(this,"guanning","qun",7);
	guanning->setStartHp(3);
	guanning->addSkill(new Dunshi);
	addMetaObject<DunshiCard>();

	General*tenyear_huangchengyan = new General(this,"tenyear_huangchengyan","qun",3);
	tenyear_huangchengyan->addSkill(new TenyearJiezhen);
	tenyear_huangchengyan->addSkill(new TenyearZecai);
	tenyear_huangchengyan->addSkill(new TenyearZecaiLose);
	tenyear_huangchengyan->addSkill(new TenyearYinshi);
	related_skills.insertMulti("tenyearzecai","#tenyearzecai");
	addMetaObject<TenyearJiezhenCard>();

	General*huzhao = new General(this,"huzhao","qun",3);
	huzhao->addSkill(new Midu);
	huzhao->addSkill(new Xianwang);
	addMetaObject<MiduCard>();

	General*sp_wanglie = new General(this,"sp_wanglie","qun",3);
	sp_wanglie->addSkill(new Chongwang);
	sp_wanglie->addSkill(new Huagui);

	General*th_mengjie = new General(this,"th_mengjie","qun",3);
	th_mengjie->addSkill(new Yinlu);
	th_mengjie->addSkill(new Youqi);
	addMetaObject<YinluCard>();
	skills << new YlLequan << new YlHuoxi << new YlZhangqi << new YlYunxiang;
}
{//悬壶济世
	General*jiping = new General(this,"jiping","qun",3);
	jiping->addSkill(new Xunli);
	jiping->addSkill(new Zhishi);
	jiping->addSkill(new ZhishiChoose);
	jiping->addSkill(new Lieyi);
	jiping->addSkill(new LieyiTarget);
	related_skills.insertMulti("zhishi","#zhishi");
	related_skills.insertMulti("lieyi","#lieyi");
	addMetaObject<XunliPutCard>();
	addMetaObject<XunliCard>();
	addMetaObject<ZhishiCard>();
	addMetaObject<LieyiCard>();

	General*zhenghun = new General(this,"zhenghun","wei",3);
	zhenghun->addSkill(new QiangzhiZH);
	zhenghun->addSkill(new Pitian);
	zhenghun->addSkill(new PitianKeep);
	addMetaObject<QiangzhiZHCard>();
}
{//纵横捭阖
	General*luyusheng = new General(this,"luyusheng","wu",3,false);
	luyusheng->addSkill(new Zhente);
	luyusheng->addSkill(new Zhiwei);
	luyusheng->addSkill(new ZhiweiEffect);
	related_skills.insertMulti("zhiwei","#zhiwei");

	General*huaxin = new General(this,"huaxin","wei",3);
	huaxin->addSkill(new Wanggui);
	huaxin->addSkill(new Xibing);

	General*tenyear_xunchen = new General(this,"tenyear_xunchen","qun",3);
	tenyear_xunchen->addSkill(new TenyearFenglve);
	tenyear_xunchen->addSkill(new Anyong);
	tenyear_xunchen->addSkill(new AnyongRecord);
	related_skills.insertMulti("anyong","#anyong");
	addMetaObject<TenyearFenglveCard>();
	addMetaObject<TenyearFenglveGiveCard>();

	General*tenyear_dengzhi = new General(this,"tenyear_dengzhi","shu",3);
	tenyear_dengzhi->addSkill(new Jianliang);
	tenyear_dengzhi->addSkill(new Weimeng);
	addMetaObject<JianliangCard>();
	addMetaObject<WeimengCard>();

	General*fengxi = new General(this,"fengxi","wu",3);
	fengxi->addSkill(new Yusui);
	fengxi->addSkill(new Boyan);
	fengxi->addSkill(new BoyanLimit);
	related_skills.insertMulti("boyan","#boyan-limit");
	addMetaObject<BoyanCard>();

	General*miheng = new General(this,"miheng","qun",3);
	miheng->addSkill(new Kuangcai);
	miheng->addSkill(new KuangcaiTarget);
	miheng->addSkill(new Shejian);
	related_skills.insertMulti("kuangcai","#kuangcai");

	General*tenyear_yanghu = new General(this,"tenyear_yanghu","wei",3);
	tenyear_yanghu->addSkill(new TenyearDeshao);
	tenyear_yanghu->addSkill(new TenyearMingfa);
	tenyear_yanghu->addSkill(new TenyearMingfaEffect);
	related_skills.insertMulti("tenyearmingfa","#tenyearmingfa");
}
{//匡鼎炎汉
	General*liuba = new General(this,"liuba","shu",3);
	liuba->addSkill(new Zhubi);
	liuba->addSkill(new Liuzhuan);
	liuba->addSkill(new Liuzhuanbf);
	related_skills.insertMulti("liuzhuan","#liuzhuan");

	General*tenyear_yangyi = new General(this,"tenyear_yangyi","shu",3);
	tenyear_yangyi->addSkill("tenyearjuanxia");
	tenyear_yangyi->addSkill("#tenyearjuanxia-slash");
	tenyear_yangyi->addSkill("dingcuo");

	General*tenyear_huangquan = new General(this,"tenyear_huangquan","shu",3);
	tenyear_huangquan->addSkill(new TenyearQuanjian);
	tenyear_huangquan->addSkill(new TenyearQuanjianLimit);
	tenyear_huangquan->addSkill(new TenyearTujue);
	addMetaObject<TenyearQuanjianCard>();

	General*furongfuqian = new General(this,"furongfuqian","shu",6);
	furongfuqian->setStartHp(4);
	furongfuqian->addSkill(new TenyearXuewei);
	furongfuqian->addSkill(new TenyearXueweiEffect);
	furongfuqian->addSkill(new TenyearYuguan);

	General*tenyear_huojun = new General(this,"tenyear_huojun","shu",4);
	tenyear_huojun->addSkill(new TenyearGue);
	tenyear_huojun->addSkill(new TenyearSigong);
	addMetaObject<TenyearGueCard>();

	General*xianglang = new General(this,"xianglang","shu",3);
	xianglang->addSkill(new Kanji);
	xianglang->addSkill(new Qianzheng);
	addMetaObject<KanjiCard>();
	addMetaObject<QianzhengCard>();

	General*th_lifeng = new General(this,"th_lifeng","shu",3);
	th_lifeng->addSkill(new ThTunchu);
	th_lifeng->addSkill(new ThTunchuLimit);
	th_lifeng->addSkill(new ThShuliang);
	addMetaObject<ThShuliangCard>();

	General*th_xiangchong = new General(this,"th_xiangchong","shu",4);
	th_xiangchong->addSkill(new ThGuying);
	th_xiangchong->addSkill(new ThMuzhen);
	addMetaObject<ThMuzhenCard>();

	General*zhugejun = new General(this,"zhugejun","shu",3);
	zhugejun->addSkill(new Gengdu);
	zhugejun->addSkill(new GengduLimit);
	zhugejun->addSkill(new Gumai);
	addMetaObject<GengduCard>();
}
{//太平甲子
	General*guanhai = new General(this,"guanhai","qun",4);
	guanhai->addSkill(new Suoliang);
	guanhai->addSkill(new Qinbao);

	General*liupi = new General(this,"liupi","qun",4);
	liupi->addSkill(new Juying);
	liupi->addSkill(new JuyingClear);
	liupi->addSkill(new JuyingTargetMod);

	General*zhangkai = new General(this,"zhangkai","qun",4);
	zhangkai->addSkill(new XiangshuZK);
	zhangkai->addSkill(new XiangshuZKEffect);
}
{//异军突起
	General*gongsundu = new General(this,"gongsundu","qun",4);
	gongsundu->addSkill(new Zhenze);
	gongsundu->addSkill(new Anliao);
	addMetaObject<AnliaoCard>();

	General*th_zhurong = new General(this,"th_zhurong","qun",4,false);
	th_zhurong->addSkill(new Manhou);
	addMetaObject<ManhouCard>();
	addMetaObject<TanluanCard>();
	skills << new Tanluan;
}
{//正音雅乐
	General*yue_diaochan = new General(this,"yue_diaochan","qun",3,false);
	yue_diaochan->addSkill(new Tanban);
	yue_diaochan->addSkill(new TanbanLimit);
	yue_diaochan->addSkill(new Diou);

	General*yue_zhouyu = new General(this,"yue_zhouyu","wu",3);
	yue_zhouyu->addSkill(new Guyin);
	yue_zhouyu->addSkill(new Pinglu);
	addMetaObject<PingluCard>();

	General*yue_caiyong = new General(this,"yue_caiyong","qun",3);
	yue_caiyong->addSkill(new ThJiaowei);
	yue_caiyong->addSkill(new Feibai);

	General*yue_zhugeguo = new General(this,"yue_zhugeguo","shu",3,false);
	yue_zhugeguo->addSkill(new Xidi);
	yue_zhugeguo->addSkill(new Chengyan);
}
{//百战虎贲
	General*tenyear_wutugu = new General(this,"tenyear_wutugu","qun",15);
	tenyear_wutugu->addSkill(new TenyearRanshang);
	tenyear_wutugu->addSkill(new TenyearHanyong);

	General*wenyang = new General(this,"wenyang","wei",5);
	wenyang->addSkill(new Lvli);
	wenyang->addSkill(new Choujue);
	wenyang->addRelateSkill("beishui");
	wenyang->addRelateSkill("qingjiao");
	skills << new Beishui << new Qingjiao;

	General*tenyear_xiahouba = new General(this,"tenyear_xiahouba","shu");
	tenyear_xiahouba->addSkill(new TenyearBaobian);
	tenyear_xiahouba->addRelateSkill("tiaoxin");
	tenyear_xiahouba->addRelateSkill("tenyearpaoxiao");
	tenyear_xiahouba->addRelateSkill("tenyearshensu");

	General*huangfusong = new General(this,"huangfusong","qun",4);
	huangfusong->addSkill(new Fenyue);
	huangfusong->addSkill(new FenyueRevived);
	related_skills.insertMulti("fenyue","#fenyue-revived");
	addMetaObject<FenyueCard>();

	General*wangshuang = new General(this,"wangshuang*xh_tianzhu","wei",8);
	wangshuang->addSkill(new Zhuilie);
	wangshuang->addSkill(new ZhuilieSlash);
	related_skills.insertMulti("zhuilie","#zhuilie-slash");

	General*tenyear_liuzan = new General(this,"tenyear_liuzan","wu",4);
	tenyear_liuzan->addSkill(new TenyearFenyin);
	tenyear_liuzan->addSkill(new Liji);
	addMetaObject<LijiCard>();

	General*tenyear_huangzu = new General(this,"tenyear_huangzu","qun",4);
	tenyear_huangzu->addSkill(new TenyearJinggong);
	tenyear_huangzu->addSkill(new TenyearJinggongTargetMod);
	tenyear_huangzu->addSkill(new TenyearXiaojun);
	related_skills.insertMulti("tenyearjinggong","#tenyearjinggong");

	General*th_lincao = new General(this,"th_lincao","wu",5,true,false,false,4);
	th_lincao->addSkill(new Dufeng);
	th_lincao->addSkill(new DufengRange);

	General*lvju = new General(this,"lvju","wu",4);
	lvju->addSkill(new Zhengyue);
	addMetaObject<ZhengyueCard>();

	General*huzhun = new General(this,"huzhun","wei",4);
	huzhun->addSkill(new Zhantao);
	huzhun->addSkill(new Anjing);

	General*linglie = new General(this,"linglie","wu",4);
	linglie->addSkill(new Shouhu);
	linglie->addSkill(new ShouhuBf);
	linglie->addSkill(new Ningzhun);
	addMetaObject<ShouhuCard>();
}
{//奇人异士
	General*tenyear_zhangbao = new General(this,"tenyear_zhangbao","qun",3);
	tenyear_zhangbao->addSkill("tenyearzhoufu");
	tenyear_zhangbao->addSkill(new TenyearYingbing);

	General*gexuan = new General(this,"gexuan","wu",3);
	gexuan->addSkill(new Lianhua);
	gexuan->addSkill(new LianhuaEffect);
	gexuan->addSkill(new Zhafu);
	related_skills.insertMulti("lianhua","#lianhua-effect");
	addMetaObject<ZhafuCard>();

	General*guanlu = new General(this,"guanlu","wei",3);
	guanlu->addSkill(new Tuiyan("tuiyan"));
	guanlu->addSkill(new Busuan);
	guanlu->addSkill(new Mingjie("mingjie"));

	General*tenyear_guanlu = new General(this,"tenyear_guanlu","wei",3);
	tenyear_guanlu->addSkill(new Tuiyan("tenyeartuiyan"));
	tenyear_guanlu->addSkill("busuan");
	tenyear_guanlu->addSkill(new Mingjie("tenyearmingjie"));
	addMetaObject<BusuanCard>();

	General*puyuan = new General(this,"puyuan","shu",4);
	puyuan->addSkill(new Tianjiang);
	puyuan->addSkill(new Zhuren);
	addMetaObject<TianjiangCard>();
	addMetaObject<ZhurenCard>();

	General*wufan = new General(this,"wufan","wu",4);
	wufan->addSkill(new Tianyun);
	wufan->addSkill(new TianyunInitial);
	wufan->addSkill(new Yuyan);
	related_skills.insertMulti("tianyun","#tianyun");

	General*dukui = new General(this,"dukui","wei",3);
	dukui->addSkill(new Fanyin);
	dukui->addSkill(new FanyinEffect);
	dukui->addSkill(new Peiqi);

	General*zhaozhi = new General(this,"zhaozhi","shu",3);
	zhaozhi->addSkill(new Tongguan);
	zhaozhi->addSkill(new Mengjie);

	General*tenyearzhouxuan = new General(this,"tenyearzhouxuan","wei",3);
	tenyearzhouxuan->addSkill(new Wumei);
	tenyearzhouxuan->addSkill(new Zhanmeng);
	tenyearzhouxuan->addSkill(new ZhanmengEffect);

	General*zhujianping = new General(this,"zhujianping","qun",3);
	zhujianping->addSkill(new Xiangmian);
	zhujianping->addSkill(new Tianji);
	addMetaObject<XiangmianCard>();

	General*zerong = new General(this,"zerong","qun",4);
	zerong->addSkill(new Cansi);
	zerong->addSkill(new CansiDraw);
	zerong->addSkill(new Fozong);
	
	General*suyue = new General(this,"suyue","wei",3);
	suyue->addSkill(new Gongtu);
	suyue->addSkill(new Hangzhu);
	addMetaObject<HangzhuCard>();
}
{//计将安出
	General*tenyear_wangyun = new General(this,"tenyear_wangyun","qun",4);
	tenyear_wangyun->addSkill(new TenyearLianji);
	tenyear_wangyun->addSkill(new TenyearMoucheng);
	tenyear_wangyun->addRelateSkill("tenyearjingong");
	addMetaObject<TenyearLianjiCard>();

	General*jianggan = new General(this,"jianggan*xh_sibi","wei",3);
	jianggan->addSkill(new Weicheng);
	jianggan->addSkill(new Daoshu);
	addMetaObject<DaoshuCard>();

	General*zhaoang = new General(this,"zhaoang","wei",4);
	zhaoang->setStartHp(3);
	zhaoang->addSkill(new Zhongjie);
	zhaoang->addSkill(new Sushou);
	addMetaObject<SushouCard>();

	General*tenyear_liuye = new General(this,"tenyear_liuye","wei",3);
	tenyear_liuye->addSkill(new TenyearPoyuan);
	tenyear_liuye->addSkill(new TenyearHuace);

	General*yanghong = new General(this,"yanghong","qun",3);
	yanghong->addSkill(new JianjiYH);
	yanghong->addSkill(new JianjiYHTargetMod);
	yanghong->addSkill(new Yuanmo);
	yanghong->addSkill(new YuanmoRange);
	addMetaObject<JianjiYHCard>();

}
{//豆寇梢头
	General*tenyear_zhugeguo = new General(this,"tenyear_zhugeguo","shu",3,false);
	tenyear_zhugeguo->addSkill(new TenyearYuhua);
	tenyear_zhugeguo->addSkill(new TenyearQirang);
	tenyear_zhugeguo->addSkill(new TenyearQirangEffect);
	related_skills.insertMulti("tenyearqirang","#tenyearqirang");

	General*huaman = new General(this,"huaman","shu",3,false);
	huaman->addSkill(new SpManyi);
	huaman->addSkill(new Mansi);
	huaman->addSkill(new Souying);
	huaman->addSkill(new Zhanyuan);
	huaman->addSkill(new ZhanyuanRecord("zhanyuan"));
	huaman->addRelateSkill("xili");
	related_skills.insertMulti("zhanyuan","#zhanyuan");
	skills << new Xili << new ZhanyuanRecord("secondzhanyuan");

	General*tenyear_xinxianying = new General(this,"tenyear_xinxianying","wei",3,false);
	tenyear_xinxianying->addSkill(new TenyearZhongjian);
	tenyear_xinxianying->addSkill(new TenyearCaishi);
	tenyear_xinxianying->addSkill(new TenyearCaishiPro);
	related_skills.insertMulti("tenyearcaishi","#tenyearcaishi-pro");
	addMetaObject<TenyearZhongjianCard>();

	General*xuelingyun = new General(this,"xuelingyun","wei",3,false);
	xuelingyun->addSkill(new Xialei);
	xuelingyun->addSkill(new Anzhi);
	addMetaObject<AnzhiCard>();

	General*tenyear_ruiji = new General(this,"tenyear_ruiji","wu",4,false);
	tenyear_ruiji->addSkill(new TenyearWangyuan);
	tenyear_ruiji->addSkill(new TenyearLiying);
	tenyear_ruiji->addSkill(new TenyearLingyin);
	tenyear_ruiji->addSkill(new TenyearLingyinEffect);
	addMetaObject<TenyearLingyinCard>();

	General*duanqiaoxiao = new General(this,"duanqiaoxiao","wei",3,false);
	duanqiaoxiao->addSkill(new Caizhuang);
	duanqiaoxiao->addSkill(new Huayi);
	duanqiaoxiao->addSkill(new HuayiEffect);
	addMetaObject<CaizhuangCard>();

	General*moqiongshu = new General(this,"moqiongshu","wei",3,false);
	moqiongshu->addSkill(new Wanchan);
	moqiongshu->addSkill(new Jiangzhi);
	addMetaObject<WanchanCard>();

	General*caoyuan = new General(this,"caoyuan","qun",3,false);
	caoyuan->addSkill(new ThWuyan);
	caoyuan->addSkill(new ThWuyanBf);
	caoyuan->addSkill(new Zhanyu);
	addMetaObject<ThWuyanCard>();

	General*cuilingyi = new General(this,"cuilingyi","wei",3,false);
	cuilingyi->addSkill(new Huashang);
	cuilingyi->addSkill(new Yuzhi);

	General*xuwen = new General(this,"xuwen","wei",3,false);
	xuwen->addSkill(new Fuhui);
	xuwen->addSkill(new Mohua);
	xuwen->addSkill(new MohuaPro);
	addMetaObject<FuhuiCard>();
	addMetaObject<MohuaCard>();

}
{//皇家贵胄
	General*tenyear_shixie = new General(this,"tenyear_shixie","qun",3);
	tenyear_shixie->addSkill(new Tenyearbiluan);
	tenyear_shixie->addSkill(new TenyearbiluanTrigger);
	tenyear_shixie->addSkill(new TenyearLixia);
	tenyear_shixie->addSkill(new TenyearLixiaTrigger);
	related_skills.insertMulti("tenyearbiluan","#tenyearbiluan-trigger");
	related_skills.insertMulti("tenyearlixia","#tenyearlixia-trigger");

	General*tenyear_sunhao = new General(this,"tenyear_sunhao$","wu",5);
	tenyear_sunhao->addSkill(new TenyearCanshi);
	tenyear_sunhao->addSkill(new TenyearChouhai);
	tenyear_sunhao->addSkill("guiming");

	General*liubian = new General(this,"liubian$*xh_tianji","qun",3);
	liubian->addSkill(new Shiyuan);
	liubian->addSkill(new SpDushi);
	liubian->addSkill(new SpDushiPro);
	liubian->addSkill(new Skill("yuwei$",Skill::Compulsory));
	related_skills.insertMulti("spdushi","#spdushi-pro");

	General*caomao = new General(this,"caomao$","wei",4);
	caomao->setStartHp(3);
	caomao->addSkill(new Qianlong);
	caomao->addSkill(new Fensi);
	caomao->addSkill(new Juetao);
	caomao->addSkill(new JuetaoPro);
	caomao->addSkill(new Zhushi);
	related_skills.insertMulti("juetao","#juetao");
	addMetaObject<QianlongCard>();

	General*tenyear_liuyu = new General(this,"tenyear_liuyu","qun",3);
	tenyear_liuyu->addSkill(new TenyearSuifu);
	tenyear_liuyu->addSkill(new TenyearSuifuRecord);
	tenyear_liuyu->addSkill(new TenyearPijing);
	related_skills.insertMulti("tenyearsuifu","#tenyearsuifu");
	skills << new TenyearZimu;

	General*dingshangwan = new General(this,"dingshangwan","wei",3,false);
	dingshangwan->addSkill(new Fengyan);
	dingshangwan->addSkill(new Fudao);
	addMetaObject<FengyanCard>();

	General*th_qinghe = new General(this,"th_qinghe","wei",3,false);
	th_qinghe->addSkill(new ThZhangji);
	th_qinghe->addSkill(new ThZhengui);
	addMetaObject<ThZhenguiCard>();

	General*th_xiahouhui = new General(this,"th_xiahouhui","wei",3,false);
	th_xiahouhui->addSkill(new Dujun);
	th_xiahouhui->addSkill(new Jikun);
	addMetaObject<JikunCard>();

	General*bianyue = new General(this,"bianyue","wei",3,false);
	bianyue->addSkill(new Bizu);
	bianyue->addSkill(new Wuxie);
	addMetaObject<BizuCard>();

	General*xuxin = new General(this,"xuxin","wu",3,false);
	xuxin->addSkill(new Yuxian);
	xuxin->addSkill(new Minshan);

}
{//往者可谏
	General*tenyear_erqiao = new General(this,"tenyear_erqiao","wu",3,false);
	tenyear_erqiao->addSkill("tenyearxingwu");
	tenyear_erqiao->addSkill("olluoyan");

	General*tenyear_new_spmachao = new General(this,"tenyear_new_spmachao","qun",4);
	tenyear_new_spmachao->addSkill("newzhuiji");
	tenyear_new_spmachao->addSkill(new TenyearNewShichou);
	addMetaObject<TenyearNewShichouCard>();
}
{//章台春望
	General*guozhao = new General(this,"guozhao","wei",3,false);
	guozhao->addSkill(new Pianchong);
	guozhao->addSkill(new PianchongEffect);
	guozhao->addSkill(new Zunwei);
	related_skills.insertMulti("pianchong","#pianchong-effect");
	addMetaObject<ZunweiCard>();

	General*fanyufeng = new General(this,"fanyufeng","qun",3,false);
	fanyufeng->addSkill(new Bazhan("bazhan"));
	fanyufeng->addSkill(new Jiaoying("jiaoying"));
	fanyufeng->addSkill(new JiaoyingMove("jiaoying"));
	related_skills.insertMulti("jiaoying","#jiaoying-move");

	General*second_fanyufeng = new General(this,"second_fanyufeng","qun",3,false);
	second_fanyufeng->addSkill(new Bazhan("secondbazhan"));
	second_fanyufeng->addSkill(new Jiaoying("secondjiaoying"));
	second_fanyufeng->addSkill(new JiaoyingMove("secondjiaoying"));
	related_skills.insertMulti("secondjiaoying","#secondjiaoying-move");
	addMetaObject<BazhanCard>();
	addMetaObject<SecondBazhanCard>();

	General*ruanyu = new General(this,"ruanyu","wei",3);
	ruanyu->addSkill(new Xingzuo);
	ruanyu->addSkill(new XingzuoFinish);
	ruanyu->addSkill(new Miaoxian);
	related_skills.insertMulti("xingzuo","#xingzuo-finish");
	addMetaObject<XingzuoCard>();
	addMetaObject<MiaoxianCard>();

	General*yangwan = new General(this,"yangwan*xh_nvshi","shu",3,false);
	yangwan->addSkill(new Youyan);
	yangwan->addSkill(new Zhuihuan);
	yangwan->addSkill(new ZhuihuanEffect);
	related_skills.insertMulti("zhuihuan","#zhuihuan");

}
{//锦瑟良缘
	General*caojinyu = new General(this,"caojinyu","wei",3,false);
	caojinyu->addSkill(new Yuqi);
	caojinyu->addSkill(new Shanshen);
	caojinyu->addSkill(new Xianjing);
	caojinyu->addSkill("#kuimang-record");
	related_skills.insertMulti("shanshen","#kuimang-record");
	addMetaObject<YuqiCard>();

	General*sunyi = new General(this,"sunyi","wu",5);
	sunyi->addSkill(new Jiqiaosy);
	sunyi->addSkill(new JiqiaosyEnter);
	sunyi->addSkill(new Xiongyisy);
	related_skills.insertMulti("jiqiaosy","#jiqiaosy");
	addMetaObject<JiqiaosyCard>();

	General*fengyu = new General(this,"fengyu","qun",3,false);
	fengyu->addSkill(new Tiqi);
	fengyu->addSkill(new TiqiRecord);
	fengyu->addSkill(new Baoshu);
	fengyu->addSkill(new BaoshuDraw);
	related_skills.insertMulti("tiqi","#tiqi");
	related_skills.insertMulti("baoshu","#baoshu");
	addMetaObject<BaoshuCard>();

	General*laiyinger = new General(this,"laiyinger","qun",3,false);
	laiyinger->addSkill(new Xiaowu);
	laiyinger->addSkill(new Huaping);
	laiyinger->addSkill(new HuapingInvoke);
	related_skills.insertMulti("huaping","#huaping");
	addMetaObject<XiaowuCard>();
	addMetaObject<ShawuCard>();
	skills << new Shawu;

	General*caohua = new General(this,"caohua","wei",3,false);
	caohua->addSkill(new Caiyi);
	caohua->addSkill(new Guili);

	General*zhugemengxue = new General(this,"zhugemengxue","wei",3,false);
	zhugemengxue->addSkill(new Jichun);
	zhugemengxue->addSkill(new Hanying);
	addMetaObject<JichunCard>();

	General*wenyuan = new General(this,"wenyuan","shu",3,false);
	wenyuan->addSkill(new Kengqiang);
	wenyuan->addSkill(new Kuishang);
	wenyuan->addSkill(new Shangjue);
	skills << new Kunli;

	General*zhanghuai = new General(this,"zhanghuai","wu",3,false);
	zhanghuai->addSkill(new Laoyan);
	zhanghuai->addSkill(new ThJueyan);

	General*dongxu = new General(this,"dongxu","qun",3,false);
	dongxu->addSkill(new Qingleng);
	dongxu->addSkill(new Dandu);

}
{//笔舌如椽
	General*tenyear_zhugeke = new General(this,"tenyear_zhugeke","wu",3);
	tenyear_zhugeke->addSkill(new TenyearAocai);
	tenyear_zhugeke->addSkill(new TenyearDuwu);
	addMetaObject<TenyearAocaiCard>();
	addMetaObject<TenyearDuwuCard>();

	General*tenyear_chenlin = new General(this,"tenyear_chenlin","wei",3);
	tenyear_chenlin->addSkill("bifa");
	tenyear_chenlin->addSkill(new TenyearSongci);
	addMetaObject<TenyearSongciCard>();

	General*tenyear_yangxiu = new General(this,"tenyear_yangxiu","wei",3);
	tenyear_yangxiu->addSkill(new TenyearDanlao);
	tenyear_yangxiu->addSkill(new TenyearJilei);
	tenyear_yangxiu->addSkill(new TenyearJileiClear);
	related_skills.insertMulti("tenyearjilei","#tenyearjilei-clear");

	General*luotong = new General(this,"luotong","wu",3);
	luotong->addSkill(new Jinjian);
	luotong->addSkill(new JinjianEffect);
	luotong->addSkill(new Renzheng);
	related_skills.insertMulti("jinjian","#jinjian");

	General*wangchang = new General(this,"wangchang","wei",3);
	wangchang->addSkill(new Kaiji);
	wangchang->addSkill(new Pingxi);
	wangchang->addSkill(new PingxiDiscard);
	related_skills.insertMulti("pingxi","#pingxi_discard");
	addMetaObject<KaijiCard>();

	General*chengbing = new General(this,"chengbing","wu",3);
	chengbing->addSkill(new Jingzao);
	chengbing->addSkill(new Enyu);
	addMetaObject<JingzaoCard>();

	General*tenyear_yangbiao = new General(this,"tenyear_yangbiao","qun",3);
	tenyear_yangbiao->addSkill(new TenyearZhaohan);
	tenyear_yangbiao->addSkill(new TenyearJinjie);
	tenyear_yangbiao->addSkill(new TenyearJue);
	addMetaObject<TenyearZhaohanCard>();
	addMetaObject<TenyearJueCard>();

	General*wuzhi = new General(this,"wuzhi","wei",3);
	wuzhi->addSkill(new Weiti);
	wuzhi->addSkill(new Yuanrong);
	addMetaObject<WeitiCard>();

	General*zhongyu = new General(this,"zhongyu","wei",3);
	zhongyu->addSkill(new Jieshi);
	zhongyu->addSkill(new Zhidui);
	addMetaObject<JieshiCard>();

	General*zhushuo = new General(this,"zhushuo","wei",4);
	zhushuo->addSkill(new Shuhe);
	zhushuo->addSkill(new Jilie);
	addMetaObject<JilieCard>();

	General*huanjie = new General(this,"huanjie","wei",3);
	huanjie->addSkill(new Jianli);
	huanjie->addSkill(new Qingzheng);
	addMetaObject<JianliCard>();
}
{//钟灵毓秀
	General*dongguiren = new General(this,"dongguiren","qun",3,false);
	dongguiren->addSkill(new Lianzhi);
	dongguiren->addSkill(new Lingfang);
	dongguiren->addSkill(new Fengying);
	addMetaObject<FengyingCard>();
	skills << new Shouze;

	General*tenyear_tengfanglan = new General(this,"tenyear_tengfanglan","wu",3,false);
	tenyear_tengfanglan->addSkill(new TenyearLuochong);
	tenyear_tengfanglan->addSkill(new TenyearAichen);
	tenyear_tengfanglan->addSkill(new TenyearAichenSpade);

	General*zhangjinyun = new General(this,"zhangjinyun","shu",3,false);
	zhangjinyun->addSkill(new Huizhi);
	zhangjinyun->addSkill(new Jijiao);
	zhangjinyun->addSkill(new JijiaoRecord);
	addMetaObject<JijiaoCard>();

	General*zhugejing = new General(this,"zhugejing","qun",4);
	zhugejing->addSkill(new Yanzuo);
	zhugejing->addSkill(new ThZhuyin);
	zhugejing->addSkill(new Pijian);
	addMetaObject<YanzuoCard>();

	General*erliu = new General(this,"erliu","wei",3,false);
	erliu->addSkill(new ThQixin);
	erliu->addSkill(new Jiusi);
	addMetaObject<JiusiCard>();
}



}
ADD_PACKAGE(TenyearHc)
