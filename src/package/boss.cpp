#include "boss.h"
//#include "settings.h"
//#include "skill.h"
#include "standard.h"
//#include "client.h"
//#include "clientplayer.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
//#include "wrapped-card.h"

class BossGuimei : public ProhibitSkill
{
public:
    BossGuimei() : ProhibitSkill("bossguimei")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("DelayedTrick") && to->hasSkill(objectName());
    }
};

class BossDidong : public PhaseChangeSkill
{
public:
    BossDidong() : PhaseChangeSkill("bossdidong")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossdidong-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            player->turnOver();
        }
        return false;
    }
};

class BossDidong2 : public PhaseChangeSkill
{
public:
    BossDidong2() : PhaseChangeSkill("bossdidong")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		if (target->getPhase()==Player::Finish)
			return owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
		return false;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossdidong-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            player->turnOver();
        }
        return false;
    }
};

class BossShanbeng : public TriggerSkill
{
public:
    BossShanbeng() : TriggerSkill("bossshanbeng")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->hasSkill(objectName());
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (player != death.who) return false;

        bool sendLog = false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getEquips().isEmpty()) continue;
            if (!sendLog) {
                sendLog = true;
                room->sendCompulsoryTriggerLog(player, this);
            }
            p->throwAllEquips(objectName());
        }
        return false;
    }
};

class BossShanbeng2 : public TriggerSkill
{
public:
    BossShanbeng2() : TriggerSkill("bossshanbeng")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==Death){
			DeathStruct death = data.value<DeathStruct>();
			return owner==target&&owner==death.who&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		room->sendCompulsoryTriggerLog(player, this);
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getEquips().isEmpty()) continue;
            room->doAnimate(1,player->objectName(),p->objectName());
        }
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getEquips().isEmpty()) continue;
            p->throwAllEquips(objectName());
        }
        return false;
    }
};

class BossBeiming : public TriggerSkill
{
public:
    BossBeiming() :TriggerSkill("bossbeiming")
    {
        events << Death;
        frequency = Compulsory;
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
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (killer && killer != player) {
            LogMessage log;
            log.type = "#BeimingThrow";
            log.from = player;
            log.to << killer;
            log.arg = objectName();
            room->sendLog(log);

            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());

            killer->throwAllHandCards(objectName());
        }

        return false;
    }
};

class BossBeiming2 : public TriggerSkill
{
public:
    BossBeiming2() :TriggerSkill("bossbeiming")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==Death){
			DeathStruct death = data.value<DeathStruct>();
			return owner==target&&owner==death.who&&death.damage&&death.damage->from
			&&death.damage->from!=owner&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
		LogMessage log;
		log.type = "#BeimingThrow";
		log.from = player;
		log.to << death.damage->from;
		log.arg = objectName();
		room->sendLog(log);

		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		death.damage->from->throwAllHandCards(objectName());
        return false;
    }
};

class BossLuolei : public PhaseChangeSkill
{
public:
    BossLuolei() : PhaseChangeSkill("bossluolei")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossluolei-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 1, DamageStruct::Thunder));
        }
        return false;
    }
};

class BossLuolei2 : public PhaseChangeSkill
{
public:
    BossLuolei2() : PhaseChangeSkill("bossluolei")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Start&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossluolei-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 1, DamageStruct::Thunder));
        }
        return false;
    }
};

class BossGuihuo : public PhaseChangeSkill
{
public:
    BossGuihuo() : PhaseChangeSkill("bossguihuo")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossguihuo-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class BossGuihuo2 : public PhaseChangeSkill
{
public:
    BossGuihuo2() : PhaseChangeSkill("bossguihuo")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Start&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        ServerPlayer *player = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(), "bossguihuo-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class BossMingbao : public TriggerSkill
{
public:
    BossMingbao() : TriggerSkill("bossmingbao")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->hasSkill(objectName());
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (player != death.who) return false;

        room->sendCompulsoryTriggerLog(player, this);

        foreach(ServerPlayer *p, room->getOtherPlayers(player))
            room->damage(DamageStruct(objectName(), nullptr, p, 1, DamageStruct::Fire));
        return false;
    }
};

class BossMingbao2 : public TriggerSkill
{
public:
    BossMingbao2() : TriggerSkill("bossmingbao")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==Death){
			DeathStruct death = data.value<DeathStruct>();
			return owner==target&&owner==death.who&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->sendCompulsoryTriggerLog(player, this);

        foreach(ServerPlayer *p, room->getOtherPlayers(player))
            room->damage(DamageStruct(objectName(), nullptr, p, 1, DamageStruct::Fire));
        return false;
    }
};

