#include "mobile-strengthen.h"
#include "settings.h"
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
#include "yjcm2013.h"
#include "yjcm2014.h"
//#include "json.h"

class MobileLiyong : public TriggerSkill
{
public:
    MobileLiyong() : TriggerSkill("mobileliyong")
    {
        events << CardOffset << TargetSpecified << ConfirmDamage << Damage;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card->isKindOf("Slash")||player->getPhase()!=Player::Play||!player->hasSkill(objectName())) return false;
            room->setPlayerMark(player, "&mobileliyong-PlayClear", 1);
        } else if (event == TargetSpecified) {
            if (player->getPhase() != Player::Play || player->getMark("&mobileliyong-PlayClear") <= 0) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;
            room->setPlayerMark(player, "&mobileliyong-PlayClear", 0);

            if (use.to.isEmpty()) return false;
            LogMessage log;
            log.type = "#MobileliyongSkillInvalidity";
            log.from = player;
            log.to = use.to;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());

            use.no_respond_list << "_ALL_TARGETS";
            room->setCardFlag(use.card, "mobileliyong_damage");

            foreach (ServerPlayer *p, use.to) {
                if (p->isDead()) continue;
                p->addMark("mobileliyong");
                room->addPlayerMark(p, "@skill_invalidity");
            }
            data = QVariant::fromValue(use);
        } else if (event == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card&&damage.card->hasFlag("mobileliyong_damage"))
				++damage.damage;
            data = QVariant::fromValue(damage);
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card&&damage.card->hasFlag("mobileliyong_damage")&&damage.to->isAlive()){
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->loseHp(HpLostStruct(player, 1, objectName(), player));
			}
        }
        return false;
    }
};

class MobileLiyongClear : public TriggerSkill
{
public:
    MobileLiyongClear() : TriggerSkill("#mobileliyong-clear")
    {
        events << EventPhaseChanging << Death;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent) const
    {
        return 5;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != target || target->getPhase() != Player::Play)
                return false;
        }
        foreach (ServerPlayer *player, room->getAllPlayers()) {
            if (player->getMark("mobileliyong")<1) continue;
            room->removePlayerMark(player, "@skill_invalidity", player->getMark("mobileliyong"));
            player->setMark("mobileliyong", 0);

            foreach(ServerPlayer *p, room->getAllPlayers())
                room->filterCards(p, p->getCards("he"), false);
            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        }
        return false;
    }
};

MobileQingjianCard::MobileQingjianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileQingjianCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->isDead() || effect.to->isDead()) return;
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "mobileqingjian", "");
    effect.from->getRoom()->obtainCard(effect.to, this, reason, false);
	effect.from->addMark("mobileqingjian_num",subcardsLength());
}

class MobileQingjianVS : public ViewAsSkill
{
public:
    MobileQingjianVS() : ViewAsSkill("mobileqingjian")
    {
        expand_pile = "mobileqingjian";
        response_pattern = "@@mobileqingjian!";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return Self->getPile("mobileqingjian").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        MobileQingjianCard *c = new MobileQingjianCard;
        c->addSubcards(cards);
        return c;
    }
};

class MobileQingjian : public TriggerSkill
{
public:
    MobileQingjian() : TriggerSkill("mobileqingjian")
    {
        events << EventPhaseChanging << CardsMoveOneTime;
        view_as_skill = new MobileQingjianVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!room->getTag("FirstRound").toBool() && player->getPhase() != Player::Draw
			&& move.to == player && move.to_place == Player::PlaceHand&&player->isAlive()&&player->hasSkill(objectName())) {
				if (!room->hasCurrent()) return false;
				if (player->isKongcheng() || player->getMark("mobileqingjian-Clear") > 0) return false;
				const Card *c = room->askForExchange(player, "mobileqingjian", 99999, 1, false, "@mobileqingjian-put", true);
				if (!c) return false;
				room->broadcastSkillInvoke("mobileqingjian");
				room->addPlayerMark(player, "mobileqingjian-Clear");
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = "mobileqingjian";
				room->sendLog(log);
				room->notifySkillInvoked(player, "mobileqingjian");
				player->addToPile("mobileqingjian", c);
			}
		}else{
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || p->getPile("mobileqingjian").isEmpty()) continue;
				p->setMark("mobileqingjian_num",0);
				while (!p->getPile("mobileqingjian").isEmpty()) {
					if (p->isDead()) break;
					if (!room->askForUseCard(p, "@@mobileqingjian!", "@mobileqingjian")) {
						ServerPlayer *target = room->getOtherPlayers(p).at(qrand() % room->getOtherPlayers(p).length());
						LogMessage log;
						log.type = "#ChoosePlayerWithSkill";
						log.from = p;
						log.to << target;
						log.arg = "mobileqingjian";
						room->sendLog(log);
						room->broadcastSkillInvoke(objectName());
						room->notifySkillInvoked(p, "mobileqingjian");
						room->doAnimate(1, p->objectName(), target->objectName());
						DummyCard *dummy = new DummyCard(p->getPile("mobileqingjian"));
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, p->objectName(), target->objectName(), "mobileqingjian", "");
						room->obtainCard(target, dummy, reason, false);
						p->addMark("mobileqingjian_num",dummy->subcardsLength());
						delete dummy;
					}
				}
				if (p->getMark("mobileqingjian_num") > 1)
					p->drawCards(1, objectName());
			}
		}
        return false;
    }
};

class MobileFenji : public TriggerSkill
{
public:
    MobileFenji() : TriggerSkill("mobilefenji")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(objectName()) || !player->isKongcheng()) continue;
            if (!p->askForSkillInvoke(this, player)) continue;
            p->peiyin(this);
            player->drawCards(2, objectName());
            room->loseHp(HpLostStruct(p, 1, objectName(), p));
        }
        return false;
    }
};

MobileQiangxiCard::MobileQiangxiCard()
{
}

bool MobileQiangxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return Self->inMyAttackRange(to_select, subcards) && targets.isEmpty()
	&& to_select != Self && to_select->getMark("mobileqiangxi_used-PlayClear") <= 0;
}

void MobileQiangxiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->addPlayerMark(effect.to, "mobileqiangxi_used-PlayClear");

    if (subcards.isEmpty())
        room->loseHp(HpLostStruct(effect.from, 1, "mobileqiangxi", effect.from));

    room->damage(DamageStruct("mobileqiangxi", effect.from, effect.to));
}

class MobileQiangxi : public ViewAsSkill
{
public:
    MobileQiangxi() : ViewAsSkill("mobileqiangxi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        foreach (const Player *p, player->getAliveSiblings()) {
            if (player->inMyAttackRange(p) && p->getMark("mobileqiangxi_used-PlayClear") <= 0)
                return true;
        }
        return false;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.isEmpty() && to_select->isKindOf("Weapon") && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return new MobileQiangxiCard;
        else if (cards.length() == 1) {
            MobileQiangxiCard *card = new MobileQiangxiCard;
            card->addSubcards(cards);
            return card;
        }
		return nullptr;
    }
};

class MobileJieming : public MasochismSkill
{
public:
    MobileJieming() : MasochismSkill("mobilejieming")
    {
    }

    void onDamaged(ServerPlayer *xunyu, const DamageStruct &damage) const
    {
        Room *room = xunyu->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            ServerPlayer *to = room->askForPlayerChosen(xunyu, room->getAlivePlayers(), objectName(), "@mobilejieming-invoke", true, true);
            if (!to) break;
            room->broadcastSkillInvoke(objectName());
            to->drawCards(2, objectName());
            if (to->getHandcardNum() < to->getMaxHp() && xunyu->isAlive())
                xunyu->drawCards(1, objectName());
            if (!xunyu->isAlive())
                break;
        }
    }
};

MobileNiepanCard::MobileNiepanCard()
{
    target_fixed = true;
}

void MobileNiepanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->doSuperLightbox(source, "mobileniepan");

    room->removePlayerMark(source, "@mobileniepanMark");

    source->throwAllHandCardsAndEquips("mobileniepan");
    foreach (const Card *trick, source->getJudgingArea()) {
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(),"mobileniepan","");
        room->throwCard(trick, reason, nullptr);
    }

    source->drawCards(3, "mobileniepan");

    int n = qMin(3 - source->getHp(), source->getMaxHp() - source->getHp());
    if (n > 0)
        room->recover(source, RecoverStruct(source, nullptr, n, "mobileniepan"));

    if (source->isChained())
        room->setPlayerChained(source);

    if (!source->faceUp())
        source->turnOver();
}

class MobileNiepanVS : public ZeroCardViewAsSkill
{
public:
    MobileNiepanVS() : ZeroCardViewAsSkill("mobileniepan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@mobileniepanMark") > 0;
    }

    const Card *viewAs() const
    {
        return new MobileNiepanCard;
    }
};

class MobileNiepan : public TriggerSkill
{
public:
    MobileNiepan() : TriggerSkill("mobileniepan")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@mobileniepanMark";
        view_as_skill = new MobileNiepanVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && target->getMark("@mobileniepanMark") > 0;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *pangtong, QVariant &data) const
    {
        DyingStruct dying_data = data.value<DyingStruct>();
        if (dying_data.who != pangtong)
            return false;

        if (pangtong->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName());
            room->doSuperLightbox(pangtong, "mobileniepan");

            room->removePlayerMark(pangtong, "@mobileniepanMark");

            pangtong->throwAllHandCardsAndEquips(objectName());
            foreach (const Card *trick, pangtong->getJudgingArea()) {
                CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, pangtong->objectName(),objectName(),"");
                room->throwCard(trick, reason, nullptr);
            }

            pangtong->drawCards(3, objectName());

            int n = qMin(3 - pangtong->getHp(), pangtong->getMaxHp() - pangtong->getHp());
            if (n > 0)
                room->recover(pangtong, RecoverStruct(pangtong, nullptr, n, "mobileniepan"));

            if (pangtong->isChained())
                room->setPlayerProperty(pangtong, "chained", false);

            if (!pangtong->faceUp())
                pangtong->turnOver();
        }
        return false;
    }
};

class MobileShuangxiongVS : public OneCardViewAsSkill
{
public:
    MobileShuangxiongVS() : OneCardViewAsSkill("mobileshuangxiong")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("ViewAsSkill_mobileshuangxiongEffect") > 0;
    }

    bool viewFilter(const Card *card) const
    {
        if (card->isEquipped())
            return false;
        return Self->getMark("mobileshuangxiong_"+card->getColorString()+"-Clear")<1;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Duel *duel = new Duel(originalCard->getSuit(), originalCard->getNumber());
        duel->addSubcard(originalCard);
        duel->setSkillName("mobileshuangxiong");
        return duel;
    }
};

class MobileShuangxiong : public TriggerSkill
{
public:
    MobileShuangxiong() : TriggerSkill("mobileshuangxiong")
    {
        events << EventPhaseStart << Damaged << EventPhaseChanging;
        view_as_skill = new MobileShuangxiongVS;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Draw||!player->hasSkill(objectName())) return false;
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(player, "ViewAsSkill_mobileshuangxiongEffect");
            QList<int> ids = room->getNCards(2);
            LogMessage log;
            log.type = "$TurnOver";
            log.from = player;
            log.card_str = ListI2S(ids).join("+");
            room->sendLog(log);
            room->fillAG(ids);
            int id = room->askForAG(player, ids, false, objectName());
            room->clearAG();
            room->returnToTopDrawPile(ids);
            room->obtainCard(player, id, true);
            const Card *card = Sanguosha->getCard(id);
            room->addPlayerMark(player, "mobileshuangxiong_"+card->getColorString()+"-Clear");
            return true;
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            room->setPlayerMark(player, "ViewAsSkill_mobileshuangxiongEffect",0);
        } else if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.from || player->getMark("ViewAsSkill_mobileshuangxiongEffect")<1
			|| !damage.card->isKindOf("Duel") || !damage.card->getSkillNames().contains("mobileshuangxiong")) return false;
            DummyCard *dummy = new DummyCard();
            foreach (int id, ListV2I(damage.from->getTag("DuelSlash" + damage.card->toString()).toList())) {
                if (room->getCardPlace(id) == Player::DiscardPile)
                    dummy->addSubcard(id);
            }
			dummy->deleteLater();
            if (dummy->subcardsLength()<1) return false;
            if (!player->askForSkillInvoke(this, QString("mobileshuangxiong_invoke:%1").arg(damage.from->objectName()))) return false;
            room->broadcastSkillInvoke(objectName());
            room->obtainCard(player, dummy);
        }
        return false;
    }
};

class MobileLuanjiVS : public ViewAsSkill
{
public:
    MobileLuanjiVS() : ViewAsSkill("mobileluanji")
    {
        response_or_use = true;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QStringList suits = Self->property("mobileluanji_suitstring").toString().split("+");
        if (selected.isEmpty() || selected.length() == 1)
            return !to_select->isEquipped() && !suits.contains(to_select->getSuitString());
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 2) {
            ArcheryAttack *aa = new ArcheryAttack(Card::SuitToBeDecided, 0);
            aa->addSubcards(cards);
            aa->setSkillName(objectName());
            return aa;
        }
		return nullptr;
    }
};

class MobileLuanji : public TriggerSkill
{
public:
    MobileLuanji() : TriggerSkill("mobileluanji")
    {
        events << PreCardUsed << EventPhaseChanging << CardResponded << CardFinished << DamageDone;
        view_as_skill = new MobileLuanjiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("ArcheryAttack") || !use.card->getSkillNames().contains("mobileluanji")) return false;
            if (!use.card->isVirtualCard()) return false;
            QStringList suits = player->property("mobileluanji_suitstring").toString().split("+");
            foreach (int id, use.card->getSubcards()) {
                suits << Sanguosha->getCard(id)->getSuitString();
            }
            room->setPlayerProperty(player, "mobileluanji_suitstring", suits.join("+"));
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                room->setPlayerProperty(player, "mobileluanji_suitstring", QString());
        } else if (event == CardResponded) {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (res.m_isRetrial) return false;  //不加改判会崩，不知为啥
            const Card *card = res.m_card;
            if (!card->isKindOf("Jink")) return false;
            const Card *tocard = res.m_toCard;
            if (!tocard || !tocard->isKindOf("ArcheryAttack")) return false;
            ServerPlayer *who = res.m_who;
            if (!who->hasSkill(objectName()) || player->isDead()) return false;
            player->drawCards(1, objectName());
        } else if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("ArcheryAttack")) return false;
            if (use.card->hasFlag("mobileluanji_damage")) {
                room->setCardFlag(use.card, "-mobileluanji_damage");
                return false;
            }
            if (use.to.length() > 0 && player->hasSkill(objectName())) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->drawCards(use.to.length(), objectName());
            }
        } else if (event == DamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("ArcheryAttack") || !damage.from || !damage.by_user) return false;
            room->setCardFlag(damage.card, "mobileluanji_damage");
        }
        return false;
    }
};

MobileZaiqiCard::MobileZaiqiCard()
{
}

bool MobileZaiqiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    return targets.length() < Self->getMark("mobilezaiqi-Clear");
}

void MobileZaiqiCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead()) return;
    QStringList choices;
    choices << "draw";
    if (effect.from->isAlive() && effect.from->isWounded())
        choices << "recover=" + effect.from->objectName();
    Room *room = effect.from->getRoom();
    QString choice = room->askForChoice(effect.to, "mobilezaiqi", choices.join("+"), QVariant::fromValue(effect.from));
    if (choice == "draw")
        effect.to->drawCards(1, "mobilezaiqi");
    else {
        room->recover(effect.from, RecoverStruct("mobilezaiqi", effect.to));
    }
}

class MobileZaiqiVS : public ZeroCardViewAsSkill
{
public:
    MobileZaiqiVS() : ZeroCardViewAsSkill("mobilezaiqi")
    {
        response_pattern = "@@mobilezaiqi";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new MobileZaiqiCard;
    }
};

