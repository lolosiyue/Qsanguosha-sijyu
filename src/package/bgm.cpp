#include "bgm.h"
//#include "skill.h"
//#include "standard.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
#include "standard-generals.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

class Kuiwei : public TriggerSkill
{
public:
    Kuiwei() : TriggerSkill("kuiwei")
    {
        events << EventPhaseStart;
    }

    static int getWeaponCount(ServerPlayer *caoren)
    {
        int n = 0;
        foreach (ServerPlayer *p, caoren->getRoom()->getAlivePlayers()) {
            if (p->getWeapon()) n++;
        }
        return n;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive()
            && (target->hasSkill(objectName()) || target->getMark("@kuiwei") > 0);
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caoren, QVariant &) const
    {
        if (caoren->getPhase() == Player::Finish) {
            if (!caoren->hasSkill(objectName())) return false;
            if (!caoren->askForSkillInvoke(objectName()+"$-1"))
                return false;

            int n = getWeaponCount(caoren);
            caoren->drawCards(n + 2, objectName());
            caoren->turnOver();

            if (caoren->getMark("@kuiwei") == 0)
                room->addPlayerMark(caoren, "@kuiwei");
        } else if (caoren->getPhase() == Player::Draw) {
            if (caoren->getMark("@kuiwei") == 0)
                return false;
            room->removePlayerMark(caoren, "@kuiwei");
            int n = getWeaponCount(caoren);
            if (n > 0) {
                LogMessage log;
                log.type = "#KuiweiDiscard";
                log.from = caoren;
                log.arg = QString::number(n);
                log.arg2 = objectName();
                room->sendLog(log);

                room->askForDiscard(caoren, objectName(), n, n, false, true);
            }
        }
        return false;
    }
};

class Kuiwei2 : public TriggerSkill
{
public:
    Kuiwei2() : TriggerSkill("kuiwei")
    {
        events << EventPhaseStart;
    }

    static int getWeaponCount(Room *room)
    {
        int n = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getWeapon()) n++;
        }
        return n;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event==EventPhaseStart){
            if (target->getPhase()==Player::Finish)
				return owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
            if (target->getPhase()==Player::Draw&&target->getMark("@kuiwei")>0)
				return owner==target&&owner->isAlive();
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caoren, QVariant &,ServerPlayer*) const
    {
        if (caoren->getPhase() == Player::Finish) {
            if (caoren->askForSkillInvoke(objectName()+"$-1")){
				int n = getWeaponCount(room)+2;
				caoren->drawCards(n, objectName());
				caoren->turnOver();
				room->setPlayerMark(caoren, "@kuiwei",1);
			}
        } else if (caoren->getPhase() == Player::Draw) {
            room->removePlayerMark(caoren, "@kuiwei");
            int n = getWeaponCount(room);
            if (n > 0) {
                LogMessage log;
                log.type = "#KuiweiDiscard";
                log.from = caoren;
                log.arg = QString::number(n);
                log.arg2 = objectName();
                room->sendLog(log);
                room->askForDiscard(caoren, objectName(), n, n, false, true);
            }
        }
        return false;
    }
};

class Yanzheng : public OneCardViewAsSkill
{
public:
    Yanzheng() : OneCardViewAsSkill("yanzheng")
    {
        filter_pattern = ".|.|.|equipped";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "nullification" && player->getHandcardNum() > player->getHp();
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Nullification *ncard = new Nullification(originalCard->getSuit(), originalCard->getNumber());
        ncard->addSubcard(originalCard);
        ncard->setSkillName(objectName());
        return ncard;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        return player->getHandcardNum() > player->getHp() && !player->getEquips().isEmpty();
    }
};

class Manjuan : public TriggerSkill
{
public:
    Manjuan() : TriggerSkill("manjuan")
    {
        events << BeforeCardsMove;
        frequency = Frequent;
    }

    void doManjuan(ServerPlayer *sp_pangtong, int card_id) const
    {
        Room *room = sp_pangtong->getRoom();
        sp_pangtong->setFlags("ManjuanInvoke");
        QList<int> DiscardPile = room->getDiscardPile(), toGainList;
        const Card *card = Sanguosha->getCard(card_id);
        foreach (int id, DiscardPile) {
            const Card *cd = Sanguosha->getCard(id);
            if (cd->getNumber() == card->getNumber())
                toGainList << id;
        }
        if (toGainList.isEmpty()) return;

        room->fillAG(toGainList, sp_pangtong);
        int id = room->askForAG(sp_pangtong, toGainList, true, objectName());
        room->clearAG(sp_pangtong);
        if (id > -1)
            room->moveCardTo(Sanguosha->getCard(id), sp_pangtong, Player::PlaceHand, true);
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *sp_pangtong, QVariant &data) const
    {
        if (sp_pangtong->hasFlag("ManjuanInvoke")) {
            sp_pangtong->setFlags("-ManjuanInvoke");
            return false;
        }

        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (room->getTag("FirstRound").toBool()||move.to!=sp_pangtong||move.to_place!=Player::PlaceHand)
            return false;
        room->broadcastSkillInvoke(objectName());
        DummyCard *dummy = new DummyCard(move.card_ids);
        dummy->deleteLater();
        move.card_ids.clear();
        data = QVariant::fromValue(move);

        LogMessage log;
        log.type = "$ManjuanGot";
        log.from = sp_pangtong;
        log.card_str = ListI2S(dummy->getSubcards()).join("+");
        room->sendLog(log);
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, sp_pangtong->objectName(), "manjuan", "");
		room->moveCardTo(dummy, nullptr, nullptr, Player::DiscardPile, reason);

        if (!sp_pangtong->hasFlag("CurrentPlayer") || !sp_pangtong->askForSkillInvoke(this, data))
            return false;

        foreach (int _card_id, dummy->getSubcards()) {
            doManjuan(sp_pangtong, _card_id);
            if (!sp_pangtong->isAlive()) break;
        }
        return false;
    }
};

class Manjuan2 : public TriggerSkill
{
public:
    Manjuan2() : TriggerSkill("manjuan")
    {
        events << BeforeCardsMove;
        frequency = Frequent;
    }
    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==BeforeCardsMove){
            if (owner->hasFlag("ManjuanInvoke")||room->getTag("FirstRound").toBool()){
				owner->setFlags("-ManjuanInvoke");
				return false;
			}
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to==owner&&move.to_place==Player::PlaceHand)
				return owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    void doManjuan(ServerPlayer *sp_pangtong, int card_id) const
    {
        QList<int> toGainList;
        Room *room = sp_pangtong->getRoom();
        const Card *card = Sanguosha->getCard(card_id);
        foreach (int id, room->getDiscardPile()) {
            if (Sanguosha->getCard(id)->getNumber() == card->getNumber())
                toGainList << id;
        }
        if (toGainList.isEmpty()) return;
        room->fillAG(toGainList, sp_pangtong);
        int id = room->askForAG(sp_pangtong, toGainList, true, objectName());
        room->clearAG(sp_pangtong);
        if (id > -1){
			sp_pangtong->setFlags("ManjuanInvoke");
            room->moveCardTo(Sanguosha->getCard(id), sp_pangtong, Player::PlaceHand, true);
		}
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer*owner) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "$ManjuanGot";
        log.from = owner;
        log.card_str = ListI2S(move.card_ids).join("+");
        room->sendLog(log);
        room->notifySkillInvoked(owner, objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, owner->objectName(), "manjuan", "");
        DummyCard *dummy = new DummyCard(move.card_ids);
        move.card_ids.clear();
        data = QVariant::fromValue(move);
		room->moveCardTo(dummy, nullptr, nullptr, Player::DiscardPile, reason);
        dummy->deleteLater();
        if (player!=owner||!owner->askForSkillInvoke(this, data))
            return false;
        foreach (int id, dummy->getSubcards()) {
            doManjuan(player, id);
            if (!player->isAlive()) break;
        }
        return false;
    }
};

class Zuixiang : public TriggerSkill
{
public:
    Zuixiang() : TriggerSkill("zuixiang")
    {
        events << EventPhaseStart << CardEffected;
        limit_mark = "@sleep";
        frequency = Limited;
    }

    void doZuixiang(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->broadcastSkillInvoke("zuixiang");
        if (player->getPile("dream").isEmpty())
            room->doSuperLightbox(player, "zuixiang");

        QList<int> ids = room->getNCards(3, false);
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "zuixiang", ""));
        room->moveCardsAtomic(move, true);

        room->getThread()->delay();

        player->addToPile("dream", ids, true);

        QSet<int> numbers;
        ids = player->getPile("dream");
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            if (numbers.contains(card->getNumber())) {
				player->addMark("zuixiangHasTrigger");
	
				LogMessage log;
				log.type = "$ZuixiangGot";
				log.from = player;
	
				log.card_str = ListI2S(ids).join("+");
				room->sendLog(log);
	
				player->setFlags("ManjuanInvoke");
				CardsMoveStruct move(ids, player, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "zuixiang", ""));
				room->moveCardsAtomic(move, true);
                break;
            }
            numbers.insert(card->getNumber());
        }
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *sp_pangtong, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (sp_pangtong->getPhase() == Player::Start&&sp_pangtong->getMark("zuixiangHasTrigger")<1) {
                if (TriggerSkill::triggerable(sp_pangtong) && sp_pangtong->getMark("@sleep") > 0) {
                    if (sp_pangtong->askForSkillInvoke(this)){
						room->removePlayerMark(sp_pangtong, "@sleep");
						doZuixiang(sp_pangtong);
					}
                } else if (!sp_pangtong->getPile("dream").isEmpty())
                    doZuixiang(sp_pangtong);
            }
        } else if (triggerEvent == CardEffected && TriggerSkill::triggerable(sp_pangtong)) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
            foreach (int id, sp_pangtong->getPile("dream")) {
                if (Sanguosha->getCard(id)->getTypeId() == effect.card->getTypeId()) {
					LogMessage log;
					log.type = "#ZuiXiang2";
					log.from = effect.to;
					if (effect.from){
						log.to << effect.from;
						log.type = "#ZuiXiang1";
					}
					log.arg = effect.card->objectName();
					log.arg2 = objectName();
					room->sendLog(log);
					room->broadcastSkillInvoke(objectName());
					return true;
                }
            }
        }
        return false;
    }