class BossBaolian : public PhaseChangeSkill
{
public:
    BossBaolian() : PhaseChangeSkill("bossbaolian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        room->sendCompulsoryTriggerLog(target, this);

        target->drawCards(2, objectName());
        return false;
    }
};

class BossBaolian2 : public PhaseChangeSkill
{
public:
    BossBaolian2() : PhaseChangeSkill("bossbaolian")
    {
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Finish&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        room->sendCompulsoryTriggerLog(target, this);

        target->drawCards(2, objectName());
        return false;
    }
};

class BossManjia : public ViewAsEquipSkill
{
public:
    BossManjia() : ViewAsEquipSkill("bossmanjia")
    {
    }

    QString viewAsEquip(const Player *target) const
    {
        if (target->hasEquipArea(1) && !target->getArmor())
            return "vine";
        return "";
    }
};

class BossXiaoshou : public PhaseChangeSkill
{
public:
    BossXiaoshou() : PhaseChangeSkill("bossxiaoshou")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHp() > target->getHp())
                players << p;
        }
        ServerPlayer *player = room->askForPlayerChosen(target, players, objectName(), "bossxiaoshou-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 2));
        }
        return false;
    }
};

class BossXiaoshou2 : public PhaseChangeSkill
{
public:
    BossXiaoshou2() : PhaseChangeSkill("bossxiaoshou")
    {
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
        if(owner==target&&target->getPhase()==Player::Finish&&owner->isAlive()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getHp()>owner->getHp())
					return owner->hasSkill(objectName());
			}
		}
		return false;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHp() > target->getHp())
                players << p;
        }
        ServerPlayer *player = room->askForPlayerChosen(target, players, objectName(), "bossxiaoshou-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), target, player, 2));
        }
        return false;
    }
};

class BossGuiji : public TriggerSkill
{
public:
    BossGuiji() : TriggerSkill("bossguiji")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Start || player->getJudgingArea().isEmpty())
            return false;

        room->sendCompulsoryTriggerLog(player, this);

        QList<const Card *> dtricks = player->getJudgingArea();
        int index = qrand() % dtricks.length();
        room->throwCard(dtricks.at(index), objectName(), nullptr, player);
        return false;
    }
};

class BossGuiji2 : public TriggerSkill
{
public:
    BossGuiji2() : TriggerSkill("bossguiji")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
		if (event==EventPhaseEnd){
			return owner==target&&target->getPhase()==Player::Start&&!target->getJudgingArea().isEmpty()&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->sendCompulsoryTriggerLog(player, this);
        QList<const Card *> dtricks = player->getJudgingArea();
        int index = qrand() % dtricks.length();
        room->throwCard(dtricks.at(index), objectName(), nullptr, player);
        return false;
    }
};

class BossLianyu : public PhaseChangeSkill
{
public:
    BossLianyu() : PhaseChangeSkill("bosslianyu")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish)
            return false;

        if (room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach(ServerPlayer *p, room->getOtherPlayers(target))
                room->damage(DamageStruct(objectName(), target, p, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class BossLianyu2 : public PhaseChangeSkill
{
public:
    BossLianyu2() : PhaseChangeSkill("bosslianyu")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Finish&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach(ServerPlayer *p, room->getOtherPlayers(target))
                room->damage(DamageStruct(objectName(), target, p, 1, DamageStruct::Fire));
        }
        return false;
    }
};

class BossTaiping : public DrawCardsSkill
{
public:
    BossTaiping() : DrawCardsSkill("bosstaiping")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        Room *room = player->getRoom();

        room->sendCompulsoryTriggerLog(player, this);

        return n + 2;
    }
};

class BossSuoming : public PhaseChangeSkill
{
public:
    BossSuoming() : PhaseChangeSkill("bosssuoming")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        QList<ServerPlayer *> to_chain;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (!p->isChained())
                to_chain << p;
        }

        if (!to_chain.isEmpty() && room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach (ServerPlayer *p, to_chain) {
                if (p->isChained()) continue;
                room->setPlayerChained(p);
            }
        }
        return false;
    }
};

class BossSuoming2 : public PhaseChangeSkill
{
public:
    BossSuoming2() : PhaseChangeSkill("bosssuoming")
    {
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
        if(owner==target&&target->getPhase()==Player::Finish&&owner->isAlive()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->isChained()||p==owner) continue;
				return owner->hasSkill(objectName());
			}
		}
		return false;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (p->isChained()) continue;
                room->setPlayerChained(p);
            }
        }
        return false;
    }
};

