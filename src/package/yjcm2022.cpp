#include "yjcm2022.h"
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

class Liandui : public TriggerSkill
{
public:
    Liandui() : TriggerSkill("liandui")
    {
        events << CardUsed << CardResponded;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = nullptr;
        if (event == CardUsed)
            card = data.value<CardUseStruct>().card;
        else {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (!res.m_isUse) return false;
            card = res.m_card;
        }
        if (!card || card->isKindOf("SkillCard")) return false;

        ServerPlayer *last = room->getTag("ChongwangLastUser").value<ServerPlayer *>();
        if (!last||player==last||!last->isAlive()) return false;

        if (player->hasSkill(objectName())) {
            if (player->askForSkillInvoke(this, "liandui:" + last->objectName())) {
                player->peiyin(this);
                last->drawCards(2, objectName());
				if (!last->isAlive()) return false;
            }
        }

        if (last->hasSkill(objectName())) {
            if (player->askForSkillInvoke("lianduiother", "lianduiother:" + last->objectName())) {
                LogMessage log;
                log.type = "#InvokeOthersSkill";
                log.arg = objectName();
                log.from = player;
                log.to << last;
                room->sendLog(log);
                last->peiyin(this);
                room->notifySkillInvoked(last, objectName());
                last->drawCards(2, objectName());
            }
        }
        return false;
    }
};

BiejunCard::BiejunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
	mute = true;
}

bool BiejunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getMark("biejunTarget-PlayClear") <= 0 && to_select->hasSkill("biejun");
}

void BiejunCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to){
		use.from->skillInvoked("biejun",-1,p);
		room->giveCard(use.from, p, this, "biejun");
		if(p->hasCard(subcards.last())&&room->hasCurrent())
			room->setCardTip(subcards.last(),"biejun-Clear");
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class BiejunGive : public OneCardViewAsSkill
{
public:
    BiejunGive() : OneCardViewAsSkill("biejun-give&")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getHandcardNum()>0&&player->usedTimes("BiejunCard")<1;
    }

    const Card *viewAs(const Card *c) const
    {
        BiejunCard *card = new BiejunCard;
        card->addSubcard(c);
        return card;
    }
};

class Biejun : public TriggerSkill
{
public:
    Biejun() : TriggerSkill("biejun")
    {
        global = true;
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill << DamageInflicted;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == DamageInflicted){
			if (player->getMark("biejunDamage-Clear") > 0) return false;
			player->addMark("biejunDamage-Clear");
			foreach (const Card*h, player->getHandcards()) {
				if (h->hasTip("biejun"))
					return false;
			}
			if (player->hasSkill(objectName()) && player->askForSkillInvoke("biejun$-1", data)) {
				player->turnOver();
	
				DamageStruct damage = data.value<DamageStruct>();
				LogMessage log;
				log.type = damage.from ? "#BiejunPrevent1" : "#BiejunPrevent2";
				log.from = player;
				log.to << damage.from;
				log.arg = QString::number(damage.damage);
				room->sendLog(log);
				return true;
			}
		}else if (triggerEvent == EventAcquireSkill)
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("biejun-give",true)&&player->hasSkill(objectName(),true)){
					room->attachSkillToPlayer(p, "biejun-give");
			}
		}else if(player->getPhase()==Player::Play){
			if (triggerEvent == EventPhaseStart) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->hasSkill(objectName(),true)){
						room->attachSkillToPlayer(player, "biejun-give");
						break;
					}
				}
			}else{
				if (player->hasSkill("biejun-give",true))
					room->detachSkillFromPlayer(player, "biejun-give", true);
			}
		}
        return false;
    }
};