private:
    QMap<Card::CardType, QString> type;
};

class Zuixiang2 : public TriggerSkill
{
public:
    Zuixiang2() : TriggerSkill("zuixiang")
    {
        events << EventPhaseStart << CardEffected;
        limit_mark = "@sleep";
        frequency = Limited;
    }

    void doZuixiang(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->broadcastSkillInvoke("zuixiang");
        if (player->getPile("dream").isEmpty())
            room->doSuperLightbox(player, "zuixiang");

        QList<int> ids = room->getNCards(3, false);
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "zuixiang", ""));
        room->moveCardsAtomic(move, true);

        room->getThread()->delay();

        player->addToPile("dream", ids, true);

        QSet<int> numbers;
        ids = player->getPile("dream");
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            if (numbers.contains(card->getNumber())) {
				player->addMark("zuixiangHasTrigger");
	
				LogMessage log;
				log.type = "$ZuixiangGot";
				log.from = player;
	
				log.card_str = ListI2S(ids).join("+");
				room->sendLog(log);
	
				player->setFlags("ManjuanInvoke");
				CardsMoveStruct move(ids, player, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "zuixiang", ""));
				room->moveCardsAtomic(move, true);
                break;
            }
            numbers.insert(card->getNumber());
        }
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==EventPhaseStart){
            if (owner->getPhase() == Player::Start&&owner->getMark("zuixiangHasTrigger")<1){
				if(owner->getMark("@sleep")>0)
					return owner==target&&owner->isAlive()&&owner->hasSkill(objectName());
				return !owner->getPile("dream").isEmpty();
			}
        }else if(target==owner){
			CardEffectStruct effect = data.value<CardEffectStruct>();
            foreach (int id, owner->getPile("dream")) {
                if (Sanguosha->getCard(id)->getTypeId() == effect.card->getTypeId())
					return true;
			}
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *sp_pangtong, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
			if (sp_pangtong->getPile("dream").isEmpty()){
				if(sp_pangtong->askForSkillInvoke(this)){
					room->removePlayerMark(sp_pangtong, "@sleep");
					doZuixiang(sp_pangtong);
				}
			}else
				doZuixiang(sp_pangtong);
        } else if (triggerEvent == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#ZuiXiang2";
			log.from = effect.to;
			if (effect.from){
				log.to << effect.from;
				log.type = "#ZuiXiang1";
			}
			log.arg = effect.card->objectName();
			log.arg2 = objectName();
			room->sendLog(log);
			return true;
        }
        return false;
    }

private:
    QMap<Card::CardType, QString> type;
};

class ZuixiangClear : public CardLimitSkill
{
public:
    ZuixiangClear() : CardLimitSkill("#zuixiang-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->hasSkill("zuixiang")){
            QStringList trs;
			foreach (int id, target->getPile("dream"))
				trs << Sanguosha->getCard(id)->getType();
            return trs.join(",");
		}
        return "";
    }
};

class Jie : public TriggerSkill
{
public:
    Jie() : TriggerSkill("jie")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.chain || damage.transfer || !damage.by_user
            || !damage.card || !damage.card->isKindOf("Slash") || !damage.card->isRed())
            return false;

        LogMessage log;
        log.type = "#Jie";
        log.from = player;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        data = QVariant::fromValue(damage);

        return false;
    }
};

class Jie2 : public TriggerSkill
{
public:
    Jie2() : TriggerSkill("jie")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.chain || damage.transfer || !damage.by_user
				|| !damage.card || !damage.card->isKindOf("Slash") || !damage.card->isRed())
				return false;
			return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        LogMessage log;
        log.type = "#Jie";
        log.from = player;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        data = QVariant::fromValue(damage);

        return false;
    }
};

DaheCard::DaheCard()
{
    mute = true;
}

bool DaheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void DaheCard::use(Room *room, ServerPlayer *zhangfei, QList<ServerPlayer *> &targets) const
{
	if (targets.first()->getGeneralName().contains("lvbu"))
		room->broadcastSkillInvoke("dahe", 2);
	else
		room->broadcastSkillInvoke("dahe", 1);
	PindianStruct *pd = zhangfei->PinDian(targets.first(), "dahe");
	if(pd->success){
		room->addPlayerMark(targets.first(), "&dahe-Clear");
		QList<ServerPlayer *> to_givelist;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= zhangfei->getHp())
				to_givelist << p;
		}
		ServerPlayer *to_give = room->askForPlayerChosen(zhangfei, to_givelist, "dahe", "@dahe-give", true);
		if (!to_give) return;
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, zhangfei->objectName(), to_give->objectName(), "dahe", "");
		to_give->obtainCard(pd->to_card);
	}else{
		if (!zhangfei->isKongcheng()) {
			room->showAllCards(zhangfei);
			room->askForDiscard(zhangfei, "dahe", 1, 1, false, false);
		}
	}
}

class DaheViewAsSkill : public ZeroCardViewAsSkill
{
public:
    DaheViewAsSkill() : ZeroCardViewAsSkill("dahe")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("DaheCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new DaheCard;
    }
};

class Dahe : public TriggerSkill
{
public:
    Dahe() :TriggerSkill("dahe")
    {
        events << CardUsed;
        view_as_skill = new DaheViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Jink")||use.card->getSuit() != Card::Heart||player->getMark("&dahe-Clear")<1) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) break;
                if (p->isDead() || !p->hasSkill(objectName())) continue;
				use.nullified_list << "_ALL_TARGETS";
				data = QVariant::fromValue(use);
                LogMessage log;
                log.type = "#DaheEffect";
                log.from = p;
                log.to << player;
                log.arg = use.card->getSuitString();
                log.arg2 = "dahe";
                room->sendLog(log);
            }
		}
        return false;
    }
};

class Dahe2 : public TriggerSkill
{
public:
    Dahe2() :TriggerSkill("#dahe")
    {
        events << CardUsed;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Jink")&&use.card->getSuit()!=Card::Heart)
				return target->getMark("&dahe-Clear")>0&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data,ServerPlayer*owner) const
    {
        if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			use.nullified_list << "_ALL_TARGETS";
			data = QVariant::fromValue(use);
			LogMessage log;
			log.type = "#DaheEffect";
			log.from = owner;
			log.to << player;
			log.arg = use.card->getSuitString();
			log.arg2 = "dahe";
			room->sendLog(log);
		}
        return false;
    }
};

TanhuCard::TanhuCard()
{
}

bool TanhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void TanhuCard::use(Room *room, ServerPlayer *lvmeng, QList<ServerPlayer *> &targets) const
{
    if (lvmeng->pindian(targets.first(), "tanhu")) {
        room->broadcastSkillInvoke("tanhu", 2);
        targets.first()->setFlags("TanhuTarget");
        lvmeng->tag["TanhuInvoke"] = QVariant::fromValue(targets.first());
        room->setFixedDistance(lvmeng, targets.first(), 1);
    } else
        room->broadcastSkillInvoke("tanhu", 3);
}

class TanhuViewAsSkill : public ZeroCardViewAsSkill
{
public:
    TanhuViewAsSkill() : ZeroCardViewAsSkill("tanhu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TanhuCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new TanhuCard;
    }
};

class Tanhu : public TriggerSkill
{
public:
    Tanhu() : TriggerSkill("tanhu")
    {
        events << EventPhaseChanging << Death << TrickCardCanceling;
        view_as_skill = new TanhuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TrickCardCanceling) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from && effect.from->tag["TanhuInvoke"].value<ServerPlayer *>() == player)
                return player->hasFlag("TanhuTarget");
        } else {
            if (triggerEvent == EventPhaseChanging) {
                PhaseChangeStruct change = data.value<PhaseChangeStruct>();
                if (change.to != Player::NotActive) return false;
            } else if (triggerEvent == Death) {
                DeathStruct death = data.value<DeathStruct>();
                if (death.who != player) return false;
            }
            ServerPlayer *target = player->tag["TanhuInvoke"].value<ServerPlayer *>();
			if (!target) return false;
            target->setFlags("-TanhuTarget");
            room->removeFixedDistance(player, target, 1);
            player->tag.remove("TanhuInvoke");
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 1;
    }
};

class TanhuBf2 : public TriggerSkill
{
public:
    TanhuBf2() : TriggerSkill("#tanhu")
    {
        events << EventPhaseChanging << Death << TrickCardCanceling;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TrickCardCanceling) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from && effect.from->tag["TanhuInvoke"].value<ServerPlayer *>() == player)
                return player->hasFlag("TanhuTarget");
        } else {
            if (triggerEvent == EventPhaseChanging) {
                PhaseChangeStruct change = data.value<PhaseChangeStruct>();
                if (change.to != Player::NotActive) return false;
            } else if (triggerEvent == Death) {
                DeathStruct death = data.value<DeathStruct>();
                if (death.who != player) return false;
            }
            ServerPlayer *target = player->tag["TanhuInvoke"].value<ServerPlayer *>();
			if (!target) return false;
            target->setFlags("-TanhuTarget");
            room->removeFixedDistance(player, target, 1);
            player->tag.remove("TanhuInvoke");
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 1;
    }
};