class BossXixing : public PhaseChangeSkill
{
public:
    BossXixing() : PhaseChangeSkill("bossxixing")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        QList<ServerPlayer *> chain;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (p->isChained()) chain << p;
        }
        ServerPlayer *player = room->askForPlayerChosen(target, chain, objectName(), "bossxixing-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            target->setFlags("bossxixing");
            try {
                room->damage(DamageStruct(objectName(), target, player, 1, DamageStruct::Thunder));
                if (target->isAlive() && target->hasFlag("bossxixing")) {
                    target->setFlags("-bossxixing");
                    if (target->isWounded())
                        room->recover(target, RecoverStruct(objectName(), target));
                }
            }
            catch (TriggerEvent triggerEvent) {
                if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                    target->setFlags("-bossxixing");
                throw triggerEvent;
            }
        }
        return false;
    }
};

class BossXixing2 : public PhaseChangeSkill
{
public:
    BossXixing2() : PhaseChangeSkill("bossxixing")
    {
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
        if(owner==target&&target->getPhase()==Player::Start&&owner->isAlive()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!p->isChained()||p==owner) continue;
				return owner->hasSkill(objectName());
			}
		}
		return false;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        QList<ServerPlayer *> chain;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isChained()) chain << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(player, chain, objectName(), "bossxixing-invoke", true, true);
        if (target) {
            room->broadcastSkillInvoke(objectName());
			room->damage(DamageStruct(objectName(), player, target, 1, DamageStruct::Thunder));
			if (player->isAlive() && player->isWounded())
				room->recover(player, RecoverStruct(objectName(), player));
        }
        return false;
    }
};

class BossQiangzheng : public PhaseChangeSkill
{
public:
    BossQiangzheng() : PhaseChangeSkill("bossqiangzheng")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;

        bool can_invoke = false;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (!p->isKongcheng()) {
                can_invoke = true;
                break;
            }
        }

        if (can_invoke&&room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach (ServerPlayer *p, room->getOtherPlayers(target))
				room->doAnimate(1,target->objectName(),p->objectName());
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (p->isAlive() && !p->isKongcheng()) {
                    int card_id = room->askForCardChosen(target, p, "h", "bossqiangzheng");

                    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName());
                    room->obtainCard(target, Sanguosha->getCard(card_id), reason, false);
					if(target->isDead()) break;
                }
            }
        }
        return false;
    }
};

class BossQiangzheng2 : public PhaseChangeSkill
{
public:
    BossQiangzheng2() : PhaseChangeSkill("bossqiangzheng")
    {
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
        if(owner==target&&target->getPhase()==Player::Finish&&owner->isAlive()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->isKongcheng()||p==owner) continue;
				return owner->hasSkill(objectName());
			}
		}
		return false;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (room->askForSkillInvoke(target, objectName()+"$-1")) {
            foreach (ServerPlayer *p, room->getOtherPlayers(target))
				room->doAnimate(1,target->objectName(),p->objectName());
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (p->isAlive() && !p->isKongcheng()) {
                    int card_id = room->askForCardChosen(target, p, "h", "bossqiangzheng");

                    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName());
                    room->obtainCard(target, Sanguosha->getCard(card_id), reason, false);
					if(target->isDead()) break;
                }
            }
        }
        return false;
    }
};

class BossZuijiu : public TriggerSkill
{
public:
    BossZuijiu() : TriggerSkill("bosszuijiu")
    {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")) {
            LogMessage log;
            log.type = "#ZuijiuBuff";
            log.from = player;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());

            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

class BossZuijiu2 : public TriggerSkill
{
public:
    BossZuijiu2() : TriggerSkill("bosszuijiu")
    {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			return damage.card&&damage.card->isKindOf("Slash")&&owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
		LogMessage log;
		log.type = "#ZuijiuBuff";
		log.from = player;
		log.to << damage.to;
		log.arg = QString::number(damage.damage);
		log.arg2 = QString::number(++damage.damage);
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		data = QVariant::fromValue(damage);
        return false;
    }
};

class BossModao : public PhaseChangeSkill
{
public:
    BossModao() : PhaseChangeSkill("bossmodao")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Start) return false;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());

        target->drawCards(2, objectName());
        return false;
    }
};

class BossModao2 : public PhaseChangeSkill
{
public:
    BossModao2() : PhaseChangeSkill("bossmodao")
    {
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Start&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        room->sendCompulsoryTriggerLog(target, this);

        target->drawCards(2, objectName());
        return false;
    }
};

class BossQushou : public PhaseChangeSkill
{
public:
    BossQushou() : PhaseChangeSkill("bossqushou")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Play) return false;

        SavageAssault *sa = new SavageAssault(Card::NoSuit, 0);
        sa->setSkillName(objectName());
        if (sa->isAvailable(target)&&target->askForSkillInvoke(objectName(),false)) {
            room->useCard(CardUseStruct(sa, target));
        }
		sa->deleteLater();
        return false;
    }
};