class MobileZaiqi : public TriggerSkill
{
public:
    MobileZaiqi() : TriggerSkill("mobilezaiqi")
    {
        events << CardsMoveOneTime << EventPhaseEnd;
        view_as_skill = new MobileZaiqiVS;
        global = true;//
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
				move.from->addMark("mobilejuece-Clear");
			if (move.from==player){
				int n = 0, m = 0;
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_places[i] == Player::PlaceEquip)
						n++;
					if (Sanguosha->getCard(move.card_ids.at(i))->isKindOf("EquipCard")
                        && (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
						&& (move.from_places[i] == Player::PlaceEquip || move.from_places[i] == Player::PlaceHand))
						m++;
				}
				n = qMin(n, 3 - player->getMark("&shanjia") - player->getMark("shanjiaMark"));
				m = qMin(m, 3 - player->getMark("&olshanjia") - player->getMark("olshanjiaMark"));
				if (n > 0) {
					if (player->hasSkill("shanjia", true))
						room->addPlayerMark(player, "&shanjia", n);
					else
						player->addMark("shanjiaMark", n);
				}
				if (m > 0) {
					if (player->hasSkill("olshanjia", true))
						room->addPlayerMark(player, "&olshanjia", m);
					else
						player->addMark("olshanjiaMark", m);
				}
			}
        	if (!room->getTag("FirstRound").toBool()&&move.to == player && move.to_place == Player::PlaceHand)
            	player->addMark("chengzhao-Clear", move.card_ids.length());
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				QVariantList armors = room->getTag("MobileYanJianyiRecord").toList();
				foreach (int id, move.card_ids) {
					const Card *card = Sanguosha->getCard(id);
					if (card->isKindOf("Armor")) armors << id;
				}
				room->setTag("MobileYanJianyiRecord", armors);
			}
			if (move.from==player&&(move.from_places.contains(Player::PlaceEquip)||move.from_places.contains(Player::PlaceHand))) {
				if (move.to==player&&(move.to_place == Player::PlaceEquip||move.to_place==Player::PlaceHand)) return false;
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_places.at(i) == Player::PlaceEquip || move.from_places.at(i) == Player::PlaceHand)
						player->addMark("shangjian-Clear");
				}
			}
            if (move.to_place != Player::DiscardPile) return false;
            foreach (int id, move.card_ids) {
                player->addMark(QString::number(id)+"AnzhiRecord-Clear");
                if (Sanguosha->getCard(id)->isRed())
                    room->addPlayerMark(player, "mobilezaiqi-Clear");
            }
        } else {
            if (player->isDead() || player->getPhase() != Player::Discard || !player->hasSkill(objectName())) return false;
            int n = player->getMark("mobilezaiqi-Clear");
            if (n <= 0) return false;
            room->askForUseCard(player, "@@mobilezaiqi", "@mobilezaiqi:" + QString::number(n));
        }
        return false;
    }
};

class MobileLieren : public TriggerSkill
{
public:
    MobileLieren() : TriggerSkill("mobilelieren")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        foreach (ServerPlayer *p, use.to) {
            if (player->isDead()) break;
            if (p->isDead() || !player->canPindian(p) || !player->askForSkillInvoke(this, QVariant::fromValue(p))) continue;
            room->broadcastSkillInvoke(objectName());
            PindianStruct *pindian = player->PinDian(p, "mobilelieren", nullptr);
            if (!pindian) return false;
            if (pindian->from_number > pindian->to_number) {
                if (p->isNude()) continue;
                int card_id = room->askForCardChosen(player, p, "he", objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                room->obtainCard(player, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
            } else {
                if (p->isDead()) continue;
                int from_id = pindian->from_card->getEffectiveId();
                int to_id = pindian->to_card->getEffectiveId();
                if (room->getCardPlace(from_id) != Player::DiscardPile || room->getCardPlace(to_id) != Player::DiscardPile) continue;
                QList<CardsMoveStruct> exchangeMove;
                QList<int> from_ids, to_ids;
                from_ids << from_id;
                to_ids << to_id;
                CardsMoveStruct move1(to_ids, player, Player::PlaceHand,
                    CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), p->objectName(), "mobilelieren", ""));
                CardsMoveStruct move2(from_ids, p, Player::PlaceHand,
                    CardMoveReason(CardMoveReason::S_REASON_SWAP, p->objectName(), player->objectName(), "mobilelieren", ""));
                exchangeMove.push_back(move1);
                exchangeMove.push_back(move2);
                room->moveCardsAtomic(exchangeMove, true);
            }
        }
        return false;
    }
};

class MobileXingshang : public TriggerSkill
{
public:
    MobileXingshang() : TriggerSkill("mobilexingshang")
    {
        events << Death;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caopi, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *player = death.who;
        if (caopi == player) return false;
        QStringList choices;
        if (!player->isNude())
            choices << "get";
        if (caopi->getLostHp() > 0)
            choices << "recover";
        if (choices.isEmpty()) return false;
        if (caopi->isAlive() && room->askForSkillInvoke(caopi, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            QString choice = room->askForChoice(caopi, objectName(), choices.join("+"), data);
            if (choice == "get") {
                DummyCard *dummy = new DummyCard(player->handCards());
                QList <const Card *> equips = player->getEquips();
                foreach(const Card *card, equips)
                    dummy->addSubcard(card);

                if (dummy->subcardsLength() > 0) {
                    CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, caopi->objectName());
                    room->obtainCard(caopi, dummy, reason, false);
                }
                delete dummy;
            } else {
                room->recover(caopi, RecoverStruct("mobilexingshang", caopi));
            }
        }
        return false;
    }
};

class MobileFangzhu : public MasochismSkill
{
public:
    MobileFangzhu() : MasochismSkill("mobilefangzhu")
    {
    }

    void onDamaged(ServerPlayer *caopi, const DamageStruct &) const
    {
        Room *room = caopi->getRoom();
        ServerPlayer *to = room->askForPlayerChosen(caopi, room->getOtherPlayers(caopi), objectName(),
            "@mobilefangzhu-invoke", caopi->getMark("JilveEvent") != int(Damaged), true);
        if (to) {
            if (caopi->hasInnateSkill("fangzhu") || !caopi->hasSkill("jilve")) {
                room->broadcastSkillInvoke("mobilefangzhu");
            } else
                room->broadcastSkillInvoke("jilve", 2);

            int losthp = caopi->getLostHp();
            if (losthp <= 0) {
                QString choice = room->askForChoice(to, objectName(), "turnover+losehp");
                if (choice == "turnover")
                    to->turnOver();
                else
                    room->loseHp(HpLostStruct(to, 1, objectName(), caopi));
                return;
            }

            int candis = 0;
            foreach (const Card *c, to->getCards("he")) {
                if (to->canDiscard(to, c->getEffectiveId()))
                    candis++;
            }
            if (candis < losthp) {
                to->drawCards(losthp, objectName());
                to->turnOver();
            } else {
                if (room->askForDiscard(to, objectName(), losthp, losthp, true, true, "mobilefangzhu-discard:" + QString::number(losthp))) {
                    room->loseHp(HpLostStruct(to, 1, objectName(), caopi));
                    return;
                }
                to->drawCards(losthp, objectName());
                to->turnOver();
            }
        }
    }
};

MobilePoluCard::MobilePoluCard()
{
}

bool MobilePoluCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    if (Self->isDead())
        return to_select != Self;
    return true;
}

void MobilePoluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->addPlayerMark(source, "mobilepolu_usedtimes");
    int mark = source->getMark("mobilepolu_usedtimes");
    room->drawCards(targets, mark, "mobilepolu");
}

class MobilePoluVS : public ZeroCardViewAsSkill
{
public:
    MobilePoluVS() : ZeroCardViewAsSkill("mobilepolu")
    {
        response_pattern = "@@mobilepolu";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new MobilePoluCard;
    }
};

class MobilePolu : public TriggerSkill
{
public:
    MobilePolu() : TriggerSkill("mobilepolu")
    {
        events << Death;
        view_as_skill = new MobilePoluVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        int n = 0;
        if (death.who == player && player->hasSkill(objectName()))
            n++;
        if (death.damage && death.damage->from && death.damage->from == player && player->hasSkill(objectName()))//&& player->isAlive()
            n++;
        if (n == 0) return false;
        for (int i = 1; i <= n; i++) {
            int mark = player->getMark("mobilepolu_usedtimes");
            if (!room->askForUseCard(player, "@@mobilepolu", "@mobilepolu:" + QString::number(mark + 1))) break;
        }
        return false;
    }
};

class MobileJiuchiVS : public OneCardViewAsSkill
{
public:
    MobileJiuchiVS() : OneCardViewAsSkill("mobilejiuchi")
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

class MobileJiuchi : public TriggerSkill
{
public:
    MobileJiuchi() : TriggerSkill("mobilejiuchi")
    {
        events << Damage;
        view_as_skill = new MobileJiuchiVS;
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
                room->broadcastSkillInvoke("mobilejiuchi");
                room->notifySkillInvoked(player, "mobilejiuchi");
            }
            room->addPlayerMark(player, "benghuai_nullification-Clear");
        }
        return false;
    }
};

class MobileTuntian : public TriggerSkill
{
public:
    MobileTuntian() : TriggerSkill("mobiletuntian")
    {
        events << CardsMoveOneTime << FinishJudge;
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && !target->hasFlag("CurrentPlayer");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                && !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
                && player->askForSkillInvoke("mobiletuntian", data)) {
                room->broadcastSkillInvoke("mobiletuntian");
                JudgeStruct judge;
                judge.pattern = ".|heart";
                judge.good = false;
                judge.reason = "mobiletuntian";
                judge.who = player;
                room->judge(judge);
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == "mobiletuntian" && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge){
				if(judge->isGood())
					player->addToPile("field", judge->card);
				else
					room->obtainCard(player, judge->card, true);
			}
        }
        return false;
    }
};

class MobileTuntianDistance : public DistanceSkill
{
public:
    MobileTuntianDistance() : DistanceSkill("#mobiletuntian-dist")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        int n = from->getPile("field").length();
		if (n>0&&from->hasSkill("mobiletuntian"))
            return -n;
        return 0;
    }
};

MobileTiaoxinCard::MobileTiaoxinCard()
{
}

void MobileTiaoxinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool use_slash = false;
    if (effect.to->canSlash(effect.from, nullptr, false))
        use_slash = room->askForUseSlashTo(effect.to, effect.from, "@mobiletiaoxin-slash:" + effect.from->objectName());
    if (!use_slash && effect.from->canDiscard(effect.to, "he"))
        room->throwCard(room->askForCardChosen(effect.from, effect.to, "he", "mobiletiaoxin", false, Card::MethodDiscard), effect.to, effect.from);
}

class MobileTiaoxin : public ZeroCardViewAsSkill
{
public:
    MobileTiaoxin() : ZeroCardViewAsSkill("mobiletiaoxin")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileTiaoxinCard");
    }

    const Card *viewAs() const
    {
        return new MobileTiaoxinCard;
    }
};

class MobileZhiji : public PhaseChangeSkill
{
public:
    MobileZhiji() : PhaseChangeSkill("mobilezhiji")
    {
        frequency = Wake;
        waked_skills = "tenyearguanxing";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
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

        room->doSuperLightbox(jiangwei, "mobilezhiji");

        room->setPlayerMark(jiangwei, "mobilezhiji", 1);

        if (jiangwei->isWounded() && room->askForChoice(jiangwei, objectName(), "recover+draw") == "recover")
            room->recover(jiangwei, RecoverStruct("mobilezhiji", jiangwei));
        else
            room->drawCards(jiangwei, 2, objectName());

        if (room->changeMaxHpForAwakenSkill(jiangwei, -1, objectName()))
            room->acquireSkill(jiangwei, "tenyearguanxing");
        return false;
    }
};

class MobileHunzi : public PhaseChangeSkill
{
public:
    MobileHunzi() : PhaseChangeSkill("mobilehunzi")
    {
        frequency = Wake;
		waked_skills = "yingzi,yinghun";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *sunce, Room *room) const
    {
        if (sunce->getHp() <= 2) {
            LogMessage log;
            log.type = "#HunziWake";
            log.from = sunce;
            log.arg = QString::number(sunce->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!sunce->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(sunce, objectName());

        room->doSuperLightbox(sunce, "mobilehunzi");

        room->setPlayerMark(sunce, "mobilehunzi", 1);
        if (room->changeMaxHpForAwakenSkill(sunce, -1, objectName()))
            room->handleAcquireDetachSkills(sunce, "yingzi|yinghun");
        return false;
    }
};

class MobileBeige : public TriggerSkill
{
public:
    MobileBeige() : TriggerSkill("mobilebeige")
    {
        events << Damaged;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash"))
                return false;

            foreach (ServerPlayer *caiwenji, room->getAllPlayers()) {
                if (!TriggerSkill::triggerable(caiwenji)) continue;
                if (caiwenji->canDiscard(caiwenji, "he") && room->askForCard(caiwenji, "..", "@mobilebeige:" + player->objectName(), data, objectName())) {
                    room->broadcastSkillInvoke(objectName());
                    JudgeStruct judge;
                    judge.good = true;
                    judge.play_animation = false;
                    judge.who = player;
                    judge.reason = objectName();
                    room->judge(judge);

                    switch (judge.card->getSuit()) {
                    case Card::Heart: {
                        int n = qMin(player->getMaxHp() - player->getHp(), damage.damage);
                        if (n > 0)
                            room->recover(player, RecoverStruct(caiwenji, nullptr, n, "mobilebeige"));
                        break;
                    }
                    case Card::Diamond: {
                        player->drawCards(3, objectName());
                        break;
                    }
                    case Card::Club: {
                        if (damage.from && damage.from->isAlive())
                            room->askForDiscard(damage.from, "mobilebeige", 2, 2, false, true);
                        break;
                    }
                    case Card::Spade: {
                        if (damage.from && damage.from->isAlive())
                            damage.from->turnOver();
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
        return false;
    }
};

MobileZhijianCard::MobileZhijianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->getEquip(equip_index) == nullptr && !Self->isProhibited(to_select, card);
}

void MobileZhijianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *erzhang = effect.from;
    erzhang->getRoom()->moveCardTo(this, erzhang, effect.to, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_PUT,
        erzhang->objectName(), "mobilezhijian", ""));

    LogMessage log;
    log.type = "$ZhijianEquip";
    log.from = effect.to;
    log.card_str = QString::number(getEffectiveId());
    erzhang->getRoom()->sendLog(log);

    erzhang->drawCards(1, "mobilezhijian");
}

class MobileZhijianVS : public OneCardViewAsSkill
{
public:
    MobileZhijianVS() :OneCardViewAsSkill("mobilezhijian")
    {
        filter_pattern = "EquipCard|.|.|hand";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileZhijianCard *zhijian_card = new MobileZhijianCard;
        zhijian_card->addSubcard(originalCard);
        return zhijian_card;
    }
};

class MobileZhijian : public TriggerSkill
{
public:
    MobileZhijian() : TriggerSkill("mobilezhijian")
    {
        events << CardUsed;
        view_as_skill = new MobileZhijianVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("EquipCard") || player->getPhase() != Player::Play) return false;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->drawCards(1, "mobilezhijian");
        return false;
    }
};

MobileFangquanCard::MobileFangquanCard()
{
	mute = true;
}

void MobileFangquanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *liushan = effect.from, *player = effect.to;

    LogMessage log;
    log.type = "#Fangquan";
    log.from = liushan;
    log.to << player;
    room->sendLog(log);

    room->setTag("MobileFangquanTarget", QVariant::fromValue(player));
}

class MobileFangquanViewAsSkill : public OneCardViewAsSkill
{
public:
    MobileFangquanViewAsSkill() : OneCardViewAsSkill("mobilefangquan")
    {
        filter_pattern = ".|.|.|hand!";
        response_pattern = "@@mobilefangquan";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileFangquanCard *fangquan = new MobileFangquanCard;
        fangquan->addSubcard(originalCard);
        return fangquan;
    }
};

class MobileFangquan : public TriggerSkill
{
public:
    MobileFangquan() : TriggerSkill("mobilefangquan")
    {
        events << EventPhaseChanging << EventPhaseStart;
        view_as_skill = new MobileFangquanViewAsSkill;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseStart) return 1;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liushan, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            switch (change.to) {
            case Player::Play: {
                if (!TriggerSkill::triggerable(liushan) || liushan->isSkipped(Player::Play))
                    return false;
                if (liushan->askForSkillInvoke(this)) {
                    room->broadcastSkillInvoke(objectName());
                    liushan->setFlags(objectName());
                    liushan->skip(Player::Play, true);
                }
                break;
            }
            case Player::NotActive: {
                if (liushan->hasFlag(objectName())) {
                    if (!liushan->canDiscard(liushan, "h"))
                        return false;
                    room->askForUseCard(liushan, "@@mobilefangquan", "@mobilefangquan-give", -1, Card::MethodDiscard);
                }
                break;
            }
            default:
                break;
            }
        } else if (triggerEvent == EventPhaseStart && liushan->getPhase() == Player::NotActive) {
            Room *room = liushan->getRoom();
            if (!room->getTag("MobileFangquanTarget").isNull()) {
                ServerPlayer *target = room->getTag("MobileFangquanTarget").value<ServerPlayer *>();
                room->removeTag("MobileFangquanTarget");
                if (target->isAlive())
                    target->gainAnExtraTurn();
            }
        }
        return false;
    }
};

class MobileFangquanMax : public MaxCardsSkill
{
public:
    MobileFangquanMax() : MaxCardsSkill("#mobilefangquan-max")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasFlag("mobilefangquan")||target->getMark("&mobileanguo")>0)
            return target->getMaxHp();
        return -1;
    }
};

