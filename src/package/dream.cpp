#include "dream.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
//#include "maneuvering.h"
//#include "json.h"
//#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

class IfAnxu : public TriggerSkill
{
public:
    IfAnxu() : TriggerSkill("ifanxu")
    {
        events << Damaged;
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(player->askForSkillInvoke(objectName())){
			player->drawCards(2,objectName());
			Card*dc = room->askForDiscard(player,objectName(),2,2,false,true);
			if(dc&&dc->getSuit()==Card::NoSuit){
				room->recover(player,RecoverStruct(objectName(),player));
			}else if(dc&&dc->isBlack()){
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"ifanxu0");
				if(tp){
					room->doAnimate(1,player->objectName(),tp->objectName());
					room->loseHp(tp,1,true,player,objectName());
				}
			}
		}
        return false;
    }
};

IfMishouCard::IfMishouCard()
{
	will_throw = false;
}

bool IfMishouCard::targetFilter(const QList<const Player *> &targets, const Player *tp, const Player *Self) const
{
	return targets.isEmpty()&&tp!=Self;
}

void IfMishouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach(ServerPlayer*tp,targets){
		room->addPlayerMark(tp,"ifmishouBf-SelfClear");
		room->giveCard(source,tp,this,"ifmishou");
	}
}

class IfMishouvs : public OneCardViewAsSkill
{
public:
	IfMishouvs(): OneCardViewAsSkill("ifmishou")
	{
		response_pattern = "@@ifmishou";
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*sc = new IfMishouCard;
		sc->addSubcard(originalCard);
		return sc;
	}
};

class IfMishou : public TriggerSkill
{
public:
    IfMishou() : TriggerSkill("ifmishou")
    {
        events << TargetSpecified << DamageCaused << EventPhaseStart;
		view_as_skill = new IfMishouvs;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&player->getMark("ifmishouBf-SelfClear")>0&&player->getMark("ifmishouBfto-SelfClear")<1){
				player->addMark("ifmishouBfto-SelfClear");
				foreach(ServerPlayer*tp,use.to){
					room->addPlayerMark(player,tp->objectName()+"ifmishouBfto-SelfClear");
				}
			}
		}else if(event==EventPhaseStart){
            if(player->getPhase()==Player::Finish&&player->hasSkill(objectName())&&player->getHandcardNum()>0){
				room->askForUseCard(player,"@@ifmishou","ifmishou0");
			}
		}else if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&player->getMark("ifmishouBf-SelfClear")>0){
				room->loseHp(damage.to,damage.damage,true,player,damage.card->objectName());
				return true;
			}
		}
        return false;
    }
};

class IfMishouBf : public TargetModSkill
{
public:
	IfMishouBf(): TargetModSkill("#IfMishouBf")
	{
		pattern = ".";
	}

	int getResidueNum(const Player*from,const Card*card,const Player*to) const
	{
		if(from->getMark("&ifdianbian-Clear")>0)
			return 999;
		int n = 0;
		if(card->isKindOf("Slash")){
            if(to&&from->getMark(to->objectName()+"ifmishouBfto-SelfClear")>0)
				return 999;
			n += from->getMark("ifxiechangUp");
		}
		return n;
	}

	int getDistanceLimit(const Player*,const Card*card,const Player*) const
	{
		if(card->getSkillName()=="ifshenfeng")
			return 999;
		return 0;
	}

	int getExtraTargetNum(const Player*,const Card*) const
	{
		return 0;
	}
};

IfDianbianCard::IfDianbianCard()
{
	target_fixed = true;
}

void IfDianbianCard::use(Room*room,ServerPlayer*source,QList<ServerPlayer*>&) const
{
	room->removePlayerMark(source,"@ifdianbian");
	room->doSuperLightbox(source,"ifdianbian");
	Card*dc = dummyCard();
	foreach(int id,room->getDiscardPile()){
        const Card*c = Sanguosha->getEngineCard(id);
		if(c->isKindOf("Slash")||c->getTypeId()==3){
			if(source->getMark("ifdianbianId"+c->toString())>0)
				dc->addSubcard(id);
		}
	}
	source->obtainCard(dc);
	room->setPlayerMark(source,"&ifdianbian-Clear",1);
}

class IfDianbianvs : public ZeroCardViewAsSkill
{
public:
	IfDianbianvs(): ZeroCardViewAsSkill("ifdianbian")
	{
	}
	const Card*viewAs() const
	{
		return new IfDianbianCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("@ifdianbian")>0&&player->getMark("ifdianbianCan")>0;
	}
};

class IfDianbian : public TriggerSkill
{
public:
	IfDianbian(): TriggerSkill("ifdianbian")
	{
		events << CardsMoveOneTime << SwappedPile << Death << EventPhaseChanging;
		limit_mark = "@ifdianbian";
		waked_skills = "ifpiyong";
		view_as_skill = new IfDianbianvs;
		frequency = Skill::Limited;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&player==move.from
			&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				foreach(int id,move.card_ids){
					player->addMark(QString("ifdianbianId%1").arg(id));
				}
			}
		}else if(event==SwappedPile){
			foreach(QString m,player->getMarkNames()){
				if(m.contains("ifdianbianId"))
					player->setMark(m,0);
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
            if(death.hplost&&death.hplost->to)
				room->setPlayerMark(player,"ifdianbianCan",1);
			else
				room->setPlayerMark(player,"ifdianbianCan",0);
			if(player->getMark("&ifdianbian-Clear")>0)
				room->gainMaxHp(player,1,objectName());
		}else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->getMark("&ifdianbian-Clear")>0){
						int n = 0;
						foreach(ServerPlayer*q,room->getAlivePlayers()){
							n = qMax(n,q->getMaxHp());
						}
						if(p->getMaxHp()>=n)
							room->handleAcquireDetachSkills(p,"-ifanxu|ifpiyong");
					}
				}
			}
		}
		return false;
	}
};

IfPiyongCard::IfPiyongCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodRecast;
}

void IfPiyongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int n = ((const Weapon*)Sanguosha->getCard(getEffectiveId())->getRealCard())->getRange();
	LogMessage log;
	log.type = "$RecastCard";
	log.from = source;
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST,source->objectName(),"ifpiyong","");
	room->moveCardTo(this,nullptr,Player::DiscardPile,reason,true);
	source->drawCards(1,"recast");
	foreach(int id,source->drawCardsList(n,"ifpiyong")){
		room->setCardTip(id,"ifpiyong");
	}
}

class IfPiyong : public OneCardViewAsSkill
{
public:
	IfPiyong(): OneCardViewAsSkill("ifpiyong$")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return to_select->isKindOf("Weapon");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*sc = new IfPiyongCard;
		sc->addSubcard(originalCard);
		return sc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->hasLordSkill(objectName());
	}
};

class IfXiance : public TriggerSkill
{
public:
	IfXiance(): TriggerSkill("ifxiance")
	{
		events << EventPhaseChanging << TargetSpecified << CardUsed << CardResponded << DamageInflicted;
		frequency = Skill::Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->getMark("&ifxian_ce")>0){
						p->loseMark("&ifxian_ce");
                    }
				}
			}
		}else if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&!use.to.contains(player)&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&ifxian_ce",use.to.length());
				room->setCardFlag(use.card,"ifxian_ce");
			}
		}else if(event==DamageInflicted){
			player->addMark("ifxianceNum-Clear");
			if(player->getMark("ifxianceNum-Clear")==1&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,objectName());
				DamageStruct damage = data.value<DamageStruct>();
				int n = player->getMark("&ifxian_ce")*damage.damage;
				return player->damageRevises(data,n-damage.damage);
			}
		}else{
			const Card*c = nullptr;
			if(event==CardUsed){
				CardUseStruct use = data.value<CardUseStruct>();
				c = use.whocard;
			}else{
				CardResponseStruct res = data.value<CardResponseStruct>();
				c = res.m_toCard;
			}
			if(c&&c->hasFlag("ifxian_ce")){
				room->loseHp(player,1,true,nullptr,objectName());
			}
		}
		return false;
	}
};