class BossQushou2 : public PhaseChangeSkill
{
public:
    BossQushou2() : PhaseChangeSkill("bossqushou")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Play&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        SavageAssault *sa = new SavageAssault(Card::NoSuit, 0);
        sa->setSkillName(objectName());
        if (sa->isAvailable(target)&&room->askForSkillInvoke(target, objectName(),false)) {
            room->useCard(CardUseStruct(sa, target));
        }
		sa->deleteLater();
        return false;
    }
};

class BossMojian : public PhaseChangeSkill
{
public:
    BossMojian() : PhaseChangeSkill("bossmojian")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Play) return false;

        ArcheryAttack *aa = new ArcheryAttack(Card::NoSuit, 0);
        aa->setSkillName(objectName());
        if (aa->isAvailable(target)&&target->askForSkillInvoke(objectName(),false)) {
            room->useCard(CardUseStruct(aa, target, QList<ServerPlayer *>()));
        }
		aa->deleteLater();
        return false;
    }
};

class BossMojian2 : public PhaseChangeSkill
{
public:
    BossMojian2() : PhaseChangeSkill("bossmojian")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Play&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        ArcheryAttack *aa = new ArcheryAttack(Card::NoSuit, 0);
        aa->setSkillName(objectName());
        if (aa->isAvailable(target)&&room->askForSkillInvoke(target, objectName(),false)) {
            room->useCard(CardUseStruct(aa, target, QList<ServerPlayer *>()));
        }
		aa->deleteLater();
        return false;
    }
};

class BossDanshu : public TriggerSkill
{
public:
    BossDanshu() : TriggerSkill("bossdanshu")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player != move.from || player->hasFlag("CurrentPlayer")
            || (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip)))
            return false;
        if (room->askForSkillInvoke(player, objectName(), data)) {
            JudgeStruct judge;
            judge.who = player;
            judge.reason = objectName();
            judge.good = true;
            judge.pattern = ".|red";
            room->judge(judge);

            if (judge.isGood() && player->isAlive() && player->isWounded())
                room->recover(player, RecoverStruct(objectName(), player));
        }
        return false;
    }
};

class BossDanshu2 : public TriggerSkill
{
public:
    BossDanshu2() : TriggerSkill("bossdanshu")
    {
        events << CardsMoveOneTime;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			return move.from==owner&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
			&&owner!=target&&owner->isAlive()&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (room->askForSkillInvoke(player, objectName(), data)) {
            JudgeStruct judge;
            judge.who = player;
            judge.reason = objectName();
            judge.good = true;
            judge.pattern = ".|red";
            room->judge(judge);

            if (judge.isGood() && player->isAlive() && player->isWounded())
                room->recover(player, RecoverStruct(objectName(), player));
        }
        return false;
    }
};


BossModePackage::BossModePackage()
    : Package("~BossMode")
{
    General *chi = new General(this, "boss_chi", "qun", 5, true, true);
    chi->addSkill(new BossGuimei);
    chi->addSkill(new BossDidong);
    chi->addSkill(new BossShanbeng);

    General *mei = new General(this, "boss_mei", "qun", 5, false, true);
    mei->addSkill("bossguimei");
    mei->addSkill("nosenyuan");
    mei->addSkill(new BossBeiming);

    General *wang = new General(this, "boss_wang", "qun", 5, true, true);
    wang->addSkill("bossguimei");
    wang->addSkill(new BossLuolei);
    wang->addSkill("huilei");

    General *liang = new General(this, "boss_liang", "qun", 5, false, true);
    liang->addSkill("bossguimei");
    liang->addSkill(new BossGuihuo);
    liang->addSkill(new BossMingbao);

    General *niutou = new General(this, "boss_niutou", "qun", 10, true, true);
    niutou->addSkill(new BossBaolian);
    niutou->addSkill("mengjin");
    niutou->addSkill(new BossManjia);
    niutou->addSkill(new BossXiaoshou);

    General *mamian = new General(this, "boss_mamian", "qun", 9, true, true);
    mamian->addSkill(new BossGuiji);
    mamian->addSkill("nosfankui");
    mamian->addSkill(new BossLianyu);
    mamian->addSkill("nosjuece");

    General *heiwuchang = new General(this, "boss_heiwuchang", "qun", 15, true, true);
    heiwuchang->addSkill("bossguiji");
    heiwuchang->addSkill(new BossTaiping);
    heiwuchang->addSkill(new BossSuoming);
    heiwuchang->addSkill(new BossXixing);

    General *baiwuchang = new General(this, "boss_baiwuchang", "qun", 18, true, true);
    baiwuchang->addSkill("bossbaolian");
    baiwuchang->addSkill(new BossQiangzheng);
    baiwuchang->addSkill(new BossZuijiu);
    baiwuchang->addSkill("nosjuece");

    General *luocha = new General(this, "boss_luocha", "qun", 20, false, true);
    luocha->addSkill(new BossModao);
    luocha->addSkill(new BossQushou);
    luocha->addSkill("yizhong");
    luocha->addSkill("kuanggu");

    General *yecha = new General(this, "boss_yecha", "qun", 18, true, true);
    yecha->addSkill("bossmodao");
    yecha->addSkill(new BossMojian);
    yecha->addSkill("bazhen");
    yecha->addSkill(new BossDanshu);
}
ADD_PACKAGE(BossMode)

