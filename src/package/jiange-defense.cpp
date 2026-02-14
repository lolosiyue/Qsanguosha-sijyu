#include "jiange-defense.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
#include "wrapped-card.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "room.h"
#include "roomthread.h"

bool isJianGeFriend(const Player *a, const Player *b)
{
    return a->getRole() == b->getRole();
}

// WEI Souls

class JGChiying : public TriggerSkill
{
public:
    JGChiying() : TriggerSkill("jgchiying")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *zidan = room->findPlayerBySkillName(objectName());
        if (zidan && isJianGeFriend(zidan, damage.to) && damage.damage > 1) {
            LogMessage log;
            log.type = "#JGChiying";
            log.from = zidan;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(zidan, objectName());

            damage.damage = 1;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class JGJingfan : public DistanceSkill
{
public:
    JGJingfan() : DistanceSkill("jgjingfan")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        if (!isJianGeFriend(from, to)) {
			int dist = 0;
            foreach (const Player *p, from->getAliveSiblings()) {
                if (p->hasSkill(objectName())&&isJianGeFriend(p, from))
                    dist--;
            }
            return dist;
        }
        return 0;
    }
};

class JGKonghun : public PhaseChangeSkill
{
public:
    JGKonghun() : PhaseChangeSkill("jgkonghun")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Play || !target->isWounded()) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!isJianGeFriend(p, target))
                enemies << p;
        }

        int enemy_num = enemies.length();
        if (target->getLostHp() >= enemy_num && room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());
            foreach(ServerPlayer *p, enemies) {
                room->damage(DamageStruct(objectName(), target, p, 1, DamageStruct::Thunder));
                if (target->isWounded())
                    room->recover(target, RecoverStruct("jgkonghun", target));
            }
        }
        return false;
    }
};

class JGFanshi : public PhaseChangeSkill
{
public:
    JGFanshi() : PhaseChangeSkill("jgfanshi")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());

        room->loseHp(HpLostStruct(target, 1, objectName(), target));
        return false;
    }
};

class JGXuanlei : public PhaseChangeSkill
{
public:
    JGXuanlei() : PhaseChangeSkill("jgxuanlei")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->getJudgingArea().isEmpty() && !isJianGeFriend(p, target))
                enemies << p;
        }

        if (!enemies.isEmpty()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(target, objectName());

            foreach(ServerPlayer *p, enemies)
                room->damage(DamageStruct(objectName(), target, p, 1, DamageStruct::Thunder));
        }
        return false;
    }
};

class JGChuanyun : public PhaseChangeSkill
{
public:
    JGChuanyun() : PhaseChangeSkill("jgchuanyun")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHp() >= target->getHp())
                players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *player = room->askForPlayerChosen(target, players, objectName(), "jgchuanyun-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 1));
        }
        return false;
    }
};

class JGLeili : public TriggerSkill
{
public:
    JGLeili() : TriggerSkill("jgleili")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")) {
            QList<ServerPlayer *> enemies;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!isJianGeFriend(p, player) && p != damage.to)
                    enemies << p;
            }
            ServerPlayer *target = room->askForPlayerChosen(player, enemies, objectName(), "jgleili-invoke", true, true);
            if (target) {
                room->broadcastSkillInvoke(objectName());
                room->damage(DamageStruct(objectName(), player, target, 1, DamageStruct::Thunder));
            }
        }
        return false;
    }
};

class JGFengxing : public PhaseChangeSkill
{
public:
    JGFengxing() : PhaseChangeSkill("jgfengxing")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (!isJianGeFriend(p, target) && target->canSlash(p, false))
                enemies << p;
        }
        if (enemies.isEmpty()) return false;

        ServerPlayer *player = room->askForPlayerChosen(target, enemies, objectName(), "jgfengxing-invoke", true);
        if (player) {
            room->broadcastSkillInvoke(objectName());

            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName(objectName());
		    		slash->deleteLater();
            room->useCard(CardUseStruct(slash, target, player));
        }
        return false;
    }
};

class JGHuodi : public PhaseChangeSkill
{
public:
    JGHuodi() : PhaseChangeSkill("jghuodi")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> enemies;
        bool turnedFriend = false;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (isJianGeFriend(p, target)) {
                if (!p->faceUp() && !turnedFriend) turnedFriend = true;
            } else {
                enemies << p;
            }
        }
        if (turnedFriend) {
            ServerPlayer *player = room->askForPlayerChosen(target, enemies, objectName(), "jghuodi-invoke", true);
            if (player) {
                room->broadcastSkillInvoke(objectName());
                player->turnOver();
            }
        }
        return false;
    }
};

class JGJueji : public DrawCardsSkill
{
public:
    JGJueji() : DrawCardsSkill("jgjueji")
    {
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        Room *room = player->getRoom();

        if (!player->isWounded()) return n;
        int reduce = 0;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!isJianGeFriend(p, player) && TriggerSkill::triggerable(p)
                && room->askForSkillInvoke(p, objectName()))
                reduce++;
        }
        return n - reduce;
    }
};

JGJiaoxieCard::JGJiaoxieCard()
{
}