class MouduanStart : public TriggerSkill
{
public:
    MouduanStart() : TriggerSkill("#mouduan-start")
    {
        events << EventAcquireSkill << EventLoseSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lvmeng, QVariant &data) const
    {
        if (data.toString() != "mouduan")
			return false;
		if (triggerEvent == EventLoseSkill) {
			if (lvmeng->getMark("@wu") > 0) {
				lvmeng->loseMark("@wu");
				room->handleAcquireDetachSkills(lvmeng, "-jiang|-qianxun", true);
			} else if (lvmeng->getMark("@wen") > 0) {
				lvmeng->loseMark("@wen");
				room->handleAcquireDetachSkills(lvmeng, "-yingzi|-keji", true);
			}
        }else {
            if (lvmeng->getMark("@wu") > 0)
                room->handleAcquireDetachSkills(lvmeng, "jiang|qianxun");
            if (lvmeng->getMark("@wen") > 0)
                room->handleAcquireDetachSkills(lvmeng, "yingzi|keji");
        }
        return false;
    }
};

class Mouduan : public TriggerSkill
{
public:
    Mouduan() : TriggerSkill("mouduan")
    {
        events << EventPhaseStart << CardsMoveOneTime;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *lvmeng = room->findPlayerBySkillName(objectName());

        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && player->isAlive() && player->hasSkill(objectName(), true)
                && player->getMark("@wu") > 0 && player->getHandcardNum() <= 2) {
                room->broadcastSkillInvoke(objectName());
                room->sendCompulsoryTriggerLog(player, objectName());

                player->loseMark("@wu");
                player->gainMark("@wen");
                room->handleAcquireDetachSkills(player, "-jiang|-qianxun|yingzi|keji", true);
            }
        } else if (player->getPhase() == Player::RoundStart && lvmeng && lvmeng->getMark("@wen") > 0
            && lvmeng->canDiscard(lvmeng, "he") && room->askForCard(lvmeng, "..", "@mouduan", QVariant(), objectName())) {
            if (lvmeng->getHandcardNum() > 2) {
                room->broadcastSkillInvoke(objectName());
                lvmeng->loseMark("@wen");
                lvmeng->gainMark("@wu");
                room->handleAcquireDetachSkills(lvmeng, "-yingzi|-keji|jiang|qianxun", true);
            }
        }
        return false;
    }
};

class Mouduan2 : public TriggerSkill
{
public:
    Mouduan2() : TriggerSkill("mouduan")
    {
        events << EventPhaseStart << CardsMoveOneTime << GameStart;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event==GameStart){
			return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }else if(event==EventPhaseStart){
			return target->isAlive()&&target->getPhase()==Player::RoundStart&&target!=owner
			&&owner->getMark("@wen")>0&&owner->canDiscard(owner,"he")&&owner->hasSkill(objectName());
        }else if(event==CardsMoveOneTime){
			return owner->getHandcardNum()<=2&&owner->getMark("@wen")<1&&owner->isAlive()&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data,ServerPlayer*owner) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            room->sendCompulsoryTriggerLog(player, objectName());
			player->loseAllMarks("@wu");
			player->gainMark("@wen");
			room->handleAcquireDetachSkills(player, "-jiang|-qianxun|yingzi|keji", true);
        }else if (triggerEvent == GameStart) {
			room->sendCompulsoryTriggerLog(player, this);
            player->gainMark("@wu");
            room->handleAcquireDetachSkills(player, "jiang|qianxun");
        } else if (room->askForCard(owner, "..", "@mouduan", data, objectName())) {
			room->broadcastSkillInvoke(objectName());
			owner->loseAllMarks("@wen");
			owner->gainMark("@wu");
			room->handleAcquireDetachSkills(owner, "-yingzi|-keji|jiang|qianxun", true);
        }
        return false;
    }
};

class Zhaolie : public DrawCardsSkill
{
public:
    Zhaolie() : DrawCardsSkill("zhaolie")
    {
    }

    int getDrawNum(ServerPlayer *liubei, int n) const
    {
        Room *room = liubei->getRoom();
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (liubei->inMyAttackRange(p)) victims << p;
        }
        ServerPlayer *victim = room->askForPlayerChosen(liubei, victims, "zhaolie$-1", "zhaolie-invoke", true, true);
        if (victim) {
            n--;
			QList<int> ids;
			int no_basic = 0;
			DummyCard *dummy = new DummyCard;
			foreach (int card_id, room->showDrawPile(liubei,3,"zhaolie")) {
				const Card *card = Sanguosha->getCard(card_id);
				if (!card->isKindOf("BasicCard") || card->isKindOf("Peach")) {
					if (!card->isKindOf("BasicCard")) no_basic++;
					ids << card_id;
				} else
					dummy->addSubcard(card_id);
			}
			room->getThread()->delay();
			room->throwCard(ids, "zhaolie", nullptr);
			dummy->deleteLater();

			if (no_basic<1 && dummy->subcardsLength()<1)
				return n;
	
			if (no_basic == 0) {
				if (room->askForSkillInvoke(victim, "zhaolie_obtain", "obtain:" + liubei->objectName(),false)) {
					room->obtainCard(liubei, dummy);
				} else {
					room->obtainCard(victim, dummy);
				}
			} else {
				if (victim->canDiscard("he")
					&& room->askForDiscard(victim, "zhaolie", no_basic, no_basic, true, true, "@zhaolie-discard:" + liubei->objectName())) {
					if (liubei->isAlive())
						room->obtainCard(liubei, dummy);
					else
						room->throwCard(dummy, "zhaolie", nullptr);
				} else {
					room->damage(DamageStruct("zhaolie", liubei, victim, no_basic));
					if (victim->isAlive())
						room->obtainCard(victim, dummy);
					else
						room->throwCard(dummy, "zhaolie", nullptr);
				}
			}
        }
        return n;
    }
};

class Zhaolie2 : public DrawCardsSkill
{
public:
    Zhaolie2() : DrawCardsSkill("zhaolie")
    {
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*owner,QVariant data) const
    {
		DrawStruct draw = data.value<DrawStruct>();
		return draw.reason=="draw_phase"&&target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
    }

    int getDrawNum(ServerPlayer *liubei, int n) const
    {
        Room *room = liubei->getRoom();
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (liubei->inMyAttackRange(p)) victims << p;
        }
        ServerPlayer *victim = room->askForPlayerChosen(liubei, victims, "zhaolie$-1", "zhaolie-invoke", true, true);
        if (victim) {
            n--;
			QList<int> ids;
			int no_basic = 0;
			DummyCard *dummy = new DummyCard;
			foreach (int card_id, room->showDrawPile(liubei,3,"zhaolie")) {
				const Card *card = Sanguosha->getCard(card_id);
				if (!card->isKindOf("BasicCard") || card->isKindOf("Peach")) {
					if (!card->isKindOf("BasicCard")) no_basic++;
					ids << card_id;
				} else
					dummy->addSubcard(card_id);
			}
			room->getThread()->delay();
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, "", "zhaolie", "");
			room->throwCard(ids, reason, nullptr);
			dummy->deleteLater();

			if (no_basic<1 && dummy->subcardsLength()<1)
				return n;
	
			if (no_basic == 0) {
				if (room->askForSkillInvoke(victim, "zhaolie_obtain", "obtain:" + liubei->objectName(),false)) {
					room->obtainCard(liubei, dummy);
				} else {
					room->obtainCard(victim, dummy);
				}
			} else {
				if (victim->canDiscard("he")
					&& room->askForDiscard(victim, "zhaolie", no_basic, no_basic, true, true, "@zhaolie-discard:" + liubei->objectName())) {
					if (liubei->isAlive())
						room->obtainCard(liubei, dummy);
					else
						room->throwCard(dummy, reason, nullptr);
				} else {
					if (no_basic > 0)
						room->damage(DamageStruct("zhaolie", liubei, victim, no_basic));
					if (victim->isAlive())
						room->obtainCard(victim, dummy);
					else
						room->throwCard(dummy, reason, nullptr);
				}
			}
        }
        return n;
    }
};

ShichouCard::ShichouCard()
{
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool ShichouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getKingdom() == "shu" && to_select != Self;
}

void ShichouCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    ServerPlayer *player = effect.from, *victim = effect.to;

    if (!player->isLord() && player->hasSkill("weidi")) {
        room->broadcastSkillInvoke("weidi");
    } else {
        room->broadcastSkillInvoke("shichou");
    }
	room->doSuperLightbox(player, "shichou");

    room->removePlayerMark(player, "@hate");
    room->setPlayerMark(player, "xhate", 1);
    victim->gainMark("@hate_to");
    room->setPlayerMark(victim, "hate_" + player->objectName(), 1);

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), victim->objectName(), "shichou", "");
    room->obtainCard(victim, this, reason, false);
}

class ShichouViewAsSkill : public ViewAsSkill
{
public:
    ShichouViewAsSkill() : ViewAsSkill("shichou")
    {
        response_pattern = "@@shichou";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *) const
    {
        return selected.length() < 2;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        ShichouCard *card = new ShichouCard;
        card->addSubcards(cards);
        return card;
    }
};