class MobilePojun : public TriggerSkill
{
public:
    MobilePojun() : TriggerSkill("mobilepojun")
    {
        events << TargetSpecified << EventPhaseChanging << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card != nullptr && use.card->isKindOf("Slash") && TriggerSkill::triggerable(player)) {
                foreach (ServerPlayer *t, use.to) {
                    if (player->isDead()) return false;
                    if (t->isDead()) continue;
                    int n = qMin(t->getCards("he").length(), t->getHp());
                    if (n > 0 && player->askForSkillInvoke(this, QVariant::fromValue(t))) {
                        room->broadcastSkillInvoke(objectName());

                        DummyCard *dummy = new DummyCard;

                        for (int i = 0; i < n; ++i) {
                            int id = room->askForCardChosen(player, t, "he", objectName() + "_dis", false, Card::MethodNone, dummy->getSubcards(), i>0);
                            if (id<0) break;
							dummy->addSubcard(id);
                        }

                        t->addToPile("mobilepojun", dummy, false);
						dummy->deleteLater();
                    }
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile("mobilepojun");
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
                }
            }
        } else if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from->hasSkill(objectName()) || damage.to->isDead()) return false;
            if (!damage.card || !damage.card->isKindOf("Slash") || !damage.by_user) return false;
            if (damage.from->getHandcardNum() < damage.to->getHandcardNum()) return false;
            if (damage.from->getEquips().length() < damage.to->getEquips().length()) return false;
            LogMessage log;
            log.type = "#MobilepojunDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(damage.from, objectName());

            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

MobileGanluCard::MobileGanluCard()
{
}

void MobileGanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
    Room *room = first->getRoom();
    room->swapEquips(first, second, "mobileganlu");
}

bool MobileGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool MobileGanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.isEmpty())
        return true;
    else if (targets.length() == 1) {
        int n1 = targets.first()->getEquips().length();
        int n2 = to_select->getEquips().length();
        return qAbs(n1 - n2) <= Self->getLostHp() || targets.first() == Self || to_select == Self;
    }
    return false;
}

void MobileGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    LogMessage log;
    log.type = "#GanluSwap";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    swapEquip(targets.first(), targets[1]);
}

class MobileGanlu : public ZeroCardViewAsSkill
{
public:
    MobileGanlu() : ZeroCardViewAsSkill("mobileganlu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileGanluCard");
    }

    const Card *viewAs() const
    {
        return new MobileGanluCard;
    }
};

MobileJieyueCard::MobileJieyueCard()
{
    mute = true;
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void MobileJieyueCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileJieyueVS : public ViewAsSkill
{
public:
    MobileJieyueVS() : ViewAsSkill("mobilejieyue")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.isEmpty()) return true;
        if (selected.length() == 1) {
            if (Self->getHandcards().contains(selected.first()) && !Self->getEquips().isEmpty())
                return to_select->isEquipped();
            else if (Self->getEquips().contains(selected.first()) && !Self->isKongcheng())
                return !to_select->isEquipped();
        }
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;
        int x = 0;
        if (!Self->isKongcheng())
            x++;
        if (!Self->getEquips().isEmpty())
            x++;
        if (cards.length() != x) return nullptr;

        MobileJieyueCard *c = new MobileJieyueCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mobilejieyue";
    }
};

class MobileJieyue : public PhaseChangeSkill
{
public:
    MobileJieyue() : PhaseChangeSkill("mobilejieyue")
    {
        view_as_skill = new MobileJieyueVS;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        if (player->isNude()) return false;

        QList<int> list = player->handCards() + player->getEquipsId();
        ServerPlayer *target = room->askForYiji(player, list, objectName(), false, false, true, 1,
                     room->getOtherPlayers(player), CardMoveReason(), "mobilejieyue-invoke", true);
        if (!target || target->isDead()) return false;

        if (target->isNude()) {
            player->drawCards(3, objectName());
            return false;
        }

        const Card *card = room->askForUseCard(target, "@@mobilejieyue", "@mobilejieyue:" + player->objectName());
        if (card) {
            DummyCard *dummy = new DummyCard;
            QList<int> ids = card->getSubcards();
            foreach (int id, target->handCards() + target->getEquipsId()) {
                if (ids.contains(id)) continue;
                if (!target->canDiscard(target, id)) continue;
                dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() > 0)
                room->throwCard(dummy, target, nullptr);
            delete dummy;
        } else {
            player->drawCards(3, objectName());
        }
        return false;
    }
};

class MobileDangxian : public PhaseChangeSkill
{
public:
    MobileDangxian() : PhaseChangeSkill("mobiledangxian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() == Player::RoundStart) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);

            QList<int> slash;
            foreach (int id, room->getDiscardPile()) {
                if (Sanguosha->getCard(id)->isKindOf("Slash"))
                    slash << id;
            }
            if (!slash.isEmpty())
                room->obtainCard(player, slash.at(qrand() % slash.length()), true);

            if (player->isDead()) return false;

            player->insertPhase(Player::Play);/*
            room->broadcastProperty(player, "phase");
            RoomThread *thread = room->getThread();
            if (!thread->trigger(EventPhaseStart, room, player))
                thread->trigger(EventPhaseProceeding, room, player);
            thread->trigger(EventPhaseEnd, room, player);

            player->setPhase(Player::RoundStart);
            room->broadcastProperty(player, "phase");*/
        }
        return false;
    }
};

class MobileFuli : public TriggerSkill
{
public:
    MobileFuli() : TriggerSkill("mobilefuli")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@mobilefuliMark";
    }

    int getKingdoms(Room *room) const
    {
        QSet<QString> kingdom_set;
        foreach(ServerPlayer *p, room->getAlivePlayers())
            kingdom_set << p->getKingdom();
        return kingdom_set.size();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liaohua, QVariant &data) const
    {
        DyingStruct dying_data = data.value<DyingStruct>();
        if (dying_data.who != liaohua || liaohua->getMark("@mobilefuliMark") <= 0) return false;
        if (liaohua->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName());

            room->doSuperLightbox(liaohua, "mobilefuli");

            room->removePlayerMark(liaohua, "@mobilefuliMark");
            int x = getKingdoms(room);
            int n = qMin(x - liaohua->getHp(), liaohua->getMaxHp() - liaohua->getHp());
            if (n > 0) room->recover(liaohua, RecoverStruct(liaohua, nullptr, n, "mobilefuli"));
            foreach(ServerPlayer *p, room->getOtherPlayers(liaohua)) {
                if (p->getHp() >= liaohua->getHp())
                    return false;
            }
            liaohua->turnOver();
        }
        return false;
    }
};

MobileAnxuCard::MobileAnxuCard()
{
}

bool MobileAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.isEmpty())
        return to_select != Self;
    if (targets.length() == 1) {
        if (targets.first()->isNude())
            return to_select != Self && !to_select->isNude();
        else
            return to_select != Self;
    }
    return targets.length() < 2;
}

bool MobileAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void MobileAnxuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, card_use.from, data);
    card_use = data.value<CardUseStruct>();

    LogMessage log;
    log.from = card_use.from;
    log.to << card_use.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    thread->trigger(CardUsed, room, card_use.from, data);
    card_use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, card_use.from, data);
}

void MobileAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (targets.last()->isNude()) return;

    int id = room->askForCardChosen(targets.first(), targets.last(), "he", "mobileanxu");
    Player::Place place = room->getCardPlace(id);
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, targets.first()->objectName(), "mobileanxu", "");
    room->obtainCard(targets.first(), Sanguosha->getCard(id), reason, place != Player::PlaceHand);

    if (place != Player::PlaceHand) return;
    source->drawCards(1, "mobileanxu");
}

class MobileAnxuVS : public ZeroCardViewAsSkill
{
public:
    MobileAnxuVS() : ZeroCardViewAsSkill("mobileanxu")
    {
    }

    const Card *viewAs() const
    {
        return new MobileAnxuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileAnxuCard");
    }
};

class MobileAnxu : public TriggerSkill
{
public:
    MobileAnxu() : TriggerSkill("mobileanxu")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MobileAnxuVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from->isAlive() && move.to && move.to->isAlive() && move.to_place == Player::PlaceHand &&
            move.reason.m_skillName == objectName() && (move.from_places.contains(Player::PlaceEquip) || move.from_places.contains(Player::PlaceHand))) {
            if (move.from->getHandcardNum() == move.to->getHandcardNum()) return false;
            ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
            ServerPlayer *to = room->findPlayerByObjectName(move.to->objectName());
            if (!from || from->isDead() || !to || to->isDead()) return false;
            ServerPlayer *less = from;
            if (to->getHandcardNum() < from->getHandcardNum())
                less = to;

            if (!player->askForSkillInvoke(this, QVariant::fromValue(less))) return false;
            room->broadcastSkillInvoke(objectName());
            less->drawCards(1, objectName());
        }
        return false;
    }
};

class MobileZongshi : public PhaseChangeSkill
{
public:
    MobileZongshi() : PhaseChangeSkill("mobilezongshi")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() == Player::Start && player->getHandcardNum() > player->getHp()) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            room->addSlashCishu(player, 1000);
        }
        return false;
    }
};

class MobileZongshiKeep : public MaxCardsSkill
{
public:
    MobileZongshiKeep() : MaxCardsSkill("#mobilezongshi-keep")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("mobilezongshi")){
			QSet<QString> kingdom_set;
            foreach(const Player *p, target->parent()->findChildren<const Player *>()) {
                if (p->isAlive()) kingdom_set << p->getKingdom();
            }
            return kingdom_set.size();
		}
        return 0;
    }
};

class MobileYicong : public DistanceSkill
{
public:
    MobileYicong() : DistanceSkill("mobileyicong")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        int correct = -from->getMark("&mobilebenxi-PlayClear");
        if (from->hasSkill(objectName()))
            correct = correct + qMin(0, 1 - from->getHp());
        if (to->hasSkill(objectName()))
            correct = correct + qMax(0, to->getLostHp() -1);
        return correct;
    }
};

class MobileXuanfeng : public TriggerSkill
{
public:
    MobileXuanfeng() : TriggerSkill("mobilexuanfeng")
    {
        events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    void perform(Room *room, ServerPlayer *lingtong) const
    {
        QStringList choices;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *target, room->getOtherPlayers(lingtong)) {
            if (lingtong->canDiscard(target, "he"))
                targets << target;
        }
        if (!targets.isEmpty())
            choices << "discard";
        if (room->canMoveField("e", room->getOtherPlayers(lingtong), room->getOtherPlayers(lingtong)))
            choices << "move";
        if (choices.isEmpty())
            return;

        if (lingtong->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());
            QString choice = room->askForChoice(lingtong, objectName(), choices.join("+"));

            if (choice == "discard") {
                ServerPlayer *first = room->askForPlayerChosen(lingtong, targets, "mobilexuanfeng");
                room->doAnimate(1, lingtong->objectName(), first->objectName());
                ServerPlayer *second = nullptr;
                int first_id = -1;
                int second_id = -1;
                if (first != nullptr) {
                    first_id = room->askForCardChosen(lingtong, first, "he", "mobilexuanfeng", false, Card::MethodDiscard);
                    room->throwCard(first_id, first, lingtong);
                }
                if (!lingtong->isAlive())
                    return;
                targets.clear();
                foreach (ServerPlayer *target, room->getOtherPlayers(lingtong)) {
                    if (lingtong->canDiscard(target, "he"))
                        targets << target;
                }
                if (!targets.isEmpty()) {
                    second = room->askForPlayerChosen(lingtong, targets, "mobilexuanfeng");
                    room->doAnimate(1, lingtong->objectName(), second->objectName());
                }
                if (second != nullptr) {
                    second_id = room->askForCardChosen(lingtong, second, "he", "mobilexuanfeng", false, Card::MethodDiscard);
                    room->throwCard(second_id, second, lingtong);
                }
            } else {
                if (!room->canMoveField("e", room->getOtherPlayers(lingtong), room->getOtherPlayers(lingtong))) return;
                room->moveField(lingtong, objectName(), false, "e", room->getOtherPlayers(lingtong), room->getOtherPlayers(lingtong));
            }
        }
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lingtong, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            lingtong->setMark("mobilexuanfeng", 0);
        } else if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != lingtong)
                return false;

            if (lingtong->getPhase() == Player::Discard
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                lingtong->addMark("mobilexuanfeng", move.card_ids.length());

            if (move.from_places.contains(Player::PlaceEquip) && TriggerSkill::triggerable(lingtong))
                perform(room, lingtong);
        } else if (triggerEvent == EventPhaseEnd && TriggerSkill::triggerable(lingtong)
            && lingtong->getPhase() == Player::Discard && lingtong->getMark("mobilexuanfeng") >= 2) {
            perform(room, lingtong);
        }

        return false;
    }
};

class MobileJiushiVS : public ZeroCardViewAsSkill
{
public:
    MobileJiushiVS() : ZeroCardViewAsSkill("mobilejiushi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Analeptic::IsAvailable(player) && player->faceUp();
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

class MobileJiushi : public TriggerSkill
{
public:
    MobileJiushi() : TriggerSkill("mobilejiushi")
    {
        events << PreCardUsed << DamageDone << Damaged << TurnedOver;
        view_as_skill = new MobileJiushiVS;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == PreCardUsed)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    void getTrick(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        QList<int> tricks;
        foreach (int id, room->getDrawPile()) {
            if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
                tricks << id;
        }
        if (tricks.isEmpty()) return;
        int id = tricks.at(qrand() % tricks.length());
        room->obtainCard(player, id, true);
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getSkillNames().contains(objectName()))
                player->turnOver();
        } else if (triggerEvent == TurnedOver&&player->property("mobilejiushi_levelup").toBool()) {
            room->sendCompulsoryTriggerLog(player, this, qrand()%2+1);
            getTrick(player);
        } else if (triggerEvent == Damaged) {
            bool facedown = player->getTag("MobilePredamagedFace").toBool();
            player->removeTag("MobilePredamagedFace");
            if (facedown && !player->faceUp() && player->askForSkillInvoke(this, data)) {
                room->broadcastSkillInvoke(objectName());
                player->turnOver();
                if (player->property("mobilejiushi_levelup").toBool()) return false;
                getTrick(player);
            }
        } else if (triggerEvent == DamageDone)
            player->setTag("MobilePredamagedFace", !player->faceUp());
        return false;
    }
};

class MobileChengzhang : public PhaseChangeSkill
{
public:
    MobileChengzhang() : PhaseChangeSkill("mobilechengzhang")
    {
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        int mark = player->getMark("&mobilechengzhang") + player->getMark("mobilechengzhang_num");
        if (mark >= 7){
			LogMessage log;
			log.type = "#MobilechengzhangWake";
			log.from = player;
			log.arg = QString::number(mark);
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
        room->setPlayerMark(player, "&mobilechengzhang", 0);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        room->doSuperLightbox(player, "mobilechengzhang");
        room->setPlayerMark(player, "mobilechengzhang", 1);
        if (room->changeMaxHpForAwakenSkill(player, 0, objectName())) {
            room->recover(player, RecoverStruct("mobilechengzhang", player));
            player->drawCards(1, objectName());
            room->setPlayerProperty(player, "mobilejiushi_levelup", true);
            room->changeTranslation(player, "mobilejiushi", 2);
        }
        return false;
    }
};

MobileGongqiCard::MobileGongqiCard()
{
}

bool MobileGongqiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && Self->canDiscard(to_select, "he");
}

void MobileGongqiCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->isDead() || effect.to->isDead()) return;
    if (!effect.from->canDiscard(effect.to, "he")) return;

    Room *room = effect.from->getRoom();
    int id = room->askForCardChosen(effect.from, effect.to, "he", "mobilegongqi", false, Card::MethodDiscard);
    room->throwCard(id, effect.to, effect.from);
}

class MobileGongqi : public OneCardViewAsSkill
{
public:
    MobileGongqi() : OneCardViewAsSkill("mobilegongqi")
    {
        filter_pattern = "^BasicCard";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileGongqiCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileGongqiCard *c = new MobileGongqiCard;
        c->addSubcard(originalCard);
        return c;
    }
};

class MobileGongqiAttack : public AttackRangeSkill
{
public:
    MobileGongqiAttack() : AttackRangeSkill("#mobilegongqi-attack")
    {
        frequency = NotFrequent;
    }

    int getExtra(const Player *target, bool) const
    {
        if ((target->getOffensiveHorse() || target->getDefensiveHorse())&&target->hasSkill("mobilegongqi"))
            return 1000;
        return 0;
    }
};

class MobileQuanji : public TriggerSkill
{
public:
    MobileQuanji() : TriggerSkill("mobilequanji")
    {
        events << EventPhaseEnd << Damaged;
        frequency = Frequent;
    }