bool JGJiaoxieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length()<2 && to_select->getGeneralName().contains("jg_machine_")
	&& to_select->getKingdom() != Self->getKingdom();
}

void JGJiaoxieCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.to->isAlive()||!effect.from->isAlive()) return;
    const Card*dc = effect.from->getRoom()->askForExchange(effect.to,"jgjiaoxie",1,1,true,"jgjiaoxie0:"+effect.from->objectName());
	if(dc) effect.from->getRoom()->giveCard(effect.to,effect.from,dc,"jgjiaoxie");
}

class JGJiaoxie : public ZeroCardViewAsSkill
{
public:
    JGJiaoxie() : ZeroCardViewAsSkill("jgjiaoxie")
    {
    }

    const Card *viewAs() const
    {
        return new JGJiaoxieCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("JGJiaoxieCard")<1;
    }
};

class JGShuailing : public PhaseChangeSkill
{
public:
    JGShuailing() : PhaseChangeSkill("jgshuailing")
    {
		frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Draw) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (isJianGeFriend(p, target)&&p->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(p,this);
				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.good = true;
				judge.who = target;
				judge.reason = objectName();
				room->judge(judge);
				if(judge.isGood()&&!room->getCardOwner(judge.card->getEffectiveId())&&target->isAlive())
					target->obtainCard(judge.card);
			}
        }
        return false;
    }
};

class JGBashi : public TriggerSkill
{
public:
    JGBashi() : TriggerSkill("jgbashi")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")||use.card->isNDTrick()) {
            if (player!=use.from&&player->faceUp()&&player->askForSkillInvoke(this,data)) {
                room->broadcastSkillInvoke(objectName());
                player->turnOver();
				use.nullified_list << player->objectName();
				data.setValue(use);
            }
        }
        return false;
    }
};

class JGDanjing : public TriggerSkill
{
public:
    JGDanjing() : TriggerSkill("jgdanjing")
    {
        events << AskForPeaches;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dy = data.value<DyingStruct>();
        if (dy.who!=player&&player->getHp()>1&&isJianGeFriend(dy.who, player)) {
			Card*dc = Sanguosha->cloneCard("peach");
			dc->setSkillName("_jgdanjing");
			if(player->canUse(dc,dy.who)&&player->askForSkillInvoke(this,data,dy.who)){
				room->broadcastSkillInvoke(objectName());
				room->loseHp(player,1,true,player,objectName());
				room->useCard(CardUseStruct(dc,player,dy.who));
			}
			dc->deleteLater();
        }
        return false;
    }
};

class JGTongjun : public AttackRangeSkill
{
public:
    JGTongjun() : AttackRangeSkill("jgtongjun")
    {
        frequency = Compulsory;
    }

    int getExtra(const Player *target, bool) const
    {
        if(target->getGeneralName().contains("jg_machine_")){
			foreach (const Player *p, target->getAliveSiblings(true)) {
				if(p->getKingdom()==target->getKingdom()&&p->hasSkill(objectName()))
					return 1;
			}
		}
        return 0;
    }
};

JGYingjiCard::JGYingjiCard()
{
}

bool JGYingjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("jgyingji");
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}

void JGYingjiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->showAllCards(source);
	QStringList ts;
	foreach (const Card*h, source->getHandcards()) {
		if(ts.contains(h->getType())) continue;
		ts.append(h->getType());
	}
    Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("_jgyingji");
	dc->setMark("jgyingjiDamage",ts.length());
	room->useCard(CardUseStruct(dc,source,targets));
	dc->deleteLater();
}

class JGYingjivs : public ZeroCardViewAsSkill
{
public:
    JGYingjivs() : ZeroCardViewAsSkill("jgyingji")
    {
    }

    const Card *viewAs() const
    {
        return new JGYingjiCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("jgyingji");
		dc->deleteLater();
        return player->usedTimes("JGYingjiCard")<1&&dc->isAvailable(player)&&player->getHandcardNum()>0;
    }
};

class JGYingji : public TriggerSkill
{
public:
    JGYingji() : TriggerSkill("jgyingji")
    {
        events << DamageCaused;
		view_as_skill = new JGYingjivs;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")) {
            int n = damage.card->getMark("jgyingjiDamage");
			if(n>0){
				damage.damage = n;
				data.setValue(damage);
			}
        }
        return false;
    }
};

class JGZhene : public TriggerSkill
{
public:
    JGZhene() : TriggerSkill("jgzhene")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId()>0) {
			foreach (ServerPlayer*t, use.to) {
				if(t->getHandcardNum()<=player->getHandcardNum())
					use.no_respond_list << t->objectName();
            }
			if(use.no_respond_list.length()>0)
				room->sendCompulsoryTriggerLog(player,this);
			data.setValue(use);
        }
        return false;
    }
};

class JGWeizhu : public TriggerSkill
{
public:
    JGWeizhu() : TriggerSkill("jgweizhu")
    {
        events << DamageInflicted;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (isJianGeFriend(p, player)&&p->hasSkill(objectName())&&p->canDiscard(p,"h")){
				if(room->askForCard(p,".","jgweizhu0:"+player->objectName(),data,objectName())){
					p->peiyin(this);
					return p->damageRevises(data,-damage.damage);
				}
			}
		}
        return false;
    }
};