class Shichou : public TriggerSkill
{
public:
    Shichou() : TriggerSkill("shichou$")
    {
        events << EventPhaseStart << DamageInflicted << Dying << DamageComplete;
        frequency = Limited;
        limit_mark = "@hate";
        view_as_skill = new ShichouViewAsSkill;
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getMark("xhate") == 0 && player->hasLordSkill("shichou")
            && player->getPhase() == Player::Start && player->getCards("he").length() > 1) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getKingdom() == "shu") {
                    room->askForUseCard(player, "@@shichou", "@shichou-give", -1, Card::MethodNone);
                    break;
                }
            }
        } else if (triggerEvent == DamageInflicted && player->hasLordSkill(this) && player->getMark("ShichouTarget") == 0) {
            ServerPlayer *target = nullptr;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("hate_" + player->objectName()) > 0 && p->getMark("@hate_to") > 0) {
                    target = p;
                    break;
                }
            }
            if (target == nullptr || target->isDead())
                return false;
            LogMessage log;
            log.type = "#ShichouProtect";
            log.arg = objectName();
            log.from = player;
            log.to << target;
            room->sendLog(log);

            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

            DamageStruct newdamage = data.value<DamageStruct>();
            newdamage.to = target;
            newdamage.transfer = true;
            newdamage.transfer_reason = "shichou";
            player->tag["TransferDamage"] = QVariant::fromValue(newdamage);
            return true;
        } else if (triggerEvent == Dying) {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who != player) return false;
            if (player->getMark("@hate_to") > 0)
                player->loseAllMarks("@hate_to");
        } else if (triggerEvent == DamageComplete) {
			DamageStruct damage = data.value<DamageStruct>();
			if (player->isAlive() && damage.transfer && damage.transfer_reason == "shichou")
				player->drawCards(damage.damage, "shichou");
		}
        return false;
    }
};

class Shichou2 : public TriggerSkill
{
public:
    Shichou2() : TriggerSkill("shichou$")
    {
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@hate";
        view_as_skill = new ShichouViewAsSkill;
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event==EventPhaseStart){
            if(target->getPhase()==Player::Start&&target==owner&&owner->getMark("@hate")>0
			&&owner->getCardCount()>1&&owner->hasLordSkill(this)){
				foreach (ServerPlayer *p, room->getOtherPlayers(owner)) {
					if(p->getKingdom()=="shu") return true;
				}
			}
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart) {
            room->askForUseCard(player, "@@shichou", "@shichou-give", -1, Card::MethodNone);
        }
        return false;
    }
};

class ShichouBf2 : public TriggerSkill
{
public:
    ShichouBf2() : TriggerSkill("#shichou")
    {
        events << DamageInflicted << Dying << DamageComplete;
    }

    bool triggerable(ServerPlayer*target,Room*room,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==DamageInflicted){
            if(target==owner&&owner->isAlive()&&owner->hasLordSkill(this)){
				foreach (ServerPlayer *p, room->getOtherPlayers(owner)) {
					if (p->getMark("hate_" + owner->objectName()) > 0 && p->getMark("@hate_to") > 0)
						return true;
				}
			}
        }else if(event==Dying){
			return target==owner&&owner->getMark("@hate_to") > 0;
        }else if(event==DamageComplete){
			DamageStruct damage = data.value<DamageStruct>();
			return damage.transfer&&damage.transfer_reason=="shichou"&&target==owner&&owner->isAlive();
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted) {
            ServerPlayer *target = nullptr;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("hate_" + player->objectName()) > 0 && p->getMark("@hate_to") > 0) {
                    target = p;
                    break;
                }
            }
            if (!target)
                return false;
            LogMessage log;
            log.type = "#ShichouProtect";
            log.arg = objectName();
            log.from = player;
            log.to << target;
            room->sendLog(log);

            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

            DamageStruct newdamage = data.value<DamageStruct>();
            newdamage.to = target;
            newdamage.transfer = true;
            newdamage.transfer_reason = "shichou";
            player->tag["TransferDamage"] = QVariant::fromValue(newdamage);
            return true;
        } else if (triggerEvent == Dying) {
            player->loseAllMarks("@hate_to");
        } else if (triggerEvent == DamageComplete) {
			DamageStruct damage = data.value<DamageStruct>();
			player->drawCards(damage.damage, "shichou");
		}
        return false;
    }
};

YanxiaoCard::YanxiaoCard(Suit suit, int number)
    : DelayedTrick(suit, number)
{
    mute = true;
    handling_method = Card::MethodNone;
    setObjectName("YanxiaoCard");
    will_throw = false;
}

bool YanxiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    if (Self->isProhibited(to_select,this))
        return false;
    return targets.isEmpty();
}

void YanxiaoCard::onUse(Room *room, CardUseStruct &card_use) const
{
    int x = qrand()%2+1;
	if(card_use.from->getGeneralName().contains("daqiao")){
		x = 1;
		foreach(ServerPlayer *to, card_use.to)
			if (to->getGeneralName().contains("sunce"))
				x = 2;
	}else if(hasFlag("JINGYIN"))
		x = 0;
    room->broadcastSkillInvoke("yanxiao", x, card_use.from);
    CardMoveReason reason(CardMoveReason::S_MASK_BASIC_REASON, card_use.from->objectName(), "yanxiao", "");
	room->moveCardTo(this,nullptr,Player::PlaceTable,reason,true);
    DelayedTrick::onUse(room, card_use);
}

void YanxiaoCard::takeEffect(ServerPlayer *) const
{
}

class YanxiaoViewAsSkill : public OneCardViewAsSkill
{
public:
    YanxiaoViewAsSkill() : OneCardViewAsSkill("yanxiao")
    {
        filter_pattern = ".|diamond";
    }

    const Card *viewAs(const Card *c) const
    {
        YanxiaoCard *yanxiao = new YanxiaoCard(c->getSuit(), c->getNumber());
        yanxiao->setSkillName(objectName());
        yanxiao->addSubcard(c);
        return yanxiao;
    }
};

class Yanxiao : public PhaseChangeSkill
{
public:
    Yanxiao() : PhaseChangeSkill("yanxiao")
    {
        view_as_skill = new YanxiaoViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->getPhase() == Player::Judge && target->containsTrick("YanxiaoCard");
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        CardsMoveStruct move;
        LogMessage log;
        log.type = "$YanxiaoGot";
        log.from = target;

        foreach(const Card *delayed_trick, target->getJudgingArea())
            move.card_ids << delayed_trick->getEffectiveId();
        log.card_str = ListI2S(move.card_ids).join("+");
        target->getRoom()->sendLog(log);

        move.to = target;
        move.to_place = Player::PlaceHand;
        room->moveCardsAtomic(move, true);

        return false;
    }
};

class Yanxiao2 : public PhaseChangeSkill
{
public:
    Yanxiao2() : PhaseChangeSkill("yanxiao")
    {
        view_as_skill = new YanxiaoViewAsSkill;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent,ServerPlayer*,QVariant) const
    {
        return target && target->getPhase() == Player::Judge && target->containsTrick("YanxiaoCard");
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        CardsMoveStruct move;
        LogMessage log;
        log.type = "$YanxiaoGot";
        log.from = target;

        foreach(const Card *delayed_trick, target->getJudgingArea())
            move.card_ids << delayed_trick->getEffectiveId();
        log.card_str = ListI2S(move.card_ids).join("+");
        target->getRoom()->sendLog(log);

        move.to = target;
        move.to_place = Player::PlaceHand;
        room->moveCardsAtomic(move, true);

        return false;
    }
};

class Anxian : public TriggerSkill
{
public:
    Anxian() : TriggerSkill("anxian")
    {
        events << DamageCaused << TargetConfirming;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *daqiao, QVariant &data) const
    {
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash")
                && damage.by_user && !damage.chain && !damage.transfer
                && daqiao->askForSkillInvoke(objectName()+"$1", data)) {
                LogMessage log;
                log.type = "#Anxian";
                log.from = daqiao;
                log.arg = objectName();
                room->sendLog(log);
                if (damage.to->canDiscard(damage.to, "h"))
                    room->askForDiscard(damage.to, "anxian", 1, 1);
                daqiao->drawCards(1, objectName());
                return true;
            }
        } else if (triggerEvent == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.to.contains(daqiao) || !daqiao->canDiscard(daqiao, "h"))
                return false;
            if (use.card->isKindOf("Slash")) {
                daqiao->setFlags("-AnxianTarget");
                if (room->askForCard(daqiao, ".", "@anxian-discard", data, objectName()+"$1")) {
                    daqiao->setFlags("AnxianTarget");
                    use.from->drawCards(1, objectName());
                    if (daqiao->isAlive() && daqiao->hasFlag("AnxianTarget")) {
                        daqiao->setFlags("-AnxianTarget");
                        use.nullified_list << daqiao->objectName();
                        data = QVariant::fromValue(use);
                    }
                }
            }
        }
        return false;
    }
};

class Anxian2 : public TriggerSkill
{
public:
    Anxian2() : TriggerSkill("anxian")
    {
        events << DamageCaused << TargetConfirming;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==DamageCaused){
            DamageStruct damage = data.value<DamageStruct>();
			return damage.card&&damage.card->isKindOf("Slash")&&damage.by_user
			&&!damage.chain&&!damage.transfer&&target==owner&&owner->hasSkill(objectName());
        }else if(event==TargetConfirming){
            CardUseStruct use = data.value<CardUseStruct>();
			return use.card->isKindOf("Slash")&&target==owner
			&&target->canDiscard(target,"h")&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *daqiao, QVariant &data) const
    {
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (daqiao->askForSkillInvoke(objectName()+"$1", data)) {
                LogMessage log;
                log.type = "#Anxian";
                log.from = daqiao;
                log.arg = objectName();
                room->sendLog(log);
                if (damage.to->canDiscard(damage.to, "h"))
                    room->askForDiscard(damage.to, "anxian", 1, 1);
                daqiao->drawCards(1, objectName());
                return true;
            }
        } else if (triggerEvent == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
			if (room->askForCard(daqiao, ".", "@anxian-discard", data, objectName()+"$1")) {
				use.from->drawCards(1, objectName());
				use.nullified_list << daqiao->objectName();
				data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

YinlingCard::YinlingCard()
{
}

bool YinlingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void YinlingCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    if (!effect.from->canDiscard(effect.to, "he") || effect.from->getPile("brocade").length() >= 4)
        return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "he", "yinling", false, Card::MethodDiscard);
    effect.from->addToPile("brocade", card_id);
}

class Yinling : public OneCardViewAsSkill
{
public:
    Yinling() : OneCardViewAsSkill("yinling")
    {
        filter_pattern = ".|black!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getPile("brocade").length() < 4;
    }