class Shenqu : public TriggerSkill
{
public:
    Shenqu() : TriggerSkill("shenqu")
    {
        events << EventPhaseStart << Damaged;
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill(objectName()) || p->getHandcardNum() > p->getMaxHp()) continue;
                if (!p->askForSkillInvoke(objectName()+"$-1")) continue;
                p->drawCards(2, objectName());
            }
        } else {
            if (!player->hasSkill(objectName())) return false;
            Card *peach = new Peach(Card::NoSuit, 0);
            peach->deleteLater();
            if (peach->isAvailable(player)){
				const Card *card = room->askForCard(player, "Peach", "@shenqu-peach", data, Card::MethodUse, nullptr, true);
				if (!card) return false;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				room->useCard(CardUseStruct(card, player, player));
			}
        }
        return false;
    }
};

class Shenqu2 : public TriggerSkill
{
public:
    Shenqu2() : TriggerSkill("shenqu")
    {
        events << EventPhaseStart << Damaged;
        frequency = Frequent;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
		if (event==EventPhaseStart){
			return target->getPhase()==Player::RoundStart&&owner->getHandcardNum()<=owner->getMaxHp()&&owner->isAlive()&&owner->hasSkill(objectName());
		}else if (event==Damaged){
            Card *peach = new Peach(Card::NoSuit, 0);
            peach->deleteLater();
			return target==owner&&owner->isAlive()&&peach->isAvailable(owner)&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,ServerPlayer*owner) const
    {
        if (event == EventPhaseStart) {
			if (owner->askForSkillInvoke(objectName()+"$-1"))
				owner->drawCards(2, objectName());
        } else {
			const Card *card = room->askForCard(player, "Peach", "@shenqu-peach", data, Card::MethodUse, nullptr, true);
			if (!card) return false;
			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			room->useCard(CardUseStruct(card, player));
        }
        return false;
    }
};

JiwuCard::JiwuCard()
{
    target_fixed = true;
}

void JiwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList skills;
    if (!source->hasSkill("wansha", true))
        skills << "wansha";
    if (!source->hasSkill("lieren", true))
        skills << "lieren";
    if (!source->hasSkill("qiangxi", true))
        skills << "qiangxi";
    if (!source->hasSkill("xuanfeng", true))
        skills << "xuanfeng";
    if (!source->hasSkill("tieji", true))
        skills << "tieji";
    if (skills.isEmpty()) return;
    QString skill = room->askForChoice(source, "jiwu", skills.join("+"));
    room->acquireOneTurnSkills(source, "jiwu", skill);
}

class Jiwu : public OneCardViewAsSkill
{
public:
    Jiwu() : OneCardViewAsSkill("jiwu")
    {
        filter_pattern = ".|.|.|hand!";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        JiwuCard *c = new JiwuCard;
        c->addSubcard(originalCard);
        return c;
    }
};

class Xiuluo : public PhaseChangeSkill
{
public:
    Xiuluo() : PhaseChangeSkill("xiuluo")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->canDiscard(target, "h")
            && hasDelayedTrick(target);
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        while (hasDelayedTrick(target) && target->canDiscard(target, "h")) {
            QStringList suits;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!suits.contains(jcard->getSuitString()))
                    suits << jcard->getSuitString();
            }

            const Card *card = room->askForCard(target, QString(".|%1|.|hand").arg(suits.join(",")), "@xiuluo", QVariant(), objectName());
            if (!card) break;
            room->broadcastSkillInvoke(objectName());

            QList<int> avail_list, other_list;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!jcard->isKindOf("DelayedTrick")) continue;
                if (jcard->getSuit() == card->getSuit())
                    avail_list << jcard->getEffectiveId();
                else
                    other_list << jcard->getEffectiveId();
            }
            room->fillAG(avail_list + other_list, target, other_list);
            int id = room->askForAG(target, avail_list, false, objectName());
            room->clearAG(target);
            room->throwCard(id, objectName(),nullptr);
        }

        return false;
    }