class Sangu : public TriggerSkill
{
public:
    Sangu() : TriggerSkill("sangu")
    {
        events << EventPhaseStart << CardUsed << EventPhaseChanging << CardsMoveOneTime << EventPhaseEnd;
        global = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {

        if (event == EventPhaseStart) {
			QStringList sangus = player->getTag("SanguCards").toStringList();
            if (player->getPhase() != Player::Play || sangus.isEmpty()) return false;

			LogMessage log;
			log.from = player;
			log.type = "#SanguCard2";
			log.arg3 = "sangu";
			for (int i = 1; i <= sangus.length(); i++) {
                log.arg = QString::number(i);
                log.arg2 = sangus[i-1];
                room->sendLog(log);
            }
            foreach (const Card *h, player->getHandcards()) {
                Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                c->setSkillName("sangu");
                WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                card->takeOver(c);
                room->notifyUpdateCard(player, h->getId(), card);
            }
        } else if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
			ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@sangu-invoke", true, true);
			if (!t) return false;
			player->peiyin(this);
	
			QStringList names;
			QList<int> cards,ids = Sanguosha->getRandomCards();
			for (int i = 0; i < Sanguosha->getCardCount(); i++) {
				if (!ids.contains(i)) continue;
				const Card *c = Sanguosha->getEngineCard(i);
				if (names.contains(c->objectName())) continue;
				if (c->isKindOf("Slash")) {
					if (names.contains("slash")) continue;
					names << "slash";
					cards << i;
				} else if (c->isNDTrick() && !c->isKindOf("Collateral") && !c->isKindOf("Nullification")) {
					names << c->objectName();
					cards << i;
				}
			}
	
			QStringList choices;
			for (int i = 0; i < 3; i++) {
				if (cards.isEmpty() || player->isDead()) break;
				room->fillAG(cards, player);
				int id = room->askForAG(player, cards, i != 0, objectName(), "@sangu-card");
				room->clearAG(player);
				if (id < 0) break;
				cards.removeOne(id);
	
				choices << Sanguosha->getEngineCard(id)->objectName();
	
				LogMessage log;
				log.type = "#SanguCard";
				log.arg = choices.last();
				log.from = player;
				room->sendLog(log);
			}
	
			if (choices.isEmpty()) return false;
			t->setTag("SanguCards", choices);
	
			foreach (QString name, choices) {
				if (player->getMark("SanguRecord_" + name + "-PlayClear") <= 0) {
					room->loseHp(HpLostStruct(player, 1, objectName(), player));
					break;
				}
			}
        } else if (event == CardUsed) {
            if (player->getPhase() != Player::Play) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
			QString name = use.card->objectName();
			if (use.card->isKindOf("Slash")) name = "slash";
			player->addMark("SanguRecord_" + name + "-PlayClear");
			QStringList sangus = player->getTag("SanguCards").toStringList();

            if (sangus.isEmpty()) return false;

            name = sangus.first();
            sangus.removeOne(name);
			player->setTag("SanguCards", sangus);

            if (sangus.isEmpty())
                room->filterCards(player, player->getHandcards(), true);
            else {
				foreach (const Card *h, player->getHandcards()) {
                    Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                    c->setSkillName("sangu");
                    WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                    card->takeOver(c);
                    room->notifyUpdateCard(player, h->getId(), card);
                }
            }
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().from != Player::Play) return false;
            player->removeTag("SanguCards");
			QList<const Card *>fc;
			foreach (const Card *h, player->getHandcards()) {
				if(h->getSkillName()=="sangu") fc << h;
			}
            room->filterCards(player, fc, true);
        } else if (event == CardsMoveOneTime) {
			QStringList sangus = player->getTag("SanguCards").toStringList();
            if (sangus.isEmpty()||player->getPhase()!=Player::Play) return false;
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand) {
				foreach (const Card *h, player->getHandcards()) {
                    Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                    c->setSkillName("sangu");
                    WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                    card->takeOver(c);
                    room->notifyUpdateCard(player, h->getId(), card);
                }
            }
        }
        return false;
    }
};

class Yizu : public TriggerSkill
{
public:
    Yizu() : TriggerSkill("yizu")
    {
        events << TargetConfirmed;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!room->hasCurrent() || player->getMark("yizuTrigger-Clear") > 0) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || use.from->isDead() || player->getHp() > use.from->getHp() || !player->isWounded() || !use.to.contains(player)) return false;
        if (use.card->isKindOf("Slash") || use.card->isKindOf("Duel")) {
            room->addPlayerMark(player, "yizuTrigger-Clear");
            room->sendCompulsoryTriggerLog(player, this);
            room->recover(player, RecoverStruct("yizu", player));
        }
        return false;
    }
};