    void doQuanji(ServerPlayer *player) const
    {
        player->drawCards(1, objectName());
        if (player->isAlive() && !player->isKongcheng()) {
            Room *room = player->getRoom();
            int card_id;
            if (player->getHandcardNum() == 1) {
                room->getThread()->delay();
                card_id = player->handCards().first();
            } else {
                const Card *card = room->askForExchange(player, "quanji", 1, 1, false, "QuanjiPush");
                card_id = card->getEffectiveId();
            }
            player->addToPile("power", card_id);
        }
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play || player->getHandcardNum() <= player->getHp()) return false;
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            doQuanji(player);
        } else {
            int n = data.value<DamageStruct>().damage;
            for (int i = 0; i < n; i++) {
                if (!player->askForSkillInvoke(this)) return false;
                room->broadcastSkillInvoke(objectName());
                doQuanji(player);
            }
        }
        return false;
    }
};

class MobileQuanjiKeep : public MaxCardsSkill
{
public:
    MobileQuanjiKeep() : MaxCardsSkill("#mobilequanji")
    {
        frequency = Frequent;
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("mobilequanji"))
            return target->getPile("power").length();
        return 0;
    }
};

class MobileLihuo : public TriggerSkill
{
public:
    MobileLihuo() : TriggerSkill("mobilelihuo")
    {
        events << ChangeSlash << DamageCaused << DamageDone << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == ChangeSlash) {
            if (!TriggerSkill::triggerable(player)) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->objectName() != "slash") return false;
            FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
            fire_slash->deleteLater();
            if (!use.card->isVirtualCard() || use.card->subcardsLength() > 0) {
                if (use.card->isVirtualCard())
                    fire_slash->addSubcards(use.card->getSubcards());
                else
                    fire_slash->addSubcard(use.card);
            }
            fire_slash->setSkillName("mobilelihuo");

            bool can_use = true;
            bool has_chained = false;
            foreach (ServerPlayer *p, use.to) {
                if (!player->canSlash(p, fire_slash, false)) {
                    can_use = false;
                }
                if (p->isChained())
                    has_chained = true;
                if (!can_use && has_chained) break;
            }
            if (can_use && room->askForSkillInvoke(player, "mobilelihuo", data, false)) {
                room->broadcastSkillInvoke(objectName());
                use.changeCard(fire_slash);
                if (has_chained)
                    room->setCardFlag(use.card, "mobilelihuo");
                data = QVariant::fromValue(use);
            }
        } else if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->getSkillNames().contains(objectName()) || !damage.card->hasFlag("mobilelihuo")) return false;
            ++damage.damage;
            data = QVariant::fromValue(damage);
        } else if (event == DamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->getSkillNames().contains(objectName())) return false;
            if (damage.from->isDead() || !damage.from->hasSkill(objectName())) return false;
            int n = damage.from->getTag("mobilelihuo" + damage.card->toString()).toInt();
            n += damage.damage;
            damage.from->setTag("mobilelihuo" + damage.card->toString(), n);
        } else if (event == CardFinished) {
            if (TriggerSkill::triggerable(player) && !player->hasFlag("Global_ProcessBroken")) {
                CardUseStruct use = data.value<CardUseStruct>();
                if (!use.card->getSkillNames().contains(objectName())) return false;
                int n = player->getTag("mobilelihuo" + use.card->toString()).toInt();
                n = floor(n / 2);
                player->removeTag("mobilelihuo" + use.card->toString());
                if (n <= 0) return false;
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                room->loseHp(HpLostStruct(player, n, objectName(), player));
            }
        }
        return false;
    }
};

MobileZongxuanCard::MobileZongxuanCard()
{
    target_fixed = true;
}

void MobileZongxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->drawCards(1, "mobilezongxuan");
    if (source->isDead() || source->isNude()) return;
    const Card *c = room->askForExchange(source, "mobilezongxuan", 1, 1, true, "mobilezongxuan-put");
    CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "mobilezongxuan", "");
    room->moveCardTo(c, nullptr, Player::DrawPile, reason, false);
}

MobileZongxuanPutCard::MobileZongxuanPutCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobilezongxuan";
}

void MobileZongxuanPutCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
}

class MobileZongxuanVS : public ViewAsSkill
{
public:
    MobileZongxuanVS() : ViewAsSkill("mobilezongxuan")
    {
        response_pattern = "@@mobilezongxuan";
        expand_pile = "#mobilezongxuan";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        else
            return Self->getPile("#mobilezongxuan").contains(to_select->getEffectiveId());
        return false;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileZongxuanCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (!cards.isEmpty()) return nullptr;
            return new MobileZongxuanCard;
        } else {
            if (cards.isEmpty()) return nullptr;
            MobileZongxuanPutCard *put = new MobileZongxuanPutCard;
            put->addSubcards(cards);
            return put;
        }
        return nullptr;
    }
};

class MobileZongxuan : public TriggerSkill
{
public:
    MobileZongxuan() : TriggerSkill("mobilezongxuan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MobileZongxuanVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || move.from != player)
            return false;
        if (move.to_place == Player::DiscardPile
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
            QList<int> zongxuan_card;
            for (int i = 0; i < move.card_ids.length(); i++) {
                int id = move.card_ids.at(i);
                if ((move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip) &&
                        room->getCardPlace(id) == Player::DiscardPile)
                    zongxuan_card << id;
            }
            if (zongxuan_card.isEmpty())
                return false;

            room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, true);

            try {
                const Card *c = room->askForUseCard(player, "@@mobilezongxuan", "@mobilezongxuan");
                if (c) {
                    QList<int> subcards = c->getSubcards();
                    foreach (int id, subcards) {
                        if (zongxuan_card.contains(id))
                            zongxuan_card.removeOne(id);
                    }
                    LogMessage log;
                    log.type = "$YinshicaiPut";
                    log.from = player;
                    log.card_str = ListI2S(subcards).join("+");
                    room->sendLog(log);

                    room->notifyMoveToPile(player, subcards, objectName(), Player::DiscardPile, false);

                    CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "mobilezongxuan", "");
                    room->moveCardTo(c, nullptr, Player::DrawPile, reason, true, true);
                }
            }
            catch (TriggerEvent triggerEvent) {
                if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
                    if (!zongxuan_card.isEmpty())
                        room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, false);
                }
                throw triggerEvent;
            }
            if (!zongxuan_card.isEmpty())
                room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, false);
        }
        return false;
    }
};

MobileJunxingCard::MobileJunxingCard()
{
}

void MobileJunxingCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead()) return;
    int length = subcardsLength();
    int can_dis = 0;
    QList<int> list = effect.to->handCards() + effect.to->getEquipsId();
    foreach (int id, list) {
        if (effect.to->canDiscard(effect.to, id))
            can_dis++;
    }

    if (can_dis < length) {
        effect.to->turnOver();
        effect.to->drawCards(length, "mobilejunxing");
        return;
    }

    Room *room = effect.from->getRoom();
    effect.to->setTag("mobilejunxing_effect", QVariant::fromValue(effect.from));
    const Card *card = room->askForDiscard(effect.to, "mobilejunxing", length, length, true, true);
    effect.to->removeTag("mobilejunxing_effect");
    if (!card) {
        effect.to->turnOver();
        effect.to->drawCards(length, "mobilejunxing");
    } else
        room->loseHp(HpLostStruct(effect.to, 1, "mobilejunxing", effect.from));
}

class MobileJunxing : public ViewAsSkill
{
public:
    MobileJunxing() : ViewAsSkill("mobilejunxing")
    {
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !to_select->isEquipped();
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileJunxingCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        MobileJunxingCard *c = new MobileJunxingCard;
        c->addSubcards(cards);
        return c;
    }
};

class MobileJuece : public PhaseChangeSkill
{
public:
    MobileJuece() : PhaseChangeSkill("mobilejuece")
    {
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getMark("mobilejuece-Clear") > 0)
                players << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@mobilejuece", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), player, target));
        return false;
    }
};

MobileMiejiCard::MobileMiejiCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileMiejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

void MobileMiejiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->showCard(effect.from, getSubcards().first());

    CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "", "mobilemieji", "");
    room->moveCardTo(this, effect.from, nullptr, Player::DrawPile, reason, true);

    QList<const Card *> trick, nottrick;
    foreach (const Card *c, effect.to->getCards("he")) {
        if (c->isKindOf("TrickCard"))
            trick << c;
        else if (!c->isKindOf("TrickCard") && effect.to->canDiscard(effect.to, c->getEffectiveId()))
            nottrick << c;
    }

    if (trick.isEmpty() && nottrick.isEmpty()) return;

    if (trick.isEmpty() && !nottrick.isEmpty())
        room->askForDiscard(effect.to, "mobilemieji", 2, 2, false, true, "@mobilemieji_nottrick", "^TrickCard");
    else if (nottrick.isEmpty() && !trick.isEmpty()) {
        const Card *c = room->askForExchange(effect.to, "mobilemieji", 1, 1, false, "@mobilemieji_trick:" + effect.from->objectName(), false, "TrickCard");
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
        room->obtainCard(effect.from, c, reason, true);
    } else {
        const Card *cc = room->askForUseCard(effect.to, "@@mobilemieji!", "@mobilemieji:" + effect.from->objectName());
        if (!cc) {
            if (!trick.isEmpty()) {
                const Card *give = trick.at(qrand() % trick.length());
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
                room->obtainCard(effect.from, give, reason, true);
                return;
            }
            if (!nottrick.isEmpty()) {
                DummyCard *dis = new DummyCard;
                const Card *d = nottrick.at(qrand() % nottrick.length());
                nottrick.removeOne(d);
                dis->addSubcard(d);
                if (!nottrick.isEmpty()) {
                    const Card *dd = nottrick.at(qrand() % nottrick.length());
                    dis->addSubcard(dd);
                }
                room->throwCard(dis, effect.to, nullptr);
                delete dis;
            }
        } else {
            const Card *c = Sanguosha->getCard(cc->getSubcards().first());
            if (c->isKindOf("TrickCard")) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
                room->obtainCard(effect.from, c, reason, true);
            } else
                room->throwCard(cc, effect.to, nullptr);
        }
    }

}

MobileMiejiDiscardCard::MobileMiejiDiscardCard()
{
    will_throw = false;
    target_fixed = true;
    mute = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobilemieji";
}

void MobileMiejiDiscardCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileMieji : public ViewAsSkill
{
public:
    MobileMieji() : ViewAsSkill("mobilemieji")
    {
        response_pattern = "@@mobilemieji!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileMiejiCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
            return selected.isEmpty() && to_select->isKindOf("TrickCard") && to_select->isBlack();

        if (selected.length() > 1) return false;
        if (selected.isEmpty())
            return to_select->isKindOf("TrickCard") || (!to_select->isKindOf("TrickCard") && !Self->isJilei(to_select));
        if (selected.length() == 1 && selected.first()->isKindOf("TrickCard"))
            return false;
        if (selected.length() == 1 && !selected.first()->isKindOf("TrickCard"))
            return !to_select->isKindOf("TrickCard") && !Self->isJilei(to_select);
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (cards.length() != 1) return nullptr;
            MobileMiejiCard *c = new MobileMiejiCard;
            c->addSubcards(cards);
            return c;
        } else {
            QList<const Card *> ccc = Self->getHandcards() + Self->getEquips();
            int n = 0;
            foreach (const Card *c, ccc) {
                if (!c->isKindOf("TrickCard") && !Self->isJilei(c))
                    n++;
            }
            n = qMin(n, 2);

            const Card *c = cards.first();
            if (c->isKindOf("TrickCard") && cards.length() != 1) return nullptr;
            if (!c->isKindOf("TrickCard") && cards.length() != n) return nullptr;

            MobileMiejiDiscardCard *dis = new MobileMiejiDiscardCard;
            dis->addSubcards(cards);
            return dis;
        }
        return nullptr;
    }
};

MobileXianzhenCard::MobileXianzhenCard()
{
    will_throw = false;
    handling_method = Card::MethodPindian;
}

bool MobileXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileXianzhenCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.from->canPindian(effect.to, false)) return;

    Room *room = effect.from->getRoom();

    if (effect.from->pindian(effect.to, "mobilexianzhen")) {
        room->addPlayerMark(effect.to, "Armor_Nullified");
        room->addPlayerMark(effect.from, "mobilexianzhen_from-PlayClear");
        room->addPlayerMark(effect.to, "mobilexianzhen_to-PlayClear");
    } else {
        room->addPlayerMark(effect.from, "mobilexianzhen_lose-PlayClear");
        room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
    }
}

class MobileXianzhenVS : public ZeroCardViewAsSkill
{
public:
    MobileXianzhenVS() : ZeroCardViewAsSkill("mobilexianzhen")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canPindian() && !player->hasUsed("MobileXianzhenCard");
    }

    const Card *viewAs() const
    {
       return new MobileXianzhenCard;
    }
};

class MobileXianzhen : public TriggerSkill
{
public:
    MobileXianzhen() : TriggerSkill("mobilexianzhen")
    {
        events << Pindian << EventPhaseProceeding;
        view_as_skill = new MobileXianzhenVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Pindian) {
            PindianStruct *pindian = data.value<PindianStruct *>();
            if (pindian->reason != objectName()) return false;
            if (!pindian->from_card->isKindOf("Slash")) return false;
            room->addPlayerMark(player, "mobilexianzhen_slash-Clear");
        } else {
            if (player->getPhase() != Player::Discard || player->getMark("mobilexianzhen_slash-Clear") <= 0) return false;
            QList<int> slash;
            foreach (const Card *c, player->getCards("h")) {
                if (c->isKindOf("Slash"))
                    slash << c->getEffectiveId();
            }
            if (slash.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player,objectName(), true, true);
            room->ignoreCards(player, slash);
        }
        return false;
    }
};

class MobileXianzhenClear : public TriggerSkill
{
public:
    MobileXianzhenClear() : TriggerSkill("#mobilexianzhen-clear")
    {
        events << EventPhaseEnd << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &data) const
    {
        if (triggerEvent == EventPhaseEnd) {
            if (gaoshun->getPhase() != Player::Play) return false;
            if (gaoshun->getMark("mobilexianzhen_lose-PlayClear") > 0)
                room->removePlayerCardLimitation(gaoshun, "use", "Slash$1");
            if (gaoshun->getMark("mobilexianzhen_from-PlayClear") > 0) {
                foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                    if (p->getMark("mobilexianzhen_to-PlayClear") <= 0) continue;
                    room->removePlayerMark(p, "Armor_Nullified");
                }
            }
        } else {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->getMark("mobilexianzhen_lose-PlayClear") > 0)
                room->removePlayerCardLimitation(death.who, "use", "Slash$1");
            if (death.who->getMark("mobilexianzhen_from-PlayClear") > 0) {
                foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                    if (p->getMark("mobilexianzhen_to-PlayClear") <= 0) continue;
                    room->removePlayerMark(p, "Armor_Nullified");
                }
            }
        }
        return false;
    }
};

class MobileXianzhenTargetMod : public TargetModSkill
{
public:
    MobileXianzhenTargetMod() : TargetModSkill("#mobilexianzhen-target")
    {
        frequency = NotFrequent;
        pattern = "^SkillCard";
    }

    int getResidueNum(const Player *from, const Card *, const Player *to) const
    {
        if (from->getMark("mobilexianzhen_from-PlayClear") > 0 && to && to->getMark("mobilexianzhen_to-PlayClear") > 0)
            return 1000;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *to) const
    {
        if (from->getMark("mobilexianzhen_from-PlayClear") > 0 && to && to->getMark("mobilexianzhen_to-PlayClear") > 0)
            return 1000;
        return 0;
    }
};

class MobileJinjiu : public FilterSkill
{
public:
    MobileJinjiu() : FilterSkill("mobilejinjiu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->objectName() == "analeptic";
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

class MobileJinjiuLimit : public CardLimitSkill
{
public:
    MobileJinjiuLimit() : CardLimitSkill("#mobilejinjiu-limit")
    {
    }

    bool gaoshun(const Player *target) const
    {
        foreach (const Player *p, target->getAliveSiblings()) {
            if (p->hasFlag("CurrentPlayer") && p->hasSkill("mobilejinjiu"))
                return true;
        }
        return false;
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (gaoshun(target))
            return "Analeptic";
        return "";
    }
};

class MobileJinjiuEffect : public TriggerSkill
{
public:
    MobileJinjiuEffect() : TriggerSkill("#mobilejinjiu")
    {
        events << DamageInflicted;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasSkill("mobilejinjiu")) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card||!damage.card->isKindOf("Slash")) return false;
        int drank = damage.card->getTag("drank").toInt();
        if (drank <= 0) return false;
        room->broadcastSkillInvoke("mobilejinjiu");
        room->notifySkillInvoked(player, "mobilejinjiu");
        int n = damage.damage;
        LogMessage log;
        log.type = "#MobilejinjiuReduce";
        log.from = player;
        log.arg = "mobilejinjiu";
        damage.damage -= drank;
        log.arg2 = QString::number(damage.damage);
        if (damage.damage <= 0) {
            log.type = "#MobilejinjiuPrevent";
            log.arg2 = QString::number(n);
            room->sendLog(log);
            return true;
        }
        room->sendLog(log);
        data = QVariant::fromValue(damage);
        return false;
    }
};