private:
    static bool hasDelayedTrick(const ServerPlayer *target)
    {
        foreach(const Card *card, target->getJudgingArea())
            if (card->isKindOf("DelayedTrick")) return true;
        return false;
    }
};

class Xiuluo2 : public PhaseChangeSkill
{
public:
    Xiuluo2() : PhaseChangeSkill("xiuluo")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant) const
    {
		return owner==target&&target->getPhase()==Player::Start&&target->canDiscard(target, "h")
		&&hasDelayedTrick(target)&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        while (hasDelayedTrick(target) && target->canDiscard(target, "h")) {
            QStringList suits;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!suits.contains(jcard->getSuitString()))
                    suits << jcard->getSuitString();
            }

            const Card *card = room->askForCard(target, QString(".|%1|.|hand").arg(suits.join(",")), "@xiuluo", QVariant(), objectName());
            if (!card) break;
            room->broadcastSkillInvoke(objectName());

            QList<int> avail_list, other_list;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!jcard->isKindOf("DelayedTrick")) continue;
                if (jcard->getSuit() == card->getSuit())
                    avail_list << jcard->getEffectiveId();
                else
                    other_list << jcard->getEffectiveId();
            }
            room->fillAG(avail_list + other_list, target, other_list);
            int id = room->askForAG(target, avail_list, false, objectName());
            room->clearAG(target);
            room->throwCard(id, objectName(),nullptr);
        }

        return false;
    }

private:
    static bool hasDelayedTrick(const ServerPlayer *target)
    {
        foreach(const Card *card, target->getJudgingArea())
            if (card->isKindOf("DelayedTrick")) return true;
        return false;
    }
};

class Shenwei : public DrawCardsSkill
{
public:
    Shenwei() : DrawCardsSkill("shenwei")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        Room *room = player->getRoom();
        room->sendCompulsoryTriggerLog(player, this);
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!player->isYourFriend(p))
				n++;
		}
        return n;
    }
};

class ShenweiKeep : public MaxCardsSkill
{
public:
    ShenweiKeep() : MaxCardsSkill("#shenwei")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("shenwei")){
			int n = 0;
			foreach (const Player *p, target->getAliveSiblings()) {
				if (!target->isYourFriend(p))
					n++;
			}
            return n;
		}
        return 0;
    }
};

class Shenji : public TargetModSkill
{
public:
    Shenji() : TargetModSkill("shenji")
    {
        frequency = NotCompulsory;
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        if (from->hasSkill(objectName()))
            return 2;
        return 0;
    }
    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill(objectName()))
			return 1;
        return 0;
    }
};

class Jingjia : public TriggerSkill
{
public:
    Jingjia() : TriggerSkill("jingjia")
    {
        events << GameStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->sendCompulsoryTriggerLog(player,this);
		foreach (int id, Sanguosha->getRandomCards()) {
			if(room->getCardOwner(id)) continue;
			const Card *c = Sanguosha->getCard(id);
			if (c->objectName()=="wushuangji"||c->objectName()=="baihuapao"||c->objectName()=="shimandai"||c->objectName()=="zijinguan"){
				const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
				if(player->getEquip(equip->location())) continue;
				room->moveCardTo(c,nullptr,Player::PlaceTable);
				c->use(room,player,QList<ServerPlayer *>() << player);
			}
		}
        return false;
    }
};

class BossAozhan : public TriggerSkill
{
public:
    BossAozhan() : TriggerSkill("bossaozhan")
    {
        events << DamageForseen << EventPhaseChanging << DrawNCards;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.damage>1) {
				foreach (const Card *e, player->getEquips()) {
					if (e->isKindOf("Armor")){
						room->sendCompulsoryTriggerLog(player,this);
						return true;
					}
				}
			}
		}else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::Judge){
				foreach (const Card *e, player->getEquips()) {
					if (e->isKindOf("Treasure")){
						room->sendCompulsoryTriggerLog(player,this);
						player->skip(Player::Judge);
						return false;
					}
				}
			}
		}else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
            if (draw.reason=="draw_phase"){
				foreach (const Card *e, player->getEquips()) {
					if (e->isKindOf("Horse")){
						room->sendCompulsoryTriggerLog(player,this);
						draw.num += 1;
						data = QVariant::fromValue(draw);
						return false;
					}
				}
			}
		}
        return false;
    }
};