class IfZhenshi : public TriggerSkill
{
public:
	IfZhenshi(): TriggerSkill("ifzhenshi")
	{
		events << EventPhaseStart << TargetConfirmed << CardsMoveOneTime;
		waked_skills = "ifjiusuo";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::RoundStart){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(player->getMark("ifzhenshiTo"+p->objectName())>0){
						player->setMark("ifzhenshiTo"+p->objectName(),0);
						room->detachSkillFromPlayer(p,"ifjiusuo");
                    }
				}
			}else if(player->getPhase()==Player::Finish&&player->hasSkill(objectName())){
				QList<Card::Suit> suits;
				foreach(int id,room->getDiscardPile()){
					if(player->getMark(QString("ifzhenshiId%1-Clear").arg(id))>0){
						Card*c = Sanguosha->getCard(id);
						if(suits.contains(c->getSuit())) continue;
						suits.append(c->getSuit());
					}
				}
				if(suits.isEmpty()) return false;
				QList<ServerPlayer*>tps = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,suits.length(),QString("ifzhenshi0:%1").arg(suits.length()));
				if(tps.isEmpty()) return false;
				player->skillInvoked(objectName());
				foreach(ServerPlayer*p,tps)
					player->setMark("ifzhenshiTo"+p->objectName(),1);
			}
		}else if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()&&use.to.contains(player)){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->getMark("ifzhenshiTo"+player->objectName())>0){
						room->acquireSkill(player,"ifjiusuo");
						break;
                    }
				}
			}
		}else if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id,move.card_ids){
					player->addMark(QString("ifzhenshiId%1-Clear").arg(id));
				}
			}
		}
		return false;
	}
};

class IfJiusuo : public TriggerSkill
{
public:
	IfJiusuo(): TriggerSkill("ifjiusuo")
	{
		events << TargetConfirmed;
		frequency = Skill::Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()&&use.from&&use.from!=player&&use.to.contains(player)){
				foreach(ServerPlayer*p,use.to){
					if(p->hasSkill(objectName(),true)) continue;
					return false;
				}
				room->sendCompulsoryTriggerLog(player,this);
				JudgeStruct judge;
				judge.reason = objectName();
				judge.who = use.from;
				judge.pattern = ".|.|0~8";
				judge.good = false;
				judge.negative = true;
				room->judge(judge);
				if(judge.isBad()){
					use.nullified_list << "_ALL_TARGETS";
					data.setValue(use);
					if(judge.card->getNumber()<use.from->getHp())
						room->loseHp(use.from,1,true,player,objectName());
					if(judge.card->getNumber()<use.from->getCardCount()){
						int id = room->askForCardChosen(player,use.from,"he",objectName());
						if(id>=0) room->obtainCard(player,id,false);
					}
				}
			}
		}
		return false;
	}
};

class IfYinglvevs : public OneCardViewAsSkill
{
public:
	IfYinglvevs(): OneCardViewAsSkill("ifyinglve")
	{
		response_pattern = "@@ifyinglve";
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag(objectName()).value<const Card *>();
			if(c==nullptr) return nullptr;
			pattern = c->objectName();
		}else
			pattern = Self->property("ifyinglveCn").toString();
		Card*sc = Sanguosha->cloneCard(pattern);
		sc->setSkillName("_"+objectName());
		sc->addSubcard(originalCard);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("ifyinglveUse-PlayClear")<1;
	}
};

class IfYinglve : public TriggerSkill
{
public:
	IfYinglve() : TriggerSkill("ifyinglve")
	{
		events << TargetConfirmed << CardFinished;
		view_as_skill = new IfYinglvevs;
	}
	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(objectName(), "fire_slash,fire_attack");
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(event==TargetConfirmed){
			if(use.card->isDamageCard()&&use.card->getSkillNames().contains(objectName())&&use.from->hasSkill(objectName(),true)){
				if(player==use.from)
					room->addPlayerMark(player,"ifyinglveUse-PlayClear");
				if(use.to.contains(player)&&player->canDiscard("he")
					&&room->askForDiscard(player,objectName(),2,2,true,true,"ifyinglve0:"+use.card->objectName())){
					use.nullified_list << player->objectName();
					data.setValue(use);
				}
			}
		}else{
			if(use.card->isDamageCard()&&use.card->getSkillNames().contains(objectName())&&player->hasSkill(objectName(),true)){
                if(use.card->hasFlag("DamageDone")||room->getCardOwner(use.card->getEffectiveId())) return false;
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"ifyinglve1:"+use.card->objectName(),true,true);
				if(tp){
					room->giveCard(player,tp,use.card,objectName());
					player->drawCards(1,objectName());
					if(tp->isAlive()&&tp->getHandcardNum()>0){
						QString cn = "fire_slash";
						if(use.card->objectName()==cn) cn = "fire_attack";
						room->setPlayerProperty(tp,"ifyinglveCn",cn);
						room->askForUseCard(tp,"@@ifyinglve","ifyinglve2:"+cn);
					}
				}
			}
		}
		return false;
	}
};

class IfBihe : public TriggerSkill
{
public:
	IfBihe() : TriggerSkill("ifbihe")
	{
		events << TargetSpecifying << ConfirmDamage << Damage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()&&use.to.length()==1){
				foreach(ServerPlayer*p,room->getAllPlayers()){
                    if(p->hasFlag("CurrentPlayer")&&p->hasSkill(objectName())&&p->canDiscard("he")){
						p->setTag("ifbiheData", data);
						Card*dc = room->askForDiscard(p,objectName(),2,2,true,true,"ifbihe0:"+use.card->objectName(),".",objectName());
						if(dc==nullptr) continue;
						if(dc->isBlack())
							use.no_respond_list << "_ALL_TARGETS";
						else if(dc->isRed()){
							room->setCardFlag(use.card,"ifbiheBf");
							room->setCardFlag(use.card,"ifbiheOwner"+p->objectName());
						}else{
							ServerPlayer*tp = room->askForPlayerChosen(player,room->getCardTargets(player,use.card,use.to),objectName(),"ifbihe1:"+use.card->objectName(),true);
							if(tp){
								use.to << tp;
								room->sortByActionOrder(use.to);
								use.extra_use++;
							}
						}
						data.setValue(use);
					}
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("ifbiheBf")){
				int n = qAbs(damage.from->getHandcardNum()-damage.to->getHandcardNum());
				n = qMin(n,5);
				return player->damageRevises(data,n-damage.damage);
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("ifbiheBf")){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(damage.card->hasFlag("ifbiheOwner"+p->objectName())){
						int n = p->getMaxHp()-p->getHandcardNum();
						if(n>0) p->drawCards(n,objectName());
					}
				}
			}
		}
		return false;
	}
};

IfShijiCard::IfShijiCard()
{
	will_throw = false;
}

bool IfShijiCard::targetFilter(const QList<const Player *> &targets, const Player *tp, const Player *Self) const
{
	return targets.isEmpty()&&Self!=tp;
}

void IfShijiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach(ServerPlayer*tp,targets){
		room->giveCard(source,tp,this,"ifshiji",true);
		if(source->canDiscard(tp,"h")){
			int id = room->askForCardChosen(source,tp,"h","ifshiji",false,Card::MethodDiscard);
			if(id>=0){
				room->throwCard(id,"ifshiji",tp,source);
				const Card*c1 = Sanguosha->getEngineCard(getEffectiveId());
				const Card*c2 = Sanguosha->getEngineCard(id);
				if(c1->getColor()!=c2->getColor()){
					room->recover(tp,RecoverStruct("ifshiji",source));
				}
				if(c1->getType()==c2->getType()){
					QList<ServerPlayer *>tps;
					tps << source << tp;
					room->drawCards(tps,1,"ifshiji");
				}
			}
		}
	}
}