class JGZhenxi : public TriggerSkill
{
public:
    JGZhenxi() : TriggerSkill("jgzhenxi")
    {
        events << Damaged << DrawNCards;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (isJianGeFriend(p, player)&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					room->addPlayerMark(player,"&jgzhenxi");
				}
			}
        }else{
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("&jgzhenxi")<1) return false;
			draw.num += player->getMark("&jgzhenxi");
			data.setValue(draw);
			room->setPlayerMark(player,"&jgzhenxi",0);
		}
        return false;
    }
};

JGHanjunCard::JGHanjunCard()
{
	target_fixed = true;
}

void JGHanjunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->addPlayerMark(source,"jghanjunUse-Clear");
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if(isJianGeFriend(p,source)) continue;
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	QList<int>ids;
	foreach (ServerPlayer *p, room->getAllPlayers()) {
		if(isJianGeFriend(p,source)) continue;
		QList<const Card*>cs = p->getCards("he");
		qShuffle(cs);
		foreach (const Card*c, cs) {
			if(source->canDiscard(p,c->getId())){
				room->throwCard(c,"jghanjun",p,source);
				if(!room->getCardOwner(c->getId())) ids << c->getId();
				break;
			}
		}
	}
	if(ids.isEmpty()||!source->isAlive()) return;
	room->fillAG(ids,source);
	int id = room->askForAG(source,ids,true,"jghanjun");
	room->clearAG(source);
	if(id<0) return;
	foreach (int cid, ids) {
		if(Sanguosha->getCard(id)->getTypeId()==3){
			if(Sanguosha->getCard(cid)->getTypeId()!=3)
				ids.removeOne(cid);
		}else if(Sanguosha->getCard(cid)->getTypeId()==3)
			ids.removeOne(cid);
	}
	Card*dc = dummyCard(ids);
	source->obtainCard(dc);
}

class JGHanjun : public ZeroCardViewAsSkill
{
public:
    JGHanjun() : ZeroCardViewAsSkill("jghanjun")
    {
    }

    const Card *viewAs() const
    {
        return new JGHanjunCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("jghanjunUse-Clear")<1&&player->hasTurn();
    }
};

class JGPigua : public PhaseChangeSkill
{
public:
    JGPigua() : PhaseChangeSkill("jgpigua")
    {
		frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase()!=Player::Start||player->hasEquip()) return false;
        QList<int> ids = room->getDiscardPile();
		qShuffle(ids);
		foreach (int id, ids) {
            if (Sanguosha->getCard(id)->getTypeId()==3){
				room->sendCompulsoryTriggerLog(player,this);
				room->loseHp(player,1,true,player,objectName());
				room->obtainCard(player,id);
				return false;
			}
        }
		ids = room->getDrawPile();
		qShuffle(ids);
		foreach (int id, ids) {
            if (Sanguosha->getCard(id)->getTypeId()==3){
				room->sendCompulsoryTriggerLog(player,this);
				room->loseHp(player,1,true,player,objectName());
				room->obtainCard(player,id);
				return false;
			}
        }
        return false;
    }
};



// Offensive Machines

class JGJiguan : public ProhibitSkill
{
public:
    JGJiguan() : ProhibitSkill("jgjiguan")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("Indulgence")&&to->hasSkill(objectName());
    }
};

class JGTanshi : public DrawCardsSkill
{
public:
    JGTanshi() : DrawCardsSkill("jgtanshi")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        Room *room = player->getRoom();

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());

        return n - 1;
    }
};

class JGTunshi : public PhaseChangeSkill
{
public:
    JGTunshi() : PhaseChangeSkill("jgtunshi")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        QList<ServerPlayer *> to_damage;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getHandcardNum() > target->getHandcardNum() && !isJianGeFriend(p, target))
                to_damage << p;
        }

        if (!to_damage.isEmpty()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(target, objectName());

            foreach(ServerPlayer *p, to_damage)
                room->damage(DamageStruct(objectName(), target, p));
        }
        return false;
    }
};

class JGLianyu : public PhaseChangeSkill
{
public:
    JGLianyu() : PhaseChangeSkill("jglianyu")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish)
            return false;

        if (room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());

            QList<ServerPlayer *> enemies;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (!isJianGeFriend(p, target))
                    enemies << p;
            }
            foreach(ServerPlayer *p, enemies)
                room->damage(DamageStruct(objectName(), target, p, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class JGDidong : public PhaseChangeSkill
{
public:
    JGDidong() : PhaseChangeSkill("jgdidong")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!isJianGeFriend(p, target))
                enemies << p;
        }
        ServerPlayer *player = room->askForPlayerChosen(target, enemies, objectName(), "jgdidong-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            player->turnOver();
        }
        return false;
    }
};

class JGDixian : public PhaseChangeSkill
{
public:
    JGDixian() : PhaseChangeSkill("jgdixian")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!isJianGeFriend(p, target))
                enemies << p;
        }
        if (room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());
            target->turnOver();
            foreach (ServerPlayer *p, enemies) {
                if (p->isAlive() && !p->getEquips().isEmpty())
                    p->throwAllEquips();
            }
        }
        return false;
    }
};