MobileQiaoshuiCard::MobileQiaoshuiCard()
{
}

bool MobileQiaoshuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileQiaoshuiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!source->canPindian(targets.first(), false)) return;
    bool success = source->pindian(targets.first(), "mobileqiaoshui", nullptr);
    if (success)
        source->setFlags("MobileQiaoshuiSuccess");
    else {
        source->setFlags("MobileQiaoshuiNotSuccess");
        room->setPlayerCardLimitation(source, "use", "TrickCard", true);
    }
}

class MobileQiaoshuiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    MobileQiaoshuiViewAsSkill() : ZeroCardViewAsSkill("mobileqiaoshui")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileQiaoshuiCard") && player->canPindian();
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mobileqiaoshui!";
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern.endsWith("!"))
            return new ExtraCollateralCard;
        return new MobileQiaoshuiCard;
    }
};

class MobileQiaoshui : public TriggerSkill
{
public:
    MobileQiaoshui() : TriggerSkill("mobileqiaoshui")
    {
        events << PreCardUsed << EventPhaseEnd;
        view_as_skill = new MobileQiaoshuiViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseEnd)
            return 0;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *jianyong, QVariant &data) const
    {
        if (jianyong->getPhase() != Player::Play) return false;
        if (event == PreCardUsed && jianyong->isAlive()) {
            if (!jianyong->hasFlag("MobileQiaoshuiSuccess")) return false;

            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isNDTrick() || use.card->isKindOf("BasicCard")) {
                jianyong->setFlags("-MobileQiaoshuiSuccess");
                if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY)
                    return false;

                QList<ServerPlayer *> available_targets;
                if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
                    room->setPlayerFlag(jianyong, "MobileQiaoshuiExtraTarget");
                    foreach (ServerPlayer *p, room->getAlivePlayers()) {
                        if (use.to.contains(p) || jianyong->isProhibited(p, use.card)) continue;
                        if (use.card->targetFixed()) {
                            if (!use.card->isKindOf("Peach") || p->isWounded())
                                available_targets << p;
                        } else {
							if (use.card->isKindOf("Collateral")){
								int x = 0;
								if (use.card->targetFilter(QList<const Player *>(), p, jianyong, x)||x>0)
									available_targets << p;
							}else if (use.card->targetFilter(QList<const Player *>(), p, jianyong))
                                available_targets << p;
                        }
                    }
                    room->setPlayerFlag(jianyong, "-MobileQiaoshuiExtraTarget");
                }
                QStringList choices;
                choices << "cancel";
                if (use.to.length() > 1) choices.prepend("remove");
                if (!available_targets.isEmpty()) choices.prepend("add");
                if (choices.length() == 1) return false;

                QString choice = room->askForChoice(jianyong, "mobileqiaoshui", choices.join("+"), data);
                if (choice == "add") {
                    ServerPlayer *extra;
                    if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos.append(use.card->toString());
						foreach (ServerPlayer *t, use.to)
							tos.append(t->objectName());
						room->setPlayerProperty(jianyong, "extra_collateral", tos);
                        room->askForUseCard(jianyong, "@@mobileqiaoshui!", "@qiaoshui-add:::collateral");
                        extra = jianyong->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
						jianyong->removeTag("ExtraCollateralTarget");
                        if (!extra) {
                            extra = available_targets.at(qrand() % available_targets.length());
                            QList<ServerPlayer *> victims;
                            foreach (ServerPlayer *p, room->getOtherPlayers(extra)) {
                                if (extra->canSlash(p))
                                    victims << p;
                            }
                            extra->setTag("attachTarget", QVariant::fromValue((victims.at(qrand() % victims.length()))));
                        }
					}else
                        extra = room->askForPlayerChosen(jianyong, available_targets, "qiaoshui", "@qiaoshui-add:::" + use.card->objectName());
                    use.to.append(extra);
                    room->sortByActionOrder(use.to);

                    LogMessage log;
                    log.type = "#QiaoshuiAdd";
                    log.from = jianyong;
                    log.to << extra;
                    log.card_str = use.card->toString();
                    log.arg = "mobileqiaoshui";
                    room->sendLog(log);
                } else if(choice != "cancel"){
                    ServerPlayer *removed = room->askForPlayerChosen(jianyong, use.to, "qiaoshui", "@qiaoshui-remove:::" + use.card->objectName());
                    use.to.removeOne(removed);

                    LogMessage log;
                    log.type = "#QiaoshuiRemove";
                    log.from = jianyong;
                    log.to << removed;
                    log.card_str = use.card->toString();
                    log.arg = "mobileqiaoshui";
                    room->sendLog(log);
                }
            }
            data = QVariant::fromValue(use);
        } else if (event == EventPhaseEnd) {
            if (!jianyong->hasFlag("MobileQiaoshuiNotSuccess")) return false;
            jianyong->setFlags("-MobileQiaoshuiNotSuccess");
            room->removePlayerCardLimitation(jianyong, "use", "TrickCard");
        }
        return false;
    }
};

class MobileQiaoshuiTargetMod : public TargetModSkill
{
public:
    MobileQiaoshuiTargetMod() : TargetModSkill("#mobileqiaoshui-target")
    {
        frequency = NotFrequent;
        pattern = "Slash,TrickCard+^DelayedTrick";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("MobileQiaoshuiExtraTarget"))
            return 1000;
        return 0;
    }
};

MobileZongshihCard::MobileZongshihCard()
{
    mute = true;
    handling_method = Card::MethodNone;
    will_throw = false;
    target_fixed = true;
}

void MobileZongshihCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileZongshihVS : public OneCardViewAsSkill
{
public:
    MobileZongshihVS() : OneCardViewAsSkill("mobilezongshih")
    {
        expand_pile = "#mobilezongshih_draw,#mobilezongshih_pindian";
        response_pattern = "@@mobilezongshih";
    }

    bool viewFilter(const Card *to_select) const
    {
        QList<int> draw = Self->getPile("#mobilezongshih_draw");
        QList<int> pdlist = Self->getPile("#mobilezongshih_pindian");
        return draw.contains(to_select->getEffectiveId()) || pdlist.contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileZongshihCard *card = new MobileZongshihCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class MobileZongshih : public TriggerSkill
{
public:
    MobileZongshih() : TriggerSkill("mobilezongshih")
    {
        events << Pindian;
        view_as_skill = new MobileZongshihVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PindianStruct *pindian = data.value<PindianStruct *>();
        QList<ServerPlayer *> jianyongs;
        if (pindian->from->isAlive() && pindian->from->hasSkill(objectName()))
            jianyongs << pindian->from;
        if (pindian->to->isAlive() && pindian->to->hasSkill(objectName()))
            jianyongs << pindian->to;
        if (jianyongs.isEmpty()) return false;
        room->sortByActionOrder(jianyongs);

        foreach (ServerPlayer *p, jianyongs) {
            if (p->isDead() || !p->hasSkill(objectName()) || !p->askForSkillInvoke(this)) continue;
            room->broadcastSkillInvoke(objectName());
            QList<int> draw = room->getNCards(1);
            LogMessage log;
            log.type = "$ViewDrawPile";
            log.from = p;
            log.card_str = ListI2S(draw).join("+");
            room->sendLog(log, p);
            room->notifyMoveToPile(p, draw, "mobilezongshih_draw", Player::DrawPile, true);

            QList<int> pdlist;
            if (pindian->from_number > pindian->to_number && room->getCardPlace(pindian->to_card->getEffectiveId()) == Player::PlaceTable)
                pdlist << pindian->to_card->getEffectiveId();
            else if (pindian->from_number < pindian->to_number && room->getCardPlace(pindian->from_card->getEffectiveId()) == Player::PlaceTable)
                pdlist << pindian->from_card->getEffectiveId();
            else if (pindian->from_number == pindian->to_number) {
                 if (room->getCardPlace(pindian->from_card->getEffectiveId()) == Player::PlaceTable)
                    pdlist << pindian->from_card->getEffectiveId();
                 if (room->getCardPlace(pindian->to_card->getEffectiveId()) == Player::PlaceTable && !pdlist.contains(pindian->to_card->getEffectiveId()))
                    pdlist << pindian->to_card->getEffectiveId();
            }
            if (!pdlist.isEmpty())
                room->notifyMoveToPile(p, pdlist, "mobilezongshih_pindian", Player::PlaceTable, true);

            const Card *c = room->askForUseCard(p, "@@mobilezongshih", "@mobilezongshih", -1, Card::MethodNone);
            room->notifyMoveToPile(p, draw, "mobilezongshih_draw", Player::DrawPile, false);
            if (!pdlist.isEmpty())
                room->notifyMoveToPile(p, pdlist, "mobilezongshih_pindian", Player::PlaceTable, false);
            room->returnToTopDrawPile(draw);
            if (!c) continue;
            room->obtainCard(p, c, false);
        }

        return false;
    }
};

class MobileDanshou : public PhaseChangeSkill
{
public:
    MobileDanshou() : PhaseChangeSkill("mobiledanshou")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            int n = p->getMark("mobiledanshou_num" + player->objectName() + "-Clear");
            if (n <= 0) {
                room->sendCompulsoryTriggerLog(p, this, 2);
                p->drawCards(1, objectName());
            } else {
                if (player->isDead() || !p->canDiscard(p, "he")) continue;
                p->setTag("mobiledanshou_target", QVariant::fromValue(player));
                const Card *card = room->askForDiscard(p, objectName(), n, n, true, true,
                            QString("@mobiledanshou-dis:%1::%2").arg(player->objectName()).arg(n), ".", objectName());
                if (!card) continue;
				room->broadcastSkillInvoke(objectName(),1,p);
                room->damage(DamageStruct(objectName(), p, player));
            }
        }
        return false;
    }
};

class MobileDuodao : public MasochismSkill
{
public:
    MobileDuodao() : MasochismSkill("mobileduodao")
    {
        frequency = Frequent;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        if (!damage.from || damage.from->isDead() || !damage.from->getWeapon()) return;
        if (!player->askForSkillInvoke(this, damage.from)) return;
        player->getRoom()->broadcastSkillInvoke(objectName());
        player->obtainCard(damage.from->getWeapon());
    }
};

class MobileAnjian : public TriggerSkill
{
public:
    MobileAnjian() : TriggerSkill("mobileanjian")
    {
        events << DamageCaused << TargetSpecified;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->hasFlag("mobileanjian_damage_" + damage.to->objectName())) return false;
            room->setCardFlag(damage.card, "-mobileanjian_damage_" + damage.to->objectName());
            ++damage.damage;
            data = QVariant::fromValue(damage);
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
            foreach (ServerPlayer *p, use.to) {
                if (player->isDead()) break;
                if (p->isDead() || p->inMyAttackRange(player)) continue;
                player->setTag("mobileanjian_usedata", data);
                QString choice = room->askForChoice(player, objectName(), "noresponse+damage", QVariant::fromValue(p));
                player->removeTag("mobileanjian_usedata");
                LogMessage log;
                log.type = "#FumianFirstChoice";
                log.from = player;
                log.arg = "mobileanjian:" + choice;
                room->sendLog(log);
                if (choice == "noresponse") {
                    use.no_respond_list << p->objectName();
                    data = QVariant::fromValue(use);
                } else
                    room->setCardFlag(use.card, "mobileanjian_damage_" + p->objectName());
            }
        }
        return false;
    }
};

class MobileZhuikong : public TriggerSkill
{
public:
    MobileZhuikong() : TriggerSkill("mobilezhuikong")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::RoundStart)
            return false;

        foreach (ServerPlayer *fuhuanghou, room->getOtherPlayers(player)) {
            if (TriggerSkill::triggerable(fuhuanghou) && fuhuanghou->getMark("mobilezhuikong_lun") <= 0
                && player->getHp() >= fuhuanghou->getHp() && fuhuanghou->canPindian(player)
                && fuhuanghou->askForSkillInvoke(objectName(), player)) {
                room->broadcastSkillInvoke(objectName());
                room->addPlayerMark(fuhuanghou, "mobilezhuikong_lun");
                PindianStruct *pindian = fuhuanghou->PinDian(player, objectName());
                if (pindian->success) {
                    room->setPlayerFlag(player, "mobilezhuikong");
                } else {
                    int to_card_id = pindian->to_card->getEffectiveId();
                    if (room->getCardPlace(to_card_id) != Player::DiscardPile) return false;
                    room->obtainCard(fuhuanghou, to_card_id);
                    Slash *slash = new Slash(Card::NoSuit, 0);
                    slash->setSkillName("_mobilezhuikong");
                    slash->deleteLater();
                    if (!player->canSlash(fuhuanghou, slash, false)) return false;
                    room->useCard(CardUseStruct(slash, player, fuhuanghou));
                }
            }
        }
        return false;
    }
};

class MobileZhuikongProhibit : public ProhibitSkill
{
public:
    MobileZhuikongProhibit() : ProhibitSkill("#mobilezhuikong")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (card->getTypeId() != Card::TypeSkill && from->hasFlag("mobilezhuikong"))
            return to != from;
        return false;
    }
};

class MobileQiuyuan : public TriggerSkill
{
public:
    MobileQiuyuan() : TriggerSkill("mobileqiuyuan")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        QList<ServerPlayer *> targets = room->getOtherPlayers(player);
        if (targets.contains(use.from))
            targets.removeOne(use.from);

        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@mobileqiuyuan-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());

        const Card *card = nullptr;
        if (!target->isKongcheng())
            card = room->askForCard(target, "BasicCard+^Slash", "@mobileqiuyuan-give:" + player->objectName(), data, Card::MethodNone);
        if (card) {
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "mobileqiuyuan", "");
            room->obtainCard(player, card, reason);
        } else {
            if (!use.from->canSlash(target, use.card, false)) return false;
            LogMessage log;
            log.type = "#BecomeTarget";
            log.from = target;
            log.card_str = use.card->toString();
            room->sendLog(log);
            use.to.append(target);
            room->sortByActionOrder(use.to);
            data = QVariant::fromValue(use);
        }
        return false;
    }
};

class MobileJingce : public PhaseChangeSkill
{
public:
    MobileJingce() : PhaseChangeSkill("mobilejingce")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        int n = room->getTag("mobilejingce_record").toInt();
        if (n < player->getHp()) return false;
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        player->drawCards(2, objectName());
        return false;
    }
};

class MobileJingceRecord : public TriggerSkill
{
public:
    MobileJingceRecord() : TriggerSkill("#mobilejingce-record")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (player==move.from&&move.to_place == Player::DiscardPile && ((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE
			 ||move.reason.m_reason == CardMoveReason::S_REASON_RESPONSE)) {
                //改判打出的牌CardMoveReason::S_REASON_RETRIAL，置入弃牌堆的原因是判定，而不是打出
                const Card *card = move.reason.m_extraData.value<const Card *>();
                if (!card || card->isKindOf("SkillCard")) return false;
                int n = room->getTag("mobilejingce_record").toInt();
                if (card->isVirtualCard())
                    n += card->subcardsLength();
                else
                    n++;
                room->setTag("mobilejingce_record", n);
            }
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            room->setTag("mobilejingce_record", 0);
        }
        return false;
    }
};

MobileDingpinCard::MobileDingpinCard()
{
}

bool MobileDingpinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getMark("mobiledingpin_target-PlayClear") == 0;
}

void MobileDingpinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    JudgeStruct judge;
    judge.who = effect.to;
    judge.good = true;
    judge.pattern = ".|black";
    judge.reason = "mobiledingpin";

    room->judge(judge);

    if (judge.isGood()) {
        effect.to->drawCards(qMin(3, effect.to->getHp()), "mobiledingpin");
        room->addPlayerMark(effect.to, "mobiledingpin_target-PlayClear");
    } else {
        Card::Suit suit = judge.card->getSuit();
        if (suit == Card::Diamond)
            effect.from->turnOver();
        else if (suit == Card::Heart)
            room->removePlayerMark(effect.from, "dingpin_" + Sanguosha->getCard(subcards.first())->getType() + "-Clear");
    }
}

class MobileDingpinVS : public OneCardViewAsSkill
{
public:
    MobileDingpinVS() : OneCardViewAsSkill("mobiledingpin")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return Self->getMark("dingpin_" + to_select->getType() + "-Clear") == 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileDingpinCard *card = new MobileDingpinCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class MobileDingpin : public TriggerSkill
{
public:
    MobileDingpin() : TriggerSkill("mobiledingpin")
    {
        events << PreCardUsed << BeforeCardsMove;
        view_as_skill = new MobileDingpinVS;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (!player->isAlive() || player->getPhase() == Player::NotActive) return false;
		if (triggerEvent == PreCardUsed) {
			const Card *card = data.value<CardUseStruct>().card;
			if (!card || card->getTypeId() == Card::TypeSkill) return false;
			room->addPlayerMark(player, "dingpin_" + card->getType() + "-Clear");
		} else if (triggerEvent == BeforeCardsMove) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (player != move.from || ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD))
				return false;
			foreach (int id, move.card_ids) {
				const Card *c = Sanguosha->getCard(id);
				room->addPlayerMark(player, "dingpin_" + c->getType() + "-Clear");
			}
		}
        return false;
    }
};

