#include "lei.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
//#include "maneuvering.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"

class Wanglie : public TriggerSkill
{
public:
    Wanglie() : TriggerSkill("wanglie")
    {
        events << CardUsed;
        global = true;
    }
    bool triggerable(const ServerPlayer *player) const
    {
        return player&&player->isAlive()&&player->getPhase()==Player::Play;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            room->addPlayerMark(player, "wanglie-PlayClear");
            if (!player->hasSkill(objectName())||!player->askForSkillInvoke(this,data)) return false;
            room->broadcastSkillInvoke(objectName());
            use.no_respond_list << "_ALL_TARGETS";
            data = QVariant::fromValue(use);
            room->setPlayerCardLimitation(player, "use", ".", true);
        }
        return false;
    }
};

class WanglieMod : public TargetModSkill
{
public:
    WanglieMod() : TargetModSkill("#wangliemod")
    {
        pattern = ".";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->getPhase() == Player::Play && from->getMark("wanglie-PlayClear")<1 && from->hasSkill("wanglie"))
            return 999;
        return 0;
    }
};

class Zuilun : public TriggerSkill
{
public:
    Zuilun() : TriggerSkill("zuilun")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        global = true;
    }
    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive();
    }

    int ZuilunGetNum(ServerPlayer *player) const
    {
        int num = 0;
        if (player->getMark("damage_point_round") > 0)
            num++;
        bool minhandnum = true;
        foreach (ServerPlayer *p, player->getRoom()->getAlivePlayers()) {
            if (p->getHandcardNum() < player->getHandcardNum()) {
                minhandnum = false;
                break;
            }
        }
        if (minhandnum)
            num++;
        if (player->getMark("zuilun_discard-Clear") <= 0)
            num++;
        return num;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
            if (!player->hasSkill(objectName())||!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            int num = ZuilunGetNum(player);
			QList<int>ids = room->getNCards(3);
			room->fillAG(ids,player);
			Card*dc = dummyCard();
			for (int i = 0; i < num; i++) {
				int id = room->askForAG(player,ids,false,objectName(),"zuilun0");
				room->takeAG(player,id,false,QList<ServerPlayer *>()<<player);
				dc->addSubcard(id);
				ids.removeOne(id);
			}
			room->clearAG(player);
            room->askForGuanxing(player, ids, Room::GuanxingUpOnly);
			room->obtainCard(player, dc, false);
            if (num<1&&player->isAlive()) {
                ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zuilun-lose");
                room->doAnimate(1, player->objectName(), target->objectName());
                QList<ServerPlayer *> losers;
                losers << player << target;
                room->sortByActionOrder(losers);
                foreach (ServerPlayer *p, losers) {
                    if (p->isDead()) continue;
                    room->loseHp(HpLostStruct(p, 1, "zuilun", player));
                }
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                if (move.from == player && player->hasFlag("CurrentPlayer"))
                    player->addMark("zuilun_discard-Clear");
            }
        }
        return false;
    }
};

class Fuyin : public TriggerSkill
{
public:
    Fuyin() : TriggerSkill("fuyin")
    {
        events << TargetConfirmed;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.to.contains(player)) return false;
        if (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) return false;
        if (player->getMark("fuyin-Clear") > 0) return false;
        room->addPlayerMark(player, "fuyin-Clear");
        if (use.from->isDead()) return false;
        if (player->getHandcardNum() > use.from->getHandcardNum()) return false;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        use.nullified_list << player->objectName();
        data = QVariant::fromValue(use);
        return false;
    }
};

class Liangyin : public TriggerSkill
{
public:
    Liangyin() : TriggerSkill("liangyin")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::PlaceSpecial) {
            if (move.to_pile_name.startsWith("#")) return false;
            bool invoke = false;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceSpecial) continue;
                invoke = true;
                break;
            }

            if (!invoke) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() > player->getHandcardNum())
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@liangyin-draw", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            target->drawCards(1, objectName());
        } else if (move.to && move.to_place == Player::PlaceHand) {
            if (!move.from_places.contains(Player::PlaceSpecial)) return false;
            bool invoke = false;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) != Player::PlaceSpecial) continue;
                if (move.from_pile_names.at(i).startsWith("#")) continue;
                invoke = true;
                break;
            }
            if (!invoke) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() < player->getHandcardNum() && !p->isNude())
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@liangyin-discard", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            if (!target->canDiscard(target, "he")) return false;
            room->askForDiscard(target, objectName(), 1, 1, false, true);
        }
        return false;
    }
};

class Kongsheng : public PhaseChangeSkill
{
public:
    Kongsheng() : PhaseChangeSkill("kongsheng")
    {
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() == Player::Start) {
            if (player->isNude()) return false;
            const Card *card = room->askForExchange(player, objectName(), player->getCards("he").length(), 1, true, "kongsheng-put", true);
            if (!card) return false;
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());
            player->addToPile(objectName(), card);
        } else if (player->getPhase() == Player::Finish) {
            QList<int> pile = player->getPile(objectName());
            if (pile.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            DummyCard *dummy = new DummyCard();
            foreach (int id, pile) {
                if (player->isDead()) break;
                const Card *card = Sanguosha->getCard(id);
                if (card->isKindOf("EquipCard")) {
                    if (!card->isAvailable(player) || player->isProhibited(player, card)) {
                        dummy->addSubcard(id);
                    } else
                        room->useCard(CardUseStruct(card, player, player), true);;
                } else
                    dummy->addSubcard(id);
            }
            if (player->isAlive() && !dummy->getSubcards().isEmpty()) {
                LogMessage log;
                log.type = "$KuangbiGet";
                log.from = player;
                log.arg = "kongsheng";
                log.card_str = ListI2S(dummy->getSubcards()).join("+");
                room->sendLog(log);
                room->obtainCard(player, dummy, true);
            }
            delete dummy;
        }
        return false;
    }
};

class QianjieChain : public TriggerSkill
{
public:
    QianjieChain() : TriggerSkill("#qianjie-chain")
    {
        events << ChainStateChange;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->isChained()) return false;
        room->sendCompulsoryTriggerLog(player, "qianjie", true, true);
        return true;
    }
};

class Qianjie : public ProhibitSkill
{
public:
    Qianjie() : ProhibitSkill("qianjie")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("DelayedTrick")&&to->hasSkill("qianjie");
    }
};

class QianjiePindianPro : public ProhibitPindianSkill
{
public:
    QianjiePindianPro() : ProhibitPindianSkill("#qianjiepindianpro")
    {
    }