class BushiLK : public TriggerSkill
{
public:
    BushiLK() : TriggerSkill("bushilk")
    {
        events << EventPhaseStart << CardUsed << CardFinished << PostCardResponded << TargetConfirmed;
        waked_skills = "#bushilk";
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Start) {
                if (!player->askForSkillInvoke(this, "bushilk")) return false;
                player->peiyin(this);

                QStringList suits, changes, args;
                suits << "heart" << "club" << "diamond" << "spade";
                changes << "cishu" << "shiyong" << "wuxiao" << "huode";

                int i = 0;
                foreach (QString change, changes) {
                    i++;
                    QStringList choices;
                    foreach (QString suit, suits)
                        choices << change + "=" + suit;
                    QString choice = room->askForChoice(player, objectName(), choices.join("+"));
                    QString suit = choice.split("=").last();
                    suits.removeOne(suit);
                    args << suit;

                    QString property = "SkillDescriptionSuit" + QString::number(i) + "_bushilk";
                    room->setPlayerProperty(player, property.toStdString().c_str(), suit);
					player->setSkillDescriptionSwap("bushilk","%suit"+QString::number(i),suit+"_char");
                }

                room->changeTranslation(player, objectName(), 1);

                LogMessage log;
                log.type = "#BushiChange";
                log.from = player;
                log.arg = objectName();
                log.arg2 = args[0];
                log.arg3 = args[1];
                log.arg4 = args[2];
                log.arg5 = args[3];
                room->sendLog(log);
            } else if (player->getPhase() == Player::Finish) {
                QString suit = player->property("SkillDescriptionSuit4_bushilk").toString();
                if (suit.isEmpty())
                    suit = "diamond";

                QList<int> ids;
                foreach (int id, room->getDrawPile()) {
                    if (Sanguosha->getCard(id)->getSuitString() == suit)
                        ids << id;
                }
                if (ids.isEmpty()) return false;
                room->sendCompulsoryTriggerLog(player, this);
                int id = ids.at(qrand() % ids.length());
                room->obtainCard(player, id);
            }
        } else if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            QString suit = player->property("SkillDescriptionSuit1_bushilk").toString();
            if (suit.isEmpty()) suit = "spade";
            if (use.card->getSuitString() != suit) return false;
            use.m_addHistory = false;
            data = QVariant::fromValue(use);
        } else if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            QString suit = player->property("SkillDescriptionSuit2_bushilk").toString();
            if (suit.isEmpty())
                suit = "heart";
            if (use.card->getSuitString() != suit) return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->drawCards(1, objectName());
        } else if (event == PostCardResponded) {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (res.m_card->isKindOf("SkillCard")) return false;
            QString suit = player->property("SkillDescriptionSuit2_bushilk").toString();
            if (suit.isEmpty())
                suit = "heart";
            if (res.m_card->getSuitString() != suit) return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->drawCards(1, objectName());
        } else if (event == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard") || !use.to.contains(player)) return false;
            QString suit = player->property("SkillDescriptionSuit3_bushilk").toString();
            if (suit.isEmpty())
                suit = "club";
            if (use.card->getSuitString() != suit) return false;
            if (player->canDiscard(player, "h") && room->askForCard(player, ".|.|.|hand", "@bushilk:" + use.card->objectName(), data, objectName())) {
                player->peiyin(this);
                use.nullified_list << player->objectName();
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class BushiLKTargetMod : public TargetModSkill
{
public:
    BushiLKTargetMod() : TargetModSkill("#bushilk")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        QString suit = from->property("SkillDescriptionSuit1_bushilk").toString();
        if (suit.isEmpty()) suit = "spade";
        if (card->getSuitString() == suit && from->hasSkill("bushilk"))
            return 1000;
        return 0;
    }
};

class Zhongzhuang : public TriggerSkill
{
public:
    Zhongzhuang() : TriggerSkill("zhongzhuang")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || !damage.by_user) return false;
        int attack = player->getAttackRange();
        if (attack > 3) {
            room->sendCompulsoryTriggerLog(player, this);
            damage.damage++;
            data = QVariant::fromValue(damage);
        } else if (attack < 3) {
            if (damage.damage <= 1) return false;
            room->sendCompulsoryTriggerLog(player, this);
            damage.damage = 1;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class Koujing : public TriggerSkill
{
public:
    Koujing() : TriggerSkill("koujing")
    {
        events << Damaged << EventPhaseChanging << EventPhaseStart;
        waked_skills = "#koujing-target";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") || use.card->getSkillNames().contains("koujing")) return false;
            use.m_addHistory = false;
            data = QVariant::fromValue(use);
        } else if (event == EventPhaseStart) {
			if(player->isAlive()&&player->getPhase()==Player::Play&&!player->isKongcheng()&&player->hasSkill(objectName())){
				const Card *c = room->askForExchange(player, objectName(), 999, 1, false, "@koujing-slash", true);
				if (!c) return false;
		
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player, objectName());
				foreach (int id, c->getSubcards()) {
					room->addPlayerMark(player, "koujing_slash_" + QString::number(id) + "-Clear");
					const Card *cc = Sanguosha->getCard(id);
					Slash *c = new Slash(cc->getSuit(), cc->getNumber());
					c->setSkillName("koujing");
					WrappedCard *card = Sanguosha->getWrappedCard(id);
					card->takeOver(c);
					room->notifyUpdateCard(player, id, card);
				}
			}
        } else if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card||!damage.card->isKindOf("Slash")||!damage.card->getSkillNames().contains("koujing")) return false;

            ServerPlayer *user = room->getCardUser(damage.card);
            if (!user || user->isDead()) return false;

            QList<int> shows;
            foreach (int id, user->handCards()) {
                if (user->getMark("koujing_slash_" + QString::number(id) + "-Clear") > 0)
                    shows << id;
            }
            if (shows.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(user, "koujing", true, true);
            room->showCard(user, shows);

            if (player->isDead() || player->isKongcheng()) return false;
            room->fillAG(shows, player);
            player->setTag("KoujingShowCards", ListI2V(shows));
            bool invoke = player->askForSkillInvoke("koujing", "koujing", false);
            room->clearAG(player);
            if (!invoke) return false;

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(player->handCards(), user, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), user->objectName(), "koujing", ""));
            CardsMoveStruct move2(shows, player, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, user->objectName(), player->objectName(), "koujing", ""));
            exchangeMove.push_back(move1);
            exchangeMove.push_back(move2);
            room->moveCardsAtomic(exchangeMove, false);
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            QList<const Card *> shows;
            foreach (const Card *c, player->getCards("he")) {
				if(c->getSkillName()=="koujing")
					shows << c;
			}
            room->filterCards(player, shows, true);
        } else if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.from_places.contains(Player::PlaceHand)) {
                for (int i = 0; i < move.card_ids.length(); i++) {
                    if (move.from_places.at(i) == Player::PlaceHand) {
                        int id = move.card_ids.at(i);
                        room->setPlayerMark(player, "koujing_slash_" + QString::number(id) + "-Clear", 0);
                    }
                }
            }
        }
        return false;
    }
};