class MobileZhongyong : public TriggerSkill
{
public:
    MobileZhongyong() : TriggerSkill("mobilezhongyong")
    {
        events << CardOffset << CardFinished << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.reason.m_skillName != objectName() || move.to_place != Player::PlaceHand || move.to != player) return false;
            QStringList limited = player->getTag("mobilezhongyong_limited").toStringList();
            foreach (int id, move.card_ids) {
                QString str = QString::number(id);
                if (limited.contains(str)) continue;
                limited << str;
                room->setPlayerCardLimitation(player, "use", str, true);
            }
            player->setTag("mobilezhongyong_limited", limited);
        } else if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("Slash")) return false;
            QStringList jink = effect.from->getTag("mobilezhongyong_" + effect.card->toString()).toStringList();
            QString str = effect.offset_card->toString();
            if (!jink.contains(str)) {
                jink << str;
                effect.from->setTag("mobilezhongyong_" + effect.card->toString(), jink);
            }
        } else {
            if (player->getPhase() != Player::Play) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;

            QStringList jink = player->getTag("mobilezhongyong_" + use.card->toString()).toStringList();
            player->removeTag("mobilezhongyong_" + use.card->toString());

            CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, player->objectName(), objectName(), "");

            if (jink.isEmpty()) {
                if (room->CardInPlace(use.card, Player::DiscardPile) && player->askForSkillInvoke(this, data)) {
                    room->broadcastSkillInvoke(this);
                    room->obtainCard(player, use.card, reason, true);
                }
            } else {
                QList<int> jinks;
                foreach (QString str, jink) {
                    const Card *card = Card::Parse(str);
                    if (!card || !room->CardInPlace(card, Player::DiscardPile)) continue;

                    if (card->isVirtualCard())
                        jinks << card->getSubcards();
                    else
                        jinks << card->getEffectiveId();
                }
                if (jinks.isEmpty()) return false;

                QList<ServerPlayer *> targets;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (use.to.contains(p)) continue;
                    targets << p;
                }
                if (!targets.contains(player))
                    targets << player;
                if (targets.isEmpty()) return false;

                room->fillAG(jinks, player);
                ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@mobilezhongyong-invoke", true, true);
                room->clearAG(player);
                if (!target) return false;
                room->broadcastSkillInvoke(this);

                DummyCard get_jink(jinks);

                if (target == player) {
                    room->obtainCard(player, &get_jink, reason, true);
                    if (player->isAlive() && room->CardInPlace(use.card, Player::DiscardPile)) {
                        if (targets.contains(player))
                            targets.removeOne(player);
                        if (targets.isEmpty()) return false;
                        QList<int> slashs;
                        if (use.card->isVirtualCard())
                            slashs = use.card->getSubcards();
                        else
                            slashs << use.card->getEffectiveId();
                        if (slashs.isEmpty()) return false;
                        room->fillAG(slashs, player);
                        ServerPlayer *target2 = room->askForPlayerChosen(player, targets, "mobilezhongyong_slash", "@mobilezhongyong-give", true, false);
                        room->clearAG(player);
                        if (!target2) return false;
                        room->doAnimate(1, player->objectName(), target2->objectName());
                        CardMoveReason reason2(CardMoveReason::S_REASON_GIVE, player->objectName(), target2->objectName(), objectName(), "");
                        room->obtainCard(target2, use.card, reason2, true);
                    }
                } else {
                    CardMoveReason reason2(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), objectName(), "");
                    room->obtainCard(target, &get_jink, reason2, true);
                    if (player->isAlive()) {
                        room->addSlashCishu(player, 1);
                        room->addPlayerMark(player, "&mobilezhongyong");
                    }
                }
            }
        }
        return false;
    }
};

class MobileZhongyongEffect : public TriggerSkill
{
public:
    MobileZhongyongEffect() : TriggerSkill("#mobilezhongyong-effect")
    {
        events << PreCardUsed << ConfirmDamage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            int mark = player->getMark("&mobilezhongyong");
            if (!use.card->isKindOf("Slash") || mark <= 0) return false;
            room->setCardFlag(use.card, "mobilezhongyong_damage_" + QString::number(mark));
            room->setPlayerMark(player, "&mobilezhongyong", 0);
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card||!damage.card->isKindOf("Slash")) return false;
            int damage_num = 0;
            foreach (QString flag, damage.card->getFlags()) {
                if (!flag.startsWith("mobilezhongyong_damage_")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 3) continue;
                damage_num = flags.last().toInt();
                break;
            }
            if (damage_num <= 0) return false;
            damage.damage += damage_num;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class MobileZhongyongRemove : public TriggerSkill
{
public:
    MobileZhongyongRemove() : TriggerSkill("#mobilezhongyong-remove")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            QStringList limited = p->getTag("mobilezhongyong_limited").toStringList();
            p->removeTag("mobilezhongyong_limited");
            foreach (QString str, limited)
                room->removePlayerCardLimitation(p, "use", str + "$1");
        }
        return false;
    }
};

MobileShenxingCard::MobileShenxingCard()
{
    target_fixed = true;
}

void MobileShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isAlive()) {
        int n = 1;
        if (!Sanguosha->getCard(subcards.first())->sameColorWith(Sanguosha->getCard(subcards.last())))
            n++;
        room->drawCards(source, n, "mobileshenxing");
    }
}

class MobileShenxing : public ViewAsSkill
{
public:
    MobileShenxing() : ViewAsSkill("mobileshenxing")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        MobileShenxingCard *card = new MobileShenxingCard;
        card->addSubcards(cards);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount(true) >= 2 && player->canDiscard(player, "he") && player->usedTimes("MobileShenxingCard") < player->getHp();
    }
};

MobileBingyiCard::MobileBingyiCard()
{
}

bool MobileBingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    QList<const Card *>cards = Self->getHandcards();
    if (cards.isEmpty()) return false;

    bool same_color = true, same_type = true;
    Card::Color color = cards.first()->getColor();
    int type_id = cards.first()->getTypeId();

    foreach (const Card *c, cards) {
        if (c->getColor() != color) {
            same_color = false;
            break;
        }
    }
    if (!same_color) {
        foreach (const Card *c, cards) {
            if (c->getTypeId() != type_id) {
                same_type = false;
                break;
            }
        }
    }
    if (!same_color && !same_type)
        return targets.isEmpty();
    return targets.length() <= Self->getHandcardNum();
}

bool MobileBingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    QList<const Card *>cards = Self->getHandcards();
    if (cards.isEmpty()) return false;

    bool same_color = true, same_type = true;
    Card::Color color = cards.first()->getColor();
    int type_id = cards.first()->getTypeId();

    foreach (const Card *c, cards) {
        if (c->getColor() != color) {
            same_color = false;
            break;
        }
    }
    if (!same_color) {
        foreach (const Card *c, cards) {
            if (c->getTypeId() != type_id) {
                same_type = false;
                break;
            }
        }
    }
    if (!same_color && !same_type)
        return false;
    return targets.length() < Self->getHandcardNum();
}

void MobileBingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source);
    foreach(ServerPlayer *p, targets)
        room->drawCards(p, 1, "mobilebingyi");
}

class MobileBingyiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    MobileBingyiViewAsSkill() : ZeroCardViewAsSkill("mobilebingyi")
    {
        response_pattern = "@@mobilebingyi";
    }

    const Card *viewAs() const
    {
        return new MobileBingyiCard;
    }
};

class MobileBingyi : public PhaseChangeSkill
{
public:
    MobileBingyi() : PhaseChangeSkill("mobilebingyi")
    {
        view_as_skill = new MobileBingyiViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish || target->isKongcheng()) return false;
        room->askForUseCard(target, "@@mobilebingyi", "@mobilebingyi");
        return false;
    }
};

class MobileQieting : public TriggerSkill
{
public:
    MobileQieting() : TriggerSkill("mobileqieting")
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
        if (change.to != Player::NotActive || player->getMark("damage_point_turn-Clear") > 0) return false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!TriggerSkill::triggerable(p) || !p->askForSkillInvoke(this, player)) continue;
            room->broadcastSkillInvoke(this);

            QStringList choices;
            QList<int> disable_ids;
            if (player->isAlive()) {
                if (!player->isKongcheng())
                    choices << "view=" + player->objectName();
                for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
                    if (player->getEquip(i) && (p->getEquip(i) || !p->hasEquipArea(i)))
                        disable_ids << player->getEquip(i)->getEffectiveId();
                }
                if (player->getEquips().length() > disable_ids.length())
                    choices << "move=" + player->objectName();
            }
            choices << "draw";
            QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
            if (choice == "draw")
                p->drawCards(1, objectName());
            else if (choice.startsWith("move")) {
                int id = room->askForCardChosen(p, player, "e", objectName(), false, Card::MethodNone, disable_ids);
                if (id>-1) room->moveCardTo(Sanguosha->getCard(id), p, Player::PlaceEquip);
            } else {
                int n = qMin(player->getHandcardNum(), 2);
                if (n <= 0) return false;
                QList<int> cards;
                for (int i = 0; i < n; ++i) {
                    int id = room->askForCardChosen(p, player, "h", "mobileqieting_view", false, Card::MethodNone, cards);
					if(id<0) break;
                    cards << id;
                }
                if (cards.isEmpty() || p->isDead()) return false;
                room->fillAG(cards, p);
                int id = room->askForAG(p, cards, false, objectName());
                room->clearAG(p);
                room->obtainCard(p, id, false);
            }
        }
        return false;
    }
};

MobileJianyingDialog *MobileJianyingDialog::getInstance(const QString &object)
{
    static MobileJianyingDialog *instance;
    if (instance == nullptr || instance->objectName() != object)
        instance = new MobileJianyingDialog(object);

    return instance;
}

MobileJianyingDialog::MobileJianyingDialog(const QString &object)
    : GuhuoDialog(object, true, false)
{
}

bool MobileJianyingDialog::isButtonEnabled(const QString &button_name) const
{
    const Card *c = map[button_name];
    c->setFlags(objectName());
    return button_name != "normal_slash" && !Self->isCardLimited(c, Card::MethodUse) && c->isAvailable(Self);
}

MobileJianyingCard::MobileJianyingCard()
{
    mute = true;
    handling_method = Card::MethodUse;
    will_throw = false;
}

bool MobileJianyingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->addSubcard(subcards.first());
    card->setCanRecast(false);
    QString suitstring = Self->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //card->setFlags("CardInformationHelper|" + suitstring + "|" + QString::number(card->getNumber()));
		if(suitstring=="spade")
			card->setSuit(Card::Spade);
		else if(suitstring=="club")
			card->setSuit(Card::Club);
		else if(suitstring=="heart")
			card->setSuit(Card::Heart);
		else
			card->setSuit(Card::Diamond);
	}
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool MobileJianyingCard::targetFixed() const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->addSubcard(subcards.first());
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFixed();
}

bool MobileJianyingCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->setCanRecast(false);
    QString suitstring = Self->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //card->setFlags("CardInformationHelper|" + suitstring + "|" + QString::number(card->getNumber()));
		if(suitstring=="spade")
			card->setSuit(Card::Spade);
		else if(suitstring=="club")
			card->setSuit(Card::Club);
		else if(suitstring=="heart")
			card->setSuit(Card::Heart);
		else
			card->setSuit(Card::Diamond);
	}
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *MobileJianyingCard::validate(CardUseStruct &card_use) const
{
    Room *room = card_use.from->getRoom();

    Card *use_card = Sanguosha->cloneCard(user_string);
    use_card->setSkillName("mobilejianying");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();

    QString suitstring = card_use.from->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //room->setCardFlag(use_card, "CardInformationHelper|" + suitstring + "|" + QString::number(getNumber()));
		if(suitstring=="spade")
			use_card->setSuit(Card::Spade);
		else if(suitstring=="club")
			use_card->setSuit(Card::Club);
		else if(suitstring=="heart")
			use_card->setSuit(Card::Heart);
		else
			use_card->setSuit(Card::Diamond);
	}
	room->addPlayerMark(card_use.from,"mobilejianyingUse-PlayClear");
	card_use.m_addHistory = false;
    return use_card;
}

class MobileJianyingVS : public OneCardViewAsSkill
{
public:
    MobileJianyingVS() : OneCardViewAsSkill("mobilejianying")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("mobilejianyingUse-PlayClear")<1;
    }

    bool viewFilter(const Card *card) const
    {
        const Card *_card = Self->getTag("mobilejianying").value<const Card *>();
        if (_card == nullptr) return false;
        Card *c = Sanguosha->cloneCard(_card);
        c->addSubcard(card);
        c->setSkillName(objectName());
        return c->isAvailable(Self) && !Self->isCardLimited(card, Card::MethodUse);
    }

    const Card *viewAs(const Card *originalCard) const
    {
        const Card *c = Self->getTag("mobilejianying").value<const Card *>();
        if (c && c->isAvailable(Self)) {
            MobileJianyingCard *card = new MobileJianyingCard;
            card->setUserString(c->objectName());
            card->addSubcard(originalCard);
            return card;
        }
        return nullptr;
    }
};

class MobileJianying : public Jianying
{
public:
    MobileJianying() : Jianying()
    {
        setObjectName("mobilejianying");
        jianying = "MobileJianying";
        view_as_skill = new MobileJianyingVS;
        frequency = NotFrequent;
    }

    QDialog *getDialog() const
    {
        return MobileJianyingDialog::getInstance("mobilejianying");
    }
};

class MobileJianyingTargetMod : public TargetModSkill
{
public:
    MobileJianyingTargetMod() : TargetModSkill("#mobilejianying-target")
    {
        frequency = NotFrequent;
        pattern = "BasicCard";
    }

    int getResidueNum(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "mobilejianying" || card->hasFlag("mobilejianying"))
            return 1000;
        return 0;
    }
};

MobileYanzhuCard::MobileYanzhuCard()
{
}

void MobileYanzhuCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    if (to->isAllNude()) return;
    Room *room = from->getRoom();
    QStringList choices;
    if (!to->getEquips().isEmpty())
        choices << "equip=" + from->objectName();
    choices << "obtain=" + from->objectName();
    QString choice = room->askForChoice(to, "mobileyanzhu", choices.join("+"), QVariant::fromValue(from));
    if (choice.startsWith("equip")) {
        DummyCard dummy;
        dummy.addSubcards(to->getEquips());
        room->obtainCard(from, &dummy);
        room->handleAcquireDetachSkills(from, "-mobileyanzhu");
        room->setPlayerProperty(from, "MobileXingxueLevelUp", true);
        room->changeTranslation(from, "mobilexingxue", 1);
    } else {
        int id = room->askForCardChosen(from, to, "hej", "mobileyanzhu");
        room->obtainCard(from, id, false);
    }
}

class MobileYanzhu : public ZeroCardViewAsSkill
{
public:
    MobileYanzhu() : ZeroCardViewAsSkill("mobileyanzhu")
    {
    }

    const Card *viewAs() const
    {
        return new MobileYanzhuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileYanzhuCard");
    }
};

MobileXingxueCard::MobileXingxueCard()
{
}

bool MobileXingxueCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    bool xingxue = Self->property("MobileXingxueLevelUp").toBool();
    int max = xingxue ? Self->getMaxHp() : Self->getHp();
    return targets.length() < max;
}

bool MobileXingxueCard::hasAliveTargets(ServerPlayer *player, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
        if (p == player) continue;
        if (p->isAlive())
            return true;
    }
    return false;
}

void MobileXingxueCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    bool xingxue = source->property("MobileXingxueLevelUp").toBool();
    QStringList target_names;
    foreach (ServerPlayer *t, targets)
        target_names << t->objectName();

    foreach (ServerPlayer *t, targets) {
        t->drawCards(1, "mobilexingxue");
        if (t->isAlive() && !t->isNude()) {
            QStringList choices;
            if (xingxue && hasAliveTargets(t, targets))
                choices << "give";
            choices << "put";

            QString choice = room->askForChoice(t, "mobilexingxue", choices.join("+"), target_names);

            if (choice == "put") {
                const Card *c = room->askForExchange(t, "mobilexingxue", 1, 1, true, "@xingxue-put");
                int id = c->getSubcards().first();

                CardsMoveStruct m(id, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, t->objectName()));
                room->setPlayerFlag(t, "Global_GongxinOperator");
                room->moveCardsAtomic(m, false);
                room->setPlayerFlag(t, "-Global_GongxinOperator");
            } else {
                QList<ServerPlayer *> new_targets = targets;
                new_targets.removeOne(t);
                QList<int> cards = t->handCards() + t->getEquipsId();
                room->askForYiji(t, cards, "mobilexingxue", false, false, false, 1, new_targets);
            }
        }
    }
}