// SHU Souls

class JGJizhen : public PhaseChangeSkill
{
public:
    JGJizhen() : PhaseChangeSkill("jgjizhen")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> to_draw;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isWounded() && isJianGeFriend(p, target))
                to_draw << p;
        }

        if (!to_draw.isEmpty()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(target, objectName());
            room->drawCards(to_draw, 1, objectName());
        }
        return false;
    }
};

class JGLingfeng : public PhaseChangeSkill
{
public:
    JGLingfeng() : PhaseChangeSkill("jglingfeng")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Draw) return false;
        if (target->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());

            int card1 = room->drawCard();
            int card2 = room->drawCard();
            QList<int> ids;
            ids << card1 << card2;
            bool diff = (Sanguosha->getCard(card1)->getColor() != Sanguosha->getCard(card2)->getColor());

            CardsMoveStruct move;
            move.card_ids = ids;
            move.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), "");
            move.to_place = Player::PlaceTable;
            room->moveCardsAtomic(move, true);
            room->getThread()->delay();

            DummyCard *dummy = new DummyCard(move.card_ids);
            room->obtainCard(target, dummy);
            delete dummy;

            if (diff) {
                QList<ServerPlayer *> enemies;
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (!isJianGeFriend(p, target))
                        enemies << p;
                }
                Q_ASSERT(!enemies.isEmpty());
                ServerPlayer *enemy = room->askForPlayerChosen(target, enemies, objectName(), "@jglingfeng");
                if (enemy)
                    room->loseHp(HpLostStruct(enemy, 1, objectName(), target));
            }
        }
        return true;
    }
};

class JGBiantian : public TriggerSkill
{
public:
    JGBiantian() : TriggerSkill("jgbiantian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart && TriggerSkill::triggerable(player)
            && player->getPhase() == Player::Start) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());

            JudgeStruct judge;
            judge.good = true;
            judge.play_animation = false;
            judge.who = player;
            judge.reason = objectName();

            room->judge(judge);

            if (!player->isAlive()) return false;
            if (judge.card->getColor() == Card::Red) {
                const TriggerSkill *kuangfeng = Sanguosha->getTriggerSkill("kuangfeng");
                room->getThread()->addTriggerSkill(kuangfeng);
				player->tag["kuangfengUse"] = true;
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (!isJianGeFriend(p, player)) {
                        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                        p->gainMark("&kuangfeng");
                    }
                }
            } else if (judge.card->getColor() == Card::Black) {
                const TriggerSkill *dawu = Sanguosha->getTriggerSkill("dawu");
                room->getThread()->addTriggerSkill(dawu);
				player->tag["dawuUse"] = true;
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (isJianGeFriend(p, player)) {
                        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                        p->gainMark("&dawu");
                    }
                }
            }
        }
        return false;
    }
};

class JGGongshen : public PhaseChangeSkill
{
public:
    JGGongshen() : PhaseChangeSkill("jggongshen")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        ServerPlayer *offensive_machine = nullptr, *defensive_machine = nullptr;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->property("jiange_defense_type").toString() == "machine") {
                if (isJianGeFriend(p, target))
                    defensive_machine = p;
                else
                    offensive_machine = p;
            }
        }
        QStringList choicelist;
        if (defensive_machine && defensive_machine->isWounded())
            choicelist << "recover";
        if (offensive_machine)
            choicelist << "damage";
        if (choicelist.isEmpty())
            return false;

        choicelist << "cancel";
        QString choice = room->askForChoice(target, objectName(), choicelist.join("+"));
        if (choice != "cancel") {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = target;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(target, objectName());

            if (choice == "recover")
                room->recover(defensive_machine, RecoverStruct("jggongshen", target));
            else
                room->damage(DamageStruct(objectName(), target, offensive_machine, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class JGZhinang : public PhaseChangeSkill
{
public:
    JGZhinang() : PhaseChangeSkill("jgzhinang")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        if (room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());

            QList<int> ids = room->getNCards(3, false);
            CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), ""));
            room->moveCardsAtomic(move, true);

            room->getThread()->delay();
            room->getThread()->delay();

            QList<int> card_to_throw;
            QList<int> card_to_give;
            for (int i = 0; i < 3; i++) {
                if (Sanguosha->getCard(ids[i])->getTypeId() == Card::TypeBasic)
                    card_to_throw << ids[i];
                else
                    card_to_give << ids[i];
            }
            ServerPlayer *togive = nullptr;
            if (!card_to_give.isEmpty()) {
                QList<ServerPlayer *> friends;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (isJianGeFriend(p, target))
                        friends << p;
                }
                togive = room->askForPlayerChosen(target, friends, objectName(), "@jgzhinang");
            }
            if (!card_to_throw.isEmpty()) {
                DummyCard *dummy = new DummyCard(card_to_throw);
                CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), "");
                room->throwCard(dummy, reason, nullptr);
                delete dummy;
            }
            if (togive) {
                DummyCard *dummy2 = new DummyCard(card_to_give);
                room->obtainCard(togive, dummy2);
                delete dummy2;
            }
        }
        return false;
    }
};

