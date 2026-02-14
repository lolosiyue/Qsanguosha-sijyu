#include "yjcm.h"
//#include "skill.h"
//#include "standard.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
//#include "ai.h"
//#include "general.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "mobile.h"

class Yizhong : public TriggerSkill
{
public:
    Yizhong() : TriggerSkill("yizhong")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && TriggerSkill::triggerable(target) && target->getArmor() == nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("Slash")&&effect.card->isBlack()) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#SkillNullify";
            log.from = player;
            log.arg = objectName();
            log.arg2 = effect.card->objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());

            return true;
        }
        return false;
    }
};

class Luoying : public TriggerSkill
{
public:
    Luoying() : TriggerSkill("luoying")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caozhi, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && move.from != caozhi
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            || move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE)) {
            QList<int> card_ids;
            int i = 0;
            foreach (int card_id, move.card_ids) {
                if (Sanguosha->getCard(card_id)->getSuit() == Card::Club
					&& room->getCardPlace(card_id) == Player::DiscardPile
                    && (move.from_places[i] == Player::PlaceJudge
					|| move.from_places[i] == Player::PlaceHand
					|| move.from_places[i] == Player::PlaceEquip))
                    card_ids << card_id;
                i++;
            }
            if (card_ids.length()>0 && caozhi->askForSkillInvoke(this, data)) {
				room->broadcastSkillInvoke(objectName(),qrand()%2+1,caozhi);
                int ai_delay = Config.AIDelay;
                Config.AIDelay = 0;
				DummyCard *dummy = new DummyCard();
				room->fillAG(card_ids, caozhi);
                while (card_ids.length() > 0) {
                    int id = room->askForAG(caozhi, card_ids, dummy->subcardsLength()>0, objectName(), "@luoying_get");
                    if (id == -1) break;
					room->takeAG(caozhi, id, false, QList<ServerPlayer *>() << caozhi);
                    card_ids.removeOne(id);
                    dummy->addSubcard(id);
                }
				room->clearAG(caozhi);
                Config.AIDelay = ai_delay;
				move.reason.m_skillName = objectName();
				room->moveCardTo(dummy, caozhi, Player::PlaceHand, move.reason, true);
				delete dummy;
            }
        }
        return false;
    }
};

class Jiushi : public ZeroCardViewAsSkill
{
public:
    Jiushi() : ZeroCardViewAsSkill("jiushi")
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

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return qrand() % 2 + 1;
    }
};

class JiushiFlip : public TriggerSkill
{
public:
    JiushiFlip() : TriggerSkill("#jiushi-flip")
    {
        events << PreCardUsed << DamageDone << DamageComplete;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == PreCardUsed)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getSkillNames().contains("jiushi"))
                player->turnOver();
        } else if (triggerEvent == DamageDone) {
            player->tag["PredamagedFace"] = !player->faceUp();
        } else if (triggerEvent == DamageComplete) {
            bool facedown = player->tag.value("PredamagedFace").toBool();
            player->tag.remove("PredamagedFace");
            if (facedown && !player->faceUp() && player->hasSkill("jiushi") && player->askForSkillInvoke("jiushi", data)) {
                room->broadcastSkillInvoke("jiushi", 3);
                player->turnOver();
            }
        }

        return false;
    }
};

class Wuyan : public TriggerSkill
{
public:
    Wuyan() : TriggerSkill("wuyan")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->getTypeId() == Card::TypeTrick) {
            if (triggerEvent == DamageInflicted && TriggerSkill::triggerable(player)) {
                LogMessage log;
                log.type = "#WuyanGood";
                log.from = player;
                log.arg = damage.card->objectName();
                log.arg2 = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 2);
                room->notifySkillInvoked(player, objectName());

                return true;
            } else if (triggerEvent == DamageCaused && damage.from && TriggerSkill::triggerable(damage.from)) {
                LogMessage log;
                log.type = "#WuyanBad";
                log.from = player;
                log.arg = damage.card->objectName();
                log.arg2 = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 1);
                room->notifySkillInvoked(player, objectName());

                return true;
            }
        }

        return false;
    }
};

JujianCard::JujianCard()
{
    mute = true;
}

bool JujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void JujianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.to->getGeneralName().contains("zhugeliang") || effect.to->getGeneralName() == "wolong")
        room->broadcastSkillInvoke("jujian", 2);
    else
        room->broadcastSkillInvoke("jujian", 1);

    QStringList choicelist;
    choicelist << "draw";
    if (effect.to->isWounded())
        choicelist << "recover";
    if (!effect.to->faceUp() || effect.to->isChained())
        choicelist << "reset";
    QString choice = room->askForChoice(effect.to, "jujian", choicelist.join("+"));

    if (choice == "draw")
        effect.to->drawCards(2, "jujian");
    else if (choice == "recover")
        room->recover(effect.to, RecoverStruct("jujian", effect.from));
    else if (choice == "reset") {
        if (effect.to->isChained())
            room->setPlayerChained(effect.to);
        if (!effect.to->faceUp())
            effect.to->turnOver();
    }
}

class JujianViewAsSkill : public OneCardViewAsSkill
{
public:
    JujianViewAsSkill() : OneCardViewAsSkill("jujian")
    {
        filter_pattern = "^BasicCard!";
        response_pattern = "@@jujian";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        JujianCard *jujianCard = new JujianCard;
        jujianCard->addSubcard(originalCard);
        return jujianCard;
    }
};

class Jujian : public PhaseChangeSkill
{
public:
    Jujian() : PhaseChangeSkill("jujian")
    {
        view_as_skill = new JujianViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *xushu, Room *room) const
    {
        if (xushu->getPhase() == Player::Finish && xushu->canDiscard(xushu, "he"))
            room->askForUseCard(xushu, "@@jujian", "@jujian-card", -1, Card::MethodDiscard);
        return false;
    }
};

class Enyuan : public TriggerSkill
{
public:
    Enyuan() : TriggerSkill("enyuan")
    {
        events << CardsMoveOneTime << Damaged;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.from && move.from->isAlive() && move.from != move.to && move.card_ids.size() >= 2 && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
                    && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)) {
                move.from->setFlags("EnyuanDrawTarget");
                bool invoke = room->askForSkillInvoke(player, objectName(), data);
                move.from->setFlags("-EnyuanDrawTarget");
                if (invoke) {
                    room->drawCards((ServerPlayer *)move.from, 1, objectName());
                    room->broadcastSkillInvoke(objectName(), 1);
                }
            }
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *source = damage.from;
            if (!source || source == player) return false;
            int x = damage.damage;
            for (int i = 0; i < x; i++) {
                if (source->isAlive() && player->isAlive() && room->askForSkillInvoke(player, objectName(), data)) {
                    room->broadcastSkillInvoke(objectName(), 2);
                    const Card *card = nullptr;
                    if (!source->isKongcheng()) {
                        source->tag["enyuan_data"] = data;
                        card = room->askForExchange(source, objectName(), 1, 1, false, "EnyuanGive::" + player->objectName(), true);
                        source->tag.remove("enyuan_data");
                    }
                    if (card) {
                        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(),
                            player->objectName(), objectName(), "");
                        reason.m_playerId = player->objectName();
                        room->moveCardTo(card, source, player, Player::PlaceHand, reason);
                    } else {
                        room->loseHp(HpLostStruct(source, 1, objectName(), player));
                    }
                } else {
                    break;
                }
            }
        }
        return false;
    }
};

