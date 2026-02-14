#include "doudizhu.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "wind.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

FeiyangCard::FeiyangCard()
{
    //mute = true;
    target_fixed = true;
    //will_throw = false;
}

void FeiyangCard::onUse(Room *, CardUseStruct &) const
{
}

class FeiyangVS : public ViewAsSkill
{
public:
    FeiyangVS() : ViewAsSkill("feiyang")
    {
        response_pattern = "@@feiyang";
        expand_pile = "#feiyang";
        attached_lord_skill = true;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() > 2 || to_select->isEquipped()) return false;

        QList<int> ids = Self->getPile("#feiyang");
		if (selected.length() == 1) {
            if (ids.contains(selected.first()->getEffectiveId()))
                return !ids.contains(to_select->getEffectiveId());
        } else if (selected.length() == 2) {
            if (ids.contains(selected.first()->getEffectiveId()) || ids.contains(selected.last()->getEffectiveId()))
                return !ids.contains(to_select->getEffectiveId());
            else
                return ids.contains(to_select->getEffectiveId());
        }
		return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 3) {
            FeiyangCard *c = new FeiyangCard;
            c->addSubcards(cards);
            return c;
        }
        return nullptr;
    }
};

class Feiyang : public PhaseChangeSkill
{
public:
    Feiyang() : PhaseChangeSkill("feiyang")
    {
        view_as_skill = new FeiyangVS;
        attached_lord_skill = true;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Judge || !player->canDiscard(player, "j") || player->hasFlag(objectName())) return false;
        int n = 0;
        foreach (int id, player->handCards()) {
            if (player->canDiscard(player, id)) {
                n++;
                if (n >= 2)
                    break;
            }
        }
        if (n < 2) return false;

        QList<int> ids = player->getJudgingAreaID();
		room->notifyMoveToPile(player, ids, objectName(), Player::PlaceDelayedTrick, true);
        const Card *c = room->askForUseCard(player, "@@feiyang", "@feiyang");
        room->notifyMoveToPile(player, ids, objectName(), Player::PlaceDelayedTrick, false);
        if (!c) return false;

        room->setPlayerFlag(player, objectName());
        DummyCard *dummy = new DummyCard, *card = new DummyCard;
        foreach (int id, c->getSubcards()) {
            if (ids.contains(id))
                card->addSubcard(id);
            else
                dummy->addSubcard(id);
        }

        LogMessage log;
        log.type = "$DiscardCardWithSkill";
        log.from = player;
        log.arg = objectName();
        log.card_str = ListI2S(dummy->getSubcards()).join("+");
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), "feiyang", "");
        room->moveCardTo(dummy, player, nullptr, Player::DiscardPile, reason, true);

        if (player->isAlive() && room->getCardPlace(card->getEffectiveId()) == Player::PlaceDelayedTrick)
			room->throwCard(card, objectName(), nullptr, player);

        delete dummy;
        delete card;
        return false;
    }
};

class Feiyang2 : public PhaseChangeSkill
{
public:
    Feiyang2() : PhaseChangeSkill("feiyang")
    {
        view_as_skill = new FeiyangVS;
        attached_lord_skill = true;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Judge&&!target->hasFlag(objectName())
		&&owner->isAlive()&&target->canDiscard(target,"j")&&target->canDiscard(target,"h")&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        QList<int> ids = player->getJudgingAreaID();
		room->notifyMoveToPile(player, ids, objectName(), Player::PlaceDelayedTrick, true);
        const Card *c = room->askForUseCard(player, "@@feiyang", "@feiyang");
        room->notifyMoveToPile(player, ids, objectName(), Player::PlaceDelayedTrick, false);
        if (!c) return false;

        room->setPlayerFlag(player, objectName());
        DummyCard *dummy = new DummyCard, *card = new DummyCard;
        foreach (int id, c->getSubcards()) {
            if (ids.contains(id)) card->addSubcard(id);
            else dummy->addSubcard(id);
        }

        LogMessage log;
        log.type = "$DiscardCardWithSkill";
        log.from = player;
        log.arg = objectName();
        log.card_str = ListI2S(dummy->getSubcards()).join("+");
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), "feiyang", "");
        room->moveCardTo(dummy, player, nullptr, Player::DiscardPile, reason, true);

        if (player->isAlive() && room->getCardPlace(card->getEffectiveId()) == Player::PlaceDelayedTrick)
			room->throwCard(card, objectName(), nullptr, player);

        delete dummy;
        delete card;
        return false;
    }
};

class Bahu : public PhaseChangeSkill
{
public:
    Bahu() : PhaseChangeSkill("bahu")
    {
        frequency = Compulsory;
        attached_lord_skill = true;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Start) return false;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->drawCards(1, objectName());
        return false;
    }
};

class Bahu2 : public PhaseChangeSkill
{
public:
    Bahu2() : PhaseChangeSkill("bahu")
    {
        frequency = Compulsory;
        attached_lord_skill = true;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Start&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->drawCards(1, objectName());
        return false;
    }
};

class BahuTargetMod : public TargetModSkill
{
public:
    BahuTargetMod() : TargetModSkill("#bahu-target")
    {
        attached_lord_skill = true;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        int n = from->getMark("&nutao-PlayClear");
		if (from->hasSkill("bahu"))
            n++;
        if (from->hasWeapon("ddz_jingubang")){
			const Weapon*w = Sanguosha->findChild<const Weapon*>("ddz_jingubang");
			if(w&&w->getRange(from)==1)
				n = 999;
		}
        return n;
    }
    int getExtraTargetNum(const Player *from, const Card *) const
    {
        int n = 0;
        if (from->hasWeapon("ddz_jingubang")){
			const Weapon*w = Sanguosha->findChild<const Weapon*>("ddz_jingubang");
			if(w&&w->getRange(from)==4)
				n++;
		}
        return n;
    }
};



class Huoyan : public TriggerSkill
{
public:
    Huoyan() : TriggerSkill("huoyan")
    {
        events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(player->hasSkill(objectName())){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p!=player&&player->getMark("huoyan"+p->objectName())<1){
					player->addMark("huoyan"+p->objectName());
					room->addPlayerMark(player,"HandcardVisible_"+p->objectName());
				}
			}
		}else{
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(player->getMark("huoyan"+p->objectName())>0){
					player->removeMark("huoyan"+p->objectName());
					room->removePlayerMark(player,"HandcardVisible_"+p->objectName());
				}
			}
		}
        return false;
    }
};

class Huoyan2 : public TriggerSkill
{
public:
    Huoyan2() : TriggerSkill("huoyan")
    {
        events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardsMoveOneTime)
			return owner->isAlive()&&owner->hasSkill(objectName());
		return data.toString()==objectName()&&target==owner&&owner->isAlive();
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(player->hasSkill(objectName())){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->getMark("huoyan"+p->objectName())<1){
					player->addMark("huoyan"+p->objectName());
					room->addPlayerMark(player,"HandcardVisible_"+p->objectName());
				}
			}
		}else{
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->getMark("huoyan"+p->objectName())>0){
					player->removeMark("huoyan"+p->objectName());
					room->removePlayerMark(player,"HandcardVisible_"+p->objectName());
				}
			}
		}
        return false;
    }
};

class RuyiBf : public ViewAsEquipSkill
{
public:
    RuyiBf() : ViewAsEquipSkill("#ruyibf")
    {
    }

    QString viewAsEquip(const Player *target) const
    {
        if (target->hasEquipArea(0)&&target->hasSkill("ruyi"))
            return "ddz_jingubang";
        return "";
    }
};

class Ruyi : public FilterSkill
{
public:
    Ruyi() : FilterSkill("ruyi")
    {
		waked_skills = "ddz_jingubang,#ruyibf";
        frequency = Compulsory;
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("Weapon")
		&&Sanguosha->getCardPlace(to_select->getEffectiveId())==Player::PlaceHand;
    }