class KoujingTargetMod : public TargetModSkill
{
public:
    KoujingTargetMod() : TargetModSkill("#koujing-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "koujing")
            return 1000;
		if(card->isKindOf("Slash")&&from->getMark("diezhang")<1&&from->hasSkill("diezhang"))
			return 1;
        return 0;
    }
};

class Diezhang : public TriggerSkill
{
public:
    Diezhang() : TriggerSkill("diezhang")
    {
        events << CardOffset;
		change_skill = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    void diezhangEffect(ServerPlayer *player, ServerPlayer *to, int n, int x) const
    {
        Room *room = player->getRoom();
		if(n==1){
			if(player->canDiscard(player,"he")
			&&room->askForCard(player,"..","diezhang0:1:"+to->objectName(),QVariant(),objectName()+"%-1")){
				if(x==1) room->setChangeSkillState(player, objectName(), 2);
				else player->addMark("diezhangUse-Clear");
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_diezhang");
				for (int i = 0; i < x; i++) {
					if(player->canSlash(to,dc,false))
						room->useCard(CardUseStruct(dc,player,to));
				}
				dc->deleteLater();
			}
		}else{
			if(player->askForSkillInvoke(this,to)){
				if(x==1) room->setChangeSkillState(player, objectName(), 2);
				else player->addMark("diezhangUse-Clear");
				player->peiyin(this);
				player->drawCards(x,objectName());
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_diezhang");
				if(player->canSlash(to,dc,false))
					room->useCard(CardUseStruct(dc,player,to));
				dc->deleteLater();
			}
		}
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            CardUseStruct use = room->getUseStruct(effect.offset_card);
            if (use.from!=player&&use.from->isAlive()&&player->hasTurn()){
				if(player->hasSkill(objectName())){
					if(player->getMark("diezhang")<1){
						if(player->getChangeSkillState(objectName()) == 1)
							diezhangEffect(player,use.from,1,1);
					}else if(player->getMark("diezhangUse-Clear")<1){
						if(player->getChangeSkillState(objectName()) == 2)
							diezhangEffect(player,use.from,1,2);
					}
				}
				if(use.from->hasSkill(objectName())){
					if(use.from->getMark("diezhang")<1){
						if(use.from->getChangeSkillState(objectName()) == 2)
							diezhangEffect(use.from,player,2,1);
					}else if(use.from->getMark("diezhangUse-Clear")<1){
						if(use.from->getChangeSkillState(objectName()) == 1)
							diezhangEffect(use.from,player,2,2);
					}
				}
			}
        }
        return false;
    }
};