class Xuanhuo : public PhaseChangeSkill
{
public:
    Xuanhuo() : PhaseChangeSkill("xuanhuo")
    {
    }

    bool onPhaseChange(ServerPlayer *fazheng, Room *room) const
    {
        if (fazheng->getPhase() == Player::Draw) {
            ServerPlayer *to = room->askForPlayerChosen(fazheng, room->getOtherPlayers(fazheng), objectName(), "xuanhuo-invoke", true, true);
            if (to) {
                room->broadcastSkillInvoke(objectName());
                room->drawCards(to, 2, objectName());
                if (!fazheng->isAlive() || !to->isAlive())
                    return true;

                QList<ServerPlayer *> targets;
                foreach (ServerPlayer *vic, room->getOtherPlayers(to)) {
                    if (to->canSlash(vic))
                        targets << vic;
                }
                ServerPlayer *victim = nullptr;
                if (!targets.isEmpty()) {
                    victim = room->askForPlayerChosen(fazheng, targets, "xuanhuo_slash", "@dummy-slash2:" + to->objectName());

                    LogMessage log;
                    log.type = "#CollateralSlash";
                    log.from = fazheng;
                    log.to << victim;
                    room->sendLog(log);
                }

                if (victim == nullptr || !room->askForUseSlashTo(to, victim, QString("xuanhuo-slash:%1:%2").arg(fazheng->objectName()).arg(victim->objectName()))) {
                    if (to->isNude()) return true;
                    DummyCard *dummy = new DummyCard;
					for (int i = 0; i < 2; i++) {
						if(to->getCardCount()<i) break;
                        int id = room->askForCardChosen(fazheng, to, "he", "xuanhuo", false, Card::MethodNone, dummy->getSubcards());
                        if(id<0) break;
						dummy->addSubcard(id);
                    }
                    room->moveCardTo(dummy, fazheng, Player::PlaceHand, false);
                    delete dummy;
                }

                return true;
            }
        }

        return false;
    }
};

class Huilei : public TriggerSkill
{
public:
    Huilei() :TriggerSkill("huilei")
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
            log.type = "#HuileiThrow";
            log.from = player;
            log.to << killer;
            log.arg = objectName();
            room->sendLog(log);

            QString killer_name = killer->getGeneralName();
            if (killer_name.contains("zhugeliang") || killer_name == "wolong")
                room->broadcastSkillInvoke(objectName(), 1);
            else
                room->broadcastSkillInvoke(objectName(), 2);
            room->notifySkillInvoked(player, objectName());

            killer->throwAllHandCardsAndEquips(objectName());
        }

        return false;
    }
};

class Xuanfeng : public TriggerSkill
{
public:
    Xuanfeng() : TriggerSkill("xuanfeng")
    {
        events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    void perform(Room *room, ServerPlayer *lingtong) const
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *target, room->getOtherPlayers(lingtong)) {
            if (lingtong->canDiscard(target, "he"))
                targets << target;
        }
        if (targets.isEmpty())
            return;

        if (lingtong->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());

            ServerPlayer *first = room->askForPlayerChosen(lingtong, targets, "xuanfeng");
            room->doAnimate(1, lingtong->objectName(), first->objectName());
            ServerPlayer *second = nullptr;
            int first_id = -1;
            int second_id = -1;
            if (first != nullptr) {
                first_id = room->askForCardChosen(lingtong, first, "he", "xuanfeng", false, Card::MethodDiscard);
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
                second = room->askForPlayerChosen(lingtong, targets, "xuanfeng");
                room->doAnimate(1, lingtong->objectName(), second->objectName());
            }
            if (second != nullptr) {
                second_id = room->askForCardChosen(lingtong, second, "he", "xuanfeng", false, Card::MethodDiscard);
                room->throwCard(second_id, second, lingtong);
            }
        }
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lingtong, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            lingtong->setMark("xuanfeng", 0);
        } else if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != lingtong)
                return false;

            if (lingtong->getPhase() == Player::Discard
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                lingtong->addMark("xuanfeng", move.card_ids.length());

            if (move.from_places.contains(Player::PlaceEquip) && TriggerSkill::triggerable(lingtong))
                perform(room, lingtong);
        } else if (triggerEvent == EventPhaseEnd && TriggerSkill::triggerable(lingtong)
            && lingtong->getPhase() == Player::Discard && lingtong->getMark("xuanfeng") >= 2) {
            perform(room, lingtong);
        }

        return false;
    }
};

class Pojun : public TriggerSkill
{
public:
    Pojun() : TriggerSkill("pojun")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer
            && damage.to->isAlive() && !damage.to->hasFlag("Global_DebutFlag")
            && room->askForSkillInvoke(player, objectName(), data)) {
            int x = qMin(5, damage.to->getHp());
            room->broadcastSkillInvoke(objectName(), (x >= 3 || !damage.to->faceUp()) ? 2 : 1);
            damage.to->drawCards(x, objectName());
            damage.to->turnOver();
        }
        return false;
    }
};

XianzhenCard::XianzhenCard()
{
}

bool XianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void XianzhenCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.from->pindian(effect.to, "xianzhen", nullptr)) {
        effect.from->tag["XianzhenTarget"] = QVariant::fromValue(effect.to);
        room->setPlayerFlag(effect.from, "XianzhenSuccess");
        room->addPlayerMark(effect.from, effect.to->objectName()+"xianzhen-Clear");
        room->addPlayerMark(effect.to, "Armor_Nullified");
    } else
        room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
}

class XianzhenViewAsSkill : public ZeroCardViewAsSkill
{
public:
    XianzhenViewAsSkill() : ZeroCardViewAsSkill("xianzhen")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("XianzhenCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new XianzhenCard;
    }
};

class Xianzhen : public TriggerSkill
{
public:
    Xianzhen() : TriggerSkill("xianzhen")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new XianzhenViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->tag["XianzhenTarget"].value<ServerPlayer *>() != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
        }
        ServerPlayer *target = gaoshun->tag["XianzhenTarget"].value<ServerPlayer *>();
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != gaoshun) {
                if (death.who == target) {
                    room->removeFixedDistance(gaoshun, target, 1);
                    gaoshun->tag.remove("XianzhenTarget");
                    room->setPlayerFlag(gaoshun, "-XianzhenSuccess");
                }
                return false;
            }
        }
        if (target) {
            QStringList assignee_list = gaoshun->property("extra_slash_specific_assignee").toString().split("+");
            assignee_list.removeOne(target->objectName());
            room->setPlayerProperty(gaoshun, "extra_slash_specific_assignee", assignee_list.join("+"));
            room->removeFixedDistance(gaoshun, target, 1);
            gaoshun->tag.remove("XianzhenTarget");
            room->removePlayerMark(target, "Armor_Nullified");
        }
        return false;
    }
};

class XianzhenTargetMod : public TargetModSkill
{
public:
    XianzhenTargetMod() : TargetModSkill("#xianzhen_target")
    {
        frequency = NotFrequent;
        pattern = "^SkillCard";
    }