    const Card *viewAs(const Card *originalcard) const
    {
        YinlingCard *card = new YinlingCard;
        card->addSubcard(originalcard);
        return card;
    }
};

JunweiCard::JunweiCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool JunweiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() == 0;
}

void JunweiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
    room->throwCard(this, reason, nullptr);

    ServerPlayer *ganning = effect.from;
    ServerPlayer *target = effect.to;

    QVariant ai_data = QVariant::fromValue(ganning);
    const Card *card = room->askForCard(target, "Jink", "@junwei-show", ai_data, Card::MethodNone);
    if (card) {
        room->showCard(target, card->getEffectiveId());
        ServerPlayer *receiver = room->askForPlayerChosen(ganning, room->getAllPlayers(), "junweigive", "@junwei-give");
        if (receiver != target)
            receiver->obtainCard(card);
    } else {
        room->loseHp(HpLostStruct(target, 1, objectName(), ganning));
        if (!target->isAlive())
            return;
        if (target->hasEquip()) {
            int card_id = room->askForCardChosen(ganning, target, "e", objectName());
            target->addToPile("junwei_equip", card_id);
        }
    }
}

class JunweiVS : public ViewAsSkill
{
public:
    JunweiVS() : ViewAsSkill("junwei")
    {
        expand_pile = "brocade";
        response_pattern = "@@junwei";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() >= 3)
            return false;

        return Self->getPile("brocade").contains(to_select->getId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 3) {
            JunweiCard *c = new JunweiCard;
            c->addSubcards(cards);
            return c;
        }

        return nullptr;
    }
};

class Junwei : public TriggerSkill
{
public:
    Junwei() : TriggerSkill("junwei")
    {
        events << EventPhaseStart;
        view_as_skill = new JunweiVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *ganning, QVariant &) const
    {
        if (ganning->getPhase() == Player::Finish && ganning->getPile("brocade").length() >= 3)
            room->askForUseCard(ganning, "@@junwei", "junwei-invoke", -1, Card::MethodNone);

        return false;
    }
};

class JunweiGot : public TriggerSkill
{
public:
    JunweiGot() : TriggerSkill("#junwei-got")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive || player->getPile("junwei_equip").length() == 0)
            return false;
        foreach (int card_id, player->getPile("junwei_equip")) {
            const Card *card = Sanguosha->getCard(card_id);

            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(card_id, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName()));
            exchangeMove.push_back(move1);
			card = player->getEquip(equip->location());
            if (card) {
                CardsMoveStruct move2(card->getId(), nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
                exchangeMove.push_back(move2);
            }
            LogMessage log;
            log.from = player;
            log.type = "$JunweiGot";
            log.card_str = QString::number(card_id);
            room->sendLog(log);

            room->moveCardsAtomic(exchangeMove, true);
        }
        return false;
    }
};

class JunweiGot2 : public TriggerSkill
{
public:
    JunweiGot2() : TriggerSkill("#junwei-got")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*,QVariant data) const
    {
        if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			return change.to==Player::NotActive&&target->getPile("junwei_equip").length()>0;
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        foreach (int card_id, player->getPile("junwei_equip")) {
            const Card *card = Sanguosha->getCard(card_id);
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(card_id, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName()));
            exchangeMove.push_back(move1);
			card = player->getEquip(equip->location());
            if (card) {
                CardsMoveStruct move2(card->getId(), nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
                exchangeMove.push_back(move2);
            }
            LogMessage log;
            log.from = player;
            log.type = "$JunweiGot";
            log.card_str = QString::number(card_id);
            room->sendLog(log);

            room->moveCardsAtomic(exchangeMove, true);
        }
        return false;
    }
};

class Fenyong : public TriggerSkill
{
public:
    Fenyong() : TriggerSkill("fenyong")
    {
        events << Damaged << DamageInflicted << EventPhaseStart;
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == Damaged && TriggerSkill::triggerable(player)) {
            if (player->getMark("@fenyong") == 0 && room->askForSkillInvoke(player, objectName()+"%1")) {
                room->addPlayerMark(player, "@fenyong");
            }
        } else if (triggerEvent == DamageInflicted && TriggerSkill::triggerable(player)) {
            if (player->getMark("@fenyong") > 0) {
                room->broadcastSkillInvoke(objectName(), 2);
                LogMessage log;
                log.type = "#FenyongAvoid";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                return true;
            }
        } else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish) {
            const TriggerSkill *xuehen_trigger = nullptr;
            const Skill *xuehen = Sanguosha->getSkill("xuehen");
            if (xuehen) xuehen_trigger = qobject_cast<const TriggerSkill *>(xuehen);
            if (!xuehen_trigger) return false;

            QVariant data = QVariant::fromValue(player);
            foreach (ServerPlayer *xiahou, room->getAllPlayers()) {
                if (TriggerSkill::triggerable(xiahou) && xiahou->getMark("@fenyong") > 0) {
                    room->setPlayerMark(xiahou, "@fenyong", 0);
                    if (xiahou->hasSkill("xuehen"))
                        xuehen_trigger->trigger(NonTrigger, room, xiahou, data);
                }
            }
        }
        return false;
    }
};

class Fenyong2 : public TriggerSkill
{
public:
    Fenyong2() : TriggerSkill("fenyong")
    {
        events << Damaged;
        frequency = Frequent;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event==Damaged){
			return target==owner&&target->getMark("@fenyong")<1&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &,ServerPlayer*) const
    {
        if (triggerEvent == Damaged) {
            if (room->askForSkillInvoke(player, objectName()+"%1"))
                room->addPlayerMark(player, "@fenyong");
        }
        return false;
    }
};

class FenyongBf2 : public TriggerSkill
{
public:
    FenyongBf2() : TriggerSkill("#fenyong")
    {
        events << EventLoseSkill << DamageInflicted << EventPhaseStart;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event == DamageInflicted){
			return target==owner&&target->getMark("@fenyong")>0&&owner->hasSkill(objectName());
        }else if(event == EventPhaseStart){
			return target->isAlive()&&target->getPhase()==Player::Finish
			&&owner->getMark("@fenyong")>0&&owner->hasSkill(objectName());
        }else if(event == EventLoseSkill){
			return target==owner&&target->getMark("@fenyong")>0&&data.toString()=="fenyong";
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &,ServerPlayer*owner) const
    {
        if (triggerEvent == DamageInflicted) {
			room->broadcastSkillInvoke(objectName(), 2);
			LogMessage log;
			log.type = "#FenyongAvoid";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			return true;
        } else if (triggerEvent == EventPhaseStart) {
            const TriggerSkill *xuehen_trigger = Sanguosha->getTriggerSkill("xuehen");
            QVariant data = QVariant::fromValue(player);
			room->setPlayerMark(owner, "@fenyong", 0);
			if (owner->hasSkill("xuehen",true))
				xuehen_trigger->trigger(NonTrigger, room, owner, data);
        } else if (triggerEvent == EventLoseSkill) {
			room->setPlayerMark(player, "@fenyong", 0);
        }
        return false;
    }
};

class FenyongDetach : public DetachEffectSkill
{
public:
    FenyongDetach() : DetachEffectSkill("fenyong")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if (player->getMark("@fenyong") > 0)
            room->setPlayerMark(player, "@fenyong", 0);
    }
};

/* XueHen is triggered in the codes of FenYong
 * So 'events' will not be set
 * And triggerable() will always return false */
class Xuehen : public TriggerSkill
{
public:
    Xuehen() : TriggerSkill("xuehen")
    {
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *) const
    {
        return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *xiahou, QVariant &data) const
    {
        room->sendCompulsoryTriggerLog(xiahou, objectName());

        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getOtherPlayers(xiahou))
            if (xiahou->canSlash(p, false))
                targets << p;
        QString choice;
        if (targets.isEmpty()) choice = "discard";
        else choice = room->askForChoice(xiahou, objectName(), "discard+slash");
        if (choice == "slash") {
            room->broadcastSkillInvoke(objectName(), 2);

            ServerPlayer *victim = room->askForPlayerChosen(xiahou, targets, objectName(), "@dummy-slash");

            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName(objectName());
		   	slash->deleteLater();
            room->useCard(CardUseStruct(slash, xiahou, victim));
        } else {
			ServerPlayer *player = data.value<ServerPlayer *>();
            room->broadcastSkillInvoke(objectName(), 1);
            DummyCard *dummy = new DummyCard;
            for (int i = 0; i < qMin(xiahou->getLostHp(),player->getCardCount()); i++) {
                int id = room->askForCardChosen(xiahou, player, "he", objectName(), false, Card::MethodDiscard, dummy->getSubcards(), i>0);
                if(id<0) break;
				dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() > 0)
                room->throwCard(dummy, player, xiahou);
            dummy->deleteLater();
        }
        return false;
    }
};