class IfShiji : public OneCardViewAsSkill
{
public:
	IfShiji(): OneCardViewAsSkill("ifshiji")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*sc = new IfShijiCard;
		sc->addSubcard(originalCard);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("IfShijiCard")<player->getHp();
	}
};

IfAnjieCard::IfAnjieCard()
{
}

bool IfAnjieCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self) const
{
	if(user_string.isEmpty())
		return targets.isEmpty()&&to_select!=Self&&Self->getMark("ifanjieBf"+to_select->objectName())>0;
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}

bool IfAnjieCard::targetFixed() const
{
	if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	if(user_string.isEmpty())
		return false;
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetFixed();
}

bool IfAnjieCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self) const
{
	if(user_string.isEmpty())
		return targets.length()>0;
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetsFeasible(targets,Self);
}

const Card*IfAnjieCard::validateInResponse(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	QList<ServerPlayer*>tps;
	foreach(ServerPlayer*p,room->getOtherPlayers(player)){
		if(player->getMark("ifanjieBf"+p->objectName())>0)
			tps << p;
	}
	ServerPlayer*tp = room->askForPlayerChosen(player,tps,"ifanjie","ifanjie0",true,true);
	if(tp){
		QList<int>ids;
		foreach(const Card*h,tp->getHandcards()){
			if(player->isLocked(h)) continue;
			foreach(QString cn,user_string.split("+")){
				if(h->sameNameWith(cn))
					ids << h->getId();
			}
		}
		int id = room->doGongxin(player,tp,ids,"ifanjie");
		if(id>=0) return Sanguosha->getCard(id);
        room->addPlayerMark(player,"ifanjieBan_lun");
	}
	return nullptr;
}

const Card*IfAnjieCard::validate(CardUseStruct&use) const
{
	Room*room = use.from->getRoom();
	if(user_string.isEmpty()){
		QList<int>ids;
		use.from->skillInvoked("ifanjie");
		foreach(const Card*h,use.to.last()->getHandcards()){
			if(h->isAvailable(use.from)) ids << h->getId();
		}
		int id = room->doGongxin(use.from,use.to.last(),ids,"ifanjie");
		if(id>=0){
			room->setPlayerMark(use.from,"ifanjieId",id);
			if(room->askForUseCard(use.from,"@@ifanjie","ifanjie0:"+Sanguosha->getCard(id)->objectName()))
				return nullptr;
		}
		room->addPlayerMark(use.from,"ifanjieBan_lun");
		return nullptr;
	}
	return validateInResponse(use.from);
}

class IfAnjievs : public ZeroCardViewAsSkill
{
public:
	IfAnjievs() : ZeroCardViewAsSkill("ifanjie")
	{
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@ifanjie"){
			return Sanguosha->getCard(Self->getMark("ifanjieId"));
		}
		SkillCard*sc = new IfAnjieCard();
		sc->setUserString(pattern);
		return sc;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern) const
	{
		if(pattern=="@@ifanjie") return true;
		if(Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		||player->getMark("ifanjieBan_lun")>0) return false;
		bool has = false;
		foreach(const Player*p,player->getAliveSiblings()){
			has = player->getMark("ifanjieBf"+p->objectName())>0;
			if(has) break;
		}
		if(!has) return false;
		foreach(QString cn,pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(dc->getTypeId()>0)
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("ifanjieBan_lun")<1;
	}
};

class IfAnjie : public TriggerSkill
{
public:
	IfAnjie() : TriggerSkill("ifanjie")
	{
		events << HpRecover;
		view_as_skill = new IfAnjievs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==HpRecover){
			RecoverStruct rec = data.value<RecoverStruct>();
			if(rec.who)
				room->setPlayerMark(rec.who,"ifanjieBf"+player->objectName(),1);
		}
		return false;
	}
};

class IfLitian : public TriggerSkill
{
public:
	IfLitian(): TriggerSkill("iflitian")
	{
		events << EventPhaseStart;
		waked_skills = "ifhuangchu,_iffeileijia";
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&) const
	{
		if(player->getPhase()==Player::Start&&player->askForSkillInvoke(objectName())){
			QList<int>ids = room->getNCards(3);
			room->fillAG(ids, player);
			int id = room->askForAG(player,ids,false,objectName());
			room->clearAG(player);
			room->returnToTopDrawPile(ids);
			Card*c = Sanguosha->getCard(id);
			room->moveCardTo(c,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_REASON_TURNOVER,player->objectName(),objectName(),""),true);
			room->getThread()->delay();
			room->obtainCard(player,id,true);
			if(c->getSuit()==Card::Spade){
				room->detachSkillFromPlayer(player,objectName(),false,false,false);
				room->acquireSkill(player,"iflitian2",true,true,false);
			}
		}
		return false;
	}
};

class IfLitian2 : public TriggerSkill
{
public:
	IfLitian2(): TriggerSkill("iflitian2")
	{
		events << EventPhaseStart << TargetConfirmed << PreHpRecover;
        shiming_skill = true;
		waked_skills = "ifhuangchu,_iffeileijia";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())&&player->getMark(objectName())<1){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getHandcardNum()>player->getHandcardNum()||p->getHp()>player->getHp())
						return false;
				}
				room->sendShimingLog(player,this,true);
				room->gainMaxHp(player,1,objectName());
				room->handleAcquireDetachSkills(player,"-ifanjie|ifhuangchu");
			}
		}else if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Peach")&&use.to.contains(player)
			&&player->hasSkill(objectName())&&player->getMark(objectName())<1){
				room->sendCompulsoryTriggerLog(player,this);
				if(use.from!=player){
					use.nullified_list << player->objectName();
					data.setValue(use);
				}else
					room->setCardFlag(use.card,"iflitian2Bf");
			}
		}else if(event==PreHpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->hasFlag("iflitian2Bf")){
				recover.recover++;
				data.setValue(recover);
			}
		}
		return false;
	}
};

class IfHuangchu : public TriggerSkill
{
public:
	IfHuangchu(): TriggerSkill("ifhuangchu$")
	{
		events << EventPhaseStart;
		frequency = Skill::Compulsory;
		waked_skills = "_iffeileijia";
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&) const
	{
		if(player->getPhase()==Player::Start){
			room->sendCompulsoryTriggerLog(player,this);
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				room->doAnimate(1,player->objectName(),p->objectName());
			}
			QList<int>ids;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				int n = qMin(3,p->getMaxHp());
				n = p->getHandcardNum()-n;
				if(n>0){
					Card*dc = room->askForDiscard(p,objectName(),n,n);
					if(dc) ids << dc->getSubcards();
				}
			}
			foreach(int id,ids){
				if(room->getCardOwner(id))
					ids.removeOne(id);
			}
			if(ids.length()>0&&player->isAlive()){
				QList<ServerPlayer*>tps = player->assignmentCards(ids,"ifhuangchu|ifhuangchu0",room->getAlivePlayers(),ids.length(),ids.length(),true);
				if(tps.contains(player)) return false;
			}
			room->recover(player,RecoverStruct(objectName(),player));
		}else if(player->getPhase()==Player::Finish){
			const Card*t = player->getTreasure();
			if(t&&t->objectName()=="_iffeileijia") return false;
			room->sendCompulsoryTriggerLog(player,this);
			player->getDerivativeCard("_iffeileijia");
		}
		return false;
	}
};