    int getResidueNum(const Player *from, const Card *card, const Player *to) const
    {
        if (card->isKindOf("Slash")&&to&&from->getMark(to->objectName()+"xianzhen-Clear") > 0)
            return 1000;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *to) const
    {
        if (to&&from->getMark(to->objectName()+"xianzhen-Clear")>0)
            return 1000;
        return 0;
    }
};

class Jinjiu : public FilterSkill
{
public:
    Jinjiu() : FilterSkill("jinjiu")
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

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->isJieGeneral())
            index += 2;
        return index;
    }
};

MingceCard::MingceCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MingceCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    QList<ServerPlayer *> targets;
    if (Slash::IsAvailable(effect.to)) {
        foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
            if (effect.to->canSlash(p))
                targets << p;
        }
    }

    ServerPlayer *target = nullptr;
    QStringList choicelist;
    choicelist << "draw";
    if (!targets.isEmpty() && effect.from->isAlive()) {
        target = room->askForPlayerChosen(effect.from, targets, "mingce", "@dummy-slash2:" + effect.to->objectName());
        target->setFlags("MingceTarget"); // For AI

        LogMessage log;
        log.type = "#CollateralSlash";
        log.from = effect.from;
        log.to << target;
        room->sendLog(log);

        choicelist << "use";
    }

    try {
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "mingce", "");
        room->obtainCard(effect.to, this, reason);
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            if (target && target->hasFlag("MingceTarget")) target->setFlags("-MingceTarget");
        throw triggerEvent;
    }

    QString choice = room->askForChoice(effect.to, "mingce", choicelist.join("+"));
    if (target && target->hasFlag("MingceTarget")) target->setFlags("-MingceTarget");

    if (choice == "use") {
        if (effect.to->canSlash(target, nullptr, false)) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mingce");
			slash->deleteLater();
            room->useCard(CardUseStruct(slash, effect.to, target));
        }
    } else if (choice == "draw") {
        effect.to->drawCards(1, "mingce");
    }
}

class Mingce : public OneCardViewAsSkill
{
public:
    Mingce() : OneCardViewAsSkill("mingce")
    {
        filter_pattern = "EquipCard,Slash";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MingceCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MingceCard *mingceCard = new MingceCard;
        mingceCard->addSubcard(originalCard);

        return mingceCard;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (card->isKindOf("Slash"))
            return -2;
        else
            return -1;
    }
};

class Zhichi : public TriggerSkill
{
public:
    Zhichi() : TriggerSkill("zhichi")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *current = room->getCurrent();
        if (current&&current!=player) {
            int index = 1;
            if (player->isJieGeneral())
                index = 3;
            player->peiyin(this, index);
            room->notifySkillInvoked(player, objectName());
            if (player->getMark("@late") == 0)
                room->addPlayerMark(player, "@late");

            LogMessage log;
            log.type = "#ZhichiDamaged";
            log.from = player;
            room->sendLog(log);
        }

        return false;
    }
};

class ZhichiProtect : public TriggerSkill
{
public:
    ZhichiProtect() : TriggerSkill("#zhichi-protect")
    {
        events << CardEffected;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if ((effect.card->isKindOf("Slash") || effect.card->isNDTrick()) && effect.to->getMark("@late") > 0) {
            int index = 2;
            if (effect.to->isJieGeneral())
                index = 4;
            effect.to->peiyin("zhichi", index);
            LogMessage log;
            log.type = "#ZhichiAvoid";
            log.from = effect.to;
            log.arg = "zhichi";
            room->sendLog(log);
            room->notifySkillInvoked(effect.to, "zhichi");

            return true;
        }
        return false;
    }
};

class ZhichiClear : public TriggerSkill
{
public:
    ZhichiClear() : TriggerSkill("#zhichi-clear")
    {
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player || player != room->getCurrent())
                return false;
        }

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getMark("@late") > 0)
                room->setPlayerMark(p, "@late", 0);
        }

        return false;
    }
};

GanluCard::GanluCard()
{
}

void GanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
    Room *room = first->getRoom();

    QList<int> equips1, equips2;
    foreach(const Card *equip, first->getEquips())
        equips1.append(equip->getId());
    foreach(const Card *equip, second->getEquips())
        equips2.append(equip->getId());

    QList<CardsMoveStruct> exchangeMove;
    CardsMoveStruct move1(equips1, second, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), "ganlu", ""));
    CardsMoveStruct move2(equips2, first, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_SWAP, second->objectName(), first->objectName(), "ganlu", ""));
    exchangeMove.push_back(move2);
    exchangeMove.push_back(move1);
    room->moveCardsAtomic(exchangeMove, false);
}

bool GanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool GanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    switch (targets.length()) {
    case 0: return true;
    case 1: {
        int n1 = targets.first()->getEquips().length();
        int n2 = to_select->getEquips().length();
        return qAbs(n1 - n2) <= Self->getLostHp();
    }
    default:
        return false;
    }
}

void GanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    LogMessage log;
    log.type = "#GanluSwap";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    swapEquip(targets.first(), targets[1]);
}

class Ganlu : public ZeroCardViewAsSkill
{
public:
    Ganlu() : ZeroCardViewAsSkill("ganlu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("GanluCard");
    }

    const Card *viewAs() const
    {
        return new GanluCard;
    }
};

class Buyi : public TriggerSkill
{
public:
    Buyi() : TriggerSkill("buyi")
    {
        events << Dying;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *wuguotai, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *player = dying.who;
        if (player->isKongcheng()) return false;
        if (player->getHp() < 1 && wuguotai->askForSkillInvoke(this, data)) {
            const Card *card = nullptr;
            if (player == wuguotai)
                card = room->askForCardShow(player, wuguotai, objectName());
            else {
                int card_id = room->askForCardChosen(wuguotai, player, "h", "buyi");
                card = Sanguosha->getCard(card_id);
            }

            room->showCard(player, card->getEffectiveId());

            if (card->getTypeId() != Card::TypeBasic) {
                if (!player->isJilei(card))
                    room->throwCard(card, player);

                room->broadcastSkillInvoke(objectName());
                room->recover(player, RecoverStruct("buyi", wuguotai));
            }
        }
        return false;
    }
};

XinzhanCard::XinzhanCard()
{
    target_fixed = true;
}

void XinzhanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> cards = room->getNCards(3), left;

    LogMessage log;
    log.type = "$ViewDrawPile";
    log.from = source;
    log.card_str = ListI2S(cards).join("+");
    room->sendLog(log, source);

    left = cards;

    QList<int> hearts, non_hearts;
    foreach (int card_id, cards) {
        const Card *card = Sanguosha->getCard(card_id);
        if (card->getSuit() == Card::Heart)
            hearts << card_id;
        else
            non_hearts << card_id;
    }

    if (!hearts.isEmpty()) {
        DummyCard *dummy = new DummyCard;
        do {
            room->fillAG(left, source, non_hearts);
            int card_id = room->askForAG(source, hearts, true, "xinzhan");
            if (card_id == -1) {
                room->clearAG(source);
                break;
            }

            hearts.removeOne(card_id);
            left.removeOne(card_id);

            dummy->addSubcard(card_id);
            room->clearAG(source);
        } while (!hearts.isEmpty());

        if (dummy->subcardsLength() > 0) {
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_PILE, QVariant(room->getDrawPile().length() + dummy->subcardsLength()));
            source->obtainCard(dummy);
            foreach(int id, dummy->getSubcards())
                room->showCard(source, id);
        }
        delete dummy;
    }

    if (!left.isEmpty())
        room->askForGuanxing(source, left, Room::GuanxingUpOnly);
}