    bool isPindianProhibited(const Player *from, const Player *to) const
    {
        return to->hasSkill("qianjie") && from != to;
    }
};

JueyanCard::JueyanCard()
{
    target_fixed = true;
}

void JueyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choices;
    if (source->hasEquipArea(0)) choices << "0";
    if (source->hasEquipArea(1)) choices << "1";
    if (source->hasEquipArea(2) || source->hasEquipArea(3)) choices << "23";
    if (source->hasEquipArea(4)) choices << "4";
    if (choices.isEmpty()) return;
    QString choice = room->askForChoice(source, "jueyan", choices.join("+"), QVariant());
    if (choice == "0") {
        source->throwEquipArea(0);
        room->addSlashCishu(source, 3);
    } else if (choice == "1") {
        source->throwEquipArea(1);
        source->drawCards(3, objectName());
        room->addMaxCards(source, 3);
    } else if (choice == "23") {
        QList<int> list;
        list << 2 << 3;
        source->throwEquipArea(list);
        room->setPlayerFlag(source, "jueyan_distance");
    } else {
        source->throwEquipArea(4);
        if (!source->hasSkill("jizhi")) {
            room->acquireOneTurnSkills(source, "jueyan", "tenyearjizhi");
        }
    }
}

class Jueyan : public ZeroCardViewAsSkill
{
public:
    Jueyan() : ZeroCardViewAsSkill("jueyan")
    {
    }

    const Card *viewAs() const
    {
        return new JueyanCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->hasEquipArea() && !player->hasUsed("JueyanCard");
    }
};

class JueyanTargetMod : public TargetModSkill
{
public:
    JueyanTargetMod() : TargetModSkill("#jueyantargetmod")
    {
        pattern = ".";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("jueyan_distance"))
            return 1000;
        return 0;
    }
};

class Poshi : public PhaseChangeSkill
{
public:
    Poshi() : PhaseChangeSkill("poshi")
    {
        frequency = Wake;
        waked_skills = "huairou";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if(!player->hasEquipArea() || player->getHp() == 1||player->canWake("poshi")){
			room->sendCompulsoryTriggerLog(player, this);
			room->doSuperLightbox(player, "poshi");
			room->setPlayerMark(player, "poshi", 1);
			if (room->changeMaxHpForAwakenSkill(player, -1, objectName())) {
				if (player->getHandcardNum() < player->getMaxHp())
					player->drawCards(player->getMaxHp() - player->getHandcardNum(), objectName());
				room->handleAcquireDetachSkills(player, "-jueyan|huairou");
			}
		}
        return false;
    }
};

HuairouCard::HuairouCard()
{
    target_fixed = true;
    will_throw = false;
    can_recast = true;
    handling_method = Card::MethodRecast;
}

void HuairouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    LogMessage log;
    log.type = "#UseCard_Recast";
    log.from = source;
    log.card_str = QString::number(getSubcards().first());
    room->sendLog(log);
    room->moveCardTo(this, source, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, source->objectName(), getSkillName(), ""));
    source->drawCards(1, "recast");
}

class Huairou : public OneCardViewAsSkill
{
public:
    Huairou() : OneCardViewAsSkill("huairou")
    {
        filter_pattern = "EquipCard";
    }

    const Card *viewAs(const Card *c) const
    {
        HuairouCard *card = new HuairouCard;
        card->addSubcard(c);
        return card;
    }
};

class Zhengu : public TriggerSkill
{
public:
    Zhengu() : TriggerSkill("zhengu")
    {
        events << EventPhaseStart;
    }

     bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
         if (player->getPhase() != Player::Finish) return false;
         ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zhengu-invoke", true, true);
         if (!target) return false;
         room->broadcastSkillInvoke(objectName());
         room->addPlayerMark(player, "zhengu-Clear");
         QStringList list = player->property("zhengu_targets").toStringList();
         if (list.contains(target->objectName())) return false;
         list << target->objectName();
         room->setPlayerProperty(player, "zhengu_targets", list);
         room->addPlayerMark(target, "&zhengu");
         return false;
    }
};

class ZhenguEffect : public TriggerSkill
{
public:
    ZhenguEffect() : TriggerSkill("#zhengueffect")
    {
        events << EventPhaseChanging << Death << EventLoseSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                if (player->getMark("zhengu-Clear") > 0) {
                    foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                        QStringList list = player->property("zhengu_targets").toStringList();
                        if (!list.contains(p->objectName())) continue;
                        int h = player->getHandcardNum();
                        int hh = p->getHandcardNum();
                        if (h == hh) continue;
                        LogMessage log;
                        log.type = "#ZhenguEffect";
                        log.from = p;
                        log.arg = "zhengu";
                        room->sendLog(log);
                        room->broadcastSkillInvoke("zhengu");
                        room->notifySkillInvoked(player, "zhengu");
                        if (h > hh)
                            p->drawCards(qMin(h - hh, 5 - hh), "zhengu");
                        else
                            room->askForDiscard(p, "zhengu", hh - h, hh - h, false, false);
                    }
                }

                if (player->isDead()) return false;
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    QStringList list = p->property("zhengu_targets").toStringList();
                    if (list.contains(player->objectName())) {
                        list.removeOne(player->objectName());
                        room->setPlayerProperty(p, "zhengu_targets", list);
                        room->removePlayerMark(player, "&zhengu");
                        int h = p->getHandcardNum();
                        int hh = player->getHandcardNum();
                        if (h == hh) continue;
                        LogMessage log;
                        log.type = "#ZhenguEffect";
                        log.from = player;
                        log.arg = "zhengu";
                        room->sendLog(log);
                        room->broadcastSkillInvoke("zhengu");
                        room->notifySkillInvoked(p, "zhengu");
                        if (h > hh)
                            player->drawCards(qMin(h - hh, 5 - hh), objectName());
                        else
                            room->askForDiscard(player, "zhengu", hh - h, hh - h, false, false);
                    }
                }
            }
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->objectName() != player->objectName()) return false;
            if (death.who->hasSkill("zhengu")) {
                room->setPlayerProperty(player, "zhengu_targets", QStringList());
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    if (p->getMark("&zhengu") > 0)
                         room->removePlayerMark(p, "&zhengu");
                }
            }
            if (player->getMark("&zhengu") > 0)
                 room->removePlayerMark(player, "&zhengu");
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                QStringList list = p->property("zhengu_targets").toStringList();
                if (list.contains(player->objectName())) {
                    list.removeOne(player->objectName());
                    room->setPlayerProperty(p, "zhengu_targets", list);
                }
            }
        } else if (triggerEvent == EventLoseSkill) {
            if (data.toString() != "zhengu") return false;
            if (!player->isAlive()) return false;
            room->setPlayerProperty(player, "zhengu_targets", QStringList());
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("&zhengu") > 0)
                     room->removePlayerMark(p, "&zhengu");
            }
        }
        return false;
    }
};