class MobileXingxueVS : public ZeroCardViewAsSkill
{
public:
    MobileXingxueVS() : ZeroCardViewAsSkill("mobilexingxue")
    {
        response_pattern = "@@mobilexingxue";
    }

    const Card *viewAs() const
    {
        return new MobileXingxueCard;
    }
};

class MobileXingxue : public PhaseChangeSkill
{
public:
    MobileXingxue() : PhaseChangeSkill("mobilexingxue")
    {
        view_as_skill = new MobileXingxueVS;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        bool xingxue = player->property("MobileXingxueLevelUp").toBool();
        int num = xingxue ? player->getMaxHp() : player->getHp();
        if (num <= 0) return false;
        room->askForUseCard(player, "@@mobilexingxue", "@mobilexingxue:" + QString::number(num));
        return false;
    }
};

MobileSidiCard::MobileSidiCard()
{
}

bool MobileSidiCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
    if(targets.isEmpty()){
		return target!=Self&&target->getMark("mobilesidiFrom"+Self->objectName())<1;
	}
	return targets.length() < 2;
}

bool MobileSidiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length()==2;
}

void MobileSidiCard::onUse(Room *room, CardUseStruct &use) const
{
    ServerPlayer *tp = use.to.takeLast();
	use.to.first()->setTag("mobilesidiTo", QVariant::fromValue(tp));
	room->setPlayerMark(use.to.first(),"mobilesidiFrom"+use.from->objectName(),1);
	room->setPlayerMark(use.to.first(),"&mobilesidi+:+"+tp->getGeneralName(),1,QList<ServerPlayer*>()<<use.from);
	SkillCard::onUse(room,use);
}

class MobileSidiVS : public ZeroCardViewAsSkill
{
public:
    MobileSidiVS() : ZeroCardViewAsSkill("mobilesidi")
    {
        response_pattern = "@@mobilesidi";
    }

    const Card *viewAs() const
    {
        return new MobileSidiCard;
    }
};

class MobileSidi : public TriggerSkill
{
public:
    MobileSidi() : TriggerSkill("mobilesidi")
    {
        events << CardFinished << TargetSpecifying;
        view_as_skill = new MobileSidiVS;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == CardFinished) {
			const Card *card = data.value<CardUseStruct>().card;
			if (card->getTypeId()>0&&!card->isKindOf("DelayedTrick")&&player->hasSkill(objectName())){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getTag("mobilesidiTo").isNull()){
						room->askForUseCard(player,"@@mobilesidi","mobilesidi0");
						break;
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&!use.card->isKindOf("DelayedTrick")
				&&use.to.length()==1&&!player->getTag("mobilesidiTo").isNull()){
				ServerPlayer *tp = player->getTag("mobilesidiTo").value<ServerPlayer*>();
				player->removeTag("mobilesidiTo");
				if(use.to.contains(tp)){
					if(player->getMark("mobilesidiFrom"+tp->objectName())>0){
						tp->drawCards(1,objectName());
					}else{
						foreach (ServerPlayer *p, room->getAllPlayers()) {
							if(player->getMark("mobilesidiFrom"+p->objectName())>0){
								if(room->askForChoice(p,objectName(),"mobilesidi1+mobilesidi2",data)=="mobilesidi1"){
									use.to.removeOne(tp);
									data.setValue(use);
									if(!room->getCurrentDyingPlayer())
										room->damage(DamageStruct(objectName(),p,player));
								}else
									p->drawCards(2,objectName());
							}
						}
					}
				}
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("mobilesidiFrom")||m.contains("&mobilesidi+:+"))
						room->setPlayerMark(player,m,0);
				}
			}
		}
        return false;
    }
};

MobileYaomingCard::MobileYaomingCard()
{
}

bool MobileYaomingCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
    if(targets.isEmpty()){
		if(target->getHandcardNum()>Self->getHandcardNum())
			return Self->canDiscard(target,"he");
		return target->getHandcardNum()<=Self->getHandcardNum();
	}
	return false;
}

void MobileYaomingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->loseMark("&charge_num");
    foreach (ServerPlayer *t, targets) {
		QStringList choices;
		if(t->getHandcardNum()>source->getHandcardNum())
			choices << "discard";
		else{
			if(t->getHandcardNum()==source->getHandcardNum()){
				if(t!=source&&source->canDiscard(t,"he"))
					choices << "discard";
			}
			choices << "draw";
		}
		QString choice = room->askForChoice(source,"mobileyaoming",choices.join("+"),QVariant::fromValue(t));
		if(choice=="discard"){
			int id = room->askForCardChosen(source,t,"he","mobileyaoming",false,Card::MethodDiscard);
			if(id>=0) room->throwCard(id,"mobileyaoming",t,source);
		}else
			t->drawCards(1,objectName());
		if(!source->getTag("mobileyaomingChoice").isNull()){
			if(source->getTag("mobileyaomingChoice").toString()!=choice){
				source->gainMark("&charge_num");
				source->removeTag("mobileyaomingChoice");
				continue;
			}
		}
		source->setTag("mobileyaomingChoice", choice);
	}
}

class MobileYaomingVS : public ZeroCardViewAsSkill
{
public:
    MobileYaomingVS() : ZeroCardViewAsSkill("mobileyaoming")
    {
        response_pattern = "@@mobileyaoming";
    }

    const Card *viewAs() const
    {
        return new MobileYaomingCard;
    }
    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("&charge_num")>0;
    }
};

class MobileYaoming : public TriggerSkill
{
public:
    MobileYaoming() : TriggerSkill("mobileyaoming")
    {
        events << Damaged;
        view_as_skill = new MobileYaomingVS;
		setProperty("ChargeNum","2/4");
    }
    int getChargeNum(const Player *player) const
    {
        int n = 0;
        foreach (const Skill *s, player->getVisibleSkillList()){
			QString cn = s->property("ChargeNum").toString();
			if(cn.contains("/")) n += cn.split("/").last().toInt();
		}
		return n;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			int n = getChargeNum(player)-player->getMark("&charge_num");
			if(n>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->gainMark("&charge_num",qMin(damage.damage,n));
			}
			if(player->getMark("&charge_num")>0){
				room->askForUseCard(player,"@@mobileyaoming","mobileyaoming0");
			}
		}
        return false;
    }
};

class MobileQingxi : public TriggerSkill
{
public:
    MobileQingxi() : TriggerSkill("mobileqingxi")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if(player!=damage.to&&player->getMark("mobileqingxiUse-Clear")<1
			&&player->hasTurn()&&player->askForSkillInvoke(objectName()+"$-1",damage.to)){
				player->addMark("mobileqingxiUse-Clear");
				int n = qMax(1,4-player->distanceTo(damage.to));
				if(damage.to->canDiscard(damage.to,"h")&&room->askForDiscard(damage.to,objectName(),n,n,true,false,"mobileqingxi0:"+QString::number(n)))
					return false;
				player->damageRevises(data,1);
			}
		}
        return false;
    }
};

class MobileAnguo : public TriggerSkill
{
public:
    MobileAnguo() : TriggerSkill("mobileanguo")
    {
        events << GameStart << EventPhaseStart << DamageInflicted << Dying;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == GameStart) {
			ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"mobileanguo0",false,true);
			if(tp){
				player->peiyin(this);
				tp->addMark("mobileanguoTo");
				tp->gainMark("&mobileanguo");
			}
		}else if(triggerEvent==EventPhaseStart){
			if(player->getPhase()==Player::Play){
				QList<ServerPlayer *>tps;
				ServerPlayer *atp = nullptr;
				foreach (ServerPlayer *t, room->getOtherPlayers(player)) {
					if(t->getMark("mobileanguoTo")<1) tps << t;
					if(t->getMark("&mobileanguo")>0) atp = t;
				}
				if(!atp) return false;
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"mobileanguo1",true,true);
				if(tp){
					player->peiyin(this);
					tp->addMark("mobileanguoTo");
					atp->loseAllMarks("&mobileanguo");
					tp->gainMark("&mobileanguo");
				}
			}
		}else if(triggerEvent==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>=player->getHp()&&damage.from&&damage.from->getMark("&mobileanguo")<1){
				foreach (ServerPlayer *t, room->getAlivePlayers()) {
					if(t->getMark("&mobileanguo")>0){
						room->sendCompulsoryTriggerLog(player,this);
						return player->damageRevises(data,-damage.damage);
					}
				}
			}
		}else if(triggerEvent==Dying){
			DyingStruct dy = data.value<DyingStruct>();
			if(dy.who->getMark("&mobileanguo")>0&&dy.who->getHp()<1){
				dy.who->loseAllMarks("&mobileanguo");
				room->recover(dy.who,RecoverStruct(objectName(),player,1-dy.who->getHp()));
				QStringList choices;
				if(player->getHp()>1) choices << "hp";
				if(player->getMaxHp()>1) choices << "max_hp";
				if(choices.isEmpty()) return false;
				if(room->askForChoice(player,objectName(),choices.join("+"))=="hp")
					room->loseHp(player,player->getHp()-1,true,player,objectName());
				else
					room->loseMaxHp(player,player->getMaxHp()-1,objectName());
				dy.who->gainHujia();
			}
		}
        return false;
    }
};

class MobileBenxi : public TriggerSkill
{
public:
    MobileBenxi() : TriggerSkill("mobilebenxi")
    {
        events << EventPhaseStart << PreCardUsed << CardFinished;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("BasicCard")||use.card->isNDTrick()){
				int n = player->getMark("&mobilebenxi-PlayClear");
				if(n>0&&player->getMark("mobilebenxiUse-PlayClear")<1){
					player->addMark("mobilebenxiUse-PlayClear");
					room->setCardFlag(use.card,"mobilebenxiBf");
					QList<ServerPlayer *>tps = room->getCardTargets(player,use.card,use.to);
					foreach (ServerPlayer *t, tps) {
						if(player->distanceTo(t)!=1)
							tps.removeOne(t);
					}
					player->setTag("mobilebenxiUse", data);
					tps = room->askForPlayersChosen(player,tps,objectName(),0,n,"mobilebenxi1:"+use.card->objectName(),false,false);
					if(tps.length()>0){
						use.to << tps;
						room->sortByActionOrder(use.to);
						data.setValue(use);
					}
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("mobilebenxiBf")&&use.card->hasFlag("DamageDone")){
				player->drawCards(5,objectName());
			}
		}else if(triggerEvent==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())&&player->canDiscard(player,"he")){
				const Card*dc = room->askForDiscard(player,objectName(),999,1,true,true,"mobilebenxi0",".",objectName());
				if(dc){
					player->peiyin(this);
					room->setPlayerMark(player,"&mobilebenxi-PlayClear",dc->subcardsLength());
				}
			}
		}
        return false;
    }
};

class MobilePingkou : public TriggerSkill
{
public:
    MobilePingkou() : TriggerSkill("mobilepingkou")
    {
        events << EventPhaseChanging << EventPhaseSkipped;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging) {
            if(data.value<PhaseChangeStruct>().to!=Player::NotActive) return false;
            int max = player->getMark("mobilepingkouSkipped-Clear");
            if (max<1||!player->hasSkill(objectName())) return false;
 			foreach (ServerPlayer*t,room->askForPlayersChosen(player,room->getOtherPlayers(player),objectName(),0,max,"@pingkou:"+QString::number(max),true)){
				if(max>0){
					player->peiyin(this);
					max = -1;
				}
				room->damage(DamageStruct(objectName(),player,t));
			}
			if(max<0&&player->isAlive()){
				QList<int>ids = room->getDrawPile();
				qShuffle(ids);
				foreach (int id,ids){
					if(Sanguosha->getCard(id)->isKindOf("EquipCard")){
						room->obtainCard(player,id);
						break;
					}
				}
			}
       } else
            room->addPlayerMark(player, "mobilepingkouSkipped-Clear");
        return false;
    }
};

MobileFurongCard::MobileFurongCard()
{
}

bool MobileFurongCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self;
}

void MobileFurongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *t, targets) {
		QString schoice = room->askForChoice(source,"mobilefurong","zhenya+anfu",QVariant::fromValue(t));
		QString tchoice = room->askForChoice(t,"mobilefurong","fankang+guishun",QVariant::fromValue(source));
		if(schoice=="zhenya"){
			if(tchoice=="fankang"){
				room->damage(DamageStruct("mobilefurong",source,t));
				source->drawCards(1,"mobilefurong");
			}else if(t->getCardCount()>0){
				int id = room->askForCardChosen(source,t,"he","mobilefurong");
				if(id>-1){
					room->obtainCard(source,id,false);
					if(t->isDead()) continue;
					const Card*dc = room->askForExchange(source,"mobilefurong",2,2,true,"mobilefurong0:"+t->objectName());
					if(dc) room->giveCard(source,t,dc,"mobilefurong");
				}
			}
		}else{
			if(tchoice=="fankang"){
				room->damage(DamageStruct("mobilefurong",nullptr,source));
				source->drawCards(1,"mobilefurong");
			}else if(t->getCardCount()>1){
				const Card*dc = room->askForExchange(t,"mobilefurong",2,2,true,"mobilefurong1:"+source->objectName());
				if(dc) room->giveCard(t,source,dc,"mobilefurong");
			}else
				room->setPlayerMark(t,"&mobilefurong",1);
		}
	}
}

class MobileFurongVS : public ZeroCardViewAsSkill
{
public:
    MobileFurongVS() : ZeroCardViewAsSkill("mobilefurong")
    {
    }

    const Card *viewAs() const
    {
        return new MobileFurongCard;
    }
    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("MobileFurongCard")<1;
    }
};

class MobileFurong : public TriggerSkill
{
public:
    MobileFurong() : TriggerSkill("mobilefurong")
    {
        events << EventPhaseChanging;
        view_as_skill = new MobileFurongVS;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->getMark("&mobilefurong")>0;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::Draw){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->setPlayerMark(player,"&mobilefurong",0);
				player->skip(change.to);
			}
		}
        return false;
    }
};

class OlYongsi : public TriggerSkill
{
public:
    OlYongsi() : TriggerSkill("olyongsi") {
        frequency = Compulsory;
        events << DrawNCards << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
			room->sendCompulsoryTriggerLog(player, "olyongsi", true);
            room->broadcastSkillInvoke("olyongsi");

            QSet<QString> kingdoms;
            QList<ServerPlayer *> alives = room->getAlivePlayers();
            foreach(ServerPlayer *p, alives) {
                QString kingdom = p->getKingdom();
                if (!kingdoms.contains(kingdom))
                    kingdoms.insert(kingdom);
            }
			draw.num = kingdoms.count();
            data = QVariant::fromValue(draw);
        }
        else {
            if (player->getPhase() == Player::Discard) {
                room->sendCompulsoryTriggerLog(player, "olyongsi", true);
                if (!room->askForDiscard(player, "olyongsi", 1, 1, true, true, "@olyongsi"))
                    room->loseHp(HpLostStruct(player, 1, "olyongsi", player));
            }
        }
        return false;
    }
};

class OlJixi : public PhaseChangeSkill
{
public:
    OlJixi() : PhaseChangeSkill("oljixi") {
        frequency = Wake;
        waked_skills = "wangzun";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Finish
		&& player->getMark(objectName())<1&&player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getMark("oljixi_turn") == 3) {
            LogMessage msg;
            msg.type = "#oljixi-wake";
            msg.from = player;
            msg.arg = objectName();
            room->sendLog(msg);
        }else if(!player->canWake("oljixi"))
			return false;
        room->setPlayerMark(player, objectName(), 1);

        room->broadcastSkillInvoke("oljixi");
        room->notifySkillInvoked(player, "oljixi");
        room->doSuperLightbox(player, "oljixi");
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
            room->recover(player, RecoverStruct("oljixi", player));
            QStringList choices,lordskills;
            if (Sanguosha->getSkill("wangzun"))
                choices << "wangzun";
            ServerPlayer *lord = room->getLord();
            if (lord) {
                foreach(const Skill *skill, lord->getVisibleSkillList()) {
                    if (skill->isLordSkill() && lord->hasLordSkill(skill, true)) {
                        lordskills.append(skill->objectName());
                    }
                }
            }
            if (!lordskills.isEmpty())
                choices << "lordskill";
            if (choices.isEmpty())
                return false;

            QString choice = room->askForChoice(player, "oljixi", choices.join("+"));
            if (choice == "wangzun")
                room->handleAcquireDetachSkills(player, "wangzun");
            else {
                room->drawCards(player, 2, "oljixi");
                room->handleAcquireDetachSkills(player, lordskills);
            }
        }
        return false;
    }
};

class OlJixiRecord : public TriggerSkill
{
public:
    OlJixiRecord() : TriggerSkill("#oljixi") {
        global = true;
        events << EventPhaseStart << HpLost;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        return TriggerSkill::getPriority(triggerEvent) + 1;
    }

    bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::Finish)
                player->addMark("oljixi_turn");
        }
        else if (triggerEvent == HpLost) {
            player->setMark("oljixi_turn", 0);
        }
        return false;
    }
};