class Xinzhan : public ZeroCardViewAsSkill
{
public:
    Xinzhan() : ZeroCardViewAsSkill("xinzhan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("XinzhanCard") && player->getHandcardNum() > player->getMaxHp();
    }

    const Card *viewAs() const
    {
        return new XinzhanCard;
    }
};

class Quanji : public MasochismSkill
{
public:
    Quanji() : MasochismSkill("quanji")
    {
        frequency = Frequent;
    }

    void onDamaged(ServerPlayer *zhonghui, const DamageStruct &damage) const
    {
        Room *room = zhonghui->getRoom();

        int x = damage.damage;
        for (int i = 0; i < x; i++) {
            if (zhonghui->askForSkillInvoke("quanji")) {
                room->broadcastSkillInvoke("quanji");
                room->drawCards(zhonghui, 1, objectName());
                if (!zhonghui->isKongcheng()) {
                    int card_id;
                    if (zhonghui->getHandcardNum() == 1) {
                        room->getThread()->delay();
                        card_id = zhonghui->handCards().first();
                    } else {
                        const Card *card = room->askForExchange(zhonghui, "quanji", 1, 1, false, "QuanjiPush");
                        card_id = card->getEffectiveId();
                    }
                    zhonghui->addToPile("power", card_id);
                }
            }
        }

    }
};

class QuanjiKeep : public MaxCardsSkill
{
public:
    QuanjiKeep() : MaxCardsSkill("#quanji")
    {
        frequency = Frequent;
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("quanji"))
            return target->getPile("power").length();
        return 0;
    }
};

class Zili : public PhaseChangeSkill
{
public:
    Zili() : PhaseChangeSkill("zili")
    {
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive()&&target->getPhase()==Player::Start
		&&target->getMark("zili")<1&&target->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *zhonghui, Room *room) const
    {
        if (zhonghui->getPile("power").length() >= 3) {
            LogMessage log;
            log.type = "#ZiliWake";
            log.from = zhonghui;
            log.arg = QString::number(zhonghui->getPile("power").length());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!zhonghui->canWake(objectName()))
			return false;

        room->broadcastSkillInvoke(objectName());
        //room->doLightbox("$ZiliAnimate", 4000);
        room->notifySkillInvoked(zhonghui, objectName());

        room->doSuperLightbox(zhonghui, "zili");

        room->setPlayerMark(zhonghui, "zili", 1);
        if (room->changeMaxHpForAwakenSkill(zhonghui, -1, objectName())) {
            if (zhonghui->isWounded() && room->askForChoice(zhonghui, objectName(), "recover+draw") == "recover")
                room->recover(zhonghui, RecoverStruct("zili", zhonghui));
            else
                room->drawCards(zhonghui, 2, objectName());
            room->acquireSkill(zhonghui, "paiyi");
        }
        return false;
    }
};

PaiyiCard::PaiyiCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool PaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void PaiyiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhonghui = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhonghui->getRoom();
    QList<int> powers = zhonghui->getPile("power");
    if (powers.isEmpty()) return;

    room->broadcastSkillInvoke("paiyi", target == zhonghui ? 1 : 2);

    int card_id = subcards.first();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, zhonghui->objectName(), target->objectName(), "paiyi", "");
    room->throwCard(Sanguosha->getCard(card_id), reason, nullptr);
    room->drawCards(target, 2, "paiyi");
    if (target->getHandcardNum() > zhonghui->getHandcardNum())
        room->damage(DamageStruct("paiyi", zhonghui, target));
}

class Paiyi : public OneCardViewAsSkill
{
public:
    Paiyi() : OneCardViewAsSkill("paiyi")
    {
        expand_pile = "power";
        filter_pattern = ".|.|.|power";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->getPile("power").isEmpty() && !player->hasUsed("PaiyiCard");
    }

    const Card *viewAs(const Card *c) const
    {
        PaiyiCard *py = new PaiyiCard;
        py->addSubcard(c);
        return py;
    }
};

class Jueqing : public TriggerSkill
{
public:
    Jueqing() : TriggerSkill("jueqing")
    {
        frequency = Compulsory;
        events << Predamage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhangchunhua, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from == zhangchunhua) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(zhangchunhua, objectName());
            //room->loseHp(HpLostStruct(damage.to, damage.damage, objectName(), zhangchunhua, damage.ignore_hujia));
            room->loseHp(HpLostStruct(damage.to, damage.damage, objectName(), zhangchunhua));

            return true;
        }
        return false;
    }
};

Shangshi::Shangshi() : TriggerSkill("shangshi")
{
    events << HpChanged << MaxHpChanged << CardsMoveOneTime;
    frequency = Frequent;
}

int Shangshi::getMaxLostHp(ServerPlayer *zhangchunhua) const
{
    int losthp = zhangchunhua->getLostHp();
    if (losthp > 2)
        losthp = 2;
    return qMin(losthp, zhangchunhua->getMaxHp());
}

bool Shangshi::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhangchunhua, QVariant &data) const
{
    int losthp = getMaxLostHp(zhangchunhua);
    if (triggerEvent == CardsMoveOneTime) {
        bool can_invoke = false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == zhangchunhua && move.from_places.contains(Player::PlaceHand))
            can_invoke = true;
        if (move.to == zhangchunhua && move.to_place == Player::PlaceHand)
            can_invoke = true;
        if (!can_invoke)
            return false;
    } else if (triggerEvent == HpChanged || triggerEvent == MaxHpChanged) {
        if (zhangchunhua->getPhase() == Player::Discard) {
            zhangchunhua->addMark("shangshi");
            return false;
        }
    }

    if (zhangchunhua->getHandcardNum() < losthp && zhangchunhua->askForSkillInvoke(this)) {
        int n = qrand() % 2 + 1;
        if (zhangchunhua->isJieGeneral())
            n += 2;
        room->broadcastSkillInvoke("shangshi", n);
        zhangchunhua->drawCards(losthp - zhangchunhua->getHandcardNum(), objectName());
    }

    return false;
}

OLSanyaoCard::OLSanyaoCard()
{
}

bool OLSanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    QList<const Player *> players = Self->getAliveSiblings();
    players << Self;
    QString choice = Self->tag["olsanyao"].toString();
    int max = -1000;
    if (choice == "hp") {
        foreach (const Player *p, players) {
            if (max < p->getHp())
                max = p->getHp();
        }
        return to_select->getHp() == max;
    } else if (choice == "hand") {
        foreach (const Player *p, players) {
            if (max < p->getHandcardNum())
                max = p->getHandcardNum();
        }
        return to_select->getHandcardNum() == max;
    }
    return false;
}

void OLSanyaoCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (user_string == "hp")
        room->addPlayerMark(effect.from, "olsanyao_hp-PlayClear");
    else if (user_string == "hand")
        room->addPlayerMark(effect.from, "olsanyao_hand-PlayClear");
    room->damage(DamageStruct("olsanyao", effect.from, effect.to));
}