class Zhengrong : public TriggerSkill
{
public:
    Zhengrong() : TriggerSkill("zhengrong")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isDead() || damage.to == player || damage.to->getHandcardNum() <= player->getHandcardNum() || damage.to->isNude()) return false;
        QVariant newdata = QVariant::fromValue(damage.to);
        if (!player->askForSkillInvoke(objectName(), newdata)) return false;
        room->broadcastSkillInvoke(objectName());
        int card_id = room->askForCardChosen(player, damage.to, "he", "zhengrong");
        player->addToPile("rong", card_id);
        return false;
    }
};

HongjuCard::HongjuCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void HongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "hongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class HongjuVS : public ViewAsSkill
{
public:
    HongjuVS() : ViewAsSkill("hongju")
    {
        response_pattern = "@@hongju";
        expand_pile = "rong";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() < 2 * Self->getPile("rong").length())
            return !to_select->isEquipped();

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hand = 0;
        int pile = 0;
        foreach (const Card *card, cards) {
            if (Self->getHandcards().contains(card))
                hand++;
            else if (Self->getPile("rong").contains(card->getEffectiveId()))
                pile++;
        }

        if (hand == pile) {
            HongjuCard *c = new HongjuCard;
            c->addSubcards(cards);
            return c;
        }

        return nullptr;
    }
};

class Hongju : public PhaseChangeSkill
{
public:
    Hongju() : PhaseChangeSkill("hongju")
    {
        frequency = Wake;
        view_as_skill = new HongjuVS;
        waked_skills = "qingce";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPile("rong").length() >= 3&&player->aliveCount() < player->getSiblings(true).length()) {
        }else if(!player->canWake("hongju"))
			return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "hongju");
        room->setPlayerMark(player, "hongju", 1);
        if (!player->isKongcheng())
            room->askForUseCard(player, "@@hongju", "@hongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->handleAcquireDetachSkills(player, "qingce");
        return false;
    }
};

QingceCard::QingceCard()
{
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool QingceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getCardCount(true, true) > to_select->getHandcardNum();
}

void QingceCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "qingce", "");
    Room *room = effect.to->getRoom();
    room->throwCard(this, reason, nullptr);
    if (effect.to->getCards("ej").isEmpty()) return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "ej", "qingce", false, Card::MethodDiscard);
    room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

class Qingce : public OneCardViewAsSkill
{
public:
    Qingce() : OneCardViewAsSkill("qingce")
    {
        filter_pattern = ".|.|.|rong";
        expand_pile = "rong";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->getPile("rong").isEmpty();
    }

    const Card *viewAs(const Card *originalCard) const
    {
        QingceCard *card = new QingceCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Leiyongsi : public TriggerSkill
{
public:
    Leiyongsi() : TriggerSkill("leiyongsi")
    {
        events << DrawNCards << EventPhaseEnd;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
            if (draw.reason != "draw_phase") return false;
			QStringList kingdoms;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (kingdoms.contains(p->getKingdom())) continue;
                kingdoms << p->getKingdom();
            }
            LogMessage log;
            log.type = "#LeiyongsiDrawNum";
            log.from = player;
            log.arg = "leiyongsi";
            log.arg2 = QString::number(kingdoms.length());
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());
			draw.num = kingdoms.length();
            data = QVariant::fromValue(draw);
        } else {
            if (player->getPhase() != Player::Play) return false;
            int point = player->getMark("damage_point_round");
            LogMessage log;
            log.from = player;
            log.arg = QString::number(point);
            if (point == 0) {
                int draw = player->getHp() - player->getHandcardNum();
                if (draw < 0) draw = 0;
                log.type = "#LeiyongsiDraw";
                log.arg2 = QString::number(draw);
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(player, objectName());
                if (draw > 0) player->drawCards(draw, objectName());
            } else if (point > 1) {
                log.type = "#LeiyongsiMax";
                log.arg2 = QString::number(player->getLostHp());
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(player, objectName());
                room->addPlayerMark(player, "leiyongsi-Clear");
            }
        }
        return false;
    }
};

class LeiyongsiMaxCards : public MaxCardsSkill
{
public:
    LeiyongsiMaxCards() : MaxCardsSkill("#leiyongsimaxmards")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->getMark("leiyongsi-Clear")>0&&target->hasSkill("leiyongsi"))
            return target->getLostHp();
        return -1;
    }
};

class Leiweidi : public TriggerSkill
{
public:
    Leiweidi() : TriggerSkill("leiweidi$")
    {
        events << CardsMoveOneTime << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasLordSkill(this)) return false;
        if (player->getPhase() != Player::Discard) return false;
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                QVariantList discard_list = player->tag["leiweidi_ids"].toList();
                foreach (int id, move.card_ids) {
                    if (discard_list.contains(QVariant(id))) continue;
                    discard_list << id;
                }
                player->tag["leiweidi_ids"] = discard_list;
            }
        } else {
            if (player->getPhase() != Player::Discard) return false;
            QVariantList discard_list = player->tag["leiweidi_ids"].toList();
            player->tag.remove("leiweidi_ids");
            QList<ServerPlayer *> quns;
            foreach(ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getKingdom() == "qun")
                    quns << p;
            }
            if (quns.isEmpty()) return false;
            if (player->isDead()) return false;

            QList<int> dis_list;
            foreach (QVariant card_data, discard_list) {
                int card_id = card_data.toInt();
                if (room->getCardPlace(card_id) == Player::DiscardPile)
                    dis_list << card_id;
            }
            if (dis_list.isEmpty()) return false;

            QList<ServerPlayer *> _player;
            _player << player;
            CardsMoveStruct move(dis_list, nullptr, player, Player::DiscardPile, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_RECYCLE, player->objectName(), objectName(), ""));
            QList<CardsMoveStruct> moves;
            moves.append(move);
            room->notifyMoveCards(true, moves, true, _player);
            room->notifyMoveCards(false, moves, true, _player);

            QList<int> origin_list = dis_list;
            bool flag = true;
            int num = 0;
            while (!dis_list.isEmpty()) {
                num++;
                if (num != 1)
                    flag = false;
                ServerPlayer *give = room->askForYiji(player, dis_list, objectName(), false, true, true, 1, quns,
                                                       CardMoveReason(), "leiweidi-give", flag);
                if (!give) break;
                CardsMoveStruct move(QList<int>(), player, nullptr, Player::PlaceHand, Player::DiscardPile,
                    CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), nullptr, objectName(), ""));
                foreach (int id, origin_list) {
                    if (room->getCardPlace(id) != Player::DiscardPile) {
                        move.card_ids << id;
                        dis_list.removeOne(id);
                    }
                }
                origin_list = dis_list;
                QList<CardsMoveStruct> moves;
                moves.append(move);
                room->notifyMoveCards(true, moves, false, _player);
                room->notifyMoveCards(false, moves, false, _player);
                if (!player->isAlive()) return false;
                if (give && give->isAlive())
                    quns.removeOne(give);
                if (quns.isEmpty()) break;
            }

            if (!dis_list.isEmpty()) {
                CardsMoveStruct move(dis_list, player, nullptr, Player::PlaceHand, Player::DiscardPile,
                                     CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), nullptr, objectName(), ""));
                QList<CardsMoveStruct> moves;
                moves.append(move);
                room->notifyMoveCards(true, moves, false, _player);
                room->notifyMoveCards(false, moves, false, _player);
            }
        }
        return false;
    }
};