class JGJingmiao : public TriggerSkill
{
public:
    JGJingmiao() : TriggerSkill("jgjingmiao")
    {
        frequency = Compulsory;
        events << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Nullification")) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (!player->isAlive())
                    return false;
                if (TriggerSkill::triggerable(p) && !isJianGeFriend(p, player)) {
                    room->broadcastSkillInvoke(objectName());
                    room->sendCompulsoryTriggerLog(p, objectName());
                    room->loseHp(HpLostStruct(player, 1, objectName(), p));
                }
            }
        }
        return false;
    }
};

class JGYuhuo : public TriggerSkill
{
public:
    JGYuhuo() : TriggerSkill("jgyuhuo")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#JGYuhuoProtect";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = "fire_nature";
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            return true;
        }
        return false;
    }
};

class JGQiwu : public TriggerSkill
{
public:
    JGQiwu() : TriggerSkill("jgqiwu")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player == move.from
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
            int count = 0;
            foreach (int id, move.card_ids) {
                if (Sanguosha->getCard(id)->getSuit() == Card::Club)
                    count++;
            }
            if (count > 0) {
                for (int i = 0; i < count; i++) {
                    QList<ServerPlayer *> friends;
                    foreach (ServerPlayer *p, room->getAlivePlayers()) {
                        if (isJianGeFriend(p, player) && p->isWounded())
                            friends << p;
                    }
                    if (friends.isEmpty()) return false;
                    ServerPlayer *rec_friend = room->askForPlayerChosen(player, friends, objectName(), "jgqiwu-invoke", true, true);
                    if (!rec_friend) return false;
                    room->recover(rec_friend, RecoverStruct(objectName(), player));
                }
            }
        }
        return false;
    }
};

class JGTianyu : public PhaseChangeSkill
{
public:
    JGTianyu() : PhaseChangeSkill("jgtianyu")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        bool sendLog = false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->isChained() && !isJianGeFriend(p, target)) {
                if (!sendLog) {
                    room->broadcastSkillInvoke(objectName());
                    room->sendCompulsoryTriggerLog(target, objectName());
                    sendLog = true;
                }
                room->setPlayerChained(p);
            }
        }
        return false;
    }
};

class JGXiaorui : public TriggerSkill
{
public:
    JGXiaorui() : TriggerSkill("jgxiaorui")
    {
        events << Damage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
		if(damage.card&&damage.card->isKindOf("Slash")&&player->hasFlag("CurrentPlayer")){
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (isJianGeFriend(p, player)&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					room->addPlayerMark(player,"&jgxiaorui-Clear");
				}
			}
		}
        return false;
    }
};

class JGHuchen : public TriggerSkill
{
public:
    JGHuchen() : TriggerSkill("jghuchen")
    {
        events << Death << DrawNCards;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death) {
            DeathStruct de = data.value<DeathStruct>();
			if (de.damage&&de.damage->from==player){
				room->addPlayerMark(player,"&jghuchen");
			}
        }else{
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("&jghuchen")<1) return false;
			room->sendCompulsoryTriggerLog(player,this);
			draw.num += player->getMark("&jghuchen");
			data.setValue(draw);
		}
        return false;
    }
};

class JGTianjiang : public TriggerSkill
{
public:
    JGTianjiang() : TriggerSkill("jgtianjiang")
    {
        events << Damage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
		if(damage.card&&damage.card->isKindOf("Slash")&&player->getMark("jgtianjiangDamage-Clear")<1){
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (isJianGeFriend(p, player)&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					player->addMark("jgtianjiangDamage-Clear");
					player->drawCards(1,objectName());
				}
			}
		}
        return false;
    }
};

class JGFengjian : public TriggerSkill
{
public:
    JGFengjian() : TriggerSkill("jgfengjian")
    {
        events << Damage << EventPhaseChanging;
		waked_skills = "#JGFengjianProhibit";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
		if(event==Damage){
			if(damage.to->isAlive()&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				room->setPlayerMark(damage.to,"&jgfengjian+#"+player->objectName(),1);
				damage.to->addMark("jgfengjian-Clear");
			} 
		}else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to==Player::NotActive&&player->getMark("jgfengjian-Clear")<1){
				foreach (ServerPlayer *p, room->getPlayers()) {
					room->setPlayerMark(player,"&jgfengjian+#"+p->objectName(),0);
				}
			}
		}
        return false;
    }
};

class JGFengjianProhibit : public ProhibitSkill
{
public:
    JGFengjianProhibit() : ProhibitSkill("#JGFengjianProhibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->getTypeId()>0 && from->getMark("&jgfengjian+#"+to->objectName())>0;
    }
};

JGKedingCard::JGKedingCard()
{
}

bool JGKedingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	QStringList user = Self->property("jgkedingUser").toString().split("+");
    if (user.contains(to_select->objectName())||targets.length()>=subcardsLength()) return false;
    const Card*dc = Card::Parse(user.first());
    return dc->targetFilter(QList<const Player *>(),to_select,Self);
}