class BossAozhan2 : public TriggerSkill
{
public:
    BossAozhan2() : TriggerSkill("bossaozhan")
    {
        events << DamageForseen << EventPhaseChanging << DrawNCards;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>1&&target==owner&&owner->isAlive()){
				foreach (const Card *e, owner->getEquips()) {
					if (e->isKindOf("Armor"))
						return owner->hasSkill(objectName());
				}
			}
		}else if (event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to == Player::Judge&&target==owner&&owner->isAlive()){
				foreach (const Card *e, owner->getEquips()) {
					if (e->isKindOf("Treasure"))
						return owner->hasSkill(objectName());
				}
			}
		}else if (event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason=="draw_phase"&&target==owner&&owner->isAlive()){
				foreach (const Card *e, owner->getEquips()) {
					if (e->isKindOf("Horse"))
						return owner->hasSkill(objectName());
				}
			}
		}
		return false;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==DamageForseen){
			room->sendCompulsoryTriggerLog(player,this);
			return true;
		}else if(event==EventPhaseChanging){
			room->sendCompulsoryTriggerLog(player,this);
			player->skip(Player::Judge);
		}else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			draw.num += 1;
			data.setValue(draw);
		}
        return false;
    }
};

class BossAozhanMod : public TargetModSkill
{
public:
    BossAozhanMod() : TargetModSkill("#bossaozhan-mod")
    {
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill("bossaozhan")){
			foreach (const Card *e, from->getEquips()) {
				if (e->isKindOf("Weapon"))
					return 1;
			}
		}
        return 0;
    }
};


class WushuangjiSkill: public WeaponSkill {
public:
    WushuangjiSkill(): WeaponSkill("wushuangji")
	{
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card&&damage.card->isKindOf("Slash")) {
			CardUseStruct use = room->getUseStruct(damage.card);
			if(use.to.contains(damage.to)&&player->askForSkillInvoke(this,damage.to)){
				room->setEmotion(player, "weapon/"+objectName());
				if(player->canDiscard(damage.to,"he")){
					int id = room->askForCardChosen(player,damage.to,"he",objectName(),false,Card::MethodDiscard,QList<int>(),true);
					if(id>-1){
						room->throwCard(id,objectName(),damage.to,player);
						return false;
					}
				}
				player->drawCards(1,objectName());
			}
		}
		return false;
    }
};

class WushuangjiSkill2 : public WeaponSkill {
public:
    WushuangjiSkill2(): WeaponSkill("wushuangji")
	{
        events << Damage;
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&target==owner&&owner->isAlive()){
				CardUseStruct use = room->getUseStruct(damage.card);
				return use.to.contains(damage.to)&&WeaponSkill::triggerable(owner);
			}
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
		if(player->askForSkillInvoke(this,damage.to)){
			room->setEmotion(player, "weapon/"+objectName());
			if(player->canDiscard(damage.to,"he")){
				int id = room->askForCardChosen(player,damage.to,"he",objectName(),false,Card::MethodDiscard,QList<int>(),true);
				if(id>-1){
					room->throwCard(id,objectName(),damage.to,player);
					return false;
				}
			}
			player->drawCards(1,objectName());
		}
		return false;
    }
};

Wushuangji::Wushuangji(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("wushuangji");
}

class BaihuapaoSkill : public ArmorSkill {
public:
    BaihuapaoSkill(): ArmorSkill("baihuapao") {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature != DamageStruct::Normal) {
			room->setEmotion(player, "armor/"+objectName());
			room->sendCompulsoryTriggerLog(player, this);
			return player->damageRevises(data,-damage.damage);
		}
		return false;
    }
};

class BaihuapaoSkill2 : public ArmorSkill {
public:
    BaihuapaoSkill2(): ArmorSkill("baihuapao") {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.nature != DamageStruct::Normal&&target==owner&&owner->isAlive()){
				return ArmorSkill::triggerable(owner);
			}
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
        DamageStruct damage = data.value<DamageStruct>();
		room->setEmotion(player, "armor/"+objectName());
		room->sendCompulsoryTriggerLog(player, this);
		return player->damageRevises(data,-damage.damage);
    }
};

Baihuapao::Baihuapao(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("baihuapao");
}

class ShimandaiSkill: public ArmorSkill {
public:
    ShimandaiSkill(): ArmorSkill("shimandai")
	{
        events << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
	{
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const{
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId()>0&&use.to.size()==1) {
			foreach (ServerPlayer *p, use.to) {
				if(p!=use.from&&p->hasArmorEffect(objectName())&&p->askForSkillInvoke(this,data)){
					room->setEmotion(p, "armor/"+objectName());
					JudgeStruct judge;
					judge.who = p;
					judge.reason = objectName();
					judge.good = true;
					judge.pattern = ".|heart";
					room->judge(judge);
					if (judge.isGood())
						use.nullified_list << p->objectName();
					data.setValue(use);
				}
			}
		}
		return false;
    }
};

class ShimandaiSkill2 : public ArmorSkill {
public:
    ShimandaiSkill2(): ArmorSkill("shimandai")
	{
        events << TargetSpecified;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.size()==1&&target!=owner&&owner->isAlive()){
				return use.to.contains(owner)&&ArmorSkill::triggerable(owner);
			}
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data,ServerPlayer*owner) const{
		if(owner->askForSkillInvoke(this,data)){
			room->setEmotion(owner, "armor/"+objectName());
			JudgeStruct judge;
			judge.who = owner;
			judge.reason = objectName();
			judge.good = true;
			judge.pattern = ".|heart";
			room->judge(judge);
			CardUseStruct use = data.value<CardUseStruct>();
			if (judge.isGood()) use.nullified_list << owner->objectName();
			data.setValue(use);
		}
		return false;
    }
};