class Congjian : public TriggerSkill
{
public:
    Congjian() : TriggerSkill("congjian")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("TrickCard") || use.to.length() <= 1) return false;
        if (!use.to.contains(player) || player->isNude()) return false;
        QList<ServerPlayer *> targets = use.to;
        targets.removeOne(player);
        if (targets.isEmpty()) return false;
        QList<int> cards = player->handCards() + player->getEquipsId();
        QList<int> give_ids = room->askForyiji(player, cards, objectName(), false, true, true, 1, targets, CardMoveReason(), "@congjian-give", true);
        if (give_ids.isEmpty()) return false;
        int num = 1;
        if (Sanguosha->getCard(give_ids.first())->isKindOf("EquipCard"))
            num++;
        player->drawCards(num, objectName());
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->getGeneralName().contains("sp_tongyuan") || player->getGeneral2Name().contains("sp_tongyuan"))
            index = 3;
        return index;
    }
};

XiongluanCard::XiongluanCard()
{
}

bool XiongluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void XiongluanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->removePlayerMark(effect.from, "@xiongluanMark");
    room->doSuperLightbox(effect.from, "xiongluan");
    effect.from->throwJudgeArea();
    effect.from->throwEquipArea();

    room->addPlayerMark(effect.from, "xiongluan_from-Clear");
    room->addPlayerMark(effect.to, "xiongluan_to-Clear");
}

class Xiongluan : public ZeroCardViewAsSkill
{
public:
    Xiongluan() : ZeroCardViewAsSkill("xiongluan")
    {
        frequency = Limited;
        limit_mark = "@xiongluanMark";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@xiongluanMark") >= 1 && (player->hasJudgeArea() || player->hasEquipArea());
    }

    const Card *viewAs() const
    {
        return new XiongluanCard;
    }
};

class XiongluanTargetMod : public TargetModSkill
{
public:
    XiongluanTargetMod() : TargetModSkill("#xiongluan-target")
    {
        frequency = Limited;
        pattern = "^SkillCard";
    }

    int getResidueNum(const Player *from, const Card *, const Player *to) const
    {
        if (from->getMark("xiongluan_from-Clear") > 0 && to && to->getMark("xiongluan_to-Clear") > 0)
            return 1000;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *to) const
    {
        if (from->getMark("xiongluan_from-Clear") > 0 && to && to->getMark("xiongluan_to-Clear") > 0)
            return 1000;
        return 0;
    }
};

class XiongluanLimit : public CardLimitSkill
{
public:
    XiongluanLimit() : CardLimitSkill("#xiongluan-limit")
    {
        frequency = Limited;
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->getMark("xiongluan_to-Clear") > 0)
            return ".|.|.|hand";
        return "";
    }
};

class OLZhengrong : public TriggerSkill
{
public:
    OLZhengrong() : TriggerSkill("olzhengrong")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") && !(use.card->isKindOf("TrickCard") && use.card->isDamageCard())) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, use.to) {
            if (p->getHandcardNum() >= player->getHandcardNum() && !p->isNude())
                targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@olzhengrong-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        int card_id = room->askForCardChosen(player, target, "he", "olzhengrong");
        player->addToPile("rong", card_id);
        return false;
    }
};

OLHongjuCard::OLHongjuCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void OLHongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "olhongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class OLHongjuVS : public ViewAsSkill
{
public:
    OLHongjuVS() : ViewAsSkill("olhongju")
    {
        response_pattern = "@@olhongju";
        expand_pile = "rong";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() < 2 * Self->getPile("rong").length())
            return !to_select->isEquipped();

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hand = 0;
        int pile = 0;
        foreach (const Card *card, cards) {
            if (Self->getHandcards().contains(card))
                hand++;
            else if (Self->getPile("rong").contains(card->getEffectiveId()))
                pile++;
        }

        if (hand == pile) {
            OLHongjuCard *c = new OLHongjuCard;
            c->addSubcards(cards);
            return c;
        }

        return nullptr;
    }
};

class OLHongju : public PhaseChangeSkill
{
public:
    OLHongju() : PhaseChangeSkill("olhongju")
    {
        frequency = Wake;
        view_as_skill = new OLHongjuVS;
        waked_skills = "olqingce";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if(player->getPile("rong").length() < 3&&!player->canWake("olhongju"))
			return false;
		room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "olhongju");
        room->setPlayerMark(player, "olhongju", 1);
        if (!player->isKongcheng())
            room->askForUseCard(player, "@@olhongju", "@olhongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->handleAcquireDetachSkills(player, "olqingce");
        return false;
    }
};

OLQingceCard::OLQingceCard()
{
    will_throw = false;
}

bool OLQingceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getCardCount(true, true) > to_select->getHandcardNum();
}

void OLQingceCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    //把顺序调整成先获得“荣”，再弃牌
    QList<int> list;
    foreach (int id, this->getSubcards()) {
        if (effect.from->getPile("rong").contains(id))
            list << id;
    }
    foreach (int id, this->getSubcards()) {
        if (!list.contains(id))
            list << id;
    }

    foreach (int id, list) {
        if (effect.from->getPile("rong").contains(id)) {
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = effect.from;
            log.arg = "rong";
            log.card_str = Sanguosha->getCard(id)->toString();
            room->sendLog(log);
            room->obtainCard(effect.from, id, true);
        }
        else {
            CardMoveReason reason(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "olqingce", "");
            room->throwCard(Sanguosha->getCard(id), reason, effect.from, nullptr);
        }
    }

    if (effect.to->getCards("ej").isEmpty()) return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "ej", "olqingce", false, Card::MethodDiscard);
    room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

class OLQingce : public ViewAsSkill
{
public:
    OLQingce() : ViewAsSkill("olqingce")
    {
        expand_pile = "rong";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->getPile("rong").isEmpty();
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() >= 2 || to_select->isEquipped()) return false;
        if (selected.isEmpty()) return true;
        if (Self->getPile("rong").contains(selected.first()->getEffectiveId()))
            return Self->getHandcards().contains(to_select);
        else
            return Self->getPile("rong").contains(to_select->getEffectiveId());
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hand = 0;
        int pile = 0;
        foreach (const Card *card, cards) {
            if (Self->getHandcards().contains(card))
                hand++;
            else if (Self->getPile("rong").contains(card->getEffectiveId()))
                pile++;
        }

        if (hand == pile && hand == 1) {
            OLQingceCard *card = new OLQingceCard;
            card->addSubcards(cards);
            return card;
        }
        return nullptr;
    }
};

class MobileZhengrong : public TriggerSkill
{
public:
    MobileZhengrong() : TriggerSkill("mobilezhengrong")
    {
        events << CardUsed;
        frequency = Compulsory;
        global = true;
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player&&player->isAlive()&&player->getPhase()==Player::Play;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            player->addMark("mobilezhengrong_usedtimes-PlayClear");
            if (player->getMark("mobilezhengrong_usedtimes-PlayClear") % 2 != 0) return false;
            if (!player->hasSkill(objectName())) return false;

            bool has_other = false;
            foreach (ServerPlayer *p, use.to) {
                if (p != player) {
                    has_other = true;
                    break;
                }
            }
            if (!has_other) return false;

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->isNude())
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@mobilezhengrong-invoke", false, true);
            room->broadcastSkillInvoke(objectName());
            const Card * card = target->getCards("he").at(qrand() % target->getCards("he").length());
            player->addToPile("rong", card);
        }
        return false;
    }
};

MobileHongjuCard::MobileHongjuCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void MobileHongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "mobilehongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class MobileHongjuVS : public ViewAsSkill
{
public:
    MobileHongjuVS() : ViewAsSkill("mobilehongju")
    {
        response_pattern = "@@mobilehongju";
        expand_pile = "rong";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() < 2 * Self->getPile("rong").length())
            return !to_select->isEquipped();

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hand = 0;
        int pile = 0;
        foreach (const Card *card, cards) {
            if (Self->getHandcards().contains(card))
                hand++;
            else if (Self->getPile("rong").contains(card->getEffectiveId()))
                pile++;
        }

        if (hand == pile) {
            MobileHongjuCard *c = new MobileHongjuCard;
            c->addSubcards(cards);
            return c;
        }

        return nullptr;
    }
};

class MobileHongju : public PhaseChangeSkill
{
public:
    MobileHongju() : PhaseChangeSkill("mobilehongju")
    {
        frequency = Wake;
        view_as_skill = new MobileHongjuVS;
        waked_skills = "qingce";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPile("rong").length() >= 3&&player->aliveCount() < player->getSiblings(true).length()) {
        }else if(!player->canWake("mobilehongju"))
			return false;
		room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "mobilehongju");
        room->setPlayerMark(player, "mobilehongju", 1);
        player->drawCards(player->getPile("rong").length(), objectName());
        if (!player->isKongcheng())
            room->askForUseCard(player, "@@mobilehongju", "@mobilehongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->handleAcquireDetachSkills(player, "qingce");
        return false;
    }
};

class Duorui : public TriggerSkill
{
public:
    Duorui() : TriggerSkill("duorui")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play) return false;
        QStringList get_skills = player->property("duorui_skills").toStringList();
        if (!get_skills.isEmpty()) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isDead() || player == damage.to || !player->hasEquipArea()) return false;
        if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.to))) return false;
        room->broadcastSkillInvoke(objectName());

        QStringList choices;
        for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
            if (player->hasEquipArea(i))
                choices << QString::number(i);
        }
        if (choices.isEmpty()) return false;
        QString choice = room->askForChoice(player, "duorui_area", choices.join("+"));
        if (player->hasEquipArea(choice.toInt()))
            player->throwEquipArea(choice.toInt());

        QStringList list;
        QString name = damage.to->getGeneralName();
        const General *g = Sanguosha->getGeneral(name);
        if (g) {
            QList<const Skill *> skills = g->getSkillList();
            foreach (const Skill *skill, skills) {
                if (!skill->isVisible()) continue;
                //if (skill->getFrequency() == Skill::Limited) continue;
                if (skill->isLimitedSkill()) continue;
                if (skill->getFrequency() == Skill::Wake) continue;
                if (skill->isLordSkill()) continue;
                if (!list.contains(skill->objectName()))
                    list << skill->objectName();
            }
        }

        if (damage.to->getGeneral2()) {
            QString name2 = damage.to->getGeneral2Name();
            const General *g2 = Sanguosha->getGeneral(name2);
            if (g2) {
                QList<const Skill *> skills2 = g2->getSkillList();
                foreach (const Skill *skill, skills2) {
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

        QString skill = room->askForChoice(player, objectName(), list.join("+"), data);
        LogMessage log;
        log.type = "#DuoruiInvalidity";
        log.from = player;
        log.to << damage.to;
        log.arg = skill;
        room->sendLog(log);

        QStringList sks = damage.to->property("duorui_invalidity_skills").toString().split("+");
        if (!sks.contains(skill)) {
            sks << skill;
            room->setPlayerProperty(damage.to, "duorui_invalidity_skills", sks.join("+"));
            if (damage.to->getMark("&duorui+:+" + skill) <= 0)
                room->addPlayerMark(damage.to, "&duorui+:+" + skill);
            foreach(ServerPlayer *p, room->getAllPlayers())
                room->filterCards(p, p->getCards("he"), true);

            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        }

        QStringList geters = damage.to->tag["duorui_geters"].toStringList();
        if (!geters.contains(player->objectName())) {
            geters << player->objectName();
            damage.to->tag["duorui_geters"] = geters;
        }
        if (get_skills.contains(skill)) return false;
        if (!player->hasSkill(skill, true)) {
            get_skills << skill;
            room->setPlayerProperty(player, "duorui_skills", skill);
            room->acquireSkill(player, skill);
        }
        return false;
    }
};

class DuoruiClear : public TriggerSkill
{
public:
    DuoruiClear() : TriggerSkill("#duorui-clear")
    {
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QStringList sks = player->property("duorui_invalidity_skills").toString().split("+");
        if (sks.isEmpty()) return false;
        if (event == EventPhaseChanging) {
            if (player->isDead()) return false;
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        } else {
            if (data.value<DeathStruct>().who != player) return false;
        }

        room->setPlayerProperty(player, "duorui_invalidity_skills", QString());
        foreach (QString sk, sks) {
            if (player->getMark("&duorui+:+" + sk) > 0)
                room->setPlayerMark(player, "&duorui+:+" + sk, 0);
        }

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), false);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

        QStringList geters = player->tag["duorui_geters"].toStringList();
        foreach (QString name, geters) {
            ServerPlayer *p = room->findChild<ServerPlayer *>(name);
            if (!p || p->isDead()) continue;
            QStringList get_skills = p->property("duorui_skills").toStringList();
            if (get_skills.isEmpty()) continue;

            QStringList lose;
            foreach (QString sk, get_skills) {
                if (!p->hasSkill(sk, true)) continue;
                lose << "-" + sk;
            }
            room->setPlayerProperty(p, "duorui_skills", QStringList());
            if (lose.isEmpty()) continue;
            room->handleAcquireDetachSkills(p, lose);
        }
        return false;
    }
};