bool JGKedingCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcardsLength();
}

void JGKedingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QStringList user;
	foreach (ServerPlayer *p, targets) {
		user << p->objectName();
	}
	room->setPlayerProperty(source,"jgkedingUser",user.join("+"));
}

class JGKedingVs : public ViewAsSkill
{
public:
    JGKedingVs() : ViewAsSkill("jgkeding")
    {
		response_pattern = "@@jgkeding";
    }

    bool viewFilter(const QList<const Card *> &, const Card *card) const
    {
        return !card->isEquipped()&&!Self->isJilei(card);
    }

    bool isEnabledAtPlay(const Player *) const
    {
		return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
		if(cards.length()<1) return nullptr;
		JGKedingCard *card = new JGKedingCard;
		card->addSubcards(cards);
		return card;
    }
};

class JGKeding : public TriggerSkill
{
public:
    JGKeding() : TriggerSkill("jgkeding")
    {
        events << TargetSpecifying;
		view_as_skill = new JGKedingVs;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("Slash")||use.card->isNDTrick()){
			if(use.to.length()==1&&player->canDiscard(player,"h")){
				QStringList user;
				user << use.card->toString();
				user << use.to.first()->objectName();
				room->setPlayerProperty(player,"jgkedingUser",user.join("+"));
				if(room->askForUseCard(player,"@@jgkeding","jgkeding0:"+use.card->objectName())){
					user = player->property("jgkedingUser").toString().split("+");
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if(user.contains(p->objectName())) use.to << p;
					}
					room->sortByActionOrder(use.to);
					data.setValue(use);
				}
			}
		}
        return false;
    }
};

class JGKedingMod : public TargetModSkill
{
public:
    JGKedingMod() : TargetModSkill("#JGKedingMod")
    {
		pattern = ".";
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        return from->getMark("jgkedingNum");
    }
};

class JGLongwei : public TriggerSkill
{
public:
    JGLongwei() : TriggerSkill("jglongwei")
    {
        events << Dying;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dy = data.value<DyingStruct>();
        if (dy.who->getHp()<1&&isJianGeFriend(dy.who, player)&&player->askForSkillInvoke(this,data,dy.who)) {
			room->broadcastSkillInvoke(objectName());
			room->loseMaxHp(player,1,objectName());
			room->recover(dy.who,RecoverStruct(objectName(),player,1-dy.who->getHp()));
        }
        return false;
    }
};

class JGMengwu : public TriggerSkill
{
public:
    JGMengwu() : TriggerSkill("jgmengwu")
    {
        events << CardOffset;
		frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
		if(effect.card->isKindOf("Slash")){
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(1,objectName());
		}
        return false;
    }
};

class JGHupo : public FilterSkill
{
public:
    JGHupo() : FilterSkill("jghupo")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getTypeId()==2;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class JGShuhun : public TriggerSkill
{
public:
    JGShuhun() : TriggerSkill("jgshuhun")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		QList<ServerPlayer *>tps = room->getAlivePlayers();
		qShuffle(tps);
		foreach (ServerPlayer *p, tps) {
			if (isJianGeFriend(p, player)){
				room->sendCompulsoryTriggerLog(player,this);
				room->doAnimate(1,player->objectName(),p->objectName());
				room->recover(p,RecoverStruct(objectName(),player));
				break;
			}
		}
        return false;
    }
};

class JGQinzhen : public TargetModSkill
{
public:
    JGQinzhen() : TargetModSkill("jgqinzhen")
    {
    }
    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill("jgmengwu"))
            return 999;
        if (from->hasSkill("jgjinggong"))
            return 999;
        return 0;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill("jgmengwu"))
            return 999;
		int n = from->getMark("&jgxiaorui-Clear");
		foreach (const Player *p, from->getAliveSiblings(true)) {
			if(p->hasSkill("jgqinzhen")) n++;
		}
        return n;
    }
};

class JGQixian : public TriggerSkill
{
public:
    JGQixian() : TriggerSkill("jgqixian")
    {
        events << ConfirmDamage << CardsMoveOneTime;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				int n = player->getMark("&jgqixian-Clear");
				if(n>0&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,objectName());
					room->setPlayerMark(player,"&jgqixian-Clear",0);
					player->damageRevises(data,n);
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&player->getPhase()==Player::Play&&player->hasSkill(objectName(),true))
				room->addPlayerMark(player,"&jgqixian-Clear",move.card_ids.length());
		}
        return false;
    }
};

class JGJinggong : public TriggerSkill
{
public:
    JGJinggong() : TriggerSkill("jgjinggong")
    {
        events << EventPhaseChanging << PreCardUsed;
		frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive&&player->getMark("jgjinggongSlash-Clear")<1&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				room->loseHp(player,1,true,player,objectName());
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash"))
				player->addMark("jgjinggongSlash-Clear");
		}
        return false;
    }
};


// Defensive Machines