class OLSanyao : public OneCardViewAsSkill
{
public:
    OLSanyao() : OneCardViewAsSkill("olsanyao")
    {
        filter_pattern = ".!";
    }

    QDialog *getDialog() const
    {
        return TiansuanDialog::getInstance("olsanyao");
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") &&
                (player->getMark("olsanyao_hp-PlayClear") <= 0 || player->getMark("olsanyao_hand-PlayClear") <= 0);
    }

    const Card *viewAs(const Card *originalcard) const
    {
        QString choice = Self->tag["olsanyao"].toString();
        OLSanyaoCard *first = new OLSanyaoCard;
        first->addSubcard(originalcard);
        first->setUserString(choice);
        return first;
    }
};

class OLZhiman : public TriggerSkill
{
public:
    OLZhiman() : TriggerSkill("olzhiman")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to == player || damage.to->isDead()) return false;

        if (player->askForSkillInvoke(this, damage.to)) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#Yishi";
            log.from = player;
            log.arg = objectName();
            log.to << damage.to;
            room->sendLog(log);

            if (damage.to->isAllNude())
                return true;
            int card_id = room->askForCardChosen(player, damage.to, "hej", objectName());
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(card_id), reason);
            return true;
        }
        return false;
    }
};

class NosEnyuan : public TriggerSkill
{
public:
    NosEnyuan() : TriggerSkill("nosenyuan")
    {
        events << HpRecover << Damaged;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == HpRecover) {
            RecoverStruct recover = data.value<RecoverStruct>();
            if (recover.who && recover.who != player) {
                room->broadcastSkillInvoke("nosenyuan", qrand() % 2 + 1);
                room->sendCompulsoryTriggerLog(player, objectName());
                recover.who->drawCards(recover.recover, objectName());
            }
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *source = damage.from;
            if (source && source != player) {
                room->broadcastSkillInvoke("nosenyuan", qrand() % 2 + 3);
                room->sendCompulsoryTriggerLog(player, objectName());

                const Card *card = room->askForCard(source, ".|heart|.|hand", "@nosenyuan-heart", data, Card::MethodNone);
                if (card)
                    player->obtainCard(card);
                else
                    room->loseHp(HpLostStruct(source, 1, objectName(), player));
            }
        }

        return false;
    }
};

NosXuanhuoCard::NosXuanhuoCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void NosXuanhuoCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->obtainCard(this);

    Room *room = effect.from->getRoom();
    int card_id = room->askForCardChosen(effect.from, effect.to, "he", "nosxuanhuo");
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
    room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);

    QList<ServerPlayer *> targets = room->getOtherPlayers(effect.to);
    ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "nosxuanhuo", "@nosxuanhuo-give:" + effect.to->objectName());
    if (target != effect.from) {
        CardMoveReason reason2(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), target->objectName(), "nosxuanhuo", "");
        room->obtainCard(target, Sanguosha->getCard(card_id), reason2, false);
    }
}

class NosXuanhuo : public OneCardViewAsSkill
{
public:
    NosXuanhuo() :OneCardViewAsSkill("nosxuanhuo")
    {
        filter_pattern = ".|heart|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && !player->hasUsed("NosXuanhuoCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        NosXuanhuoCard *xuanhuoCard = new NosXuanhuoCard;
        xuanhuoCard->addSubcard(originalCard);
        return xuanhuoCard;
    }
};

class NosXuanfeng : public TriggerSkill
{
public:
    NosXuanfeng() : TriggerSkill("nosxuanfeng")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lingtong, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == lingtong && move.from_places.contains(Player::PlaceEquip)) {
                QStringList choicelist;
                choicelist << "nothing";
                QList<ServerPlayer *> targets1;
                foreach (ServerPlayer *target, room->getAlivePlayers()) {
                    if (lingtong->canSlash(target, nullptr, false))
                        targets1 << target;
                }
                Slash *slashx = new Slash(Card::NoSuit, 0);
                if (!targets1.isEmpty() && !lingtong->isCardLimited(slashx, Card::MethodUse))
                    choicelist << "slash";
                slashx->deleteLater();
                QList<ServerPlayer *> targets2;
                foreach (ServerPlayer *p, room->getOtherPlayers(lingtong)) {
                    if (lingtong->distanceTo(p) <= 1)
                        targets2 << p;
                }
                if (!targets2.isEmpty()) choicelist << "damage";

                QString choice = room->askForChoice(lingtong, objectName(), choicelist.join("+"));
                if (choice == "slash") {
                    ServerPlayer *target = room->askForPlayerChosen(lingtong, targets1, "nosxuanfeng_slash", "@dummy-slash");
                    room->broadcastSkillInvoke(objectName(), 1);
                    Slash *slash = new Slash(Card::NoSuit, 0);
                    slash->setSkillName(objectName());
		             		slash->deleteLater();
                    room->useCard(CardUseStruct(slash, lingtong, target));
                } else if (choice == "damage") {
                    room->broadcastSkillInvoke(objectName(), 2);

                    LogMessage log;
                    log.type = "#InvokeSkill";
                    log.from = lingtong;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(lingtong, objectName());

                    ServerPlayer *target = room->askForPlayerChosen(lingtong, targets2, "nosxuanfeng_damage", "@nosxuanfeng-damage");
                    room->damage(DamageStruct("nosxuanfeng", lingtong, target));
                }
            }
        }

        return false;
    }
};

class NosWuyan : public TriggerSkill
{
public:
    NosWuyan() : TriggerSkill("noswuyan")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isNDTrick()){
				if(use.from->hasSkill(objectName())){
					
				}
			}
            return false;
		}
		CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.to == effect.from)
            return false;
        if (effect.card->isNDTrick()) {
            if (effect.from && effect.from->hasSkill(objectName())) {
                room->broadcastSkillInvoke("noswuyan");
                LogMessage log;
                log.type = "#WuyanBaD";
                log.from = effect.from;
                log.to << effect.to;
                log.arg = effect.card->objectName();
                log.arg2 = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(effect.from, objectName());
                return true;
            }
            if (effect.to->hasSkill(objectName()) && effect.from) {
                room->broadcastSkillInvoke("noswuyan");
                LogMessage log;
                log.type = "#WuyanGooD";
                log.from = effect.to;
                log.to << effect.from;
                log.arg = effect.card->objectName();
                log.arg2 = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(effect.to, objectName());
                return true;
            }
        }
        return false;
    }
};

NosJujianCard::NosJujianCard()
{
}

void NosJujianCard::onEffect(CardEffectStruct &effect) const
{
    int n = subcardsLength();
    effect.to->drawCards(n, "nosjujian");
    Room *room = effect.from->getRoom();

    if (effect.from->isAlive() && n == 3) {
        QSet<Card::CardType> types;
        foreach(int card_id, effect.card->getSubcards())
            types << Sanguosha->getCard(card_id)->getTypeId();

        if (types.size() == 1) {
            LogMessage log;
            log.type = "#JujianRecover";
            log.from = effect.from;
            const Card *card = Sanguosha->getCard(subcards.first());
            log.arg = card->getType();
            room->sendLog(log);
            room->recover(effect.from, RecoverStruct("nosjujian", effect.from));
        }
    }
}