MobileStStandardPackage::MobileStStandardPackage()
    : Package("MobileStStandard")
{



    General *mobile_zhangfei = new General(this, "mobile_zhangfei", "shu", 4);
    mobile_zhangfei->addSkill("tenyearpaoxiao");
    mobile_zhangfei->addSkill(new MobileLiyong);
    mobile_zhangfei->addSkill(new MobileLiyongClear);
    related_skills.insertMulti("mobileliyong", "#mobileliyong-clear");

    General *mobile_xiahoudun = new General(this, "mobile_xiahoudun", "wei", 4);
    mobile_xiahoudun->addSkill("ganglie");
    mobile_xiahoudun->addSkill(new MobileQingjian);

    General *mobile2_yuanshu = new General(this, "mobile2_yuanshu", "qun");
    mobile2_yuanshu->addSkill(new OlYongsi);
    mobile2_yuanshu->addSkill(new OlJixi);
    mobile2_yuanshu->addSkill(new OlJixiRecord);
    related_skills.insertMulti("oljixi", "#oljixi");

    addMetaObject<MobileQingjianCard>();
    addMetaObject<MobileQiangxiCard>();
    addMetaObject<MobileNiepanCard>();
    addMetaObject<MobileZaiqiCard>();
    addMetaObject<MobilePoluCard>();
    addMetaObject<MobileTiaoxinCard>();
    addMetaObject<MobileZhijianCard>();
    addMetaObject<MobileFangquanCard>();
    addMetaObject<MobileGanluCard>();
    addMetaObject<MobileJieyueCard>();
    addMetaObject<MobileAnxuCard>();
    addMetaObject<MobileGongqiCard>();
    addMetaObject<MobileZongxuanCard>();
    addMetaObject<MobileZongxuanPutCard>();
    addMetaObject<MobileJunxingCard>();
    addMetaObject<MobileMiejiCard>();
    addMetaObject<MobileMiejiDiscardCard>();
    addMetaObject<MobileXianzhenCard>();
    addMetaObject<MobileQiaoshuiCard>();
    addMetaObject<MobileZongshihCard>();
    addMetaObject<MobileDingpinCard>();
    addMetaObject<MobileShenxingCard>();
    addMetaObject<MobileBingyiCard>();
    addMetaObject<MobileJianyingCard>();
    addMetaObject<MobileYanzhuCard>();
    addMetaObject<MobileXingxueCard>();
}
ADD_PACKAGE(MobileStStandard)

MobileStWindPackage::MobileStWindPackage()
    : Package("MobileStWind")
{
    General *mobile_zhoutai = new General(this, "mobile_zhoutai", "wu", 4);
    mobile_zhoutai->addSkill("buqu");
    mobile_zhoutai->addSkill(new MobileFenji);



}
ADD_PACKAGE(MobileStWind)

MobileStThicketPackage::MobileStThicketPackage()
    : Package("MobileStThicket")
{
    General *mobile_zhurong = new General(this, "mobile_zhurong", "shu", 4, false);
    mobile_zhurong->addSkill("juxiang");
    mobile_zhurong->addSkill(new MobileLieren);

    General *mobile_menghuo = new General(this, "mobile_menghuo", "shu", 4);
    mobile_menghuo->addSkill("huoshou");
    mobile_menghuo->addSkill(new MobileZaiqi);

    General *mobile_caopi = new General(this, "mobile_caopi$", "wei", 3);
    mobile_caopi->addSkill(new MobileXingshang);
    mobile_caopi->addSkill(new MobileFangzhu);
    mobile_caopi->addSkill("songwei");

    General *mobile_sunjian = new General(this, "mobile_sunjian", "wu", 4);
    mobile_sunjian->addSkill("yinghun");
    mobile_sunjian->addSkill(new MobilePolu);

    General *mobile_dongzhuo = new General(this, "mobile_dongzhuo$", "qun", 8);
    mobile_dongzhuo->addSkill(new MobileJiuchi);
    mobile_dongzhuo->addSkill("roulin");
    mobile_dongzhuo->addSkill("benghuai");
    mobile_dongzhuo->addSkill("baonue");


}
ADD_PACKAGE(MobileStThicket)

MobileStFirePackage::MobileStFirePackage()
    : Package("MobileStFire")
{
    General *mobile_wolong = new General(this, "mobile_wolong", "shu", 3);
    mobile_wolong->addSkill("bazhen");
    mobile_wolong->addSkill("olhuoji");
    mobile_wolong->addSkill("olkanpo");

    General *mobile_pangtong = new General(this, "mobile_pangtong", "shu", 3);
    mobile_pangtong->addSkill("ollianhuan");
    mobile_pangtong->addSkill(new MobileNiepan);

    General *mobile_dianwei = new General(this, "mobile_dianwei", "wei", 4);
    mobile_dianwei->addSkill(new MobileQiangxi);

    General *mobile_xunyu = new General(this, "mobile_xunyu", "wei", 3);
    mobile_xunyu->addSkill("quhu");
    mobile_xunyu->addSkill(new MobileJieming);

    General *mobile_yuanshao = new General(this, "mobile_yuanshao$", "qun", 4);
    mobile_yuanshao->addSkill(new MobileLuanji);
    mobile_yuanshao->addSkill("xueyi");

    General *mobile_yanliangwenchou = new General(this, "mobile_yanliangwenchou", "qun", 4);
    mobile_yanliangwenchou->addSkill(new MobileShuangxiong);

}
ADD_PACKAGE(MobileStFire)

MobileStMountainPackage::MobileStMountainPackage()
    : Package("MobileStMountain")
{
    General *mobile_liushan = new General(this, "mobile_liushan$", "shu", 3);
    mobile_liushan->addSkill("xiangle");
    mobile_liushan->addSkill(new MobileFangquan);
    mobile_liushan->addSkill(new MobileFangquanMax);
    mobile_liushan->addSkill("ruoyu");
    related_skills.insertMulti("mobilefangquan", "#mobilefangquan-max");

    General *mobile_jiangwei = new General(this, "mobile_jiangwei", "shu", 4);
    mobile_jiangwei->addSkill(new MobileTiaoxin);
    mobile_jiangwei->addSkill(new MobileZhiji);
    mobile_jiangwei->addRelateSkill("tenyearguanxing");

    General *mobile_dengai = new General(this, "mobile_dengai", "wei", 4);
    mobile_dengai->addSkill(new MobileTuntian);
    mobile_dengai->addSkill(new MobileTuntianDistance);
    mobile_dengai->addSkill("zaoxian");
    related_skills.insertMulti("mobiletuntian", "#mobiletuntian-dist");

    General *mobile_sunce = new General(this, "mobile_sunce$", "wu", 4);
    mobile_sunce->addSkill("jiang");
    mobile_sunce->addSkill(new MobileHunzi);
    mobile_sunce->addSkill("zhiba");

    General *mobile_erzhang = new General(this, "mobile_erzhang", "wu", 3);
    mobile_erzhang->addSkill(new MobileZhijian);
    mobile_erzhang->addSkill("guzheng");

    General *mobile_caiwenji = new General(this, "mobile_caiwenji", "qun", 3, false);
    mobile_caiwenji->addSkill(new MobileBeige);
    mobile_caiwenji->addSkill("duanchang");

}
ADD_PACKAGE(MobileStMountain)

MobileStYJ2011Package::MobileStYJ2011Package()
    : Package("MobileStYJ2011")
{
    General *mobile_caozhi = new General(this, "mobile_caozhi", "wei", 3);
    mobile_caozhi->addSkill("luoying");
    mobile_caozhi->addSkill(new MobileJiushi);
    mobile_caozhi->addSkill(new MobileChengzhang);

    General *mobile_yujin = new General(this, "mobile_yujin", "wei", 4);
    mobile_yujin->addSkill(new MobileJieyue);

    General *mobile_xusheng = new General(this, "mobile_xusheng", "wu", 4);
    mobile_xusheng->addSkill(new MobilePojun);

    General *mobile_wuguotai = new General(this, "mobile_wuguotai", "wu", 3, false);
    mobile_wuguotai->addSkill(new MobileGanlu);
    mobile_wuguotai->addSkill("buyi");

    General *mobile_lingtong = new General(this, "mobile_lingtong", "wu", 4);
    mobile_lingtong->addSkill(new MobileXuanfeng);

    General *mobile_gaoshun = new General(this, "mobile_gaoshun", "qun", 4);
    mobile_gaoshun->addSkill(new MobileXianzhen);
    mobile_gaoshun->addSkill(new MobileXianzhenClear);
    mobile_gaoshun->addSkill(new MobileXianzhenTargetMod);
    mobile_gaoshun->addSkill(new MobileJinjiu);
    mobile_gaoshun->addSkill(new MobileJinjiuLimit);
    mobile_gaoshun->addSkill(new MobileJinjiuEffect);
    related_skills.insertMulti("mobilexianzhen", "#mobilexianzhen-clear");
    related_skills.insertMulti("mobilexianzhen", "#mobilexianzhen-target");
    related_skills.insertMulti("mobilejinjiu", "#mobilejinjiu-limit");
    related_skills.insertMulti("mobilejinjiu", "#mobilejinjiu");

}
ADD_PACKAGE(MobileStYJ2011)

MobileStYJ2012Package::MobileStYJ2012Package()
    : Package("MobileStYJ2012")
{
    General *mobile_liaohua = new General(this, "mobile_liaohua", "shu", 4);
    mobile_liaohua->addSkill(new MobileDangxian);
    mobile_liaohua->addSkill(new MobileFuli);

    General *mobile_zhonghui = new General(this, "mobile_zhonghui", "wei", 4);
    mobile_zhonghui->addSkill(new MobileQuanji);
    mobile_zhonghui->addSkill(new MobileQuanjiKeep);
    mobile_zhonghui->addSkill("zili");
    mobile_zhonghui->addRelateSkill("paiyi");
    related_skills.insertMulti("mobilequanji", "#mobilequanji");

    General *mobile_bulianshi = new General(this, "mobile_bulianshi", "wu", 3, false);
    mobile_bulianshi->addSkill(new MobileAnxu);
    mobile_bulianshi->addSkill("zhuiyi");

    General *mobile_chengpu = new General(this, "mobile_chengpu", "wu", 4);
    mobile_chengpu->addSkill(new MobileLihuo);
    mobile_chengpu->addSkill("chunlao");

    General *mobile_handang = new General(this, "mobile_handang", "wu", 4);
    mobile_handang->addSkill(new MobileGongqi);
    mobile_handang->addSkill(new MobileGongqiAttack);
    mobile_handang->addSkill("jiefan");
    related_skills.insertMulti("mobilegongqi", "#mobilegongqi-attack");

    General *mobile_gongsunzan = new General(this, "mobile_gongsunzan", "qun", 4);
    mobile_gongsunzan->addSkill(new MobileYicong);
    mobile_gongsunzan->addSkill("qiaomeng");

    General *mobile_liubiao = new General(this, "mobile_liubiao", "qun", 3);
    mobile_liubiao->addSkill("olzishou");
    mobile_liubiao->addSkill(new MobileZongshi);
    mobile_liubiao->addSkill(new MobileZongshiKeep);
    related_skills.insertMulti("mobilezongshi", "#mobilezongshi-keep");


}
ADD_PACKAGE(MobileStYJ2012)

MobileStYJ2013Package::MobileStYJ2013Package()
    : Package("MobileStYJ2013")
{
    General *mobile_jianyong = new General(this, "mobile_jianyong", "shu", 3);
    mobile_jianyong->addSkill(new MobileQiaoshui);
    mobile_jianyong->addSkill(new MobileQiaoshuiTargetMod);
    mobile_jianyong->addSkill(new MobileZongshih);
    related_skills.insertMulti("mobileqiaoshui", "#mobileqiaoshui-target");

    General *mobile_manchong = new General(this, "mobile_manchong", "wei", 3);
    mobile_manchong->addSkill(new MobileJunxing);
    mobile_manchong->addSkill("yuce");

    General *mobile_guohuai = new General(this, "mobile_guohuai", "wei", 4);
    mobile_guohuai->addSkill(new MobileJingce);
    mobile_guohuai->addSkill(new MobileJingceRecord);
    related_skills.insertMulti("mobilejingce", "#mobilejingce-record");

    General *mobile_zhuran = new General(this, "mobile_zhuran", "wu", 4);
    mobile_zhuran->addSkill(new MobileDanshou);

    General *mobile_panzhangmazhong = new General(this, "mobile_panzhangmazhong", "wu", 4);
    mobile_panzhangmazhong->addSkill(new MobileDuodao);
    mobile_panzhangmazhong->addSkill(new MobileAnjian);

    General *mobile_yufan = new General(this, "mobile_yufan", "wu", 3);
    mobile_yufan->addSkill(new MobileZongxuan);
    mobile_yufan->addSkill("zhiyan");

    General *mobile_liru = new General(this, "mobile_liru", "qun", 3);
    mobile_liru->addSkill(new MobileJuece);
    mobile_liru->addSkill(new MobileMieji);
    mobile_liru->addSkill("fencheng");

    General *mobile_fuhuanghou = new General(this, "mobile_fuhuanghou", "qun", 3, false);
    mobile_fuhuanghou->addSkill(new MobileZhuikong);
    mobile_fuhuanghou->addSkill(new MobileZhuikongProhibit);
    mobile_fuhuanghou->addSkill(new MobileQiuyuan);
    related_skills.insertMulti("mobilezhuikong", "#mobilezhuikong");






}
ADD_PACKAGE(MobileStYJ2013)

MobileStYJ2014Package::MobileStYJ2014Package()
    : Package("MobileStYJ2014")
{
    General *mobile_wuyi = new General(this, "mobile_wuyi", "shu", 4);
    mobile_wuyi->addSkill(new MobileBenxi);

    General *mobile_zhoucang = new General(this, "mobile_zhoucang", "shu", 4);
    mobile_zhoucang->addSkill(new MobileZhongyong);
    mobile_zhoucang->addSkill(new MobileZhongyongEffect);
    mobile_zhoucang->addSkill(new MobileZhongyongRemove);
    related_skills.insertMulti("mobilezhongyong", "#mobilezhongyong-effect");
    related_skills.insertMulti("mobilezhongyong", "#mobilezhongyong-remove");

    General *mobile_caozhen = new General(this, "mobile_caozhen", "wei", 4);
    mobile_caozhen->addSkill(new MobileSidi);
    addMetaObject<MobileSidiCard>();

    General *mobile_chenqun = new General(this, "mobile_chenqun", "wei", 3);
    mobile_chenqun->addSkill(new MobileDingpin);
    mobile_chenqun->addSkill("faen");

    General *mobile_guyong = new General(this, "mobile_guyong", "wu", 3);
    mobile_guyong->addSkill(new MobileShenxing);
    mobile_guyong->addSkill(new MobileBingyi);

    General *mobile_zhuhuan = new General(this, "mobile_zhuhuan", "wu", 4);
    mobile_zhuhuan->addSkill("fenli");
    mobile_zhuhuan->addSkill(new MobilePingkou);

    General *mobile_caifuren = new General(this, "mobile_caifuren", "qun", 3, false);
    mobile_caifuren->addSkill(new MobileQieting);
    mobile_caifuren->addSkill("xianzhou");

    General *mobile_jushou = new General(this, "mobile_jushou", "qun", 3);
    mobile_jushou->addSkill(new MobileJianying);
    mobile_jushou->addSkill(new MobileJianyingTargetMod);
    mobile_jushou->addSkill("shibei");
    related_skills.insertMulti("mobilejianying", "#mobilejianying-target");


}
ADD_PACKAGE(MobileStYJ2014)

MobileStYJ2015Package::MobileStYJ2015Package()
    : Package("MobileStYJ2015")
{
    General *mobile_zhangyi = new General(this, "mobile_zhangyi", "shu", 4);
    mobile_zhangyi->addSkill(new MobileFurong);
    mobile_zhangyi->addSkill("shizhi");
    addMetaObject<MobileFurongCard>();

    General *mobile_caoxiu = new General(this, "mobile_caoxiu", "wei", 4);
    mobile_caoxiu->addSkill("qianju");
    mobile_caoxiu->addSkill(new MobileQingxi);

    General *mobile_sunxiu = new General(this, "mobile_sunxiu$", "wu", 3);
    mobile_sunxiu->addSkill(new MobileYanzhu);
    mobile_sunxiu->addSkill(new MobileXingxue);
    mobile_sunxiu->addSkill("zhaofu");

    General *mobile_quancong = new General(this, "mobile_quancong", "wu", 4);
    mobile_quancong->addSkill(new MobileYaoming);
    addMetaObject<MobileYaomingCard>();

    General *mobile_zhuzhi = new General(this, "mobile_zhuzhi", "wu", 4);
    mobile_zhuzhi->addSkill(new MobileAnguo);


}
ADD_PACKAGE(MobileStYJ2015)