class DuoruiInvalidity : public InvaliditySkill
{
public:
    DuoruiInvalidity() : InvaliditySkill("#duorui-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return !player->property("duorui_invalidity_skills").toString().split("+").contains(skill->objectName())
		&&!player->property("olduorui_invalidity_skills").toString().split("+").contains(skill->objectName());
    }
};

class Zhiti : public MaxCardsSkill
{
public:
    Zhiti() : MaxCardsSkill("zhiti")
    {
    }

    int getExtra(const Player *target) const
    {
        int extra = 0;
        if (target->isWounded()){
			foreach(const Player *p, target->parent()->findChildren<const Player *>()) {
				if (p->isAlive() && p->hasSkill(objectName()) && p->inMyAttackRange(target))
					extra--;
			}
		}
        return extra;
    }
};

class ZhitiEffect : public TriggerSkill
{
public:
    ZhitiEffect() : TriggerSkill("#zhiti-effect")
    {
        events << Pindian << Damage << Damaged;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *szy = nullptr;
        if (event == Pindian) {
            PindianStruct *pd = data.value<PindianStruct *>();
            if (pd->from->hasSkill("zhiti") && pd->success && pd->from->inMyAttackRange(pd->to))
                szy = pd->from;
            if (pd->to->hasSkill("zhiti") && pd->to_number > pd->from_number && pd->to->inMyAttackRange(pd->from))
                szy = pd->to;
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (event == Damage) {
                if (!player->hasSkill("zhiti") || damage.to->isDead() || !player->inMyAttackRange(damage.to)) return false;
                if (!damage.card || !damage.card->isKindOf("Duel")) return false;
                szy = player;
            } else {
                if (!player->hasSkill("zhiti") || !damage.from || damage.from->isDead() || !damage.from->isWounded()
                        || !player->inMyAttackRange(damage.from)) return false;
                szy = player;
            }
        }
        if (szy == nullptr) return false;

        QStringList areas;
        for (int i = 0; i < 5; i++) {
            if (szy->hasEquipArea(i)) continue;
            areas << QString::number(i);
        }
        if (areas.isEmpty()) return false;
        room->sendCompulsoryTriggerLog(szy, "zhiti", true, true);
        QString area = room->askForChoice(szy, "zhiti", areas.join("+"));
        if (szy->hasEquipArea(area.toInt())) return false;
        szy->obtainEquipArea(area.toInt());
        return false;
    }
};

PoxiCard::PoxiCard()
{
}

bool PoxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void PoxiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    QList<int> hand = effect.to->handCards();
    if (!hand.isEmpty()) {
        LogMessage log;
        log.type = "$ViewAllCards";
        log.from = effect.from;
        log.to << effect.to;
        log.card_str = ListI2S(hand).join("+");
        room->sendLog(log, effect.from);
        room->notifyMoveToPile(effect.from, hand, "poxi", Player::PlaceHand, true);
    }

    const Card *c = room->askForUseCard(effect.from, "@@poxi", "@poxi:" + effect.to->objectName());
    if (!hand.isEmpty()) room->notifyMoveToPile(effect.from, hand, "poxi", Player::PlaceHand, false);

    if (!c) return;

    QList<int> from_ids, to_ids;
    foreach (int id, c->getSubcards()) {
        if (hand.contains(id)) to_ids << id;
        else from_ids << id;
    }

    QList<CardsMoveStruct> moves;
    if (!from_ids.isEmpty()) {
        CardMoveReason reason1(CardMoveReason::S_REASON_THROW, effect.from->objectName(), nullptr, "poxi", "");
        moves << CardsMoveStruct(from_ids, effect.from, nullptr, Player::PlaceHand, Player::DiscardPile, reason1);
        LogMessage log;
        log.type = "$DiscardCard";
        log.from = effect.from;
        log.card_str = ListI2S(from_ids).join("+");
        room->sendLog(log);
    }
    if (!to_ids.isEmpty()) {
        CardMoveReason reason2(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), effect.to->objectName(), "poxi", "");
        moves << CardsMoveStruct(to_ids, effect.to, nullptr, Player::PlaceHand, Player::DiscardPile, reason2);
        LogMessage log;
        log.type = "$DiscardCardByOther";
        log.from = effect.from;
        log.to << effect.to;
        log.card_str = ListI2S(to_ids).join("+");
        room->sendLog(log);
    }

    if (!moves.isEmpty()) {
        room->moveCardsAtomic(moves, true);
        switch (from_ids.length()) {
        case 0:
            if (effect.from->getMaxHp() > 0)
                room->loseMaxHp(effect.from, 1, "poxi");
            break;
        case 1:
            room->addMaxCards(effect.from, -1);
            effect.from->endPlayPhase();
            break;
        case 3:
            room->recover(effect.from, RecoverStruct("poxi", effect.from));
            break;
        case 4:
            effect.from->drawCards(4, "poxi");
            break;
        default:
            break;
        }
    }
}