class NosJujian : public ViewAsSkill
{
public:
    NosJujian() : ViewAsSkill("nosjujian")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 3 && !Self->isJilei(to_select);
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("NosJujianCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        NosJujianCard *card = new NosJujianCard;
        card->addSubcards(cards);
        return card;
    }
};

class NosShangshi : public Shangshi
{
public:
    NosShangshi() : Shangshi()
    {
        setObjectName("nosshangshi");
    }

    int getMaxLostHp(ServerPlayer *zhangchunhua) const
    {
        return qMin(zhangchunhua->getLostHp(), zhangchunhua->getMaxHp());
    }
};

class OLLuoying : public TriggerSkill
{
public:
    OLLuoying() : TriggerSkill("olluoying")
    {
        events << CardsMoveOneTime;
    }
    int getPriority(TriggerEvent) const
    {
        return 3;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caozhi, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == caozhi || move.from == nullptr)
            return false;
        if (move.to_place == Player::DiscardPile
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            || move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE)) {
			DummyCard *dummy = new DummyCard();
            int i = 0;
            foreach(int id, move.card_ids) {
                if (Sanguosha->getCard(id)->getSuit() == Card::Club) {
 					if(move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE){
						if(move.from_places[i] == Player::PlaceJudge)
							dummy->addSubcard(id);
					}else if(move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)
						dummy->addSubcard(id);
                }
                i++;
            }
			dummy->deleteLater();
            if (dummy->subcardsLength()>0&&caozhi->askForSkillInvoke(this, data)) {
                room->broadcastSkillInvoke("luoying");
                room->obtainCard(caozhi, dummy);
            }
        }
        return false;
    }
};

JieyueCard::JieyueCard()
{
}

void JieyueCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.to->isNude()) {
        Room *room = effect.to->getRoom();
        const Card *card = room->askForExchange(effect.to, "jieyue", 1, 1, true, QString("@jieyue_put:%1").arg(effect.from->objectName()), true);

        if (card != nullptr)
            effect.from->addToPile("jieyue_pile", card);
        else if (effect.from->canDiscard(effect.to, "he")) {
            int id = room->askForCardChosen(effect.from, effect.to, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, effect.to, effect.from);
        }
    }
}

class JieyueVS : public OneCardViewAsSkill
{
public:
    JieyueVS() : OneCardViewAsSkill("jieyue")
    {
    }

    bool isResponseOrUse() const
    {
        return !Self->getPile("jieyue_pile").isEmpty();
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        if (to_select->isEquipped())
            return false;
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern == "@@jieyue") {
            return !Self->isJilei(to_select);
        }

        if (pattern == "jink")
            return to_select->isRed();
        else if (pattern == "nullification")
            return to_select->isBlack();
        return false;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (!player->getPile("jieyue_pile").isEmpty() && (pattern == "jink" || pattern == "nullification")) || (pattern == "@@jieyue" && !player->isKongcheng());
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        if (!player->getPile("jieyue_pile").isEmpty()) {
            foreach(const Card *card, player->getHandcards() + player->getEquips()) {
                if (card->isBlack())
                    return true;
            }
            
            foreach(int id, player->getHandPile())  {
                if (Sanguosha->getCard(id)->isBlack())
                    return true;
            }
        }

        return false;
    }

    const Card *viewAs(const Card *card) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@jieyue") {
            JieyueCard *jy = new JieyueCard;
            jy->addSubcard(card);
            return jy;
        }

        if (card->isRed()) {
            Jink *jink = new Jink(Card::SuitToBeDecided, 0);
            jink->addSubcard(card);
            jink->setSkillName(objectName());
            return jink;
        } else if (card->isBlack()) {
            Nullification *nulli = new Nullification(Card::SuitToBeDecided, 0);
            nulli->addSubcard(card);
            nulli->setSkillName(objectName());
            return nulli;
        }
        return nullptr;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (card->isKindOf("Nullification"))
            return 3;
        else if (card->isKindOf("Jink"))
            return 2;

        return 1;
    }
};

class Jieyue : public TriggerSkill
{
public:
    Jieyue() : TriggerSkill("jieyue")
    {
        events << EventPhaseStart;
        view_as_skill = new JieyueVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Start && !player->getPile("jieyue_pile").isEmpty()) {
            LogMessage log;
            log.type = "#TriggerSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            DummyCard *dummy = new DummyCard(player->getPile("jieyue_pile"));
            player->obtainCard(dummy);
            delete dummy;
        } else if (player->getPhase() == Player::Finish) {
            room->askForUseCard(player, "@@jieyue", "@jieyue", -1, Card::MethodDiscard, false);
        }
        return false;
    }
};

SanyaoCard::SanyaoCard()
{
}

bool SanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    QList<const Player *> players = Self->getAliveSiblings();
    players << Self;
    int max = -1000;
    foreach (const Player *p, players) {
        if (max < p->getHp())
            max = p->getHp();
    }
    return to_select->getHp() == max;
}

void SanyaoCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("sanyao", effect.from, effect.to));
}

class Sanyao : public OneCardViewAsSkill
{
public:
    Sanyao() : OneCardViewAsSkill("sanyao")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("SanyaoCard");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        SanyaoCard *first = new SanyaoCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Zhiman : public TriggerSkill
{
public:
    Zhiman() : TriggerSkill("zhiman")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        if (player->askForSkillInvoke(this, QVariant::fromValue(damage.to))) {
            int index = qrand() % 2 + 1;
            if (player->getGeneralName().contains("guansuo") || (!player->getGeneralName().contains("masu") && player->getGeneral2Name().contains("guansuo")))
                index = 3;
            room->broadcastSkillInvoke(objectName(), index);
            LogMessage log;
            log.type = "#Yishi";
            log.from = player;
            log.arg = objectName();
            log.to << damage.to;
            room->sendLog(log);

            if (damage.to->getEquips().isEmpty() && damage.to->getJudgingArea().isEmpty())
                return true;
            int card_id = room->askForCardChosen(player, damage.to, "ej", objectName());
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(card_id), reason);
            return true;
        }
        return false;
    }
};

class OlPojun : public TriggerSkill
{
public:
    OlPojun() : TriggerSkill("olpojun")
    {
        events << TargetSpecified << EventPhaseStart << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && TriggerSkill::triggerable(player) && player->getPhase() == Player::Play) {
                foreach (ServerPlayer *t, use.to) {
                    int n = qMin(t->getCards("he").length(), t->getHp());
                    if (n > 0 && player->askForSkillInvoke(this, QVariant::fromValue(t))) {
						room->broadcastSkillInvoke(objectName());
                        DummyCard *dummy = new DummyCard;
                        for (int i = 0; i < n; ++i) {
							int id = room->askForCardChosen(player, t, "he", objectName() + "_dis", false, Card::MethodNone, dummy->getSubcards(), i>0);
                            if (id<0) break;
							dummy->addSubcard(id);
                        }
                        t->addToPile("olpojun", dummy, false);
						dummy->deleteLater();
                    }
                }
            }
        } else if (cardGoBack(triggerEvent, player, data)) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile("olpojun");
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
                }
            }
        }

        return false;
    }

private:
    static bool cardGoBack(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data)
    {
        if (triggerEvent == EventPhaseStart)
            return player->getPhase() == Player::Finish;
        else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            return death.who == player;
        }

        return false;
    }
};