Shimandai::Shimandai(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("shimandai");
}

class ZijinguanSkill : public TreasureSkill
{
public:
    ZijinguanSkill() : TreasureSkill("zijinguan")
    {
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		if (player->getPhase()!=Player::Start) return false;
        ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"zijinguan0:",true,true);
        if (to){
			room->setEmotion(player, "treasure/"+objectName());
			room->damage(DamageStruct(objectName(),player,to));
		}
        return false;
    }
};

class ZijinguanSkill2 : public TreasureSkill
{
public:
    ZijinguanSkill2() : TreasureSkill("zijinguan")
    {
        events << EventPhaseStart;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
		if (event==EventPhaseStart){
			if(target->getPhase()==Player::Start&&target==owner&&owner->isAlive()){
				return TreasureSkill::triggerable(owner);
			}
		}
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"zijinguan0:",true,true);
        if (to){
			room->setEmotion(player, "treasure/"+objectName());
			room->damage(DamageStruct(objectName(),player,to));
		}
        return false;
    }
};

Zijinguan::Zijinguan(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("zijinguan");
}

Lianjunshengyan::Lianjunshengyan(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("lianjunshengyan");
}

void Lianjunshengyan::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if(effect.from==effect.to){
		CardUseStruct use = room->getUseStruct(this);
		effect.from->drawCards(use.to.length(),objectName());
	}else{
		if(effect.to->getLostHp()>0&&room->askForChoice(effect.to,objectName(),"r+d")=="r"){
			room->recover(effect.to,RecoverStruct(effect.from,this));
		}else
			effect.to->drawCards(1,objectName());
	}
}

HulaoPassPackage::HulaoPassPackage()
    : Package("HulaoPass")
{
    General *shenlvbu1 = new General(this, "shenlvbu1", "god", 8, true, true); // SP 008 (2-1)
    shenlvbu1->addSkill("mashu");
    shenlvbu1->addSkill("wushuang");
    shenlvbu1->addSkill(new Jingjia);
    shenlvbu1->addSkill(new BossAozhan);
    shenlvbu1->addSkill(new BossAozhanMod);
    related_skills.insertMulti("bossaozhan", "#bossaozhan-mod");

    General *shenlvbu2 = new General(this, "shenlvbu2", "god", 4, true, true); // SP 008 (2-2)
    shenlvbu2->addSkill("mashu");
    shenlvbu2->addSkill("wushuang");
    shenlvbu2->addSkill(new Xiuluo);
    shenlvbu2->addSkill(new ShenweiKeep);
    shenlvbu2->addSkill(new Shenwei);
    shenlvbu2->addSkill(new Shenji);
    related_skills.insertMulti("shenwei", "#shenwei-draw");

    General *shenlvbu3 = new General(this, "shenlvbu3", "god", 4, true, true);
    shenlvbu3->addSkill("wushuang");
    shenlvbu3->addSkill(new Shenqu);
    shenlvbu3->addSkill(new Jiwu);
    shenlvbu3->addRelateSkill("wansha");
    shenlvbu3->addRelateSkill("lieren");
    shenlvbu3->addRelateSkill("qiangxi");
    shenlvbu3->addRelateSkill("xuanfeng");
    shenlvbu3->addRelateSkill("tieji");
	addMetaObject<JiwuCard>();

    QList<Card *> cards;
    cards << new Wushuangji(Card::Diamond, 12)
		<< new Baihuapao(Card::Diamond, 1)
		<< new Shimandai(Card::Spade, 2)
		<< new Shimandai(Card::Club, 2)
		<< new Zijinguan(Card::Club, 1)
		<< new Lianjunshengyan(Card::Heart, 1)
		<< new Lianjunshengyan(Card::Heart, 3)
		<< new Lianjunshengyan(Card::Heart, 4);

    foreach (Card *card, cards)
        card->setParent(this);

    skills << new WushuangjiSkill << new BaihuapaoSkill << new ShimandaiSkill << new ZijinguanSkill;

}
ADD_PACKAGE(HulaoPass)