class JGMojian : public PhaseChangeSkill
{
public:
    JGMojian() : PhaseChangeSkill("jgmojian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Play) return false;

        ArcheryAttack *aa = new ArcheryAttack(Card::NoSuit, 0);
        aa->setSkillName("_" + objectName());
        bool can_invoke = false;
        if (!target->isCardLimited(aa, Card::MethodUse)) {
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (!room->isProhibited(target, p, aa)) {
                    can_invoke = true;
                    break;
                }
            }
        }
        if (!can_invoke) {
            delete aa;
            return false;
        }

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());
        room->useCard(CardUseStruct(aa, target, QList<ServerPlayer *>()));
        return false;
    }
};

class JGMojianProhibit : public ProhibitSkill
{
public:
    JGMojianProhibit() : ProhibitSkill("#jgmojian-prohibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("ArcheryAttack") && card->getSkillName() == "jgmojian" && isJianGeFriend(from, to);
    }
};

class JGBenlei : public PhaseChangeSkill
{
public:
    JGBenlei() : PhaseChangeSkill("jgbenlei")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!isJianGeFriend(p, target) && p->property("jiange_defense_type").toString() == "machine") {
                room->damage(DamageStruct(objectName(), target, p, 2, DamageStruct::Thunder));
                break;
            }
        }
        return false;
    }
};

class JGLingyu : public PhaseChangeSkill
{
public:
    JGLingyu() : PhaseChangeSkill("jglingyu")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        if (room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());

            target->turnOver();
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isWounded() && isJianGeFriend(p, target) && p != target)
                    room->recover(p, RecoverStruct(objectName(), target));
            }
        }
        return false;
    }
};

class JGTianyun : public PhaseChangeSkill
{
public:
    JGTianyun() : PhaseChangeSkill("jgtianyun")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!isJianGeFriend(p, target))
                enemies << p;
        }
        ServerPlayer *player = room->askForPlayerChosen(target, enemies, objectName(), "jgtianyun-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->loseHp(HpLostStruct(target, 1, objectName(), target));

            room->damage(DamageStruct(objectName(), target, player, 2, DamageStruct::Fire));
            player->throwAllEquips();
        }
        return false;
    }
};