PoxiDisCard::PoxiDisCard()
{
    mute = true;
    handling_method = Card::MethodDiscard;
    will_throw = false;
    target_fixed = true;
    m_skillName = "poxi";
}

void PoxiDisCard::onUse(Room *, CardUseStruct &) const
{
}

class Poxi : public ViewAsSkill
{
public:
    Poxi() : ViewAsSkill("poxi")
    {
        expand_pile = "#poxi";
        response_pattern = "@@poxi";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@poxi") {
            if (to_select->isEquipped() || selected.length() > 3 || Self->isJilei(to_select)) return false;
            foreach (const Card *c, selected) {
                if (c->getSuit() == to_select->getSuit())
                    return false;
            }
            return Self->getPile("#poxi").contains(to_select->getEffectiveId()) || Self->hasCard(to_select);
        };
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@poxi") {
            if (cards.length() != 4)
                return nullptr;
            PoxiDisCard *c = new PoxiDisCard;
            c->addSubcards(cards);
            return c;
        }
        return new PoxiCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("PoxiCard");
    }
};

class Jieyingg : public PhaseChangeSkill
{
public:
    Jieyingg() : PhaseChangeSkill("jieyingg")
    {
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() == Player::RoundStart) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getMark("&jygying") > 0)
                    return false;
            }
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->gainMark("&jygying");
        } else if (player->getPhase() == Player::Finish) {
            if (player->getMark("&jygying") <= 0) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@jieyingg-mark", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            player->loseAllMarks("&jygying");
            target->gainMark("&jygying");
        }
        return false;
    }
};

class jieyinggEffect : public TriggerSkill
{
public:
    jieyinggEffect() : TriggerSkill("#jieyingg-effect")
    {
        events << DrawNCards << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive() && target->getMark("&jygying") > 0;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
            int length = 0;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill("jieyingg")) continue;
                room->broadcastSkillInvoke("jieyingg");
                room->notifySkillInvoked(p, "jieyingg");
                length++;
            }
            if (length <= 0) return false;
            LogMessage log;
            log.type = "#HuaijuDraw";
            log.from = player;
            log.arg = "jieyingg";
            log.arg2 = QString::number(length);
            room->sendLog(log);
			draw.num += length;
            data = QVariant::fromValue(draw);
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->getMark("&jygying") <= 0) return false;
                if (p->isDead() || !p->hasSkill("jieyingg")) continue;
                room->sendCompulsoryTriggerLog(p, "jieyingg", true, true);
                player->loseAllMarks("&jygying");
                p->gainMark("&jygying");
                if (p->isDead() || player->isKongcheng()) continue;
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                DummyCard *handcards = player->wholeHandCards();
                room->obtainCard(p, handcards, reason, false);
            }
        }
        return false;
    }
};

class JieyinggKeep : public MaxCardsSkill
{
public:
    JieyinggKeep() : MaxCardsSkill("#jieyingg-keep")
    {
        frequency = NotFrequent;
    }

    int getExtra(const Player *target) const
    {
		int n = 0;
        if (target->getMark("&jygying") > 0) {
            foreach (const Player *p, target->parent()->findChildren<const Player *>()) {
                if (p->isAlive() && p->hasSkill("jieyingg"))
                    n++;
            }
        }
		return n;
    }
};

class JieyinggTargetMod : public TargetModSkill
{
public:
    JieyinggTargetMod() : TargetModSkill("#jieyingg-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->getMark("&jygying") > 0) {
            int n = 0;
            foreach (const Player *p, from->parent()->findChildren<const Player *>()) {
                if (p->isAlive() && p->hasSkill("jieyingg"))
                    n++;
            }
            return n;
        }
		return 0;
    }
};

class OLDuorui : public TriggerSkill
{
public:
    OLDuorui() : TriggerSkill("olduorui")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isDead()) return false;
        QStringList dr = damage.to->property("olduorui_invalidity_skills").toString().split("+");
        if (!dr.isEmpty()) return false;

        QStringList list;
        const General *g = Sanguosha->getGeneral(damage.to->getGeneralName());
        if (g) {
            foreach (const Skill *skill, g->getSkillList()) {
                if (!skill->isVisible()) continue;
                if (!list.contains(skill->objectName()))
                    list << skill->objectName();
            }
        }

        if (damage.to->getGeneral2()) {
            g = Sanguosha->getGeneral(damage.to->getGeneral2Name());
            if (g) {
                foreach (const Skill *skill, g->getSkillList()) {
                    if (!skill->isVisible()) continue;
                    if (!list.contains(skill->objectName()))
                        list << skill->objectName();
                }
            }
        }
        if (list.isEmpty()) return false;

        if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.to))) return false;
        room->broadcastSkillInvoke(objectName());
        QString skill = room->askForChoice(player, objectName(), list.join("+"), data);
        dr << skill;
        room->setPlayerProperty(damage.to, "olduorui_invalidity_skills", dr.join("+"));

        LogMessage log;
        log.type = "#DuoruiInvalidity";
        log.from = player;
        log.to << damage.to;
        log.arg = skill;
        room->sendLog(log);

        if (damage.to->getMark("&olduorui+:+" + skill) <= 0)
            room->addPlayerMark(damage.to, "&olduorui+:+" + skill);

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

        player->endPlayPhase();
        return false;
    }
};

class OLDuoruiClear : public TriggerSkill
{
public:
    OLDuoruiClear() : TriggerSkill("#olduorui-clear")
    {
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QStringList sks = player->property("olduorui_invalidity_skills").toString().split("+");
        if (sks.isEmpty()) return false;
        if (event == EventPhaseChanging) {
            if (player->isDead()) return false;
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        } else {
            if (data.value<DeathStruct>().who != player) return false;
        }

        room->setPlayerProperty(player, "olduorui_invalidity_skills", QString());
        foreach (QString sk, sks) {
            if (player->getMark("&olduorui+:+" + sk) > 0)
                room->setPlayerMark(player, "&olduorui+:+" + sk, 0);
        }

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), false);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

        return false;
    }
};

class OLZhiti : public MaxCardsSkill
{
public:
    OLZhiti() : MaxCardsSkill("olzhiti")
    {
    }