class IfRenli : public TriggerSkill
{
public:
	IfRenli(): TriggerSkill("ifrenli")
	{
		events << EventPhaseStart << Dying;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::NotActive){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->getMark("ifrenliBf")>0){
						p->setMark("ifrenliBf",0);
						p->gainAnExtraTurn();
                    }
				}
			}
		}else if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			player->addMark(dying.who->objectName()+"ifrenliDying_lun");
			if(dying.who!=player&&player->getMark(dying.who->objectName()+"ifrenliDying_lun")==1&&player->isAlive()
			&&player->hasSkill(objectName())&&player->getHandcardNum()>0&&player->askForSkillInvoke(objectName(),dying.who)){
				Card*dc = room->askForExchange(player,objectName(),2,2,false,"ifrenli0");
				if(dc){
					room->giveCard(player,dying.who,dc,objectName());
					if(dying.who->getHp()+1>0) dying.who->addMark("ifrenliBf");
					room->recover(dying.who,RecoverStruct(objectName(),player));
				}
			}
		}
		return false;
	}
};

class IfMingduan : public TriggerSkill
{
public:
	IfMingduan(): TriggerSkill("ifmingduan")
	{
		events << EventPhaseChanging;
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if(change.to==Player::NotActive||change.from==Player::NotActive){
			if(player->askForSkillInvoke(objectName())){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					room->doAnimate(1,player->objectName(),p->objectName());
				}
				int x = 0;
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->isAlive()){
						Card*dc = room->askForExchange(p,objectName(),1,1,true,"ifmingduan0",true);
						if(dc){
							tps << p;
							room->moveCardsInToDrawpile(p,dc,objectName(),1);
						}else
							x++;
					}
				}
				if(player->isAlive()){
                    QList<int>ids = room->showDrawPile(player,player->aliveCount(),objectName());
					int n = 0;
					foreach(int id,ids){
						Card*c = Sanguosha->getCard(id);
						if(c->isRed()) n++;
						else if(c->isBlack()) n--;
					}
					room->getThread()->delay();
					room->throwCard(ids,objectName(),nullptr);
					if(n>0){
						ServerPlayer*tp = room->askForPlayerChosen(player,tps,"ifmingduan1","ifmingduan1");
						if(tp){
							room->doAnimate(1,player->objectName(),tp->objectName());
							room->recover(tp,RecoverStruct(objectName(),player));
						}
					}else if(n<0){
						ServerPlayer*tp = room->askForPlayerChosen(player,tps,"ifmingduan2","ifmingduan2");
						if(tp){
							room->doAnimate(1,player->objectName(),tp->objectName());
							room->damage(DamageStruct(objectName(),player,tp));
						}
					}
					player->drawCards(x,objectName());
				}
			}
		}
		return false;
	}
};

IfSixiangCard::IfSixiangCard()
{
}

bool IfSixiangCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}

bool IfSixiangCard::targetFixed() const
{
	if(user_string.isEmpty()||Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetFixed();
}

bool IfSixiangCard::targetsFeasible(const QList<const Player*>&targets,const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard(user_string.split("+")[0]);
	dc->deleteLater();
	return dc->targetsFeasible(targets,Self);
}

const Card*IfSixiangCard::validateInResponse(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	QList<int>ids,ids2;
	foreach(int id,room->getDrawPile()){
		Card*c = Sanguosha->getCard(id);
		bool can = false;
		foreach(QString cn,user_string.split("+")){
			if(c->sameNameWith(cn)) can = true;
		}
		if(player->isLocked(c)) can = false;
		if(can&&player->getMark(c->getType()+"ifsixiangBan-Clear")<1)
			ids << id;
		else
			ids2 << id;
		if(ids.length()+ids2.length()>2) break;
	}
	room->setPlayerFlag(player,"ifsixiangFailed");
	room->fillAG(ids + ids2, player, ids2);
	int id = room->askForAG(player,ids,true,"ifsixiang");
	room->clearAG(player);
	if(id>=0){
		Card*c = Sanguosha->getCard(id);
		room->addPlayerMark(player,c->getType()+"ifsixiangBan-Clear");
		return c;
	}
	return nullptr;
}

const Card*IfSixiangCard::validate(CardUseStruct&use) const
{
	Room*room = use.from->getRoom();
	QList<int>ids,ids2;
	if(user_string.isEmpty()){
		foreach(int id,room->getDrawPile()){
			Card*c = Sanguosha->getCard(id);
			if(use.from->getMark(c->getType()+"ifsixiangBan-Clear")<1&&c->isAvailable(use.from))
				ids << id;
			else
				ids2 << id;
			if(ids.length()+ids2.length()>2) break;
		}
		room->fillAG(ids + ids2, use.from, ids2);
		int id = room->askForAG(use.from,ids,true,"ifsixiang");
		room->clearAG(use.from);
		if(id>=0){
			Card*c = Sanguosha->getCard(id);
			room->setPlayerMark(use.from,"ifsixiangId",id);
			room->askForUseCard(use.from,"@@ifsixiang","ifsixiang0:"+c->objectName(),-1,Card::MethodUse,true,nullptr,nullptr,"ifsixiangUse");
		}
		return nullptr;
	}
	return validateInResponse(use.from);
}

class IfSixiangvs : public ZeroCardViewAsSkill
{
public:
	IfSixiangvs() : ZeroCardViewAsSkill("ifsixiang")
	{
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@ifsixiang"){
			return Sanguosha->getCard(Self->getMark("ifsixiangId"));
		}
		SkillCard*sc = new IfSixiangCard();
		sc->setUserString(pattern);
		return sc;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern) const
	{
		if(pattern=="@@ifsixiang") return true;
		bool has = false;
		foreach(const Player*p,player->getSiblings()){
			has = p->isDead();
			if(has) break;
		}
		if(!has||player->hasFlag("ifsixiangFailed")) return false;
		foreach(QString cn,pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(player->getMark(dc->getType()+"ifsixiangBan-Clear")<1)
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach(const Player*p,player->getSiblings()){
			if(p->isDead())
				return true;
		}
		return false;
	}
};

class IfSixiang : public TriggerSkill
{
public:
	IfSixiang(): TriggerSkill("ifsixiang")
	{
		events << PreCardUsed;
		view_as_skill = new IfSixiangvs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("ifsixiangUse"))
				room->addPlayerMark(player,use.card->getType()+"ifsixiangBan-Clear");
		}
		return false;
	}
};

IfJizhiCard::IfJizhiCard()
{
	target_fixed = false;
}

bool IfJizhiCard::targetFilter(const QList<const Player*>&targets,const Player*to_select,const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}

const Card*IfJizhiCard::validateInResponse(ServerPlayer*player) const
{
	QList<ServerPlayer*>tps;
	Room*room = player->getRoom();
	foreach(ServerPlayer*p,room->getOtherPlayers(player)){
		if(p->hasLordSkill("ifjizhi")&&p->getMark("ifjizhiBan-Clear")<1)
			tps << p;
	}
	ServerPlayer*tp = room->askForPlayerChosen(player,tps,"ifjizhi","ifjizhi0");
	if(tp){
        room->addPlayerMark(tp,"ifjizhiBan-Clear");
		player->skillInvoked("ifjizhi",-1,tp);
		tp->drawCards(1,"ifjizhi");
		return room->askForCard(tp,"slash","ifjizhi1:"+player->objectName(),QVariant::fromValue(player),Card::MethodResponse,player);
	}
	return nullptr;
}

const Card*IfJizhiCard::validate(CardUseStruct&use) const
{
	return validateInResponse(use.from);
}

class IfJizhivs : public ZeroCardViewAsSkill
{
public:
	IfJizhivs() : ZeroCardViewAsSkill("ifjizhivs&")
	{
	}

	const Card *viewAs() const
	{
		return new IfJizhiCard();
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern) const
	{
		if(!pattern.contains("slash")) return false;
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->hasLordSkill("ifjizhi")&&p->getMark("ifjizhiBan-Clear")<1)
				return true;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach(const Player*p,player->getAliveSiblings()){
			if(p->hasLordSkill("ifjizhi")&&p->getMark("ifjizhiBan-Clear")<1)
				return Slash::IsAvailable(player);
		}
		return false;
	}
};