BGMPackage::BGMPackage()
 : Package("BGM")
{
    General *bgm_caoren = new General(this, "bgm_caoren", "wei"); // *SP 003
    bgm_caoren->addSkill(new Kuiwei);
    bgm_caoren->addSkill(new Yanzheng);

    General *bgm_pangtong = new General(this, "bgm_pangtong", "qun", 3); // *SP 004
    bgm_pangtong->addSkill(new Manjuan);
    bgm_pangtong->addSkill(new Zuixiang);
    bgm_pangtong->addSkill(new ZuixiangClear);
    related_skills.insertMulti("zuixiang", "#zuixiang-limit");

    General *bgm_zhangfei = new General(this, "bgm_zhangfei", "shu"); // *SP 005
    bgm_zhangfei->addSkill(new Jie);
    bgm_zhangfei->addSkill(new Dahe);

    General *bgm_lvmeng = new General(this, "bgm_lvmeng", "wu", 3); // *SP 006
    bgm_lvmeng->addSkill(new Tanhu);
    bgm_lvmeng->addSkill(new MouduanStart);
    bgm_lvmeng->addSkill(new Mouduan);
    related_skills.insertMulti("mouduan", "#mouduan-start");

    General *bgm_liubei = new General(this, "bgm_liubei$", "shu"); // *SP 007
    bgm_liubei->addSkill(new Zhaolie);
    bgm_liubei->addSkill(new Shichou);

    General *bgm_daqiao = new General(this, "bgm_daqiao", "wu", 3, false); // *SP 008
    bgm_daqiao->addSkill(new Yanxiao);
    bgm_daqiao->addSkill(new Anxian);

    General *bgm_ganning = new General(this, "bgm_ganning", "qun"); // *SP 009
    bgm_ganning->addSkill(new Yinling);
    bgm_ganning->addSkill(new Junwei);
    bgm_ganning->addSkill(new JunweiGot);
    related_skills.insertMulti("junwei", "#junwei-got");

    General *bgm_xiahoudun = new General(this, "bgm_xiahoudun", "wei"); // *SP 010
    bgm_xiahoudun->addSkill(new Fenyong);
    bgm_xiahoudun->addSkill(new FenyongDetach);
    bgm_xiahoudun->addSkill(new Xuehen);
    bgm_xiahoudun->addSkill(new SlashNoDistanceLimitSkill("xuehen"));
    related_skills.insertMulti("fenyong", "#fenyong-clear");
    related_skills.insertMulti("xuehen", "#xuehen-slash-ndl");

    addMetaObject<DaheCard>();
    addMetaObject<TanhuCard>();
    addMetaObject<ShichouCard>();
    addMetaObject<YanxiaoCard>();
    addMetaObject<YinlingCard>();
    addMetaObject<JunweiCard>();
}
ADD_PACKAGE(BGM)

// DIY Generals
ZhaoxinCard::ZhaoxinCard()
{
}

bool ZhaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(NoSuit, 0);
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void ZhaoxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source);

    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_zhaoxin");
	slash->deleteLater();
    room->useCard(CardUseStruct(slash, source, targets));
}

class ZhaoxinViewAsSkill : public ZeroCardViewAsSkill
{
public:
    ZhaoxinViewAsSkill() : ZeroCardViewAsSkill("zhaoxin")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "@@zhaoxin" && Slash::IsAvailable(player);
    }

    const Card *viewAs() const
    {
        return new ZhaoxinCard;
    }
};

class Zhaoxin : public TriggerSkill
{
public:
    Zhaoxin() : TriggerSkill("zhaoxin")
    {
        events << EventPhaseEnd;
        view_as_skill = new ZhaoxinViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *simazhao, QVariant &) const
    {
        if (simazhao->getPhase() != Player::Draw)
            return false;
        if (simazhao->isKongcheng() || !Slash::IsAvailable(simazhao))
            return false;

        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getAllPlayers())
            if (simazhao->canSlash(p))
                targets << p;

        if (targets.isEmpty())
            return false;

        room->askForUseCard(simazhao, "@@zhaoxin", "@zhaoxin");
        return false;
    }
};

class Zhaoxin2 : public TriggerSkill
{
public:
    Zhaoxin2() : TriggerSkill("zhaoxin")
    {
        events << EventPhaseEnd;
        view_as_skill = new ZhaoxinViewAsSkill;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event == EventPhaseEnd){
			return target==owner&&target->getPhase()==Player::Draw&&owner->isAlive()
			&&owner->getHandcardNum()>0&&Slash::IsAvailable(owner)&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *simazhao, QVariant &) const
    {
        room->askForUseCard(simazhao, "@@zhaoxin", "@zhaoxin");
        return false;
    }
};

class Langgu : public TriggerSkill
{
public:
    Langgu() : TriggerSkill("langgu")
    {
        events << Damaged << AskForRetrial;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *simazhao, QVariant &data) const
    {
        if (TriggerSkill::triggerable(simazhao) && triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();

            for (int i = 0; i < damage.damage; i++) {
                if (!simazhao->isAlive() || !simazhao->askForSkillInvoke(objectName()+"$-1", data))
                    return false;

                JudgeStruct judge;
                judge.good = true;
                judge.play_animation = false;
                judge.who = simazhao;
                judge.reason = objectName();

                room->judge(judge);
                if (simazhao->isAlive() && damage.from && damage.from->isAlive() && !damage.from->isKongcheng()) {
                    QList<int> langgu_discard, other;
                    foreach (int card_id, damage.from->handCards()) {
                        if (simazhao->canDiscard(damage.from, card_id) && Sanguosha->getCard(card_id)->getSuit() == judge.card->getSuit())
                            langgu_discard << card_id;
                        else
                            other << card_id;
                    }
                    if (langgu_discard.isEmpty()) {
                        room->showAllCards(damage.from, simazhao);
                        return false;
                    }

                    LogMessage log;
                    log.type = "$ViewAllCards";
                    log.from = simazhao;
                    log.to << damage.from;
                    log.card_str = ListI2S(damage.from->handCards()).join("+");
                    room->sendLog(log, simazhao);

                    while (!langgu_discard.isEmpty()) {
                        room->fillAG(langgu_discard + other, simazhao, other);
                        int id = room->askForAG(simazhao, langgu_discard, true, objectName());
                        if (id == -1) {
                            room->clearAG(simazhao);
                            break;
                        }
                        langgu_discard.removeOne(id);
                        other.prepend(id);
                        room->clearAG(simazhao);
                    }

                    if (!langgu_discard.isEmpty()) {
                        DummyCard *dummy = new DummyCard(langgu_discard);
                        room->throwCard(dummy, damage.from, simazhao);
                        dummy->deleteLater();
                    }
                }
            }
        } else if (TriggerSkill::triggerable(simazhao) && triggerEvent == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason != objectName() || simazhao->isKongcheng())
                return false;

            QStringList prompt_list;
            prompt_list << "@langgu-card" << judge->who->objectName()
                << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
            QString prompt = prompt_list.join(":");
            const Card *card = room->askForCard(simazhao, ".", prompt, data, Card::MethodResponse, judge->who, true);

            if (card)
                room->retrial(card, simazhao, judge, objectName());
        }
        return false;
    }
};

class Langgu2 : public TriggerSkill
{
public:
    Langgu2() : TriggerSkill("langgu")
    {
        events << Damaged << AskForRetrial;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event == Damaged){
			return target==owner&&target->isAlive()&&owner->hasSkill(objectName());
        }else if(event == AskForRetrial){
            JudgeStruct *judge = data.value<JudgeStruct *>();
			return judge->reason==objectName()&&target==owner&&target->isAlive()
			&&owner->getHandcardNum()>0&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *simazhao, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();

            for (int i = 0; i < damage.damage; i++) {
                if (!simazhao->isAlive() || !simazhao->askForSkillInvoke(objectName()+"$-1", data))
                    break;

                JudgeStruct judge;
                judge.good = true;
                judge.play_animation = false;
                judge.who = simazhao;
                judge.reason = objectName();

                room->judge(judge);
                if (simazhao->isAlive() && damage.from && damage.from->isAlive() && !damage.from->isKongcheng()) {
                    QList<int> langgu_discard, other, ids;
                    foreach (int id, damage.from->handCards()) {
                        if (simazhao->canDiscard(damage.from, id) && Sanguosha->getCard(id)->getSuit() == judge.card->getSuit())
                            langgu_discard << id;
                        else
                            other << id;
                    }

                    LogMessage log;
                    log.type = "$ViewAllCards";
                    log.from = simazhao;
                    log.to << damage.from;
                    log.card_str = ListI2S(langgu_discard+other).join("+");
                    room->sendLog(log, simazhao);

                    while (!langgu_discard.isEmpty()||ids.isEmpty()) {
                        room->fillAG(langgu_discard+other, simazhao, other);
                        int id = room->askForAG(simazhao, langgu_discard, true, objectName());
						room->clearAG(simazhao);
                        if (id == -1) break;
                        langgu_discard.removeOne(id);
                        other.prepend(id);
						ids << id;
                    }

                    room->throwCard(ids, objectName(), damage.from, simazhao);
                }
            }
        } else if (triggerEvent == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            QStringList prompt_list;
            prompt_list << "@langgu-card" << judge->who->objectName() << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
            const Card *card = room->askForCard(simazhao, ".", prompt_list.join(":"), data, Card::MethodResponse, judge->who, true);
            if (card) room->retrial(card, simazhao, judge, objectName());
        }
        return false;
    }
};

FuluanCard::FuluanCard()
{
}

bool FuluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->inMyAttackRange(to_select, subcards);
}

void FuluanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    effect.to->turnOver();
    room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
}