class Duanwan : public TriggerSkill
{
public:
    Duanwan() : TriggerSkill("duanwan")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@duanwan";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.who!=player||player->getMark("@duanwan")<1||!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, objectName());
        room->removePlayerMark(player, "@duanwan");
        int n = qMin(2, dying.who->getMaxHp()) - dying.who->getHp();
        room->recover(dying.who, RecoverStruct(player, nullptr, n, objectName()));
		n = player->getChangeSkillState("diezhang");
		room->addPlayerMark(player,"diezhang");
		n += 2;
		room->changeTranslation(player, "diezhang", n);
		foreach (QString m, player->getMarkNames()) {
			if(m.contains("&diezhang+"))
				room->setPlayerMark(player,m,0);
		}
        return false;
    }
};

class Duwang : public TriggerSkill
{
public:
    Duwang() : TriggerSkill("duwang")
    {
        events << GameStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->sendCompulsoryTriggerLog(player,this);
		Card*dc = Sanguosha->cloneCard("slash");
		foreach (int id, room->getDrawPile()) {
			if(!Sanguosha->getCard(id)->isKindOf("Slash")){
				dc->addSubcard(id);
				if(dc->subcardsLength()>4)
					break;
			}
		}
		player->addToPile("dw_ci",dc);
		dc->deleteLater();
        return false;
    }
};

class DuwangBf : public DistanceSkill
{
public:
    DuwangBf() : DistanceSkill("#duwangbf")
    {
    }

    int getFixed(const Player *from, const Player *to) const
    {
        int n = 0;
		if (from->getPile("dw_ci").length()>0&&from->hasSkill("duwang"))
            n++;
		if (to->getPile("dw_ci").length()>0&&to->hasSkill("duwang"))
            n++;
        return n;
    }
};

class Cibei : public TriggerSkill
{
public:
    Cibei() : TriggerSkill("cibei")
    {
        events << CardFinished << EventPhaseChanging << CardUsed;
		waked_skills = "#cibei_limit,#cibei_mod";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->hasFlag("cibeiBf") || !use.card->hasFlag("DamageDone")) return false;
            if (use.card->getEffectiveId()<0) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> can,ids = p->getPile("dw_ci");
				if(ids.isEmpty()||room->getCardOwner(use.card->getEffectiveId())||!p->hasSkill(objectName())) continue;
				foreach (int id, ids) {
					if(Sanguosha->getCard(id)->isKindOf("Slash")) continue;
					can << id;
				}
				if(can.length()>0&&p->askForSkillInvoke(this)){
					p->peiyin(this);
					room->fillAG(can,p);
					int id = room->askForAG(p,can,can.length()<2,objectName());
					if(id<0) id = can.first();
					room->clearAG(p);
					room->throwCard(id,objectName(),nullptr);
					p->addToPile("dw_ci",use.card);
					if(p->isDead()) continue;
					QList<ServerPlayer *>aps;
					foreach (ServerPlayer *q, room->getAllPlayers()) {
						if(p->canDiscard(q,"hej")) aps << q;
					}
					ServerPlayer *to = room->askForPlayerChosen(p,aps,objectName(),"cibei0:");
					if(to){
						room->doAnimate(1,p->objectName(),to->objectName());
						int id = room->askForCardChosen(p,to,"hej",objectName(),false,Card::MethodDiscard);
						if(id>-1) room->throwCard(id,objectName(),to,p);
					}
				}
			}
        }else if(event == CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;
			room->setCardFlag(use.card,"cibeiBf");
        }else{
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> ids = p->getPile("dw_ci");
				if(ids.isEmpty()||!p->hasSkill(objectName())) continue;
				QStringList can;
				foreach (int id, ids) {
					if(Sanguosha->getCard(id)->isKindOf("Slash"))
						can << QString::number(id);
				}
				if(can.length()>=ids.length()){
					room->sendCompulsoryTriggerLog(p,this);
					Card*dc = Sanguosha->cloneCard("slash");
					dc->addSubcards(ids);
					room->obtainCard(p,dc);
					dc->deleteLater();
					room->setPlayerProperty(p,"cibeiIds",can.join(","));
				}
			}
		}
        return false;
    }
};