class IfJizhi : public TriggerSkill
{
public:
	IfJizhi() : TriggerSkill("ifjizhi$")
	{
		events << GameStart << EventAcquireSkill << EventLoseSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
    bool trigger(TriggerEvent event,Room*room,ServerPlayer*,QVariant&data) const
	{
		if(event==EventLoseSkill){
			if(data.toString()==objectName()){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->hasLordSkill(objectName(),true)) return false;;
				}
				foreach(ServerPlayer*p,room->getAlivePlayers())
					room->detachSkillFromPlayer(p,"ifjizhivs",false,true,false);
			}
		}else{
			foreach(ServerPlayer*p,room->getAlivePlayers()){
				if(p->hasSkill("ifjizhivs")) continue;
				room->attachSkillToPlayer(p,"ifjizhivs");
			}
		}
		return false;
	}
};

class IfHaitian : public TriggerSkill
{
public:
	IfHaitian(): TriggerSkill("ifhantian")
	{
		events << TargetConfirmed << TargetSpecified << DamageDone << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
            if(damage.card&&damage.card->getSkillNames().contains(objectName()))
				player->setTag("ifhantianDone", true);
		}else if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->getMark("ifhantianBf-Clear")>0){
                        room->removePlayerCardLimitation(p,"use,response",".|.|.|hand");
						p->setMark("ifhantianBf-Clear",0);
					}
				}
			}
		}else{
			if(player->getMark("ifhantianUse-Clear")>0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.to.length()!=1||!use.from||use.card->getTypeId()<1) return false;
			ServerPlayer*tp = use.to.last();
			if(event==TargetConfirmed){
				if(!use.to.contains(player)||use.from==player) return false;
				tp = use.from;
			}else
				if(tp==player) return false;
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(objectName(),data)){
				player->addMark("ifhantianUse-Clear");
				player->setTag("ifhantianDone", false);
				CardEffectStruct effect;
				Card*dc = Sanguosha->cloneCard("lightning");
				dc->setSkillName(objectName());
				dc->deleteLater();
				effect.card = dc;
				effect.to = player;
				dc->onEffect(effect);
				if(player->getTag("ifhantianDone").toBool()||player->isDead()||tp->isDead()) return false;
				for(int i = 0; i < 2; i++){
					QList<ServerPlayer*>tps;
					if(player->canDiscard("he")) tps << player;
					if(player->canDiscard(tp,"he")) tps << tp;
					ServerPlayer*p = room->askForPlayerChosen(player,tps,objectName(),"ifhantian0");
					if(p){
						room->doAnimate(1,player->objectName(),p->objectName());
						int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard);
						if(id>=0){
							if(tp->getMark("ifhantianBf-Clear")<1&&Sanguosha->getCard(id)->isKindOf("EquipCard")){
								room->setPlayerCardLimitation(tp,"use,response",".|.|.|hand",false);
								tp->addMark("ifhantianBf-Clear");
							}
							room->throwCard(id,objectName(),p,player);
						}
					}
				}
			}
		}
		return false;
	}
};

class IfShenfengvs : public ZeroCardViewAsSkill
{
public:
	IfShenfengvs() : ZeroCardViewAsSkill("ifshenfeng")
	{
	}
	const Card *viewAs() const
	{
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName(objectName());
		foreach(const Card*h,Self->getHandcards()){
			if(h->isDamageCard()) continue;
			dc->addSubcard(h);
		}
		return dc;
	}

	bool isEnabledAtResponse(const Player*player,const QString&pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		||!pattern.contains("slash")) return false;
		foreach(const Card*h,player->getHandcards()){
			if(h->isDamageCard()) continue;
			return true;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach(const Card*h,player->getHandcards()){
			if(h->isDamageCard()) continue;
			return Slash::IsAvailable(player);
		}
		return false;
	}
};

class IfShenfeng : public TriggerSkill
{
public:
	IfShenfeng() : TriggerSkill("ifshenfeng")
	{
		events << PreCardUsed << Damage;
		view_as_skill = new IfShenfengvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())&&damage.to->canDiscard("h")){
				int n = damage.to->getHandcardNum()/2;
				Card*dc = room->askForDiscard(damage.to,objectName(),n,n);
				if(dc&&player->isAlive()){
					QList<int>ids;
					foreach(int id,dc->getSubcards()){
						if(room->getCardOwner(id)) continue;
						ids << id;
					}
					dc = dummyCard();
					room->fillAG(ids, player);
					while(ids.length()>0){
						n = room->askForAG(player,ids,false,objectName());
						dc->addSubcard(n);
						QString cn = Sanguosha->getEngineCard(n)->objectName(false);
						foreach(int id,ids){
							if(Sanguosha->getEngineCard(id)->objectName(false)==cn){
								room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
								ids.removeOne(id);
							}
						}
					}
					room->clearAG();
					player->obtainCard(dc);
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				player->addQinggangTag(use.card);
				QStringList ts;
				foreach(int id,use.card->getSubcards()){
					Card*c = Sanguosha->getCard(id);
					if(ts.contains(c->getType())) continue;
					ts.append(c->getType());
				}
				if(ts.length()>2){
					use.m_addHistory = false;
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class IfXiechang : public TriggerSkill
{
public:
	IfXiechang(): TriggerSkill("ifxiechang")
	{
		events << EventPhaseChanging << Dying;
		frequency = Compulsory;
	}
	Frequency getFrequency(const Player*target) const
	{
		if (target && target->getMark("ifxiechangUp") > 0)
			return NotFrequent;
		return Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive&&player->hasSkill(objectName())){
				QList<ServerPlayer*>tps;
				Card*dc = Sanguosha->cloneCard("fire_slash");
				dc->setSkillName(objectName());
				dc->deleteLater();
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getEquips().length()>=player->getEquips().length()&&player->canSlash(p,dc,false))
						tps << p;
				}
				if(tps.isEmpty()) return false;
				if(getFrequency(player)==Compulsory){
					room->sendCompulsoryTriggerLog(player,this);
					foreach(ServerPlayer*p,tps)
						room->doAnimate(1,player->objectName(),p->objectName());
				}else{
					tps = room->askForPlayersChosen(player,tps,objectName(),0,9,"ifxiechang0",true,true);
					if(tps.isEmpty()) return false;
				}
				room->loseHp(player,1,true,player,objectName());
				foreach(ServerPlayer*p,tps){
					Card*dc = Sanguosha->cloneCard("fire_slash");
					dc->setSkillName("_"+objectName());
					if(player->canSlash(p,dc,false))
						room->useCard(CardUseStruct(dc,player,p));
					dc->deleteLater();
				}
			}
		}else if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.damage&&dying.damage->card&&dying.damage->card->getSkillNames().contains(objectName())
			&&dying.damage->from==player&&dying.who->getCardCount()>0){
				int id = room->askForCardChosen(player,dying.who,"he",objectName());
				if(id>=0) room->obtainCard(player,id,false);
				if(getFrequency(player)!=Compulsory){
					room->recover(player,RecoverStruct(objectName(),player));
				}
			}
		}
		return false;
	}
};

class IfTianmin : public TriggerSkill
{
public:
	IfTianmin(): TriggerSkill("iftianmin")
	{
		events << Dying << Death;
		limit_mark = "@iftianmin";
		frequency = Limited;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who==player&&player->getMark("@iftianmin")>0&&player->hasSkill(objectName())
			&&player->getMark("iftianminKill")<1&&player->askForSkillInvoke(objectName(),data)){
				room->removePlayerMark(player,limit_mark);
				room->doSuperLightbox(player,objectName());
				room->recover(player,RecoverStruct(objectName(),player,player->getMaxHp()-player->getHp()));
				room->addPlayerMark(player,"ifxiechangUp");
				room->changeTranslation(player,"ifxiechang",1);
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player){
				player->addMark("iftianminKill");
			}
		}
		return false;
	}
};