class NosZhenggong: public MasochismSkill {
public:
    NosZhenggong(): MasochismSkill("noszhenggong") {
    }

    bool triggerable(const ServerPlayer *target) const
	{
        return TriggerSkill::triggerable(target) && target->getMark("nosbaijiang") == 0;
    }

    void onDamaged(ServerPlayer *zhonghui, const DamageStruct &damage) const{
        if (damage.from && damage.from->hasEquip()) {
            QVariant data = QVariant::fromValue(damage.from);
            if (!zhonghui->askForSkillInvoke(objectName(), data))
                return;

            Room *room = zhonghui->getRoom();
            room->broadcastSkillInvoke(objectName());
            int equip = room->askForCardChosen(zhonghui, damage.from, "e", objectName());
            const Card *card = Sanguosha->getCard(equip);
			zhonghui->obtainCard(card);
			if(!zhonghui->hasCard(card)||!zhonghui->isAlive()) return;

            const EquipCard *equipcard = qobject_cast<const EquipCard *>(card->getRealCard());
            int equip_index = equipcard->location();
			if(!zhonghui->hasEquipArea(equip_index)) return;

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(equip, zhonghui, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_ROB, zhonghui->objectName()));
            exchangeMove.push_back(move1);
            if (zhonghui->getEquip(equip_index)) {
                CardsMoveStruct move2(zhonghui->getEquip(equip_index)->getId(), nullptr, Player::DiscardPile,
						CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, zhonghui->objectName()));
                exchangeMove.push_back(move2);
            }
            room->moveCardsAtomic(exchangeMove, true);
        }
    }
};

class NosQuanji: public TriggerSkill {
public:
    NosQuanji(): TriggerSkill("nosquanji") {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target&&target->isAlive()&&target->getPhase()==Player::RoundStart;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
        bool skip = false;
        foreach (ServerPlayer *zhonghui, room->getOtherPlayers(player)) {
            if (!zhonghui->hasSkill(objectName()) || !zhonghui->canPindian(player))
                continue;

            if (zhonghui->askForSkillInvoke(this, player)) {
                zhonghui->peiyin(this);
                if (zhonghui->pindian(player, objectName())) {
                    if (skip) {
                    } else {
                        player->skip(Player::Start);
                        player->skip(Player::Judge);
                        skip = true;
                    }
                }
            }
        }
        return skip;
    }
};

class NosBaijiang: public PhaseChangeSkill {
public:
    NosBaijiang(): PhaseChangeSkill("nosbaijiang") {
        frequency = Wake;
		waked_skills = "nosyexin";
    }

    bool triggerable(const ServerPlayer *target) const
	{
		return PhaseChangeSkill::triggerable(target) && target->getMark("nosbaijiang")<1
		&& target->getPhase() == Player::Start;
    }

    bool onPhaseChange(ServerPlayer *zhonghui,Room *room) const
	{
		if(zhonghui->getEquips().length()>=3){
			LogMessage log;
			log.type = "#NosBaijiangWake";
			log.from = zhonghui;
			log.arg = QString::number(zhonghui->getEquips().length());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!zhonghui->canWake(objectName()))
			return false;
		room->sendCompulsoryTriggerLog(zhonghui, this);
        room->doSuperLightbox(zhonghui, objectName());
        //room->doLightbox("$NosBaijiangAnimate", 5000);

        room->setPlayerMark(zhonghui, "nosbaijiang", 1);
        if (room->changeMaxHpForAwakenSkill(zhonghui, 1,objectName())) {
            room->recover(zhonghui, RecoverStruct(zhonghui));
            room->handleAcquireDetachSkills(zhonghui, "-noszhenggong|-nosquanji|nosyexin");
        }
        return false;
    }
};

NosYexinCard::NosYexinCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void NosYexinCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	Card*dc = new DummyCard;
	Card*dc2 = new DummyCard;
	foreach (int id, subcards) {
		if(source->getPile("nospower").contains(id))
			dc->addSubcard(id);
		else if(source->handCards().contains(id))
			dc2->addSubcard(id);
	}
	source->addToPile("nospower", dc2);
	source->obtainCard(dc);
}

class NosYexinViewAsSkill: public ViewAsSkill {
public:
    NosYexinViewAsSkill(): ViewAsSkill("nosyexin")
	{
		expand_pile = "nospower";
    }
    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !to_select->isEquipped();
    }
    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;
		int n = 0;
		foreach (const Card *c, cards) {
			if(Self->getPileName(c->getId())=="nospower")
				n++;
			else
				n--;
		}
        if (n!=0) return nullptr;
        NosYexinCard *card = new NosYexinCard;
        card->addSubcards(cards);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const{
        return !player->getPile("nospower").isEmpty() && !player->hasUsed("NosYexinCard");
    }
};

class NosYexin: public TriggerSkill {
public:
    NosYexin(): TriggerSkill("nosyexin") {
        events << Damage << Damaged;
        view_as_skill = new NosYexinViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhonghui, QVariant &) const
	{
        if (!zhonghui->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName(), 1);
        zhonghui->addToPile("nospower", room->drawCard());
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
	{
        return 2;
    }
};

NosPaiyiCard::NosPaiyiCard()
{
    will_throw = false;
	mute = true;
    handling_method = Card::MethodNone;
}

bool NosPaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return !targets.isEmpty();
}

void NosPaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
    const Card *c = Sanguosha->getCard(getEffectiveId());
	foreach (ServerPlayer *p, tos) {
        int index = 1;
        if (p != source) index++;
        source->peiyin("nospaiyi", index);
		QString choice = "hand_area";
		if(c->isKindOf("EquipCard")||c->isKindOf("DelayedTrick")){
			if (!source->isProhibited(p, c)){
				if (c->isKindOf("EquipCard")){
					if(!p->getEquip(qobject_cast<const EquipCard *>(c->getRealCard())->location()))
						choice = "hand_area+equip_area";
				}else
					choice = "hand_area+judge_area";
			}
		}
		choice = room->askForChoice(source,"nospaiyi",choice,QVariant::fromValue(p));
		if(choice=="equip_area")
			room->moveCardTo(c,p,Player::PlaceEquip);
		else if(choice=="judge_area")
			room->moveCardTo(c,p,Player::PlaceDelayedTrick);
		else
			p->obtainCard(c);
		if(index>1)
			room->drawCards(source, 1, "nospaiyi");
	}
}

class NosPaiyiViewAsSkill: public ViewAsSkill {
public:
    NosPaiyiViewAsSkill(): ViewAsSkill("nospaiyi")
	{
		expand_pile = "nospower";
		response_pattern == "@@nospaiyi";
    }
    bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
    {
        return cards.isEmpty()&&Self->getPileName(to_select->getId())=="nospower";
    }
    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        NosPaiyiCard *card = new NosPaiyiCard;
        card->addSubcards(cards);
        return card;
    }
    bool isEnabledAtPlay(const Player *) const{
        return false;
    }
};

class NosPaiyi: public PhaseChangeSkill {
public:
	NosPaiyi(): PhaseChangeSkill("nospaiyi") {
		view_as_skill = new NosPaiyiViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *zhonghui,Room *room) const
	{
        if (zhonghui->getPhase() != Player::Finish||zhonghui->getPile("nospower").isEmpty())
            return false;
		room->askForUseCard(zhonghui, "@@nospaiyi","nospaiyi0");
        return false;
    }
};