class Fuluan : public ViewAsSkill
{
public:
    Fuluan() : ViewAsSkill("fuluan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount() >= 3 && !player->hasUsed("FuluanCard") && !player->hasFlag("ForbidFuluan");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *card) const
    {
        if (selected.length() >= 3)
            return false;

        if (Self->isJilei(card))
            return false;

        if (!selected.isEmpty()) {
            return card->getSuit() == selected.first()->getSuit();
        }

        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 3)
            return nullptr;

        FuluanCard *card = new FuluanCard;
        card->addSubcards(cards);

        return card;
    }
};

class Shude : public TriggerSkill
{
public:
    Shude() : TriggerSkill("shude")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *wangyuanji, QVariant &) const
    {
        if (wangyuanji->getPhase() == Player::Finish) {
            int upper = wangyuanji->getMaxHp();
            int handcard = wangyuanji->getHandcardNum();
            if (handcard < upper && wangyuanji->askForSkillInvoke(objectName()+"$-1"))
                wangyuanji->drawCards(upper - handcard, objectName());
        }
        return false;
    }
};

class Shude2 : public TriggerSkill
{
public:
    Shude2() : TriggerSkill("shude")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event == EventPhaseStart){
			return target==owner&&target->getPhase()==Player::Finish&&owner->isAlive()
			&&owner->getMaxHp()>owner->getHandcardNum()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
    {
		if (player->askForSkillInvoke(objectName()+"$-1"))
			player->drawCards(player->getMaxHp() - player->getHandcardNum(), objectName());
        return false;
    }
};

HuangenCard::HuangenCard()
{
}

bool HuangenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() >= Self->getHp()) return false;
    QStringList targetslist = Self->property("huangen_targets").toString().split("+");
    return targetslist.contains(to_select->objectName());
}

void HuangenCard::onEffect(CardEffectStruct &effect) const
{
    CardUseStruct use = effect.from->tag["huangen"].value<CardUseStruct>();
    use.nullified_list << effect.to->objectName();
    effect.from->tag["huangen"] = QVariant::fromValue(use);
    effect.to->drawCards(1, "huangen");
}

class HuangenViewAsSkill : public ZeroCardViewAsSkill
{
public:
    HuangenViewAsSkill() :ZeroCardViewAsSkill("huangen")
    {
        response_pattern = "@@huangen";
    }

    const Card *viewAs() const
    {
        return new HuangenCard;
    }
};

class Huangen : public TriggerSkill
{
public:
    Huangen() : TriggerSkill("huangen")
    {
        events << TargetConfirmed;
        view_as_skill = new HuangenViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liuxie, QVariant &data) const
    {
        if (liuxie->getHp() <= 0) return false;
        CardUseStruct use = data.value<CardUseStruct>();

        if (use.to.length() <= 1 || !use.card->isNDTrick())
            return false;

        QStringList target_list;
        foreach(ServerPlayer *p, use.to)
            target_list << p->objectName();
        room->setPlayerProperty(liuxie, "huangen_targets", target_list.join("+"));
        liuxie->tag["huangen"] = data;
        room->askForUseCard(liuxie, "@@huangen", "@huangen-card");
        data = liuxie->tag["huangen"];

        return false;
    }
};

class Huangen2 : public TriggerSkill
{
public:
    Huangen2() : TriggerSkill("huangen")
    {
        events << TargetConfirmed;
        view_as_skill = new HuangenViewAsSkill;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event == TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			return target==owner&&use.to.length()>1&&use.card->isNDTrick()
			&&owner->isAlive()&&owner->getHp()>0&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liuxie, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();

        QStringList target_list;
        foreach(ServerPlayer *p, use.to)
            target_list << p->objectName();
        room->setPlayerProperty(liuxie, "huangen_targets", target_list.join("+"));
        liuxie->tag["huangen"] = data;
        room->askForUseCard(liuxie, "@@huangen", "@huangen-card");
        data = liuxie->tag["huangen"];

        return false;
    }
};

HantongCard::HantongCard()
{
    target_fixed = true;
    mute = true;
}

class HantongViewAsSkill : public ZeroCardViewAsSkill
{
public:
    HantongViewAsSkill() : ZeroCardViewAsSkill("hantong")
    {
    }

    const Card *viewAs() const
    {
        return new HantongCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        JijiangViewAsSkill *jijiang = new JijiangViewAsSkill;
        jijiang->deleteLater();
        return player->getPile("edict").length() > 0 && jijiang->isEnabledAtPlay(player);
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        JijiangViewAsSkill *jijiang = new JijiangViewAsSkill;
        jijiang->deleteLater();
        return player->getPile("edict").length() > 0 && jijiang->isEnabledAtResponse(player, pattern);
    }
};

class Hantong : public TriggerSkill
{
public:
    Hantong() : TriggerSkill("hantong")
    {
        events << CardsMoveOneTime;
        view_as_skill = new HantongViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liuxie, QVariant &data) const
    {
        if (liuxie->getPhase() != Player::Discard) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != liuxie || move.to_place != Player::DiscardPile) return false;
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
			QList<int> to_add;
			int i = 0;
			foreach (int id, move.card_ids) {
				if (move.from_places[i]==Player::PlaceHand&&room->getCardPlace(id)==Player::DiscardPile)
					to_add.append(id);
				i++;
			}
			if (to_add.length()>0&&liuxie->askForSkillInvoke(objectName()+"$1"))
				liuxie->addToPile("edict", to_add, true, QList<ServerPlayer *>(), move.reason);
        }
        return false;
    }
};

class Hantong2 : public TriggerSkill
{
public:
    Hantong2() : TriggerSkill("hantong")
    {
        events << CardsMoveOneTime;
        view_as_skill = new HantongViewAsSkill;
    }

    bool triggerable(ServerPlayer*,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			return owner->getPhase()==Player::Discard&&move.from==owner&&move.to_place==Player::DiscardPile
			&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD
			&&move.from_places.contains(Player::PlaceHand)&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data,ServerPlayer*owner) const
    {
		int i = 0;
        QList<int> to_add;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		foreach (int id, move.card_ids) {
			if (move.from_places[i]==Player::PlaceHand&&room->getCardPlace(id)==Player::DiscardPile)
				to_add.append(id);
			i++;
		}
		if (to_add.length()>0&&owner->askForSkillInvoke(objectName()+"$1"))
			owner->addToPile("edict", to_add, true, QList<ServerPlayer *>(), move.reason);
        return false;
    }
};

class HantongBf2 : public TriggerSkill
{
public:
    HantongBf2() : TriggerSkill("#hantong")
    {
        events << CardAsked //For JiJiang and HuJia
            << TargetConfirmed //For JiuYuan
            << EventPhaseStart //For XueYi
			<< EventPhaseChanging;
    }

    static void RemoveEdict(ServerPlayer *liuxie)
    {
        Room *room = liuxie->getRoom();
        QList<int> edict = liuxie->getPile("edict");
        liuxie->peiyin("hantong",2);

        LogMessage log;
        log.type = "#InvokeSkill";
        log.arg = "hantong";
        log.from = liuxie;
        room->sendLog(log);
        room->notifySkillInvoked(liuxie, "hantong");

        room->fillAG(edict, liuxie);
        int card_id = room->askForAG(liuxie, edict, false, "hantong");
        room->clearAG(liuxie);

        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, liuxie->objectName(), "hantong_acquire", "");
        room->throwCard(Sanguosha->getCard(card_id), reason, nullptr);
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if(event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			return change.to==Player::NotActive&&owner->isAlive()&&owner->tag.value("Hantong_use").toBool();
		}
        if (owner->getPile("edict").isEmpty())
            return false;
        if(event == CardAsked){
            QString pattern = data.toStringList().first();
			if(pattern == "jink")
				return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
			else if((pattern.contains("slash") || pattern.contains("Slash")) && !target->hasFlag("Global_JijiangFailed"))
				return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }else if(event == TargetConfirmed){
            CardUseStruct use = data.value<CardUseStruct>();
			return use.card->isKindOf("Peach")&&use.to.contains(target)&&use.from->getKingdom()=="wu"
			&&target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }else if(event == EventPhaseStart){
			return target==owner&&owner->isAlive()&&owner->getPhase()==Player::Discard&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liuxie, QVariant &data,ServerPlayer*owner) const
    {
        liuxie->tag["Hantong_use"] = true;
        switch (triggerEvent) {
        case CardAsked: {
            QString pattern = data.toStringList().first();
			if (pattern == "jink") {
                liuxie->tag["HantongOriginData"] = data; // For AI
                if (room->askForSkillInvoke(liuxie, "hantong_acquire", "hujia",false)) {
                    RemoveEdict(liuxie);
                    room->acquireSkill(liuxie, "hujia");
                }
            }else{
                liuxie->tag["HantongOriginData"] = data; // For AI
                if (room->askForSkillInvoke(liuxie, "hantong_acquire", "jijiang",false)) {
                    RemoveEdict(liuxie);
                    room->acquireSkill(liuxie, "jijiang");
                }
			}
            break;
        }case TargetConfirmed: {
            if (room->askForSkillInvoke(liuxie, "hantong_acquire", "jiuyuan",false)) {
                RemoveEdict(liuxie);
                room->acquireSkill(liuxie, "jiuyuan");
            }
            break;
        }case EventPhaseStart: {
            if (room->askForSkillInvoke(liuxie, "hantong_acquire", "xueyi",false)) {
                RemoveEdict(liuxie);
                room->acquireSkill(liuxie, "xueyi");
            }
            break;
        }case EventPhaseChanging: {
            room->handleAcquireDetachSkills(owner, "-hujia|-jijiang|-jiuyuan|-xueyi", true);
            owner->tag.remove("Hantong_use");
        }default:
            break;
        }
        return false;
    }
};