class IfJianxiao : public TriggerSkill
{
public:
	IfJianxiao(): TriggerSkill("ifjianxiao")
	{
		events << TargetSpecified << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasTurn()){
				if(use.card->isKindOf("Slash")&&use.to.length()==1
				&&use.to.last()!=player &&player->hasSkill(objectName())
				&&player->getMark(use.to.last()->objectName()+"ifjianxiaoTo-Clear")>0
				&&player->askForSkillInvoke(objectName(),data)){
					room->setCardFlag(use.card,"ifjianxiaoBf");
				}
				foreach(QString m,player->getMarkNames()){
					if(m.contains("ifjianxiaoTo"))
						player->setMark(m,0);
				}
				foreach(ServerPlayer*p,use.to){
					player->addMark(p->objectName()+"ifjianxiaoTo-Clear");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("ifjianxiaoBf")){
				damage.damage++;
				data.setValue(damage);
			}
		}
		return false;
	}
};

class IfShenwu : public TriggerSkill
{
public:
	IfShenwu(): TriggerSkill("ifshenwu")
	{
		events << EventPhaseStart << DamageCaused;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())
			&&player->canDiscard("h") &&player->askForSkillInvoke(objectName())){
				player->throwAllHandCards(objectName());
				if(player->isDead()) return false;
				Card*dc = dummyCard();
				QStringList cs;
				cs << "slash" << "fire_slash" << "thunder_slash" << "duel";
				foreach(int id,room->getDrawPile()){
					Card*c = Sanguosha->getCard(id);
					if(cs.contains(c->objectName())){
						cs.removeOne(c->objectName());
						dc->addSubcard(id);
					}
				}
				player->obtainCard(dc);
			}
		}else if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getTypeId()>0&&player->hasSkill(objectName())){
				foreach(const Card*h,player->getHandcards()){
					if(h->isDamageCard()) return false;
				}
				room->sendCompulsoryTriggerLog(player,this);
				return player->damageRevises(data,1);
			}
		}
		return false;
	}
};

class IfBashi : public TriggerSkill
{
public:
	IfBashi(): TriggerSkill("ifbashi")
	{
		events << CardUsed << PreCardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				QString cn = use.card->objectName(false);
				player->addMark(cn+"ifbashiUse-Clear");
				if(player->getMark(cn+"ifbashiUse-Clear")==1&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					use.m_addHistory = false;
					use.to.clear();
					foreach(ServerPlayer*p,room->getOtherPlayers(player)){
						if(player->isProhibited(p,use.card)) continue;
						room->doAnimate(1,player->objectName(),p->objectName());
						use.to << p;
					}
					QStringList suits;
					suits << "spade" << "club" << "diamond" << "heart" << "no_suit";
					foreach(ServerPlayer*p,use.to){
						if(p->isDead()) continue;
						QStringList choices;
						choices << "1";
						foreach(const Card*h,p->getHandcards()){
							if(suits.contains(h->getSuitString())&&p->canDiscard(h->getId())){
								choices << "2";
								break;
							}
						}
						if(p->getCardCount()>1&&player->isAlive()) choices << "3="+player->objectName();
						cn = room->askForChoice(p,objectName(),choices.join("+"),data);
						if(cn=="1"){
							use.no_respond_list << p->objectName();
						}else if(cn=="2"){
							Card*dc = room->askForDiscard(p,objectName(),1,1,false,false,"",".|"+suits.join(","));
							if(dc) suits.removeOne(dc->getSuitString());
						}else{
							Card*dc = dummyCard();
							for(int i = 0; i < 2; i++){
                                int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodNone,dc->getSubcards(),true);
								if(id>=0) dc->addSubcard(id);
								else break;
							}
							player->obtainCard(dc,false);
							player->drawCards(1,objectName());
						}
					}
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class IfYinjue : public TriggerSkill
{
public:
	IfYinjue(): TriggerSkill("ifyinjue")
	{
		events << TurnedOver << DamageCaused;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==TurnedOver){
			bool up = player->faceUp();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())){
					if(up){
						if(p->canDiscard(player,"he")){
							room->sendCompulsoryTriggerLog(p,this);
							room->doAnimate(1,p->objectName(),player->objectName());
							int id = room->askForCardChosen(p,player,"he",objectName(),false,Card::MethodDiscard);
							if(id>=0) room->throwCard(id,objectName(),player,p);
						}
					}else{
						room->sendCompulsoryTriggerLog(p,this);
						room->doAnimate(1,p->objectName(),player->objectName());
						player->drawCards(1,objectName());
					}
				}
			}
		}else if(event==DamageCaused){
			if(player->faceUp()) return false;
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p,room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					if(damage.to->faceUp())
						player->damageRevises(data,1);
					else
						player->damageRevises(data,-1);
				}
			}
			damage = data.value<DamageStruct>();
			return damage.damage<1;
		}
		return false;
	}
};

IfBaqiCard::IfBaqiCard()
{
}

bool IfBaqiCard::targetFilter(const QList<const Player *> &targets, const Player *tp, const Player *Self) const
{
	return targets.isEmpty()&&tp!=Self;
}

void IfBaqiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
	foreach(ServerPlayer*tp,room->getAllPlayers()){
		if(targets.contains(tp)||tp->isDead()) continue;
		room->askForUseCard(tp,"@@ifbaqi","ifbaqi0");
	}
}

class IfBaqivs : public ZeroCardViewAsSkill
{
public:
	IfBaqivs() : ZeroCardViewAsSkill("ifbaqi")
	{
		response_pattern = "@@ifbaqi";
	}
	const Card *viewAs() const
	{
		if(Sanguosha->getCurrentCardUsePattern()==response_pattern){
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("_ifbaqi");
			return dc;
		}
		return new IfBaqiCard();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("IfBaqiCard")<1;
	}
};

class IfBaqi : public TriggerSkill
{
public:
	IfBaqi() : TriggerSkill("ifbaqi")
	{
		events << PreCardUsed;
		view_as_skill = new IfBaqivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

    bool trigger(TriggerEvent event,Room*,ServerPlayer*player,QVariant&data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.card->getSkillNames().contains(objectName()))
				player->turnOver();
		}
		return false;
	}
};

class IfHuanghuang : public TriggerSkill
{
public:
	IfHuanghuang(): TriggerSkill("ifhuanghuang$")
	{
		events << Death;
		frequency = Skill::Compulsory;
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who->getKingdom()=="qun"&&death.who->getHandcardNum()>0&&player->hasLordSkill(objectName())){
			room->sendCompulsoryTriggerLog(player,this);
			player->obtainCard(dummyCard(death.who->handCards()),false);
		}
		return false;
	}
};

class IfTunshi : public TriggerSkill
{
public:
	IfTunshi(): TriggerSkill("iftunshi")
	{
		events << Death << RoundEnd;
		frequency = Skill::Compulsory;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			QStringList skills = player->getTag("iftunshi_skills").toStringList();
			foreach(const Skill*s,death.who->getVisibleSkillList()){
				if(player->hasSkill(s,true)||s->isAttachedLordSkill()) continue;
				skills << s->objectName();
			}
			player->setTag("iftunshi_skills", skills);
			room->handleAcquireDetachSkills(player,skills);
		}else{
			QStringList skills2,skills = player->getTag("iftunshi_skills").toStringList();
			if(skills.isEmpty()) return false;
			foreach(QString sn,skills){
				skills2 << "-"+sn;
			}
			room->handleAcquireDetachSkills(player,skills2);
		}
		return false;
	}
};