class NosZili: public PhaseChangeSkill {
public:
    NosZili(): PhaseChangeSkill("noszili") {
        frequency = Wake;
		waked_skills = "nospaiyi";
    }

    bool triggerable(const ServerPlayer *target) const
	{
        return PhaseChangeSkill::triggerable(target) && target->getMark("noszili")<1
		&& target->getPhase() == Player::Start;
    }

    bool onPhaseChange(ServerPlayer *zhonghui,Room *room) const
	{
        if(zhonghui->getPile("nospower").length()>=4){
			LogMessage log;
			log.type = "#NosZiliWake";
			log.from = zhonghui;
			log.arg = QString::number(zhonghui->getPile("nospower").length());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!zhonghui->canWake(objectName()))
			return false;
		room->sendCompulsoryTriggerLog(zhonghui, this);
        room->doSuperLightbox(zhonghui, objectName());
        //room->doLightbox("$NosZiliAnimate", 5000);

        room->setPlayerMark(zhonghui, "noszili", 1);
        if (room->changeMaxHpForAwakenSkill(zhonghui,-1,objectName()))
            room->acquireSkill(zhonghui, "nospaiyi");
        return false;
    }
};

YJCMPackage::YJCMPackage()
    : Package("YJCM")
{
    General *caozhi = new General(this, "caozhi", "wei", 3); // YJ 001
    caozhi->addSkill(new Luoying);
    caozhi->addSkill(new Jiushi);
    caozhi->addSkill(new JiushiFlip);
    related_skills.insertMulti("jiushi", "#jiushi-flip");

    General *ol_caozhi = new General(this, "ol_caozhi", "wei", 3);
    ol_caozhi->addSkill(new OLLuoying);
    ol_caozhi->addSkill("jiushi");

    General *chengong = new General(this, "chengong", "qun", 3); // YJ 002
    chengong->addSkill(new Zhichi);
    chengong->addSkill(new ZhichiProtect);
    chengong->addSkill(new ZhichiClear);
    chengong->addSkill(new Mingce);
    related_skills.insertMulti("zhichi", "#zhichi-protect");
    related_skills.insertMulti("zhichi", "#zhichi-clear");

    General *nos_fazheng = new General(this, "nos_fazheng", "shu", 3);
    nos_fazheng->addSkill(new NosEnyuan);
    nos_fazheng->addSkill(new NosXuanhuo);

    General *fazheng = new General(this, "fazheng", "shu", 3); // YJ 003
    fazheng->addSkill(new Enyuan);
    fazheng->addSkill(new Xuanhuo);

    /*General *ol_fazheng = new General(this, "ol_fazheng", "shu", 3, true);
    ol_fazheng->addSkill("enyuan");
    ol_fazheng->addSkill("xuanhuo");*/

    General *gaoshun = new General(this, "gaoshun", "qun"); // YJ 004
    gaoshun->addSkill(new Xianzhen);
    gaoshun->addSkill(new Jinjiu);
    gaoshun->addSkill(new XianzhenTargetMod);
    related_skills.insertMulti("xianzhen", "#xianzhen_target");

    General *nos_lingtong = new General(this, "nos_lingtong", "wu");
    nos_lingtong->addSkill(new NosXuanfeng);
    nos_lingtong->addSkill(new SlashNoDistanceLimitSkill("nosxuanfeng"));
    related_skills.insertMulti("nosxuanfeng", "#nosxuanfeng-slash-ndl");
    General *lingtong = new General(this, "lingtong", "wu"); // YJ 005
    lingtong->addSkill(new Xuanfeng);

    General *masu = new General(this, "masu", "shu", 3); // YJ 006
    masu->addSkill(new Xinzhan);
    masu->addSkill(new Huilei);

    General *ol_masu = new General(this, "ol_masu", "shu", 3);
    ol_masu->addSkill(new OLSanyao);
    ol_masu->addSkill(new OLZhiman);
    addMetaObject<SanyaoCard>();

    General *mobile_masu = new General(this, "mobile_masu", "shu", 3);
    mobile_masu->addSkill(new Sanyao);
    mobile_masu->addSkill(new Zhiman);

    General *wuguotai = new General(this, "wuguotai", "wu", 3, false); // YJ 007
    wuguotai->addSkill(new Ganlu);
    wuguotai->addSkill(new Buyi);

    General *nos_xushu = new General(this, "nos_xushu", "shu", 3);
    nos_xushu->addSkill(new NosWuyan);
    nos_xushu->addSkill(new NosJujian);

    General *xusheng = new General(this, "xusheng", "wu"); // YJ 008
    xusheng->addSkill(new Pojun);

    General *ol_xusheng = new General(this, "ol_xusheng", "wu");
    ol_xusheng->addSkill(new OlPojun);

    General *xushu = new General(this, "xushu", "shu", 3); // YJ 009
    xushu->addSkill(new Wuyan);
    xushu->addSkill(new Jujian);

    /*General *ol_xushu = new General(this, "ol_xushu", "shu", 3);
    ol_xushu->addSkill("wuyan");
    ol_xushu->addSkill("jujian");*/

    General *yujin = new General(this, "yujin", "wei"); // YJ 010
    yujin->addSkill(new Yizhong);

    General *ol_yujin = new General(this, "ol_yujin", "wei");
    ol_yujin->addSkill(new Jieyue);
    addMetaObject<JieyueCard>();

    General *nos_zhangchunhua = new General(this, "nos_zhangchunhua", "wei", 3, false);
    nos_zhangchunhua->addSkill("jueqing");
    nos_zhangchunhua->addSkill(new NosShangshi);

    addMetaObject<NosXuanhuoCard>();
    addMetaObject<NosJujianCard>();

    General *zhangchunhua = new General(this, "zhangchunhua", "wei", 3, false); // YJ 011
    zhangchunhua->addSkill(new Jueqing);
    zhangchunhua->addSkill(new Shangshi);

    General *nos_zhonghui = new General(this, "nos_zhonghui", "wei", 3, true);
    nos_zhonghui->addSkill(new NosZhenggong);
    nos_zhonghui->addSkill(new NosQuanji);
    nos_zhonghui->addSkill(new NosBaijiang);
    nos_zhonghui->addSkill(new NosZili);

    addMetaObject<NosYexinCard>();
    addMetaObject<NosPaiyiCard>();

    General *zhonghui = new General(this, "zhonghui", "wei"); // YJ 012
    zhonghui->addSkill(new Quanji);
    zhonghui->addSkill(new QuanjiKeep);
    zhonghui->addSkill(new Zili);
    zhonghui->addRelateSkill("paiyi");
    related_skills.insertMulti("quanji", "#quanji");

    addMetaObject<MingceCard>();
    addMetaObject<GanluCard>();
    addMetaObject<XianzhenCard>();
    addMetaObject<XinzhanCard>();
    addMetaObject<JujianCard>();
    addMetaObject<PaiyiCard>();
    addMetaObject<OLSanyaoCard>();

    skills << new Paiyi << new NosPaiyi << new NosYexin;
}

ADD_PACKAGE(YJCM)