class HantongAcquire : public TriggerSkill
{
public:
    HantongAcquire() : TriggerSkill("#hantong-acquire")
    {
        events << CardAsked //For JiJiang and HuJia
            << TargetConfirmed //For JiuYuan
            << EventPhaseStart; //For XueYi
    }

    static void RemoveEdict(ServerPlayer *liuxie)
    {
        Room *room = liuxie->getRoom();
        QList<int> edict = liuxie->getPile("edict");
        liuxie->peiyin("hantong",2);

        LogMessage log;
        log.type = "#InvokeSkill";
        log.arg = "hantong";
        log.from = liuxie;
        room->sendLog(log);
        room->notifySkillInvoked(liuxie, "hantong");

        room->fillAG(edict, liuxie);
        int card_id = room->askForAG(liuxie, edict, false, "hantong");
        room->clearAG(liuxie);

        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, liuxie->objectName(), "hantong_acquire", "");
        room->throwCard(Sanguosha->getCard(card_id), reason, nullptr);
    }

    int getPriority(TriggerEvent) const
    {
        return 4;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liuxie, QVariant &data) const
    {
        ServerPlayer *current = room->getCurrent();
        if (current == nullptr || !current->isAlive())
            return false;
        if (liuxie->getPile("edict").isEmpty())
            return false;
        switch (triggerEvent) {
        case CardAsked: {
            QString pattern = data.toStringList().first();
            if ((pattern.contains("slash") || pattern.contains("Slash")) && !liuxie->hasFlag("Global_JijiangFailed")) {
                liuxie->tag["HantongOriginData"] = data; // For AI
                if (room->askForSkillInvoke(liuxie, "hantong_acquire", "jijiang",false)) {
                    RemoveEdict(liuxie);
                    room->acquireSkill(liuxie, "jijiang");
                }
            } else if (pattern == "jink") {
                liuxie->tag["HantongOriginData"] = data; // For AI
                if (room->askForSkillInvoke(liuxie, "hantong_acquire", "hujia",false)) {
                    RemoveEdict(liuxie);
                    room->acquireSkill(liuxie, "hujia");
                }
            }
            break;
        }case TargetConfirmed: {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Peach") || !use.from || use.from->getKingdom() != "wu"
                || liuxie == use.from || !liuxie->hasFlag("Global_Dying"))
                return false;

            if (room->askForSkillInvoke(liuxie, "hantong_acquire", "jiuyuan", false)) {
                RemoveEdict(liuxie);
                room->acquireSkill(liuxie, "jiuyuan");
            }
            break;
        }case EventPhaseStart: {
            if (liuxie->getPhase() != Player::Discard)
                return false;
            if (room->askForSkillInvoke(liuxie, "hantong_acquire", "xueyi",false)) {
                RemoveEdict(liuxie);
                room->acquireSkill(liuxie, "xueyi");
            }
            break;
        }default:
            break;
        }
        liuxie->tag["Hantong_use"] = true;
        return false;
    }
};

class HantongDetach : public TriggerSkill
{
public:
    HantongDetach() : TriggerSkill("#hantong-detach")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->tag.value("Hantong_use").toBool()) continue;
            room->handleAcquireDetachSkills(p, "-hujia|-jijiang|-jiuyuan|-xueyi", true);
            p->tag.remove("Hantong_use");
        }
        return false;
    }
};

const Card *HantongCard::validate(CardUseStruct &cardUse) const
{
    cardUse.m_isOwnerUse = false;
    Room *room = cardUse.from->getRoom();

    HantongAcquire::RemoveEdict(cardUse.from);
    cardUse.from->tag["Hantong_use"] = true;
    room->acquireSkill(cardUse.from, "jijiang");
    if (!room->askForUseCard(cardUse.from, "@jijiang", "@hantong-jijiang")) {
        room->setPlayerFlag(cardUse.from, "Global_JijiangFailed");
        return nullptr;
    }
	return this;
}

void HantongCard::onUse(Room *, CardUseStruct &) const
{
}

DIYYicongCard::DIYYicongCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void DIYYicongCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("retinue", this);
}

class DIYYicongViewAsSkill : public ViewAsSkill
{
public:
    DIYYicongViewAsSkill() : ViewAsSkill("diyyicong")
    {
        response_pattern = "@@diyyicong";
    }

    bool viewFilter(const QList<const Card *> &, const Card *) const
    {
        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 0)
            return nullptr;

        Card *acard = new DIYYicongCard;
        acard->addSubcards(cards);
        return acard;
    }
};

class DIYYicong : public TriggerSkill
{
public:
    DIYYicong() : TriggerSkill("diyyicong")
    {
        events << EventPhaseEnd;
        view_as_skill = new DIYYicongViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *gongsunzan, QVariant &) const
    {
        if (gongsunzan->getPhase() == Player::Discard && !gongsunzan->isNude()) {
            room->askForUseCard(gongsunzan, "@@diyyicong", "@diyyicong", -1, Card::MethodNone);
        }
        return false;
    }
};

class DIYYicong2 : public TriggerSkill
{
public:
    DIYYicong2() : TriggerSkill("diyyicong")
    {
        events << EventPhaseEnd;
        view_as_skill = new DIYYicongViewAsSkill;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event == EventPhaseEnd){
			return target->getPhase()==Player::Discard&&!target->isNude()
			&&target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *gongsunzan, QVariant &) const
    {
        room->askForUseCard(gongsunzan, "@@diyyicong", "@diyyicong", -1, Card::MethodNone);
        return false;
    }
};

class DIYYicongDistance : public DistanceSkill
{
public:
    DIYYicongDistance() : DistanceSkill("#diyyicong-dist")
    {
    }

    int getCorrect(const Player *, const Player *to) const
    {
        int n = to->getPile("retinue").length();
		if (n>0&&to->hasSkill("diyyicong"))
            return n;
        return 0;
    }
};

class Tuqi : public TriggerSkill
{
public:
    Tuqi() : TriggerSkill("tuqi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gongsunzan, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (gongsunzan->getPhase() == Player::Start && gongsunzan->getPile("retinue").length() > 0) {
                room->sendCompulsoryTriggerLog(gongsunzan, objectName());

                int n = gongsunzan->getPile("retinue").length();
                room->setPlayerMark(gongsunzan, "tuqi_dist-Clear", n);
                gongsunzan->clearOnePrivatePile("retinue");

                int index = 1;

                if (n <= 2) {
                    index++;
                    gongsunzan->drawCards(1, objectName());
                }
                room->broadcastSkillInvoke(objectName(), index);
            }
        }
        return false;
    }
};

class Tuqi2 : public TriggerSkill
{
public:
    Tuqi2() : TriggerSkill("tuqi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event == EventPhaseStart){
			return target->getPhase()==Player::Start&&target->getPile("retinue").length() > 0
			&&target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gongsunzan, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart) {
			int n = gongsunzan->getPile("retinue").length();
			int index = 1;
			if (n <= 2) index++;
			room->sendCompulsoryTriggerLog(gongsunzan, this,index);
			room->setPlayerMark(gongsunzan, "tuqi_dist-Clear", n);
			gongsunzan->clearOnePrivatePile("retinue");
			if (n <= 2) gongsunzan->drawCards(1, objectName());
        }
        return false;
    }
};

class TuqiDistance : public DistanceSkill
{
public:
    TuqiDistance() : DistanceSkill("#tuqi-dist")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        int n = from->getMark("tuqi_dist-Clear");
		if (n>0&&from->hasSkill("tuqi"))
            return -n;
        return 0;
    }
};

BGMDIYPackage::BGMDIYPackage()
 : Package("BGMDIY")
{
    General *diy_simazhao = new General(this, "diy_simazhao", "wei", 3); // DIY 001
    diy_simazhao->addSkill(new Zhaoxin);
    diy_simazhao->addSkill(new Langgu);

    General *diy_wangyuanji = new General(this, "diy_wangyuanji", "wei", 3, false); // DIY 002
    diy_wangyuanji->addSkill(new Fuluan);
    diy_wangyuanji->addSkill(new Shude);

    General *diy_liuxie = new General(this, "diy_liuxie", "qun"); // DIY 003
    diy_liuxie->addSkill(new Huangen);
    diy_liuxie->addSkill(new Hantong);
    diy_liuxie->addSkill(new HantongAcquire);
    diy_liuxie->addSkill(new HantongDetach);
    related_skills.insertMulti("hantong", "#hantong-acquire");
    related_skills.insertMulti("hantong", "#hantong-detach");

    General *diy_gongsunzan = new General(this, "diy_gongsunzan", "qun"); // DIY 004
    diy_gongsunzan->addSkill(new DIYYicong);
    diy_gongsunzan->addSkill(new DIYYicongDistance);
    diy_gongsunzan->addSkill(new Tuqi);
    diy_gongsunzan->addSkill(new TuqiDistance);
    related_skills.insertMulti("diyyicong", "#diyyicong-clear");
    related_skills.insertMulti("diyyicong", "#diyyicong-dist");
    related_skills.insertMulti("tuqi", "#tuqi-dist");

    General *diy_zhugeke = new General(this, "diy_zhugeke", "wu", 3, true);
    diy_zhugeke->addSkill("aocai");
    diy_zhugeke->addSkill("duwu");

    addMetaObject<ZhaoxinCard>();
    addMetaObject<FuluanCard>();
    addMetaObject<HuangenCard>();
    addMetaObject<HantongCard>();
    addMetaObject<DIYYicongCard>();
}
ADD_PACKAGE(BGMDIY)