    int getExtra(const Player *target) const
    {
        int wounded = 0, extra = 0;
        foreach(const Player *p, target->parent()->findChildren<const Player *>()) {
			if(p->isAlive()){
				if (p->hasSkill(objectName())&&p->inMyAttackRange(target)&&target->isWounded())
					extra--;
				if (target->hasSkill(objectName())&&p->isWounded())
					wounded++;
			}
        }
        return extra + qMin(1, wounded);
    }
};

class OLZhitiEffect : public TriggerSkill
{
public:
    OLZhitiEffect() : TriggerSkill("#olzhiti-effect")
    {
        events << DrawNCards << EventPhaseChanging;
        frequency = Compulsory;
    }

    int wounded(Room *room) const
    {
        int num = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isWounded())
                num++;
        }
        return num;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||wounded(room) < 3) return false;
            room->sendCompulsoryTriggerLog(player, "olzhiti", true, true);
			draw.num++;
            data = QVariant::fromValue(draw);
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            if (wounded(room) < 5) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!p->hasEquipArea()) continue;
                targets << p;
            }
            ServerPlayer *target = room->askForPlayerChosen(player, targets, "olzhiti", "@olzhiti-throw", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke("olzhiti");
            QList<int> areas;
            for (int i = 0; i < 5; i++) {
                if (!target->hasEquipArea(i)) continue;
                areas << i;
            }
            if (areas.isEmpty()) return false;
            int area = areas.at(qrand() % areas.length());
            target->throwEquipArea(area);
        }
        return false;
    }
};

LeiPackage::LeiPackage()
    : Package("Lei")
{
    General *chendao = new General(this, "chendao", "shu", 4);
    chendao->addSkill(new Wanglie);
    chendao->addSkill(new WanglieMod);
    related_skills.insertMulti("wanglie", "#wangliemod");

    General *zhugezhan = new General(this, "zhugezhan", "shu", 3);
    zhugezhan->addSkill(new Zuilun);
    zhugezhan->addSkill(new Fuyin);

    General *zhoufei = new General(this, "zhoufei", "wu", 3, false);
    zhoufei->addSkill(new Liangyin);
    zhoufei->addSkill(new Kongsheng);

    General *lei_lukang = new General(this, "lei_lukang", "wu", 4);
    lei_lukang->addSkill(new Qianjie);
    lei_lukang->addSkill(new QianjieChain);
    lei_lukang->addSkill(new QianjiePindianPro);
    lei_lukang->addSkill(new Jueyan);
    lei_lukang->addSkill(new JueyanTargetMod);
    lei_lukang->addSkill(new Poshi);
    related_skills.insertMulti("qianjie", "#qianjie-chain");
    related_skills.insertMulti("qianjie", "#qianjiepindianpro");
    related_skills.insertMulti("jueyan", "#jueyantargetmod");

    General *haozhao = new General(this, "haozhao", "wei", 4);
    haozhao->addSkill(new Zhengu);
    haozhao->addSkill(new ZhenguEffect);
    related_skills.insertMulti("zhengu", "#zhengueffect");

    General *guanqiujian = new General(this, "guanqiujian", "wei", 4);
    guanqiujian->addSkill(new Zhengrong);
    guanqiujian->addSkill(new Hongju);

    General *ol_guanqiujian = new General(this, "ol_guanqiujian", "wei", 4);
    ol_guanqiujian->addSkill(new OLZhengrong);
    ol_guanqiujian->addSkill(new OLHongju);

    General *mobile_guanqiujian = new General(this, "mobile_guanqiujian", "wei", 4);
    mobile_guanqiujian->addSkill(new MobileZhengrong);
    mobile_guanqiujian->addSkill(new MobileHongju);

    General *lei_yuanshu = new General(this, "lei_yuanshu$", "qun", 4);
    lei_yuanshu->addSkill(new Leiyongsi);
    lei_yuanshu->addSkill(new LeiyongsiMaxCards);
    lei_yuanshu->addSkill(new Leiweidi);
    related_skills.insertMulti("leiyongsi", "#leiyongsimaxmards");

    General *zhangxiu = new General(this, "zhangxiu", "qun", 4);
    zhangxiu->addSkill(new Congjian);
    zhangxiu->addSkill(new Xiongluan);
    zhangxiu->addSkill(new XiongluanTargetMod);
    zhangxiu->addSkill(new XiongluanLimit);
    related_skills.insertMulti("xiongluan", "#xiongluan-target");
    related_skills.insertMulti("xiongluan", "#xiongluan-limit");

    General *shenzhangliao = new General(this, "shenzhangliao", "god");
    shenzhangliao->addSkill(new Duorui);
    shenzhangliao->addSkill(new DuoruiClear);
    shenzhangliao->addSkill(new DuoruiInvalidity);
    shenzhangliao->addSkill(new Zhiti);
    shenzhangliao->addSkill(new ZhitiEffect);
    related_skills.insertMulti("duorui", "#duorui-clear");
    related_skills.insertMulti("duorui", "#duorui-inv");
    related_skills.insertMulti("zhiti", "#zhiti-effect");

    General *ol_shenzhangliao = new General(this, "ol_shenzhangliao", "god");
    ol_shenzhangliao->addSkill(new OLDuorui);
    ol_shenzhangliao->addSkill(new OLDuoruiClear);
    ol_shenzhangliao->addSkill(new OLZhiti);
    ol_shenzhangliao->addSkill(new OLZhitiEffect);
    related_skills.insertMulti("olduorui", "#olduorui-clear");
    related_skills.insertMulti("olzhiti", "#olzhiti-effect");

    General *shenganning = new General(this, "shenganning", "god", 6, true, false, false, 3);
    shenganning->addSkill(new Poxi);
    shenganning->addSkill(new Jieyingg);
    shenganning->addSkill(new jieyinggEffect);
    shenganning->addSkill(new JieyinggKeep);
    shenganning->addSkill(new JieyinggTargetMod);
    related_skills.insertMulti("jieyingg", "#jieyingg-effect");
    related_skills.insertMulti("jieyingg", "#jieyingg-keep");
    related_skills.insertMulti("jieyingg", "#jieyingg-target");
    addMetaObject<PoxiCard>();
    addMetaObject<PoxiDisCard>();


    addMetaObject<JueyanCard>();
    addMetaObject<HuairouCard>();
    addMetaObject<HongjuCard>();
    addMetaObject<QingceCard>();
    addMetaObject<XiongluanCard>();
    addMetaObject<OLHongjuCard>();
    addMetaObject<OLQingceCard>();
    addMetaObject<MobileHongjuCard>();

    skills << new Huairou << new Qingce << new OLQingce;
}
ADD_PACKAGE(Lei)