JianGeDefensePackage::JianGeDefensePackage()
    : Package("~JianGeDefense")
{
    typedef General Soul;
    typedef General Machine;

    Soul *jg_soul_caozhen = new Soul(this, "jg_soul_caozhen", "wei", 5, true, true);
    jg_soul_caozhen->addSkill(new JGChiying);
    jg_soul_caozhen->addSkill(new JGJingfan);
    jg_soul_caozhen->addRelateSkill("jgzhenxi");
	skills << new JGZhenxi;

    Soul *jg_soul_simayi = new Soul(this, "jg_soul_simayi", "wei", 5, true, true);
    jg_soul_simayi->addSkill(new JGKonghun);
    jg_soul_simayi->addSkill(new JGFanshi);
    jg_soul_simayi->addSkill(new JGXuanlei);

    Soul *jg_soul_xiahouyuan = new Soul(this, "jg_soul_xiahouyuan", "wei", 4, true, true);
    jg_soul_xiahouyuan->addSkill(new JGChuanyun);
    jg_soul_xiahouyuan->addSkill(new JGLeili);
    jg_soul_xiahouyuan->addSkill(new JGFengxing);

    Soul *jg_soul_zhanghe = new Soul(this, "jg_soul_zhanghe", "wei", 4, true, true);
    jg_soul_zhanghe->addSkill(new JGHuodi);
    jg_soul_zhanghe->addSkill(new JGJueji);

    Soul *jg_soul_zhangliao = new Soul(this, "jg_soul_zhangliao", "wei", 5, true, true);
    jg_soul_zhangliao->addSkill(new JGJiaoxie);
    jg_soul_zhangliao->addRelateSkill("jgshuailing");
    addMetaObject<JGJiaoxieCard>();
	skills << new JGShuailing;

    Soul *jg_soul_xiahoudun = new Soul(this, "jg_soul_xiahoudun", "wei", 5, true, true);
    jg_soul_xiahoudun->addSkill(new JGBashi);
    jg_soul_xiahoudun->addSkill(new JGDanjing);
    jg_soul_xiahoudun->addRelateSkill("jgtongjun");
	skills << new JGTongjun;

    Soul *jg_soul_dianwei = new Soul(this, "jg_soul_dianwei", "wei", 5, true, true);
    jg_soul_dianwei->addSkill(new JGYingji);
    jg_soul_dianwei->addSkill(new JGZhene);
    jg_soul_dianwei->addSkill(new JGWeizhu);
    addMetaObject<JGYingjiCard>();

    Soul *jg_soul_yujin = new Soul(this, "jg_soul_yujin", "wei", 5, true, true);
    jg_soul_yujin->addSkill(new JGHanjun);
    jg_soul_yujin->addSkill(new JGPigua);
    addMetaObject<JGHanjunCard>();

    Machine *jg_machine_tuntianchiwen = new Machine(this, "jg_machine_tuntianchiwen", "wei", 5, true, true);
    jg_machine_tuntianchiwen->addSkill(new JGJiguan);
    jg_machine_tuntianchiwen->addSkill(new JGTanshi);
    jg_machine_tuntianchiwen->addSkill(new JGTunshi);

    Machine *jg_machine_shihuosuanni = new Machine(this, "jg_machine_shihuosuanni", "wei", 3, true, true);
    jg_machine_shihuosuanni->addSkill("jgjiguan");
    jg_machine_shihuosuanni->addSkill(new JGLianyu);

    Machine *jg_machine_fudibian = new Machine(this, "jg_machine_fudibian", "wei", 4, true, true);
    jg_machine_fudibian->addSkill("jgjiguan");
    jg_machine_fudibian->addSkill(new JGDidong);

    Machine *jg_machine_lieshiyazi = new Machine(this, "jg_machine_lieshiyazi", "wei", 4, true, true);
    jg_machine_lieshiyazi->addSkill("jgjiguan");
    jg_machine_lieshiyazi->addSkill(new JGDixian);

    Soul *jg_soul_liubei = new Soul(this, "jg_soul_liubei", "shu", 5, true, true);
    jg_soul_liubei->addSkill(new JGJizhen);
    jg_soul_liubei->addSkill(new JGLingfeng);
    jg_soul_liubei->addRelateSkill("jgqinzhen");
	skills << new JGQinzhen;

    Soul *jg_soul_zhugeliang = new Soul(this, "jg_soul_zhugeliang", "shu", 4, true, true);
    jg_soul_zhugeliang->addSkill(new JGBiantian);
    jg_soul_zhugeliang->addSkill("bazhen");
    related_skills.insertMulti("jgbiantian", "#qixing-clear");

    Soul *jg_soul_huangyueying = new Soul(this, "jg_soul_huangyueying", "shu", 4, false, true);
    jg_soul_huangyueying->addSkill(new JGGongshen);
    jg_soul_huangyueying->addSkill(new JGZhinang);
    jg_soul_huangyueying->addSkill(new JGJingmiao);

    Soul *jg_soul_pangtong = new Soul(this, "jg_soul_pangtong", "shu", 4, true, true);
    jg_soul_pangtong->addSkill(new JGYuhuo);
    jg_soul_pangtong->addSkill(new JGQiwu);
    jg_soul_pangtong->addSkill(new JGTianyu);

    Soul *jg_soul_guanyu = new Soul(this, "jg_soul_guanyu", "shu", 5, true, true);
    jg_soul_guanyu->addSkill(new JGXiaorui);
    jg_soul_guanyu->addSkill(new JGHuchen);
    jg_soul_guanyu->addRelateSkill("jgtianjiang");
	skills << new JGTianjiang;

    Soul *jg_soul_zhaoyun = new Soul(this, "jg_soul_zhaoyun", "shu", 5, true, true);
    jg_soul_zhaoyun->addSkill(new JGFengjian);
    jg_soul_zhaoyun->addSkill(new JGFengjianProhibit);
    jg_soul_zhaoyun->addSkill(new JGKeding);
    jg_soul_zhaoyun->addRelateSkill("jglongwei");
	skills << new JGLongwei;

    Soul *jg_soul_zhangfei = new Soul(this, "jg_soul_zhangfei", "shu", 4, true, true);
    jg_soul_zhangfei->addSkill(new JGMengwu);
    jg_soul_zhangfei->addSkill(new JGHupo);
    jg_soul_zhangfei->addSkill(new JGShuhun);

    Soul *jg_soul_huangzhong = new Soul(this, "jg_soul_huangzhong", "shu", 4, true, true);
    jg_soul_huangzhong->addSkill(new JGQixian);
    jg_soul_huangzhong->addSkill(new JGJinggong);

    Machine *jg_machine_yunpingqinglong = new Machine(this, "jg_machine_yunpingqinglong", "shu", 4, true, true);
    jg_machine_yunpingqinglong->addSkill("jgjiguan");
    jg_machine_yunpingqinglong->addSkill(new JGMojian);
    jg_machine_yunpingqinglong->addSkill(new JGMojianProhibit);
    related_skills.insertMulti("jgmojian", "#jgmojian-prohibit");

    Machine *jg_machine_jileibaihu = new Machine(this, "jg_machine_jileibaihu", "shu", 4, true, true);
    jg_machine_jileibaihu->addSkill("jgjiguan");
    jg_machine_jileibaihu->addSkill("zhenwei");
    jg_machine_jileibaihu->addSkill(new JGBenlei);

    Machine *jg_machine_lingjiaxuanwu = new Machine(this, "jg_machine_lingjiaxuanwu", "shu", 5, true, true);
    jg_machine_lingjiaxuanwu->addSkill("jgjiguan");
    jg_machine_lingjiaxuanwu->addSkill("yizhong");
    jg_machine_lingjiaxuanwu->addSkill(new JGLingyu);

    Machine *jg_machine_chiyuzhuque = new Machine(this, "jg_machine_chiyuzhuque", "shu", 5, true, true);
    jg_machine_chiyuzhuque->addSkill("jgjiguan");
    jg_machine_chiyuzhuque->addSkill("jgyuhuo");
    jg_machine_chiyuzhuque->addSkill(new JGTianyun);
}

ADD_PACKAGE(JianGeDefense)