class IfTianwei : public TriggerSkill
{
public:
	IfTianwei(): TriggerSkill("iftianwei")
	{
		events << TargetSpecified << Damage << RoundEnd;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isBlack()&&use.card->getTypeId()>0&&player->hasSkill(objectName())){
				foreach(ServerPlayer*p,use.to){
					if(p!=player&&player->isAlive()&&p->isAlive()
					&&player->getMark(p->objectName()+"iftianweiBan_lun")<2){
						int n = 0;
						QStringList choices;
						foreach(const Skill*s,p->getVisibleSkillList()){
							if(s->isAttachedLordSkill()) continue;
							choices << "1="+s->objectName();
							n--;
						}
						foreach(const Skill*s,player->getVisibleSkillList()){
							if(s->isAttachedLordSkill()) continue;
							n++;
						}
						if(n>=0&&player->askForSkillInvoke(objectName(),p)){
							n = qMin(qMax(n,1),3);
							choices << QString("2=%1").arg(n);
							QString choice = room->askForChoice(p,objectName(),choices.join("+"),data);
							if(choice.contains("1=")){
								choices = choice.split("=");
								room->addPlayerMark(p,"Qingcheng_"+choices.last());
								p->addMark("iftianweiBanSkill_"+choices.last());
								p->drawCards(2,objectName());
							}else
								room->damage(DamageStruct(objectName(),player,p,n));
						}
					}
				}
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.reason==objectName())
				player->addMark(damage.to->objectName()+"iftianweiBan_lun",damage.damage);
		}else{
			foreach(QString m,player->getMarkNames()){
				if(m.contains("iftianweiBanSkill_")){
					QStringList ms = m.split("_");
					room->removePlayerMark(player,"Qingcheng_"+ms.last(),player->getMark(m));
					player->setMark(m,0);
				}
			}
		}
		return false;
	}
};

class IfXiongzhengvs : public OneCardViewAsSkill
{
public:
	IfXiongzhengvs(): OneCardViewAsSkill("ifxiongzheng")
	{
		response_pattern = "@@ifxiongzheng";
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*sc = Sanguosha->cloneCard("slash");
		sc->addSubcard(originalCard);
		sc->setSkillName(objectName());
		return sc;
	}
};

class IfXiongzheng : public TriggerSkill
{
public:
	IfXiongzheng(): TriggerSkill("ifxiongzheng")
	{
		events << RoundStart << CardFinished << Damaged;
		view_as_skill = new IfXiongzhengvs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getMark(player->objectName()+"ifxiongzhengBan_lun")>0){
						if(player->getMark("&ifxiongzheng+:+"+use.card->getType()+"+#"+p->objectName()+"_lun")>0){
							room->sendCompulsoryTriggerLog(p,objectName());
							p->drawCards(1,objectName());
						}else{
							room->setPlayerProperty(p,"ifxiongzhengSlash",player->objectName());
							room->askForUseCard(p,"@@ifxiongzheng","ifxiongzheng1:"+player->objectName());
						}
					}
				}
			}
		}else if(player->hasSkill(objectName())){
			QList<ServerPlayer*>tps;
			foreach(ServerPlayer*p,room->getOtherPlayers(player)){
				if(player->getMark(p->objectName()+"ifxiongzhengBan_lun")<1)
					tps << p;
			}
			ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"ifxiongzheng0",true,true);
			if(tp){
				player->addMark(tp->objectName()+"ifxiongzhengBan_lun");
				if(tp->getHandcardNum()>0){
					int id = room->askForCardChosen(player,tp,"h",objectName());
					if(id>=0){
						room->showCard(tp,id);
						QString t = Sanguosha->getCard(id)->getType();
						room->setPlayerMark(tp,"&ifxiongzheng+:+"+t+"+#"+player->objectName()+"_lun",1);
					}
				}
			}
		}
		return false;
	}
};

class IfXiongzhengBf : public ProhibitSkill
{
public:
	IfXiongzhengBf(): ProhibitSkill("#IfXiongzhengBf")
	{
	}

	bool isProhibited(const Player*from,const Player*to,const Card*card,const QList<const Player*>&) const
	{
		return card->getSkillName()=="ifxiongzheng"
		&&from->property("ifxiongzhengSlash").toString()!=to->objectName();
	}
};

IfEjiangCard::IfEjiangCard()
{
}

bool IfEjiangCard::targetFilter(const QList<const Player *> &targets, const Player *tp, const Player *Self) const
{
	if(targets.isEmpty()&&Self->property("ifejiangTo").toString()!=tp->objectName())
		return false;
	Card*dc = Sanguosha->cloneCard("iron_chain");
	dc->deleteLater();
	return dc->targetFilter(targets,tp,Self);
}

const Card*IfEjiangCard::validate(CardUseStruct&use) const
{
	use.from->getRoom()->addPlayerMark(use.from,"ifejiangBan-Clear");
	Card*dc = Sanguosha->cloneCard("iron_chain");
	dc->setSkillName("ifejiang");
	dc->deleteLater();
	return dc;
}

class IfEjiangvs : public ZeroCardViewAsSkill
{
public:
	IfEjiangvs() : ZeroCardViewAsSkill("ifejiang")
	{
		response_pattern = "@@ifejiang";
	}
	const Card *viewAs() const
	{
		return new IfEjiangCard();
	}
};

class IfEjiang : public TriggerSkill
{
public:
	IfEjiang() : TriggerSkill("ifejiang")
	{
		events << Damaged;
		view_as_skill = new IfEjiangvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&) const
	{
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->hasSkill(objectName())&&p->getMark("ifejiangBan-Clear")<1){
				room->setPlayerProperty(p,"ifejiangTo",player->objectName());
				room->askForUseCard(p,"@@ifejiang","ifejiang0:"+player->objectName());
			}
		}
		return false;
	}
};

class IfPini : public TriggerSkill
{
public:
	IfPini(): TriggerSkill("ifpini")
	{
		events << ChainStateChanged << Dying;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==ChainStateChanged){
			if(player->isChained()) return false;
		}else if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who!=player) return false;
		}
		foreach(ServerPlayer*p,room->getAllPlayers()){
			if(p->hasSkill(objectName())){
				QList<int>ids;
				foreach(const Card*c,player->getCards("ej")){
					bool ban = true;
					foreach(ServerPlayer*q,room->getOtherPlayers(player)){
						if(q->isChained()!=player->isChained()||p->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							if(q->getEquip(((const EquipCard*)c->getRealCard())->location())) continue;
						}
						ban = false;
						break;
					}
					if(ban)
						ids << c->getId();
				}
				foreach(const Card*c,player->getCards("h")){
					bool ban = true;
					foreach(ServerPlayer*q,room->getOtherPlayers(player)){
						if(q->isChained()!=player->isChained()) continue;
						ban = false;
						break;
					}
					if(ban)
						ids << c->getId();
				}
				if(ids.length()>=player->getCardCount(true,true)||!p->askForSkillInvoke(objectName(),player)) continue;
				room->doAnimate(1,p->objectName(),player->objectName());
				int id = room->askForCardChosen(p,player,"hej",objectName(),false,Card::MethodNone,ids);
				Card*c = Sanguosha->getCard(id);
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*q,room->getOtherPlayers(player)){
					if(q->isChained()!=player->isChained()) continue;
					if(room->getCardPlace(id)!=Player::PlaceHand){
						if(p->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							if(q->getEquip(((const EquipCard*)c->getRealCard())->location())) continue;
						}
					}
					tps << q;
				}
				p->setMark("ifpiniId",id);
				ServerPlayer*tp = room->askForPlayerChosen(p,tps,objectName(),"ifpini0");
				if(tp){
					room->doAnimate(1,p->objectName(),tp->objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER,p->objectName(),objectName(),"");
					room->moveCardTo(c,tp,room->getCardPlace(id),reason,false);
				}
			}
		}
		return false;
	}
};