class CibeiLimit : public CardLimitSkill
{
public:
    CibeiLimit() : CardLimitSkill("#cibei_limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "discard,ignore";
    }

    QString limitPattern(const Player *target) const
    {
        QString ids = target->property("cibeiIds").toString();
        if (!ids.isEmpty()&&target->hasSkill("cibei")) return ids;
		return "";
    }
};

class CibeiMod : public TargetModSkill
{
public:
    CibeiMod() : TargetModSkill("#cibei_mod")
    {
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        QStringList ids = from->property("cibeiIds").toString().split(",");
		if (ids.contains(card->toString())&&from->hasSkill("cibei"))
            return 1000;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        QStringList ids = from->property("cibeiIds").toString().split(",");
		if (ids.contains(card->toString())&&from->hasSkill("cibei"))
            return 1000;
        return 0;
    }
};

ShujianCard::ShujianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ShujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void ShujianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = from->getRoom();
    room->giveCard(from, to, this, getSkillName());
	int x = 3, n = from->getMark("shujianNum-PlayClear");
	x -= n;
	Card*dc = Sanguosha->cloneCard("dismantlement");
	dc->setSkillName("_shujian");
	QString p = "shujian1="+QString::number(x)+"+shujian2="+QString::number(x);
	if(to->canUse(dc)&&room->askForChoice(to,getSkillName(),p,QVariant::fromValue(from)).contains("shujian2")){
		for (int i = 0; i < x; i++) {
			room->askForUseCard(to,"@@shujian","shujian0:");
		}
		room->setPlayerMark(from,"shujianBan-PlayClear",1);
	}else{
		from->drawCards(x,getSkillName());
		x--;
		if(x>0)
			room->askForDiscard(from,getSkillName(),x,x,false,true);
	}
	dc->deleteLater();
	foreach (QString m, from->getMarkNames()) {
		if(m.contains("&shujian+-"))
			room->setPlayerMark(from,m,0);
	}
	n++;
	from->addMark("shujianNum-PlayClear");
	room->setPlayerMark(from,"&shujian+-"+QString::number(n)+"-PlayClear",1);
}

class Shujian : public ViewAsSkill
{
public:
    Shujian() : ViewAsSkill("shujian")
    {
        response_pattern = "@@shujian";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("ShujianCard")<3&&player->getMark("shujianBan-PlayClear")<1;
    }

    bool viewFilter(const QList<const Card *> &cards, const Card *) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@shujian") return false;
        return cards.isEmpty();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@shujian") {
			Card*dc = Sanguosha->cloneCard("dismantlement");
			dc->setSkillName("_shujian");
			return dc;
		}
        if (cards.isEmpty()) return nullptr;
		ShujianCard *card = new ShujianCard;
        card->addSubcards(cards);
        return card;
    }
};






YJCM2022Package::YJCM2022Package()
    : Package("YJCM2022")
{
    General *liwan = new General(this, "liwan", "wei", 3, false);
    liwan->addSkill(new Liandui);
    liwan->addSkill(new Biejun);
    related_skills.insertMulti("liandui", "#chongwang");

    General *zhugeshang = new General(this, "zhugeshang", "shu", 3);
    zhugeshang->addSkill(new Sangu);
    zhugeshang->addSkill(new Yizu);

    General *lukai = new General(this, "lukai", "wu", 4);
    lukai->addSkill(new BushiLK);
    lukai->addSkill(new BushiLKTargetMod);
    lukai->addSkill(new Zhongzhuang);

    General *kebineng = new General(this, "kebineng", "qun", 4);
    kebineng->addSkill(new Koujing);
    kebineng->addSkill(new KoujingTargetMod);

    General *wuanguo = new General(this, "wuanguo", "qun", 4);
    wuanguo->addSkill(new Diezhang);
    wuanguo->addSkill(new Duanwan);

    General *hanlong = new General(this, "hanlong", "wei", 4);
    hanlong->addSkill(new Duwang);
    hanlong->addSkill(new DuwangBf);
    hanlong->addSkill(new Cibei);
    hanlong->addSkill(new CibeiLimit);
    hanlong->addSkill(new CibeiMod);

    General *th_sufei = new General(this, "th_sufei", "wu", 4);
    th_sufei->addSkill(new Shujian);
    addMetaObject<ShujianCard>();

    addMetaObject<BiejunCard>();

    skills << new BiejunGive;
}

ADD_PACKAGE(YJCM2022)