    const Card *viewAs(const Card *c) const
    {
        Card *slash = Sanguosha->cloneCard("slash",c->getSuit(),c->getNumber());
        slash->setSkillName("ruyi");/*
        WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class DdzCibei : public TriggerSkill
{
public:
    DdzCibei() : TriggerSkill("ddzcibei")
    {
        events << DamageCaused;
    }
    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
    {
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to!=player&&player->getMark(damage.to->objectName()+"cibeiUse-Clear")<1
			&&player->askForSkillInvoke(objectName()+"$-1",damage.to)){
				player->addMark(damage.to->objectName()+"cibeiUse-Clear");
				player->damageRevises(data,-damage.damage);
				player->drawCards(5,objectName());
				return true;
			}
		}
        return false;
    }
};

class DdzCibei2 : public TriggerSkill
{
public:
    DdzCibei2() : TriggerSkill("ddzcibei")
    {
        events << DamageCaused;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			return damage.to!=owner&&owner->getMark(damage.to->objectName()+"cibeiUse-Clear")<1
			&&owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
    {
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(player->askForSkillInvoke(objectName()+"$-1",damage.to)){
				player->addMark(damage.to->objectName()+"cibeiUse-Clear");
				player->damageRevises(data,-damage.damage);
				player->drawCards(5,objectName());
				return true;
			}
		}
        return false;
    }
};

JingubangCard::JingubangCard()
{
    target_fixed = true;
	m_skillName = "ddz_jingubang";
}

void JingubangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QString choice = room->askForChoice(source,"ddz_jingubang","1+2+3+4");
	room->notifyWeaponRange("ddz_jingubang",choice.toInt());
}

class JingubangVs : public ZeroCardViewAsSkill
{
public:
    JingubangVs() : ZeroCardViewAsSkill("ddz_jingubang&")
    {
    }

    const Card *viewAs() const
    {
        return new JingubangCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JingubangCard");
    }
};

class JingubangSkill : public TriggerSkill
{
public:
    JingubangSkill() : TriggerSkill("ddz_jingubang&")
    {
        events << Predamage << CardUsed;
		view_as_skill = new JingubangVs;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->hasWeapon(objectName());
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==Predamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				const Weapon*w = Sanguosha->findChild<const Weapon*>(objectName());
				if(w&&w->getRange(player)==2){
					room->sendCompulsoryTriggerLog(player,this);
					return player->damageRevises(data,1);
				}
			}
		}else if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				const Weapon*w = Sanguosha->findChild<const Weapon*>(objectName());
				if(w&&w->getRange(player)==3){
					room->sendCompulsoryTriggerLog(player,this);
					use.no_respond_list << "_ALL_TARGETS";
					data.setValue(use);
				}
			}
		}
        return false;
    }
};

class JingubangSkill2 : public TriggerSkill
{
public:
    JingubangSkill2() : TriggerSkill("ddz_jingubang&")
    {
        events << Predamage << CardUsed;
		view_as_skill = new JingubangVs;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==Predamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&owner==target&&owner->isAlive()){
				const Weapon*w = Sanguosha->findChild<const Weapon*>(objectName());
				if(w&&w->getRange(owner)==2) return owner->hasSkill(objectName());
			}
		}else if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&owner==target&&owner->isAlive()){
				const Weapon*w = Sanguosha->findChild<const Weapon*>(objectName());
				if(w&&w->getRange(owner)==3) return owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==Predamage){
			room->sendCompulsoryTriggerLog(player,this);
			return player->damageRevises(data,1);
		}else if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			use.no_respond_list << "_ALL_TARGETS";
			data.setValue(use);
		}
        return false;
    }
};

Jingubang::Jingubang(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("ddz_jingubang");
}

class Longgong : public TriggerSkill
{
public:
    Longgong() : TriggerSkill("longgong")
    {
        events << DamageInflicted;
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(player->hasTurn()){
			if(player->getMark("longgongUse-Clear")<1&&player->askForSkillInvoke(objectName()+"$-1",data)){
				player->addMark("longgongUse-Clear");
				DamageStruct damage = data.value<DamageStruct>();
				player->damageRevises(data,-damage.damage);
				if(damage.from&&damage.from->isAlive()){
					foreach (int id, room->getDrawPile()) {
						if(Sanguosha->getCard(id)->isKindOf("EquipCard")){
							room->obtainCard(damage.from,id);
							break;
						}
					}
				}
				return true;
			}
		}
        return false;
    }
};

class Longgong2 : public TriggerSkill
{
public:
    Longgong2() : TriggerSkill("longgong")
    {
        events << DamageInflicted;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
		if(event==DamageInflicted){
			if(owner==target&&owner->isAlive()&&owner->getMark("longgongUse-Clear")<1){
				return owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==DamageInflicted){
			if(player->askForSkillInvoke(objectName()+"$-1",data)){
				player->addMark("longgongUse-Clear");
				DamageStruct damage = data.value<DamageStruct>();
				player->damageRevises(data,-damage.damage);
				if(damage.from&&damage.from->isAlive()){
					foreach (int id, room->getDrawPile()) {
						if(Sanguosha->getCard(id)->isKindOf("EquipCard")){
							room->obtainCard(damage.from,id);
							break;
						}
					}
				}
				return true;
			}
		}
        return false;
    }
};

SitianCard::SitianCard()
{
    target_fixed = true;
}

void SitianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choices;
	choices << "lieri" << "leidian" << "dalang" << "baoyu" << "dawu";
	qShuffle(choices);
	QString choice = room->askForChoice(source,getSkillName(),choices.first()+"+"+choices.last());
	room->addPlayerMark(source,"&"+choice+"-PlayClear");
	if(choice=="lieri"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->doAnimate(1,source->objectName(),p->objectName());
		}
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->damage(DamageStruct(getSkillName(),source,p,1,DamageStruct::Fire));
		}
	}else if(choice=="leidian"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source))
			room->doAnimate(1,source->objectName(),p->objectName());
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			JudgeStruct judge;
			judge.who = p;
			judge.reason = "lightning";
			judge.pattern = ".|spade|2~9";
			judge.negative = true;
			judge.good = false;
			room->judge(judge);
			if(judge.isBad())
				room->damage(DamageStruct("lightning",nullptr,p,3,DamageStruct::Thunder));
		}
	}else if(choice=="dalang"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->doAnimate(1,source->objectName(),p->objectName());
		}
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			DummyCard*dc = new DummyCard;
			foreach (int e, p->getEquipsId()) {
				if(source->canDiscard(p,e))
					dc->addSubcard(e);
			}
			if(dc->subcardsLength()>0)
				room->throwCard(dc,choice,p,source);
			else if(!p->hasEquip())
				room->loseHp(p,1,true,source,choice);
			dc->deleteLater();
		}
	}else if(choice=="baoyu"){
		ServerPlayer *tp = room->askForPlayerChosen(source,room->getOtherPlayers(source),choice,"baoyu0");
		if(tp){
			room->doAnimate(1,source->objectName(),tp->objectName());
			if(tp->getHandcardNum()>0){
				room->throwCard(tp->handCards(),choice,tp,source);
			}else
				room->loseHp(tp,1,true,source,choice);
		}
	}
}

class SitianVs : public ViewAsSkill
{
public:
    SitianVs() : ViewAsSkill("sitian")
    {
    }

    bool viewFilter(const QList<const Card *> &selects, const Card *card) const
    {
        if(selects.length()==1&&selects[0]->getColor()==card->getColor())
			return false;
		return selects.length()<2&&!Self->isJilei(card);
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount()>1;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
		if(cards.length()<2) return nullptr;
		Card*dc = new SitianCard;
		dc->addSubcards(cards);
		return dc;
    }
};

class Sitian : public TriggerSkill
{
public:
    Sitian() : TriggerSkill("sitian")
    {
        events << CardUsed;
		view_as_skill = new SitianVs;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==1){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark("&dawu-PlayClear")>player->getMark("dawuUse-PlayClear")){
						player->addMark("dawuUse-PlayClear");
						room->sendCompulsoryTriggerLog(p,"dawu");
						use.nullified_list << "_ALL_TARGETS";
						data.setValue(use);
					}
				}
			}
		}
        return false;
    }
};

class SitianBf2 : public TriggerSkill
{
public:
    SitianBf2() : TriggerSkill("#sitian")
    {
        events << CardUsed;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==1&&target->isAlive()&&owner!=target){
				return owner->getMark("&dawu-PlayClear")>target->getMark("dawuUse-PlayClear")&&owner->hasSkill("sitian");
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,ServerPlayer*owner) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			player->addMark("dawuUse-PlayClear");
			room->sendCompulsoryTriggerLog(owner,"dawu");
			use.nullified_list << "_ALL_TARGETS";
			data.setValue(use);
		}
        return false;
    }
};

class Nutao : public TriggerSkill
{
public:
    Nutao() : TriggerSkill("nutao")
    {
        events << TargetSpecifying << Damage;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecifying){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")){
				use.to.removeAll(player);
				if(use.to.length()>0){
					qShuffle(use.to);
					room->sendCompulsoryTriggerLog(player,this);
					room->damage(DamageStruct(objectName(),player,use.to.first(),1,DamageStruct::Thunder));
				}
			}
		}else{
            DamageStruct damage = data.value<DamageStruct>();
			if(damage.nature==DamageStruct::Thunder&&player->getPhase()==Player::Play){
				room->addPlayerMark(player,"&nutao-PlayClear");
			}
		}
        return false;
    }
};

class Nutao2 : public TriggerSkill
{
public:
    Nutao2() : TriggerSkill("nutao")
    {
        events << TargetSpecifying;
        frequency = Compulsory;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==TargetSpecifying){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")&&target->isAlive()&&owner==target){
				use.to.removeAll(target);
				return use.to.length()>0&&owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecifying){
            CardUseStruct use = data.value<CardUseStruct>();
			use.to.removeAll(player);
			qShuffle(use.to);
			room->sendCompulsoryTriggerLog(player,this);
			room->damage(DamageStruct(objectName(),player,use.to.first(),1,DamageStruct::Thunder));
		}
        return false;
    }
};

class Nutaobf2 : public TriggerSkill
{
public:
    Nutaobf2() : TriggerSkill("#nutao")
    {
        events << Damage;
        frequency = Compulsory;
    }
    bool triggerable(ServerPlayer*player,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==Damage){
            DamageStruct damage = data.value<DamageStruct>();
			if(damage.nature==DamageStruct::Thunder&&player->getPhase()==Player::Play&&player->isAlive()&&owner==player){
				return owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==Damage){
			room->addPlayerMark(player,"&nutao-PlayClear");
		}
        return false;
    }
};

class Jiuxian : public OneCardViewAsSkill
{
public:
    Jiuxian() : OneCardViewAsSkill("jiuxian")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        if(to_select->isNDTrick()&&!to_select->isSingleTargetCard()){
			Card*dc = Sanguosha->cloneCard("analeptic");
			dc->addSubcard(to_select);
			dc->setSkillName(objectName());
			dc->deleteLater();
			return dc->isAvailable(Self);
		}
		return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card*dc = Sanguosha->cloneCard("analeptic");
		dc->addSubcard(originalCard);
		dc->setSkillName(objectName());
        return dc;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount()>0;
    }
};

class JiuxianMod : public TargetModSkill
{
public:
    JiuxianMod() : TargetModSkill("#jiuxian_mod")
    {
		pattern = "Analeptic";
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        int n = 0;
		if (from->hasSkill("jiuxian"))
            n = 999;
        return n;
    }
};

class Shixian : public TriggerSkill
{
public:
    Shixian() : TriggerSkill("shixian")
    {
        events << CardUsed << CardFinished;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("shixianBf")){
				use.card->use(room,use.from,use.to);
			}
		}else{
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1) return false;
			static QHash<QString, QStringList> yun;
			if(yun.isEmpty()){
				yun["a ia ua"] << "杀" << "发" << "甲" << "下" << "法";
				yun["o e uo"] << "槊" << "渴" << "车";
				yun["ie ve"] << "劫";
				yun["ai uai"] << "铠" << "海";
				yun["ei ui"] << "梅" << "倍" << "雷" << "锐";
				yun["ao iao"] << "桃" << "刀" << "矛" << "桥" << "壳" << "灶" << "劳";
				yun["ou iu"] << "有" << "斗" << "骝" << "酒" << "走";
				yun["an ian uan van"] << "闪" << "电" << "剑" << "宛" << "断" << "扇" << "环" << "链" << "远" << "砖";
				yun["en in un vn"] << "人" << "侵" << "阵" << "盾" << "军" << "尘" << "金";
				yun["ang iang uang"] << "羊" << "僵" << "枪" << "粮";
				yun["eng ing ong ung"] << "登" << "弓" << "影" << "骍" << "攻" << "镜" << "生" << "兵" << "纵";
				yun["i er v"] << "义" << "击" << "戟" << "子" << "意" << "机" << "计" << "利" << "西" << "移" << "彼";
				yun["u"] << "蜀" << "弩" << "斧" << "兔" << "卢" << "图" << "符" << "毒" << "柱" << "腹" << "武" << "五" << "术" << "梳" << "葫" << "鹄" << "入";
			}
			QString tye = Sanguosha->translate(use.card->objectName());
			foreach (QString k, yun.keys()) {
				foreach (QString v, yun[k]) {
					if(tye.endsWith(v)){
						tye = player->tag["shixian_yun"].toString();
						if(tye==k&&player->getMark("&shixian+:+"+tye+"-Clear")>0
						&&player->askForSkillInvoke(objectName()+"$-1",data)){
							player->drawCards(1,objectName());
							use.card->setFlags("shixianBf");
						}
						room->setPlayerMark(player,"&shixian+:+"+tye+"-Clear",0);
						player->tag["shixian_yun"] = k;
						room->setPlayerMark(player,"&shixian+:+"+k+"-Clear",1);
						return false;
					}
				}
			}
		}
        return false;
    }
};

static QHash<QString, QStringList> shixian_yun;

class Shixian2 : public TriggerSkill
{
public:
    Shixian2() : TriggerSkill("shixian")
    {
        events << CardUsed;
    }
    bool triggerable(ServerPlayer*player,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->isAlive()&&owner==player){
				if(shixian_yun.isEmpty()){
					shixian_yun["a ia ua"] << "杀" << "发" << "甲" << "下" << "法";
					shixian_yun["o e uo"] << "槊" << "渴" << "车";
					shixian_yun["ie ve"] << "劫";
					shixian_yun["ai uai"] << "铠" << "海";
					shixian_yun["ei ui"] << "梅" << "倍" << "雷" << "锐";
					shixian_yun["ao iao"] << "桃" << "刀" << "矛" << "桥" << "壳" << "灶" << "劳";
					shixian_yun["ou iu"] << "有" << "斗" << "骝" << "酒" << "走";
					shixian_yun["an ian uan van"] << "闪" << "电" << "剑" << "宛" << "断" << "扇" << "环" << "链" << "远" << "砖";
					shixian_yun["en in un vn"] << "人" << "侵" << "阵" << "盾" << "军" << "尘" << "金";
					shixian_yun["ang iang uang"] << "羊" << "僵" << "枪" << "粮";
					shixian_yun["eng ing ong ung"] << "登" << "弓" << "影" << "骍" << "攻" << "镜" << "生" << "兵" << "纵";
					shixian_yun["i er v"] << "义" << "击" << "戟" << "子" << "意" << "机" << "计" << "利" << "西" << "移" << "彼";
					shixian_yun["u"] << "蜀" << "弩" << "斧" << "兔" << "卢" << "图" << "符" << "毒" << "柱" << "腹" << "武" << "五" << "术" << "梳" << "葫" << "鹄" << "入";
				}
				QString tye = Sanguosha->translate(use.card->objectName());
				foreach (QString k, shixian_yun.keys()) {
					foreach (QString v, shixian_yun[k]) {
						if(tye.endsWith(v)){
							tye = player->tag["shixian_yun"].toString();
							if(tye==k&&player->getMark("&shixian+:+"+tye+"-Clear")>0){
								return owner->hasSkill(objectName());
							}
						}
					}
				}
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(player->askForSkillInvoke(objectName()+"$-1",data)){
				player->drawCards(1,objectName());
				use.card->setFlags("shixianBf");
			}
		}
        return false;
    }
};

class Shixianbf2 : public TriggerSkill
{
public:
    Shixianbf2() : TriggerSkill("#shixian")
    {
        events << CardUsed << CardFinished;
    }
    int getPriority(TriggerEvent event) const
    {
        if (event==CardUsed) return 0;
        return TriggerSkill::getPriority(event);
    }
    bool triggerable(ServerPlayer*player,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->isAlive()&&owner==player){
				return owner->hasSkill("shixian",true);
			}
		}else if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("shixianBf")&&player->isAlive()){
				return owner==player;
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			use.card->use(room,use.from,use.to);
		}else{
            CardUseStruct use = data.value<CardUseStruct>();
			QString tye = Sanguosha->translate(use.card->objectName());
			foreach (QString k, shixian_yun.keys()) {
				foreach (QString v, shixian_yun[k]) {
					if(tye.endsWith(v)){
						tye = player->tag["shixian_yun"].toString();
						room->setPlayerMark(player,"&shixian+:+"+tye+"-Clear",0);
						player->tag["shixian_yun"] = k;
						room->setPlayerMark(player,"&shixian+:+"+k+"-Clear",1);
						return false;
					}
				}
			}
		}
        return false;
    }
};

class Santou : public TriggerSkill
{
public:
    Santou() : TriggerSkill("santou")
    {
        events << DamageInflicted;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			player->damageRevises(data,-damage.damage);
			if(damage.from){
				player->addMark(damage.from->objectName()+"santouUse-Clear");
				if(player->getMark(damage.from->objectName()+"santouUse-Clear")>1){
					if(player->getHp()>=3)
						room->loseHp(player,1,true,player,objectName());
					else if(player->getHp()>=2&&damage.nature!=DamageStruct::Normal)
						room->loseHp(player,1,true,player,objectName());
					else if(player->getHp()>=1&&damage.card&&damage.card->isRed())
						room->loseHp(player,1,true,player,objectName());
				}
			}
			return true;
		}
        return false;
    }
};

class FaqiVs : public ZeroCardViewAsSkill
{
public:
    FaqiVs() : ZeroCardViewAsSkill("faqi")
    {
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
		return pattern.contains("@@faqi");
    }

    const Card *viewAs() const
    {
		const Card *h = Sanguosha->getEngineCard(Self->getMark("faqiUse"));
		Card*dc = Sanguosha->cloneCard(h->objectName());
		dc->setSkillName(objectName());
		return dc;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }
};

class Faqi : public TriggerSkill
{
public:
    Faqi() : TriggerSkill("faqi")
    {
        events << CardFinished;
		view_as_skill = new FaqiVs;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("EquipCard")&&player->getPhase()==Player::Play){
				QList<int>ids,ids2 = Sanguosha->getRandomCards();
				QStringList names;
				for (int i = 0; i < Sanguosha->getCardCount(); i++) {
					if(ids2.contains(i)){
						const Card *h = Sanguosha->getEngineCard(i);
						if(h->isNDTrick()&&!names.contains(h->objectName())&&player->getMark(h->objectName()+"faqiUse-Clear")<1){
							Card *dc = Sanguosha->cloneCard(h->objectName());
							dc->setSkillName(objectName());
							dc->deleteLater();
							names << h->objectName();
							if(dc->isAvailable(player))
								ids << i;
						}
					}
				}
				if(ids.isEmpty()) return false;
				room->fillAG(ids,player);
				int id = room->askForAG(player,ids,ids.length()<2,objectName(),"faqi0");
				room->clearAG(player);
				if(id<0) return false;
				room->setPlayerMark(player,"faqiUse",id);
				const Card *h = Sanguosha->getEngineCard(id);
				if(room->askForUseCard(player,"@@faqi","faqi1:"+h->objectName()))
					player->addMark(h->objectName()+"faqiUse-Clear");
			}
		}
        return false;
    }
};

class Faqi2 : public TriggerSkill
{
public:
    Faqi2() : TriggerSkill("faqi")
    {
        events << CardFinished;
		view_as_skill = new FaqiVs;
    }
    bool triggerable(ServerPlayer*player,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("EquipCard")&&player->getPhase()==Player::Play&&player->isAlive()){
				return owner==player&&owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==CardFinished){
			QStringList names;
			QList<int>ids,ids2 = Sanguosha->getRandomCards();
			for (int i = 0; i < Sanguosha->getCardCount(); i++) {
				if(ids2.contains(i)){
					const Card *h = Sanguosha->getEngineCard(i);
					if(h->isNDTrick()&&!names.contains(h->objectName())&&player->getMark(h->objectName()+"faqiUse-Clear")<1){
						Card *dc = Sanguosha->cloneCard(h->objectName());
						dc->setSkillName(objectName());
						dc->deleteLater();
						names << h->objectName();
						if(dc->isAvailable(player))
							ids << i;
					}
				}
			}
			if(ids.isEmpty()) return false;
			room->fillAG(ids,player);
			int id = room->askForAG(player,ids,ids.length()<2,objectName(),"faqi0");
			room->clearAG(player);
			if(id<0) return false;
			room->setPlayerMark(player,"faqiUse",id);
			const Card *h = Sanguosha->getEngineCard(id);
			if(room->askForUseCard(player,"@@faqi","faqi1:"+h->objectName()))
				player->addMark(h->objectName()+"faqiUse-Clear");
		}
        return false;
    }
};

class Zhanjian : public TriggerSkill
{
public:
    Zhanjian() : TriggerSkill("zhanjian")
    {
        events << EventPhaseStart;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			foreach (ServerPlayer *tp, room->getAllPlayers()) {
				if(tp->getWeapon()&&tp->getWeapon()->objectName()=="qinggang_sword"
					&&player->askForSkillInvoke(objectName()+"$-1",tp)){
					player->obtainCard(tp->getWeapon());
				}
			}
		}
        return false;
    }
};

class Zhanjian2 : public TriggerSkill
{
public:
    Zhanjian2() : TriggerSkill("zhanjian")
    {
        events << EventPhaseStart;
    }
    bool triggerable(ServerPlayer*player,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->isAlive()){
				foreach (ServerPlayer *tp, room->getAlivePlayers()) {
					if(tp->getWeapon()&&tp->getWeapon()->objectName()=="qinggang_sword")
						return owner==player&&owner->hasSkill(objectName());
				}
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==EventPhaseStart){
			foreach (ServerPlayer *tp, room->getAllPlayers()) {
				if(tp->getWeapon()&&tp->getWeapon()->objectName()=="qinggang_sword"
					&&player->askForSkillInvoke(objectName()+"$-1",tp)){
					player->obtainCard(tp->getWeapon());
					break;
				}
			}
		}
        return false;
    }
};

class DdzBenxi : public TriggerSkill
{
public:
    DdzBenxi() : TriggerSkill("ddzbenxi")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
		change_skill = true;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&move.from==player){
				int n = player->getChangeSkillState(objectName());
				room->sendCompulsoryTriggerLog(player,this);
				if(n==1){
					static QHash<QString, int> skills;
					if(skills.isEmpty()){
						skills["qizhi"] = 2;
						skills["daiyan"] = 2;
						skills["ny_10th_quanmou"] = 1;
						skills["xiaowu"] = 1;
						skills["tenyearyonglve"] = 2;
						skills["ny_10th_fangdu"] = 2;
						skills["ny_tenth_xiuwen"] = 2;
						skills["spyoudi"] = 1;
						skills["ny_10th_qingbei"] = 2;
						skills["yisuan"] = 1;
						skills["duorui"] = 2;
						skills["qingtan"] = 1;
						skills["weiwu"] = 2;
						skills["fujian"] = 2;
						skills["xiantu"] = 2;
						skills["ny_10th_sijun"] = 2;
						skills["zongfan"] = 2;
					}
					QStringList sks = skills.keys();
					qShuffle(sks);
					foreach (QString sk, sks) {
						if(Sanguosha->getSkill(sk)){
							room->setChangeSkillState(player, objectName(), 2);
							room->broadcastSkillInvoke(sk,skills[sk],player);
							player->tag["ddz_benxiSkill"] = sk;
							break;
						}
					}
				}else{
					room->setChangeSkillState(player, objectName(), 1);
					QString sk = player->tag["ddz_benxiSkill"].toString();
					if(player->hasSkill(sk,true)){
						ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"ddzbenxi0:");
						if (tp) {
							room->doAnimate(1,player->objectName(),tp->objectName());
							room->damage(DamageStruct(objectName(),player,tp));
						}
					}else{
						room->acquireNextTurnSkills(player,objectName(),sk);
					}
				}
			}
        }
        return false;
    }
};

class DdzBenxi2 : public TriggerSkill
{
public:
    DdzBenxi2() : TriggerSkill("ddzbenxi")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
		change_skill = true;
    }
    bool triggerable(ServerPlayer*,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&move.from==owner){
				return owner->isAlive()&&owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &,ServerPlayer*owner) const
    {
		if(event==CardsMoveOneTime){
			int n = owner->getChangeSkillState(objectName());
			room->sendCompulsoryTriggerLog(owner,this);
			if(n==1){
				static QHash<QString, int> skills;
				if(skills.isEmpty()){
					skills["qizhi"] = 2;
					skills["daiyan"] = 2;
					skills["ny_10th_quanmou"] = 1;
					skills["xiaowu"] = 1;
					skills["tenyearyonglve"] = 2;
					skills["ny_10th_fangdu"] = 2;
					skills["ny_tenth_xiuwen"] = 2;
					skills["spyoudi"] = 1;
					skills["ny_10th_qingbei"] = 2;
					skills["yisuan"] = 1;
					skills["duorui"] = 2;
					skills["qingtan"] = 1;
					skills["weiwu"] = 2;
					skills["fujian"] = 2;
					skills["xiantu"] = 2;
					skills["ny_10th_sijun"] = 2;
					skills["zongfan"] = 2;
				}
				QStringList sks = skills.keys();
				qShuffle(sks);
				foreach (QString sk, sks) {
					if(Sanguosha->getSkill(sk)){
						room->setChangeSkillState(owner, objectName(), 2);
						room->broadcastSkillInvoke(sk,skills[sk],owner);
						owner->tag["ddz_benxiSkill"] = sk;
						break;
					}
				}
			}else{
				room->setChangeSkillState(owner, objectName(), 1);
				QString sk = owner->tag["ddz_benxiSkill"].toString();
				if(owner->hasSkill(sk,true)){
					ServerPlayer *tp = room->askForPlayerChosen(owner,room->getAlivePlayers(),objectName(),"ddzbenxi0:");
					if (tp) {
						room->doAnimate(1,owner->objectName(),tp->objectName());
						room->damage(DamageStruct(objectName(),owner,tp));
					}
				}else{
					room->acquireNextTurnSkills(owner,objectName(),sk);
				}
			}
        }
        return false;
    }
};

class Qiusuo : public TriggerSkill
{
public:
    Qiusuo() : TriggerSkill("qiusuo")
    {
        events << Damage << Damaged;
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		foreach (int id, Sanguosha->getRandomCards()) {
			if(!room->getCardOwner(id)&&Sanguosha->getCard(id)->isKindOf("IronChain")){
				room->sendCompulsoryTriggerLog(player,this);
				room->obtainCard(player,id);
				break;
			}
		}
        return false;
    }
};

LisaoCard::LisaoCard()
{
	will_throw = false;
}

bool LisaoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length()<2;
}

void LisaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = qrand()%69+1;
	QString question = QString("lisaoQuestion%1").arg(n);
	QString optionA = QString("lisaoOptionA%1").arg(n);
	QString optionB = QString("lisaoOptionB%1").arg(n);
	QList<ServerPlayer *> tps;
	foreach (ServerPlayer *p, targets) {
		QElapsedTimer timer;
		timer.start();
		QString choice = room->askForChoice(p,"lisao",optionA+"+"+optionB,QVariant::fromValue(source),question);
		p->setMark("lisaoTimer",timer.elapsed());
		if(n>35){
			if(choice==optionB)
				tps << p;
		}else{
			if(choice==optionA)
				tps << p;
		}
	}
	if(tps.length()>1){
		int t1 = tps.first()->getMark("lisaoTimer"), t2 = tps.last()->getMark("lisaoTimer");
		if(t1>t2){
			tps.removeOne(tps.first());
		}else if(t2>t1){
			tps.removeOne(tps.last());
		}
	}
	foreach (ServerPlayer *p, tps) {
		room->showAllCards(p);
	}
	foreach (ServerPlayer *p, targets) {
		if(tps.contains(p)) continue;
		room->setPlayerMark(p,"&lisao+#"+source->objectName()+"-Clear",1);
	}
}

class LisaoVs : public ZeroCardViewAsSkill
{
public:
    LisaoVs() : ZeroCardViewAsSkill("lisao")
    {
    }

    const Card *viewAs() const
    {
        return new LisaoCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("LisaoCard");
    }
};

class Lisao : public TriggerSkill
{
public:
    Lisao() : TriggerSkill("lisao")
    {
        events << CardUsed << DamageInflicted;
		view_as_skill = new LisaoVs;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("&lisao+#"+player->objectName()+"-Clear")>0)
						use.no_respond_list << p->objectName();
				}
				if(use.no_respond_list.length()>0){
					room->sendCompulsoryTriggerLog(player,this);
					data.setValue(use);
				}
			}
		}else if(event==DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if(player->getMark("&lisao+#"+p->objectName()+"-Clear")>0){
					room->sendCompulsoryTriggerLog(p,this);
					player->damageRevises(data,damage.damage);
				}
			}
		}
        return false;
    }
};

class Lisao2 : public TriggerSkill
{
public:
    Lisao2() : TriggerSkill("lisao")
    {
        events << CardUsed << DamageInflicted;
		view_as_skill = new LisaoVs;
    }
    bool triggerable(ServerPlayer*player,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player==owner&&owner->isAlive()){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("&lisao+#"+player->objectName()+"-Clear")>0)
						return owner->hasSkill(objectName());
				}
			}
		}else if(event==DamageInflicted){
			if(owner->isAlive()){
				if(player->getMark("&lisao+#"+owner->objectName()+"-Clear")>0)
					return owner->hasSkill(objectName());
			}
		}
		return false;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,ServerPlayer*owner) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getMark("&lisao+#"+player->objectName()+"-Clear")>0)
					use.no_respond_list << p->objectName();
			}
			if(use.no_respond_list.length()>0){
				room->sendCompulsoryTriggerLog(player,this);
				data.setValue(use);
			}
		}else if(event==DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
			room->sendCompulsoryTriggerLog(owner,this);
			player->damageRevises(data,damage.damage);
		}
        return false;
    }
};

class Chushan : public TriggerSkill
{
public:
    Chushan() : TriggerSkill("chushan")
    {
        events << GameStart;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			QSet<QString> existed;
			existed << "ddz_wuming";
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				existed << p->getGeneralName();
				existed << p->getGeneral2Name();
			}
			QStringList gs = Sanguosha->getRandomGenerals(3,existed);
			QString gn = room->askForGeneral(player,gs);
			gs.clear();
			foreach (const Skill*s, Sanguosha->getGeneral(gn)->getVisibleSkillList()) {
				gs << s->objectName();
			}
			existed << gn;
			gn = room->askForChoice(player,objectName(),gs.join("+"));
			room->acquireSkill(player,gn);
			gs = Sanguosha->getRandomGenerals(3,existed);
			gn = room->askForGeneral(player,gs);
			gs.clear();
			foreach (const Skill*s, Sanguosha->getGeneral(gn)->getVisibleSkillList()) {
				gs << s->objectName();
			}
			gn = room->askForChoice(player,objectName(),gs.join("+"));
			room->acquireSkill(player,gn);
		}
        return false;
    }
};

class Juanlv : public TriggerSkill
{
public:
    Juanlv() : TriggerSkill("juanlv")
    {
        events << TargetSpecified;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *tp, use.to) {
					if(tp->getGender()!=player->getGender()&&player->askForSkillInvoke(objectName()+"$-1",tp)){
						if(!tp->canDiscard(tp,"h")||!room->askForDiscard(tp,objectName(),1,1,true,false,"juanlv0:"+player->objectName()))
							player->drawCards(1,objectName());
					}
				}
			}
		}
        return false;
    }
};

QixinCard::QixinCard()
{
    target_fixed = true;
}

void QixinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = source->getChangeSkillState(getSkillName());
	if(n==1){
		room->setChangeSkillState(source, getSkillName(), 2);
		const General*g = Sanguosha->getGeneral("caojie");
		source->setGender(g->getGender());
		n = source->tag["qixinCaojieHp"].toInt();
		source->tag["qixinLiuxieHp"] = source->getHp();
		if(n>0) room->setPlayerProperty(source,"hp",n);
		else room->setPlayerProperty(source,"hp",g->getStartHp());
		source->setAvatarIcon("caojie",source->getGeneral2()&&source->getGeneral2()->hasSkill(getSkillName()));
	}else{
		room->setChangeSkillState(source, getSkillName(), 1);
		const General*g = Sanguosha->getGeneral("liuxie");
		source->setGender(g->getGender());
		n = source->tag["qixinLiuxieHp"].toInt();
		source->tag["qixinCaojieHp"] = source->getHp();
		if(n>0) room->setPlayerProperty(source,"hp",n);
		else room->setPlayerProperty(source,"hp",g->getStartHp());
		source->setAvatarIcon("liuxie",source->getGeneral2()&&source->getGeneral2()->hasSkill(getSkillName()));
	}
}

class Qixin : public ZeroCardViewAsSkill
{
public:
    Qixin() : ZeroCardViewAsSkill("qixin")
    {
		change_skill = true;
    }

    const Card *viewAs() const
    {
        return new QixinCard;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }
};

class Zhinang : public TriggerSkill
{
public:
    Zhinang() : TriggerSkill("zhinang")
    {
        events << CardFinished;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")){
				foreach (QString sk, Sanguosha->getSkillNames()) {
					if(player->hasSkill(sk,true)) continue;
					foreach (QString s, Sanguosha->getSkill(sk)->getSources()) {
						QString str = s.split("/").last().split(".").first();
						if(Sanguosha->translate("$"+str).contains("谋")
							&&player->askForSkillInvoke(objectName()+"$-1",data)){
							str = player->tag["zhinangSkill1"].toString();
							QStringList sks;
							sks << sk;
							if(!str.isEmpty()) sks << "-"+str;
							player->tag["zhinangSkill1"] = sk;
							room->handleAcquireDetachSkills(player,sks);
							return false;
						}
					}
				}
			}else if(use.card->isKindOf("EquipCard")){
				foreach (QString sk, Sanguosha->getSkillNames()) {
					if(player->hasSkill(sk,true)) continue;
					if(Sanguosha->translate(sk).contains("谋")
						&&player->askForSkillInvoke(objectName()+"$-1",data)){
						QString str = player->tag["zhinangSkill2"].toString();
						QStringList sks;
						sks << sk;
						if(!str.isEmpty()) sks << "-"+str;
						player->tag["zhinangSkill2"] = sk;
						room->handleAcquireDetachSkills(player,sks);
						break;
					}
				}
			}
		}
        return false;
    }
};

class Gouzhu : public TriggerSkill
{
public:
    Gouzhu() : TriggerSkill("gouzhu")
    {
        events << EventLoseSkill;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==EventLoseSkill){
			const Skill*sk = Sanguosha->getSkill(data.toString());
			Frequency f = sk->getFrequency(player);
			if(f==Compulsory){
				room->sendCompulsoryTriggerLog(player,this);
				room->recover(player,RecoverStruct(objectName(),player));
			}
			if(f==Wake){
				foreach (int id, Sanguosha->getRandomCards()) {
					if(!room->getCardOwner(id)&&Sanguosha->getCard(id)->isKindOf("BasicCard")){
						room->sendCompulsoryTriggerLog(player,this);
						room->obtainCard(player,id);
						break;
					}
				}
			}
			if(f==Limited){
				room->sendCompulsoryTriggerLog(player,this);
				QList<ServerPlayer *>tps = room->getOtherPlayers(player);
				qShuffle(tps);
				room->doAnimate(1,player->objectName(),tps.first()->objectName());
				room->damage(DamageStruct(objectName(),player,tps.first()));
			}
			if(sk->isChangeSkill()){
				room->sendCompulsoryTriggerLog(player,this);
				room->addMaxCards(player,1,false);
			}
			if(sk->isLordSkill()){
				room->sendCompulsoryTriggerLog(player,this);
				room->gainMaxHp(player,1,objectName());
			}
		}
        return false;
    }
};

class Huyi : public TriggerSkill
{
public:
    Huyi() : TriggerSkill("huyi")
    {
        events << CardFinished << CardResponded << GameStart << EventPhaseChanging;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		static QStringList skills;
		if(skills.isEmpty()){
			foreach (QString gn, Sanguosha->getLimitedGeneralNames()) {
				if(gn.endsWith("guanyu")||gn.endsWith("zhangfei")||gn.endsWith("zhaoyun")||gn.endsWith("huangzhong")||gn.endsWith("machao")){
					foreach (const Skill*sk, Sanguosha->getGeneral(gn)->getVisibleSkillList()) {
						skills << sk->objectName();
					}
				}
			}
		}
		if(event==GameStart){
            room->sendCompulsoryTriggerLog(player,this);
			qShuffle(skills);
			QStringList sks = skills.mid(0, 3);
			QString sk = room->askForChoice(player,objectName(),sks.join("+"));
			sks = player->tag["huyiSkills"].toStringList();
			sks << sk;
			player->tag["huyiSkills"] = sks;
			room->acquireSkill(player,sk);
		}else if(event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				QStringList sks = player->tag["huyiSkills"].toStringList();
				if(sks.length()>0&&player->askForSkillInvoke(this,"huyi0")){
					QString sk = room->askForChoice(player,objectName(),sks.join("+"));
					sks.removeOne(sk);
					player->tag["huyiSkills"] = sks;
					room->detachSkillFromPlayer(player,sk);
				}
			}
		}else{
			const Card *card = nullptr;
			if (event == CardFinished)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if(card&&card->isKindOf("BasicCard")){
				QStringList sks = player->tag["huyiSkills"].toStringList();
				if(sks.length()<5){
					qShuffle(skills);
					QString cn = "【"+Sanguosha->translate(card->objectName())+"】";
					if(card->isKindOf("Slash")) cn = "【杀】";
					foreach (QString sk, skills) {
						if(sks.contains(sk)) continue;
						if(Sanguosha->translate(":"+sk).contains(cn)){
							room->sendCompulsoryTriggerLog(player,this);
							sks << sk;
							player->tag["huyiSkills"] = sks;
							room->acquireSkill(player,sk);
							break;
						}
					}
				}
			}
		}
        return false;
    }
};

class Fengzhu : public TriggerSkill
{
public:
    Fengzhu() : TriggerSkill("fengzhu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			QList<ServerPlayer *> tos;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->isMale()) tos << p;
			}
			ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"fengzhu0",false,true);
			if(to){
				player->peiyin(this);
				room->setPlayerMark(to,"&ddz_yifu",1);
				player->drawCards(to->getHp(),objectName());
			}
		}
        return false;
    }
};

class Yuyu : public TriggerSkill
{
public:
    Yuyu() : TriggerSkill("yuyu")
    {
        events << Damaged << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark("&ddz_yifu")) tos << p;
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"yuyu0",false,true);
				if(to){
					player->peiyin(this);
					to->gainMark("&ddz_hen");
				}
			}
		}else if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if((move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
				&&move.from==player){
				int n = 0;
				for (int i = 0; i < move.from_places.length(); i++) {
					if(move.from_places.at(i)==Player::PlaceHand||move.from_places.at(i)==Player::PlaceEquip)
						n++;
				}
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark("&ddz_hen")>0&&p->getMark("&ddz_yifu")>0&&p->hasFlag("CurrentPlayer")){
						room->sendCompulsoryTriggerLog(player,this);
						p->gainMark("&ddz_hen",n);
					}
				}
			}
		}else {
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getMark("&ddz_hen")>0&&p->getMark("&ddz_yifu")>0&&p->hasFlag("CurrentPlayer")){
					room->sendCompulsoryTriggerLog(player,this);
					p->gainMark("&ddz_hen",damage.damage);
				}
			}
		}
        return false;
    }
};

class DdzZhiji : public TriggerSkill
{
public:
    DdzZhiji() : TriggerSkill("ddzzhiji")
    {
        events << TargetSpecifying << ConfirmDamage;
		waked_skills = "shenji,wushuang";
        frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target!=nullptr;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecifying){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1||!player->isAlive()||!player->hasSkill(objectName())) return false;
			if(use.card->isDamageCard()){
				bool has = true;
				foreach (ServerPlayer *p, use.to) {
					if(p->getMark("&ddz_yifu")>0&&p->getMark("&ddz_hen")>0){
						if(has){
							room->sendCompulsoryTriggerLog(player,this);
							has = false;
						}
						use.card->setFlags("ddzzhijiDamage_"+QString::number(p->getMark("&ddz_hen")));
						p->loseAllMarks("&ddz_hen");
					}
				}
			}else{
				bool has = true;
				foreach (ServerPlayer *p, use.to) {
					if(p->getMark("&ddz_yifu")>0){
						for (int i = 0; i < p->getMark("&ddz_hen"); i++) {
							if(has){
								room->sendCompulsoryTriggerLog(player,this);
								has = false;
							}
							JudgeStruct judge;
							judge.who = player;
							judge.reason = objectName();
							judge.pattern = "Slash,Duel,EquipCard";
							room->judge(judge);
							if(judge.card->isKindOf("EquipCard")){
								room->acquireSkill(player,"shenji");
							}
							if(judge.card->isKindOf("Slash")||judge.card->isKindOf("Duel")){
								room->acquireSkill(player,"wushuang");
								if(!room->getCardOwner(judge.card->getEffectiveId()))
									player->obtainCard(judge.card);
							}
							if(!player->isAlive())
								return false;
						}
					}
				}
			}	
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card){
				foreach (QString f, damage.card->getFlags()) {
					if(f.contains("ddzzhijiDamage_")){
						player->damageRevises(data,f.split("_")[1].toInt());
					}
				}
			}
		}
        return false;
    }
};

class Jiejiuvs : public OneCardViewAsSkill
{
public:
    Jiejiuvs() : OneCardViewAsSkill("jiejiu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
		return to_select->isKindOf("Analeptic");
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		foreach (QString pn, pattern.split("+")) {
			Card *c = Sanguosha->cloneCard(pn);
			if(c){
				c->deleteLater();
				if(c->isKindOf("BasicCard")&&!c->isKindOf("Analeptic")){
					return player->getCardCount()>0;
				}
			}
		}
        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *dc = Self->tag.value(objectName()).value<const Card *>();
			if(!dc) return nullptr;
			pattern = dc->objectName();
		}
		foreach (QString pn, pattern.split("+")) {
			Card*dc = Sanguosha->cloneCard(pn);
			dc->addSubcard(originalCard);
			dc->setSkillName(objectName());
			if(dc->isKindOf("Analeptic")||Self->isLocked(dc)){
				dc->deleteLater();
				return nullptr;
			}
			return dc;
		}
        return nullptr;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount()>0;
    }
};

class Jiejiu : public TriggerSkill
{
public:
    Jiejiu() : TriggerSkill("jiejiu")
    {
        events << GameStart;
		view_as_skill = new Jiejiuvs;
        frequency = Compulsory;
    }
    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance(objectName(), true, false);
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
		if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->isFemale())
					room->doAnimate(1,player->objectName(),p->objectName());
			}
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->isFemale()){
					QStringList gs,ds;
					foreach (const Skill*s, p->getVisibleSkillList()) {
						if(p->hasInnateSkill(s))
							gs << "-"+s->objectName();
					}
					if(!gs.isEmpty()){
						qShuffle(gs);
						ds << gs.first();
						ds << "lijian";
						room->handleAcquireDetachSkills(p,ds);
					}
				}
			}
		}
        return false;
    }
};

class JiejiuLimit : public CardLimitSkill
{
public:
    JiejiuLimit() : CardLimitSkill("#jiejiu-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->hasSkill("jiejiu"))
            return "Analeptic";
        return "";
    }
};

class Dingxi : public TriggerSkill
{
public:
    Dingxi() : TriggerSkill("dingxi")
    {
        events << CardUsed << CardsMoveOneTime;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("dingxiUse")){
				player->addToPile("dingxi",use.card);
			}
		}else if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&move.from==player
                &&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE){
				QList<int>ids;
				ServerPlayer *tp = player->getNextAlive(player->getAliveSiblings().length());
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->isDamageCard()&&!room->getCardOwner(id)&&player->canUse(c,tp)) ids << id;
				}
				if(ids.length()>0){
					room->fillAG(ids,player);
					if(player->askForSkillInvoke(objectName()+"$-1",tp)){
						int id = room->askForAG(player,ids,false,objectName());
						room->clearAG(player);
						const Card*c = Sanguosha->getCard(id);
						c->setFlags("dingxiUse");
						room->useCard(CardUseStruct(c,player,tp));
						return false;
					}
					room->clearAG(player);
				}
			}
		}
        return false;
    }
};

class Nengchen : public TriggerSkill
{
public:
    Nengchen() : TriggerSkill("nengchen")
    {
        events << Damaged;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card){
				QList<int>ids = player->getPile("dingxi");
				qShuffle(ids);
				foreach (int id, ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->sameNameWith(damage.card)){
						room->sendCompulsoryTriggerLog(player,this);
						player->obtainCard(c);
						break;
					}
				}
			}
		}
        return false;
    }
};

class Huojie : public TriggerSkill
{
public:
    Huojie() : TriggerSkill("huojie")
    {
        events << EventPhaseStart << Damaged;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.tips.contains("huojie_lightning")){
				Card*dc = new DummyCard(player->getPile("dingxi"));
				player->obtainCard(dc);
				dc->deleteLater();
			}
		}else if(player->getPhase()==Player::Play){
			QList<int>ids = player->getPile("dingxi");
			for (int i = 0; i < ids.length(); i++) {
				if(i==0) room->sendCompulsoryTriggerLog(player,this);
				JudgeStruct judge;
				judge.who = player;
				judge.reason = "lightning";
				judge.pattern = ".|spade|2~9";
				judge.negative = true;
				judge.good = false;
				room->judge(judge);
				if(judge.isBad()){
					DamageStruct damage = DamageStruct("lightning",nullptr,player,3,DamageStruct::Thunder);
					damage.tips << "huojie_lightning";
					room->damage(damage);
				}
				if(!player->isAlive()) break;
			}
		}
        return false;
    }
};

class Huiwan : public TriggerSkill
{
public:
    Huiwan() : TriggerSkill("huiwan")
    {
        events << DrawNCards;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==DrawNCards){
            DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason=="InitialHandCards") return false;
			QList<int>ids,ids2 = room->getDrawPile();
			qShuffle(ids2);
			QStringList names;
			foreach (int id, ids2) {
				const Card*c = Sanguosha->getCard(id);
				if((c->isNDTrick()||c->getTypeId()==1)&&!names.contains(c->objectName())
					&&player->getMark(c->objectName()+"huiwanUse-Clear")<1){
					names << c->objectName();
					ids << id;
				}
			}
			if(ids.length()>0&&player->askForSkillInvoke(objectName()+"$-1","huiwan0:"+QString::number(draw.num))){
				room->fillAG(ids,player);
				Card*dc = new DummyCard();
				int n = draw.num;
				for (int i = 0; i < n; i++) {
					int id = room->askForAG(player,ids,i>0,objectName());
					if(id<0||ids.length()<2) break;
					player->addMark(Sanguosha->getCard(id)->objectName()+"huiwanUse-Clear");
					room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
					dc->addSubcard(id);
					ids.removeOne(id);
					draw.num--;
				}
				room->clearAG(player);
				player->obtainCard(dc);
				data.setValue(draw);
				dc->deleteLater();
			}
		}
        return false;
    }
};

class Huanli : public TriggerSkill
{
public:
    Huanli() : TriggerSkill("huanli")
    {
        events << EventPhaseStart << TargetSpecified;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *tp, use.to)
					tp->addMark(player->objectName()+"huanliUse-Clear");
			}
		}else if(player->getPhase()==Player::Finish&&player->hasSkill(objectName())){
			bool has1 = false;
			if(player->getMark(player->objectName()+"huanliUse-Clear")>2){
				ServerPlayer *p = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"huanli0",true,true);
				if(p){
					player->peiyin(this);
					room->acquireOneTurnSkills(p,objectName(),"zhijian|guzheng");
					room->addPlayerMark(p,"zhijianhuanliSkill-SelfClear");
					room->addPlayerMark(p,"guzhenghuanliSkill-SelfClear");
				}
				has1 = true;
			}
			bool has2 = false;
			foreach (ServerPlayer *tp, room->getOtherPlayers(player)){
				if(tp->getMark(player->objectName()+"huanliUse-Clear")>2){
					if(player->askForSkillInvoke(objectName()+"$-1",tp)){
						room->acquireOneTurnSkills(tp,objectName(),"yingzi|fanjian");
						room->addPlayerMark(tp,"yingzihuanliSkill-SelfClear");
						room->addPlayerMark(tp,"fanjianhuanliSkill-SelfClear");
					}
					has2 = true;
				}
			}
			if(has1&&has2){
				room->acquireSkill(player,"zhiheng");
				player->setMark("huanli_zhiheng",2);
			}
		}else if(player->getPhase()==Player::NotActive&&player->getMark("huanli_zhiheng")>0){
			player->removeMark("huanli_zhiheng");
			if(player->getMark("huanli_zhiheng")<1)
				room->detachSkillFromPlayer(player,"zhiheng",false,true);
		}
        return false;
    }
};

class HuanliInvalidity : public InvaliditySkill
{
public:
    HuanliInvalidity() : InvaliditySkill("#huanli-invalidity")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
		foreach (QString m, player->getMarkNames()){
			if(player->getMark(m)>0&&m.contains("huanliSkill-SelfClear")){
				return player->getMark(skill->objectName()+"huanliSkill-SelfClear")>0;
			}
		}
		return true;
    }
};

class Yinfeng : public TriggerSkill
{
public:
    Yinfeng() : TriggerSkill("yinfeng")
    {
        events << GameStart << CardsMoveOneTime;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("GodSword")&&!room->getCardOwner(id)){
						room->sendCompulsoryTriggerLog(player,this);
						room->loseHp(player,1,true,player,objectName());
						if(!player->isAlive()) break;
						player->obtainCard(c);
					}
				}
			}else if(move.from_places.contains(Player::PlaceHand)&&move.from==player&&move.to!=player
			&&(move.to_place==Player::PlaceHand||move.to_place==Player::PlaceEquip)){
				foreach (const Card*c, player->getHandcards()){
					if(c->isKindOf("GodSword")){
						room->sendCompulsoryTriggerLog(player,this);
						room->damage(DamageStruct(objectName(),player,(ServerPlayer *)move.to));
						break;
					}
				}
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("GodSword")){
						room->sendCompulsoryTriggerLog(player,this);
						room->damage(DamageStruct(objectName(),(ServerPlayer *)move.to,player));
						break;
					}
				}
			}
		}else{
			for (int i = 0; i < Sanguosha->getCardCount(); i++) {
				const Card*c = Sanguosha->getEngineCard(i);
				if(c->isKindOf("GodSword")&&!room->getCardOwner(i)){
					room->sendCompulsoryTriggerLog(player,this);
					player->obtainCard(c);
					break;
				}
			}
		}
        return false;
    }
};

class DdzFulu : public TriggerSkill
{
public:
    DdzFulu() : TriggerSkill("ddzfulu")
    {
        events << CardFinished << PreCardUsed;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("ddzfuluBf")){
				if(player->hasSkill(objectName())&&player->getHandcardNum()>0){
					QList<int>ids = player->handCards();
					ServerPlayer *tp = room->askForYiji(player,ids,objectName(),false,false,true,1,use.to,CardMoveReason(),"ddzfulu0",true);
					if(tp&&tp->isAlive()&&player->isAlive()){
						player->peiyin(objectName());
						Card*dc = new DummyCard;
						for (int i = 0; i < 2; i++) {
							if(dc->subcardsLength()>=tp->getHandcardNum()) break;
							int id = room->askForCardChosen(player,tp,"h",objectName(),false,Card::MethodNone,dc->getSubcards(),i>0);
							if(id<0) break;
							dc->addSubcard(id);
						}
						player->obtainCard(dc,false);
						dc->deleteLater();
					}
				}
				foreach (ServerPlayer *p, use.to) {
					if(p->isAlive()&&use.card->hasFlag("ddzfuluTo"+p->objectName())&&p->hasSkill(objectName())&&player->getHandcardNum()>0){
						const Card*sc = room->askForExchange(player,objectName(),1,1,false,"ddzfulu1:"+p->objectName(),true);
						if(sc){
							p->peiyin(objectName());
							p->obtainCard(sc,false);
							if(!player->isAlive()) return false;
							Card*dc = new DummyCard;
							for (int i = 0; i < 2; i++) {
								if(dc->subcardsLength()>=p->getHandcardNum()) break;
								int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodNone,dc->getSubcards(),i>0);
								if(id<0) break;
								dc->addSubcard(id);
							}
							player->obtainCard(dc,false);
							dc->deleteLater();
						}
					}
				}
			}
		}else{
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				room->setCardFlag(use.card,"ddzfuluBf");
				foreach (ServerPlayer *p, use.to) {
					if(player->getHp()<p->getHp()&&p->hasSkill(objectName(),true)){
						use.card->setFlags("ddzfuluTo"+p->objectName());
					}
				}
			}
		}
        return false;
    }
};

class Juejue : public TriggerSkill
{
public:
    Juejue() : TriggerSkill("juejue")
    {
        events << CardUsed << ConfirmDamage << PreHpRecover << CardFinished;
        frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isKindOf("Jink")||use.card->isKindOf("Peach")||use.card->isKindOf("Analeptic")){
				QString cn = use.card->isKindOf("Slash")?"Slash":use.card->getClassName();
				player->addMark("juejue"+cn);
				if(player->getMark("juejue"+cn)==1&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					room->setCardFlag(use.card,"juejueBf");
					use.m_addHistory = false;
					data.setValue(use);
				}
			}
		}else if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("juejueBf")&&!room->getCardOwner(use.card->getEffectiveId())){
				player->obtainCard(use.card);
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("juejueBf")){
				player->damageRevises(data,1);
			}
		}else{
            RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->hasFlag("juejueBf")){
				recover.recover++;
				data.setValue(recover);
			}
		}
        return false;
    }
};

class Pimi : public TriggerSkill
{
public:
    Pimi() : TriggerSkill("pimi")
    {
        events << TargetSpecified << TargetConfirmed << ConfirmDamage << PreHpRecover;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&use.to.last()!=player){
				if(player->getMark("pimiBan-Clear")>0||!player->hasSkill(objectName())) return false;
				if(player->canDiscard(use.from,"he")&&player->askForSkillInvoke(objectName()+"$-1",data)){
					int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodDiscard);
					if(id>0){
						room->throwCard(id,objectName(),use.from,player);
						room->setCardFlag(use.card,"pimiBf");
						bool max = true,min = true;
						foreach (ServerPlayer *p, room->getAlivePlayers()) {
							if(p->getHandcardNum()>use.from->getHandcardNum()) max = false;
							if(p->getHandcardNum()<use.from->getHandcardNum()) min = false;
						}
						if(max||min){
							player->drawCards(1,objectName());
							player->addMark("pimiBan-Clear");
						}
					}
				}
			}
		}else if(event==TargetConfirmed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&use.to.last()==player&&use.from!=player){
				if(player->getMark("pimiBan-Clear")>0||!player->hasSkill(objectName())) return false;
				if(player->canDiscard(use.from,"he")&&player->askForSkillInvoke(objectName()+"$-1",data)){
					int id = room->askForCardChosen(player,use.from,"he",objectName(),false,Card::MethodDiscard);
					if(id>0){
						room->throwCard(id,objectName(),use.from,player);
						room->setCardFlag(use.card,"pimiBf");
						bool max = true,min = true;
						foreach (ServerPlayer *p, room->getAlivePlayers()) {
							if(p->getHandcardNum()>use.from->getHandcardNum()) max = false;
							if(p->getHandcardNum()<use.from->getHandcardNum()) min = false;
						}
						if(max||min){
							player->drawCards(1,objectName());
							player->addMark("pimiBan-Clear");
						}
					}
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("pimiBf")){
				player->damageRevises(data,1);
			}
		}else{
            RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->hasFlag("pimiBf")){
				recover.recover++;
				data.setValue(recover);
			}
		}
        return false;
    }
};


class Duanti : public TriggerSkill
{
public:
    Duanti() : TriggerSkill("duanti")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_skillName!="InitialHandCards"
			&&move.reason.m_reason==CardMoveReason::S_REASON_DRAW){
				room->sendCompulsoryTriggerLog(player,this);
				room->damage(DamageStruct(objectName(),nullptr,player));
			}
		}
        return false;
    }
};

class Lianwu : public TriggerSkill
{
public:
    Lianwu() : TriggerSkill("lianwu")
    {
        events << TargetSpecified << TargetConfirmed;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		CardUseStruct use = data.value<CardUseStruct>();
		if(event==TargetSpecified){
			if(use.card->isKindOf("Slash")&&use.to.length()==1){
			}else return false;
		}else if(event==TargetConfirmed){
			if(use.card->isKindOf("Slash")&&use.to.length()==1&&use.to.last()==player){
			}else return false;
		}
		if(use.from->canDiscard(use.to.last(),"he")&&use.from->askForSkillInvoke(objectName()+"$-1",data)){
			if(use.from->getWeapon()){
				int id = room->askForCardChosen(use.from,use.to.last(),"he",objectName(),false,Card::MethodDiscard);
				room->throwCard(id,objectName(),use.to.last(),use.from);
			}
			if(use.card->tag["drank"].toInt()>0&&use.from->canDiscard(use.to.last(),"he")){
				int id = room->askForCardChosen(use.from,use.to.last(),"he",objectName(),false,Card::MethodDiscard);
				room->throwCard(id,objectName(),use.to.last(),use.from);
			}
		}
        return false;
    }
};

class DdzChengxiang : public TriggerSkill
{
public:
    DdzChengxiang() : TriggerSkill("ddzchengxiang")
    {
        events << Damaged;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				if(!player->isAlive()||!player->askForSkillInvoke(objectName()+"$-1",data)) break;
				int n = 4+player->getMark("&ddzchengxiang");
				QList<int> ids;
				if(player->hasSkill("duanti")){
					bool max = true,min = true;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if(p->getHp()>player->getHp()) max = false;
						if(p->getHp()<player->getHp()) min = false;
					}
					if(max||min){
						room->sendCompulsoryTriggerLog(player,"duanti");
						if(max){
							QList<int> &dps = room->getDrawPile();
							foreach (int id, dps) {
								const Card*c = Sanguosha->getCard(id);
								if(c->isKindOf("Weapon")||c->isDamageCard()){
									dps.removeOne(id);
									ids << id;
									n--;
									break;
								}
							}
						}
						if(min){
							QList<int> &dps = room->getDrawPile();
							foreach (int id, dps) {
								const Card*c = Sanguosha->getCard(id);
								if(c->isKindOf("Peach")||c->isKindOf("Analeptic")){
									dps.removeOne(id);
									ids << id;
									n--;
									break;
								}
							}
						}
					}
				}
				ids << room->getNCards(n);
				room->setPlayerMark(player,"&ddzchengxiang",0);
				QList<int> to_get, to_throw;
				room->fillAG(ids);
				while (!ids.isEmpty()) {
					n = 0;
					foreach(int id, to_get)
						n += Sanguosha->getCard(id)->getNumber();
					foreach (int id, ids) {
						if (n + Sanguosha->getCard(id)->getNumber() > 13) {
							room->takeAG(nullptr, id, false);
							ids.removeOne(id);
							to_throw << id;
						}
					}
					if (ids.isEmpty()) break;
					int id = room->askForAG(player, ids, ids.length() < 4, objectName());
					if (id == -1) break;
					room->takeAG(player, id, false);
					ids.removeOne(id);
					to_get << id;
				}
				room->getThread()->delay();
				room->clearAG();
				player->obtainCard(dummyCard(to_get));
				n = 0;
				foreach(int id, to_get)
					n += Sanguosha->getCard(id)->getNumber();
				if(n==13) room->addPlayerMark(player,"&ddzchengxiang");
				room->throwCard(to_throw+ids,objectName(),nullptr);
			}
		}
        return false;
    }
};

class Lieti : public TriggerSkill
{
public:
    Lieti() : TriggerSkill("lieti")
    {
        events << CardsMoveOneTime << DrawNCards;
		waked_skills = "#LietiBf,#LietiBf2";
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player){
				if(move.reason.m_skillName=="InitialHandCards"){
					QList<int>ids = player->handCards();
					int n = 1;
					foreach (int id, move.card_ids) {
						if(ids.contains(id)){
							if(n<=ids.length()/2)
								room->setCardTip(id,"lt_yuanshao");
							else
								room->setCardTip(id,"lt_yuanshu");
							n++;
						}
					}
				}else{
					QString gn = player->property("lietiGN").toString();
					if(gn.isEmpty()) return false;
					room->sendCompulsoryTriggerLog(player,this);
					foreach (int id, move.card_ids) {
						if(player->handCards().contains(id))
							room->setCardTip(id,gn);
					}
				}
			}
		}else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards") return false;
			room->sendCompulsoryTriggerLog(player,this);
			draw.num += draw.num;
			data.setValue(draw);
		}
        return false;
    }
};

class LietiBf : public CardLimitSkill
{
public:
    LietiBf() : CardLimitSkill("#LietiBf")
    {
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target, const Card *c) const
    {
        if (target->hasSkill("lieti")){
			if(target->getMark("shigongUse-Clear")<1&&target->hasSkill("shigong")){
				return "";
			}
			QString gn = target->property("lietiGN").toString();
			if(!c->hasTip(gn)&&target->handCards().contains(c->getId())){
				return c->toString();
			}
		}
        return "";
    }
};

class LietiBf2 : public CardLimitSkill
{
public:
    LietiBf2() : CardLimitSkill("#LietiBf2")
    {
    }

    QString limitList(const Player *) const
    {
        return "ignore";
    }

    QString limitPattern(const Player *target, const Card *c) const
    {
        if (target->hasSkill("lieti")){
			QString gn = target->property("lietiGN").toString();
			if(!c->hasTip(gn)&&target->handCards().contains(c->getId())){
				return c->toString();
			}
		}
        return "";
    }
};

class Shigong : public TriggerSkill
{
public:
    Shigong() : TriggerSkill("shigong")
    {
        events << CardFinished << PreCardUsed;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==PreCardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				room->addPlayerMark(player,"shigongUse-Clear");
				if(player->getMark("shigongUse-Clear")==1){
					room->setCardFlag(use.card,"shigongUse");
					foreach (QString f, use.card->getTips()) {
						if(f.contains("lt_yuansh"))
							room->setCardFlag(use.card,f);
					}
				}
			}
		}else{
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("shigongUse")){
				room->sendCompulsoryTriggerLog(player,this);
				foreach (QString f, use.card->getFlags()) {
					if(f.contains("lt_yuansh")&&Sanguosha->getGeneral(f)){
						room->setPlayerProperty(player,"lietiGN",f);
						room->setPlayerProperty(player, "ChangeHeroMaxHp", player->getMaxHp()+1);
						room->changeHero(player,f,false,false);
						if(f=="lt_yuanshao"){
							Card*dc = Sanguosha->cloneCard("archery_attack");
							dc->setSkillName("_shigong");
							if(player->canUse(dc))
								room->useCard(CardUseStruct(dc,player));
							dc->deleteLater();
						}else
							player->drawCards(2,objectName());
						break;
					}
				}
			}
		}
        return false;
    }
};

class Luankui : public TriggerSkill
{
public:
    Luankui() : TriggerSkill("luankui")
    {
        events << Damage << CardsMoveOneTime << ConfirmDamage << DrawNCards;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_skillName!="InitialHandCards"){
				player->addMark("luankuiCards-Clear");
				QStringList ids;
				foreach (const Card*c, player->getHandcards()) {
					if(c->hasTip("lt_yuanshu")) ids << c->toString();
				}
				if(ids.length()>0&&player->getMark("luankuiCards-Clear")==2
				&&room->askForCard(player,ids.join(","),"luankui1",data,objectName())){
					room->setPlayerMark(player,"&luankui+draw-Clear",1);
				}
			}
		}else if(event==Damage){
			player->addMark("luankuiDamage-Clear");
			QStringList ids;
			foreach (const Card*c, player->getHandcards()) {
				if(c->hasTip("lt_yuanshao")) ids << c->toString();
			}
			if(ids.length()>0&&player->getMark("luankuiDamage-Clear")==2
			&&room->askForCard(player,ids.join(","),"luankui0",data,objectName())){
				room->setPlayerMark(player,"&luankui+damage-Clear",1);
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(player->getMark("&luankui+damage-Clear")>0){
				room->setPlayerMark(player,"&luankui+damage-Clear",0);
				player->damageRevises(data,damage.damage);
			}
		}else if(event==DrawNCards){
			if(player->getMark("&luankui+draw-Clear")>0){
				room->setPlayerMark(player,"&luankui+draw-Clear",0);
				DrawStruct draw = data.value<DrawStruct>();
				draw.num += draw.num;
				data.setValue(draw);
			}
		}
        return false;
    }
};

class Huaquan : public TriggerSkill
{
public:
    Huaquan() : TriggerSkill("huaquan")
    {
        events << TargetSpecified << CardFinished << ConfirmDamage;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->isBlack()){
				foreach (ServerPlayer *p, use.to) {
					if(player!=p){
						room->sendCompulsoryTriggerLog(player,this);
						QString choice = room->askForChoice(player,objectName(),"huaquan1+huaquan2",data);
						room->setCardFlag(use.card,choice);
						break;
					}
				}
				if(use.card->hasFlag("huaquan1")||use.card->hasFlag("huaquan2")){
					foreach(ServerPlayer*p,use.to){
						if(player!=p){
							QString choice = room->askForChoice(p,"huaquan0","huaquan1+huaquan2",data);
							if(use.card->hasFlag(choice)||!player->hasSkill("sanou",true)) continue;
							room->sendCompulsoryTriggerLog(player,"sanou",true,true,qrand()%2+1);
							p->gainMark("&so_jidao");
						}
					}
				}
			}
		}else if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("huaquan2")){
				player->drawCards(1,objectName());
			}
		}else if(event==ConfirmDamage){
            DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("huaquan1"))
				player->damageRevises(data,1);
		}
        return false;
    }
};

class Sanou : public TriggerSkill
{
public:
    Sanou() : TriggerSkill("sanou")
    {
        events << CardsMoveOneTime << Damaged << MarkChanged << EventPhaseChanging;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(player->getMark("&is_jidao")>0&&(move.from_places.contains(Player::DrawPile)||move.to_place==Player::DiscardPile)){
				player->addMark("is_jidaoNum",move.card_ids.length());
				if(player->getMark("is_jidaoNum")>9){
					player->peiyin(this,4);
					room->setPlayerMark(player,"&is_jidao",0);
				}
			}
		}else if(event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::Play&&player->getMark("&is_jidao")>0)
				player->skip(change.to);
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&so_jidao"&&mark.gain>0&&mark.count>2){
				foreach(ServerPlayer*p,room->getAlivePlayers()){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this,3);
						player->loseAllMarks("&so_jidao");
						room->setPlayerMark(player,"&is_jidao",1);
						player->setMark("is_jidaoNum",0);
						break;
					}
				}
			}
		}else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(damage.from,this,qrand()%2+1);
				player->gainMark("&so_jidao");
			}
		}
        return false;
    }
};

class Jimi : public TriggerSkill
{
public:
    Jimi() : TriggerSkill("jimi")
    {
        events << GameStart << CardsMoveOneTime;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==GameStart){
			QList<int>ids;
			foreach(int id, Sanguosha->getRandomCards(true)){
				if(room->getCardOwner(id)) continue;
				const Card*c = Sanguosha->getEngineCard(id);
				if(c->isKindOf("Peach")||c->isKindOf("Analeptic"))
					ids << id;
			}
			if(ids.isEmpty()) return false;
			QList<CardsMoveStruct> moves;
            room->sendCompulsoryTriggerLog(player,this);
			CardMoveReason reason(CardMoveReason::S_REASON_OVERRIDE, player->objectName(), "jimi", "");
			foreach(ServerPlayer*p,room->getAllPlayers()){
				foreach(const Card*h,p->getHandcards()){
					if(ids.isEmpty()) break;
					if(h->isKindOf("Peach")||h->isKindOf("Analeptic")) continue;
					moves << CardsMoveStruct(ids.takeFirst(), p, Player::PlaceHand, reason);
					moves << CardsMoveStruct(h->getId(), nullptr, Player::DrawPile, reason);
				}
			}
			room->moveCardsAtomic(moves, true);
		}else{
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place!=Player::DiscardPile) return false;
			foreach(int id, move.card_ids){
				const Card*c = Sanguosha->getEngineCard(id);
				if(c->isKindOf("Peach")||c->isKindOf("Analeptic"))
					room->addPlayerMark(player,"&jimi-Clear");
			}
			if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) return false;
			foreach(int id, move.card_ids){
				const Card*c = Sanguosha->getEngineCard(id);
				if(c->isKindOf("Peach")||c->isKindOf("Analeptic")){
					foreach(int tid, Sanguosha->getRandomCards(true)){
						if(room->getCardOwner(tid)) continue;
						c = Sanguosha->getEngineCard(tid);
						if(c->isDamageCard()&&c->nameLength()==player->getMark("&jimi-Clear")){
							room->sendCompulsoryTriggerLog(player,this);
							room->obtainCard(player,c);
							break;
						}
					}
					break;
				}
			}
		}
        return false;
    }
};

class Maodie : public TriggerSkill
{
public:
    Maodie() : TriggerSkill("maodie")
    {
        events << CardFinished << CardsMoveOneTime;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				if(use.card->hasFlag("DamageDone")){
					room->setPlayerMark(player,"&maodie-Clear",use.card->nameLength());
				}else{
					room->setPlayerMark(player,"&maodie-Clear",0);
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName(),"maodie0",false,true);
					if(tp){
						QList<int>ids = ListV2I(tp->property("InitialHandCards").toList());
						if(ids.length()>0){
							qShuffle(ids);
							room->obtainCard(player,ids.last());
						}
					}
				}
			}
		}else{
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			
		}
        return false;
    }
};

class MaodieLimit : public CardLimitSkill
{
public:
    MaodieLimit() : CardLimitSkill("#MaodieLimit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player*target,const Card*card) const
    {
        if (target->hasSkill("maodie")&&card->isDamageCard()&&card->nameLength()<target->getMark("&maodie-Clear"))
            return card->toString();
        return "";
    }
};






DoudizhuPackage::DoudizhuPackage()
    : Package("Doudizhu")
{
    related_skills.insertMulti("bahu", "#bahu-target");

    skills << new Feiyang << new Bahu << new BahuTargetMod;

    addMetaObject<FeiyangCard>();


    General *ddz_sunwukong = new General(this, "ddz_sunwukong", "qun", 3);
    ddz_sunwukong->addSkill(new Huoyan);
    ddz_sunwukong->addSkill(new Ruyi);
    ddz_sunwukong->addSkill(new RuyiBf);
    ddz_sunwukong->addSkill(new DdzCibei);

    skills << new JingubangSkill;
    addMetaObject<JingubangCard>();
    Weapon *jgb = new Jingubang(Card::Heart,9);
	jgb->setParent(this);

    General *ddz_longwang = new General(this, "ddz_longwang", "qun", 3);
    ddz_longwang->addSkill(new Longgong);
    ddz_longwang->addSkill(new Sitian);
    addMetaObject<SitianCard>();

    General *ddz_taoshen = new General(this, "ddz_taoshen", "qun", 3);
    ddz_taoshen->addSkill(new Nutao);

    General *ddz_libai = new General(this, "ddz_libai", "qun", 3);
    ddz_libai->addSkill(new Jiuxian);
    ddz_libai->addSkill(new JiuxianMod);
    ddz_libai->addSkill(new Shixian);

    General *ddz_nezha = new General(this, "ddz_nezha", "qun", 3);
    ddz_nezha->addSkill(new Santou);
    ddz_nezha->addSkill(new Faqi);

    General *ddz_shenzhaoyun = new General(this, "ddz_shenzhaoyun", "god", 1);
    ddz_shenzhaoyun->addSkill("gdjuejing");
    ddz_shenzhaoyun->addSkill("gdlonghun");
    ddz_shenzhaoyun->addSkill(new Zhanjian);

    General *ddz_wuyi = new General(this, "ddz_wuyi", "shu", 4);
    ddz_wuyi->addSkill(new DdzBenxi);

    General *ddz_quyuan = new General(this, "ddz_quyuan", "qun", 3);
    ddz_quyuan->addSkill(new Qiusuo);
    ddz_quyuan->addSkill(new Lisao);
    addMetaObject<LisaoCard>();

    General *ddz_wuming = new General(this, "ddz_wuming", "qun", 3);
    ddz_wuming->addSkill(new Chushan);

    General *ddz_sunquan = new General(this, "ddz_sunquan", "wu", 3);
    ddz_sunquan->addSkill(new Huiwan);
    ddz_sunquan->addSkill(new Huanli);
    ddz_sunquan->addSkill(new HuanliInvalidity);

    General *ddz_liuxiecaojie = new General(this, "ddz_liuxiecaojie", "qun", 6);
	ddz_liuxiecaojie->setStartHp(3);
    ddz_liuxiecaojie->addSkill(new Juanlv);
    ddz_liuxiecaojie->addSkill(new Qixin);
    addMetaObject<QixinCard>();

    General *ddz_erxun = new General(this, "ddz_erxun", "wei", 3);
    ddz_erxun->addSkill(new Zhinang);
    ddz_erxun->addSkill(new Gouzhu);

    General *ddz_wuhu = new General(this, "ddz_wuhu", "shu", 4);
    ddz_wuhu->addSkill(new Huyi);

    General *ddz_caocao = new General(this, "ddz_caocao", "qun", 4);
    ddz_caocao->addSkill(new Dingxi);
    ddz_caocao->addSkill(new Nengchen);
    ddz_caocao->addSkill(new Huojie);

    General *ddz_lvbu = new General(this, "ddz_lvbu", "qun", 5);
    ddz_lvbu->addSkill(new Fengzhu);
    ddz_lvbu->addSkill(new Yuyu);
    ddz_lvbu->addSkill(new DdzZhiji);
    ddz_lvbu->addSkill(new Jiejiu);
    ddz_lvbu->addSkill(new JiejiuLimit);

    General *ddz_hanwuhu = new General(this, "ddz_hanwuhu", "wei", 5);
    ddz_hanwuhu->addSkill(new Juejue);
    ddz_hanwuhu->addSkill(new Pimi);

    General *ddz_xiahouen = new General(this, "ddz_xiahouen", "wei", 4);
    ddz_xiahouen->addSkill(new Yinfeng);
    ddz_xiahouen->addSkill(new DdzFulu);

    General *ddz_caochong = new General(this, "ddz_caochong", "wei", 3);
    ddz_caochong->addSkill(new Duanti);
    ddz_caochong->addSkill(new Lianwu);
    ddz_caochong->addSkill(new DdzChengxiang);

    General *ddz_yuanshaoyuanshu = new General(this, "ddz_yuanshaoyuanshu", "qun", 4);
    ddz_yuanshaoyuanshu->addSkill(new Lieti);
    ddz_yuanshaoyuanshu->addSkill(new LietiBf);
    ddz_yuanshaoyuanshu->addSkill(new LietiBf2);
    ddz_yuanshaoyuanshu->addSkill(new Shigong);
    ddz_yuanshaoyuanshu->addSkill(new Luankui);

    General *lt_yuanshao = new General(this, "lt_yuanshao", "qun", 4, true, true, true);
    lt_yuanshao->addSkill("lieti");
    lt_yuanshao->addSkill("shigong");
    lt_yuanshao->addSkill("luankui");

    General *lt_yuanshu = new General(this, "lt_yuanshu", "qun", 4, true, true, true);
    lt_yuanshu->addSkill("lieti");
    lt_yuanshu->addSkill("shigong");
    lt_yuanshu->addSkill("luankui");

    General *ddz_liru = new General(this, "ddz_liru", "qun", 4);
    ddz_liru->addSkill(new Huaquan);
    ddz_liru->addSkill(new Sanou);

    General *ddz_yuanshu = new General(this, "ddz_yuanshu", "qun", 4);
    ddz_yuanshu->addSkill(new Jimi);
    ddz_yuanshu->addSkill(new Maodie);
    ddz_yuanshu->addSkill(new MaodieLimit);








}
ADD_PACKAGE(Doudizhu)