class IfLianque : public TriggerSkill
{
public:
	IfLianque(): TriggerSkill("iflianque$")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
    bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->getKingdom()=="wei"){
				foreach(ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->hasLordSkill(objectName())&&player->askForSkillInvoke(objectName(),"0:"+p->objectName(),false)){
						player->skillInvoked(objectName(),-1,p);
						room->damage(DamageStruct(objectName(),player,player,1,DamageStruct::Fire));
						QList<ServerPlayer*>tps;
						tps << player << p;
						room->drawCards(tps,1,objectName());
					}
				}
			}
		}
		return false;
	}
};

class IfMoran : public TriggerSkill
{
public:
	IfMoran(): TriggerSkill("ifmoran")
	{
		events << EventPhaseChanging << CardUsed << CardFinished;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->getMark("ifmoranBan-Clear")>0)
						room->removePlayerCardLimitation(p,"use",".");
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("NatureSlash")&&player->isWounded()){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(p->hasSkill(objectName())&&p->askForSkillInvoke(objectName(),data)){
						room->setCardFlag(use.card,"ifmoranBf");
						room->setCardFlag(use.card,"ifmoranBfFrom"+p->objectName());
						p->addMark("ifmoranBan-Clear");
						if(p->getMark("ifmoranBan-Clear")==1)
							room->setPlayerCardLimitation(p,"use",".",false);
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("ifmoranBf")&&use.card->hasFlag("DamageDone")){
				foreach(ServerPlayer*p,room->getAllPlayers()){
					if(use.card->hasFlag("ifmoranBfFrom"+p->objectName()))
						p->drawCards(2,objectName());
				}
			}
		}
		return false;
	}
};

IfJilveCard::IfJilveCard()
{
	will_throw = false;
}

bool IfJilveCard::targetFilter(const QList<const Player *> &targets, const Player *tp, const Player *) const
{
	return targets.isEmpty()&&tp->getPile("ifji_bing").isEmpty();
}

void IfJilveCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
	foreach(ServerPlayer*tp,targets){
		tp->addToPile("ifji_bing",subcards,false);
	}
}

class IfJilvevs : public OneCardViewAsSkill
{
public:
	IfJilvevs(): OneCardViewAsSkill("ifjilve")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*sc = new IfJilveCard;
		sc->addSubcard(originalCard);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("IfJilveCard")<2;
	}
};

class IfJilve : public TriggerSkill
{
public:
	IfJilve(): TriggerSkill("ifjilve")
	{
		events << DamageInflicted;
		view_as_skill = new IfJilvevs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->getPile("ifji_bing").length()>0;
	}
	bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data) const
	{
		room->sendCompulsoryTriggerLog(player,objectName());
		QList<int>ids = player->getPile("ifji_bing");
		room->throwCard(ids,objectName(),nullptr);
		DamageStruct damage = data.value<DamageStruct>();
		if(damage.from&&damage.card&&damage.card->getColor()!=Sanguosha->getEngineCard(ids.last())->getColor()){
			damage.to = damage.from;
			damage.transfer = true;
			damage.transfer_reason = objectName();
			data.setValue(damage);
			player->setTag("TransferDamage", data);
			return true;
		}
		return false;
	}
};

class IfBujia : public TriggerSkill
{
public:
	IfBujia(): TriggerSkill("ifbujia")
	{
		events << EventPhaseChanging << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->hasFlag("CurrentPlayer");
	}
	bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data) const
	{
		if(event==EventPhaseChanging){
			if(data.value<PhaseChangeStruct>().to==Player::NotActive){
				if(player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					if(player->getMark("ifbujiaNum-Clear")<0)
						room->loseHp(player,1,true,player,objectName());
					else
						room->recover(player,RecoverStruct(objectName(),player));
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player){
				if(move.to_place==Player::PlaceHand){
					player->addMark("ifbujiaNum-Clear",move.card_ids.length());
					if(player->hasSkill(objectName(),true))
						room->addPlayerMark(player,"&ifbujia+#num-Clear",move.card_ids.length());
				}
			}else if(move.from==player){
				if(move.from_places.contains(Player::PlaceHand)){
					int n = 0;
					foreach(Player::Place p,move.from_places){
						if(p==Player::PlaceHand) n--;
					}
					player->addMark("ifbujiaNum-Clear",n);
					if(player->hasSkill(objectName(),true))
						room->addPlayerMark(player,"&ifbujia+#num-Clear",n);
				}
			}
		}
		return false;
	}
};















DreamPackage::DreamPackage()
    : Package("qs_dream")
{
    General *if_liuxie = new General(this, "if_liuxie", "qun", 3);
    if_liuxie->addSkill(new IfAnxu);
    if_liuxie->addSkill(new IfMishou);
    if_liuxie->addSkill(new IfMishouBf);
    if_liuxie->addSkill(new IfDianbian);
	addMetaObject<IfMishouCard>();
	addMetaObject<IfDianbianCard>();
	addMetaObject<IfPiyongCard>();
	skills << new IfPiyong;

    General *if_pangtong = new General(this, "if_pangtong", "shu", 3);
    if_pangtong->addSkill(new IfXiance);
    if_pangtong->addSkill(new IfZhenshi);
	skills << new IfJiusuo;

    General *if_zhouyu = new General(this, "if_zhouyu", "wu", 4);
    if_zhouyu->addSkill(new IfYinglve);
    if_zhouyu->addSkill(new IfBihe);

    General *if_zhangjiao = new General(this, "if_zhangjiao", "qun", 3);
    if_zhangjiao->addSkill(new IfShiji);
    if_zhangjiao->addSkill(new IfAnjie);
    if_zhangjiao->addSkill(new IfLitian);
	skills << new IfLitian2 << new IfHuangchu;
	addMetaObject<IfShijiCard>();
	addMetaObject<IfAnjieCard>();

    General *if_liushan = new General(this, "if_liushan$", "shu", 3);
    if_liushan->addSkill(new IfRenli);
    if_liushan->addSkill(new IfMingduan);
    if_liushan->addSkill(new IfSixiang);
    if_liushan->addSkill(new IfJizhi);
	skills << new IfJizhivs;
	addMetaObject<IfSixiangCard>();
	addMetaObject<IfJizhiCard>();

    General *if_guanyu = new General(this, "if_guanyu", "shu", 4);
    if_guanyu->addSkill(new IfHaitian);
    if_guanyu->addSkill(new IfShenfeng);

    General *if_liubei = new General(this, "if_liubei", "shu", 4);
    if_liubei->addSkill(new IfXiechang);
    if_liubei->addSkill(new IfTianmin);
    if_liubei->addSkill(new IfJianxiao);

    General *if_lvbu = new General(this, "if_lvbu", "qun", 5);
    if_lvbu->addSkill(new IfShenwu);
    if_lvbu->addSkill(new IfBashi);

    General *if_liuhong = new General(this, "if_liuhong$", "qun", 4);
    if_liuhong->addSkill(new IfYinjue);
    if_liuhong->addSkill(new IfBaqi);
    if_liuhong->addSkill(new IfHuanghuang);
	addMetaObject<IfBaqiCard>();

    General *if_caopi = new General(this, "if_caopi", "wei", 3);
    if_caopi->addSkill(new IfTunshi);
    if_caopi->addSkill(new IfTianwei);
    if_caopi->addSkill(new IfXiongzheng);
    if_caopi->addSkill(new IfXiongzhengBf);

    General *if_caocao = new General(this, "if_caocao$", "wei", 4);
    if_caocao->addSkill(new IfEjiang);
    if_caocao->addSkill(new IfPini);
    if_caocao->addSkill(new IfLianque);
	addMetaObject<IfEjiangCard>();

    General *if_fazheng = new General(this, "if_fazheng", "shu", 4, true, false, false, 2);
    if_fazheng->addSkill(new IfMoran);
    if_fazheng->addSkill(new IfJilve);
    if_fazheng->addSkill(new IfBujia);
	addMetaObject<IfJilveCard>();

}
ADD_PACKAGE(Dream)
