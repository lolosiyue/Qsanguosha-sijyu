#include "yjcm2012.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "exppattern.h"
#include "room.h"
#include "roomthread.h"

class Zhenlie : public TriggerSkill
{
public:
    Zhenlie() : TriggerSkill("zhenlie")
    {
        events << TargetConfirmed;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") || use.card->isNDTrick()) {
				if (use.to.contains(player)&&use.from!=player&&player->askForSkillInvoke(this, data)) {
					int index = qrand() % 2 + 1;
					if (player->isJieGeneral())
						index += 4;
					else if (player->getGeneralName() == "second_wangyi" || player->getGeneral2Name() == "second_wangyi")
						index += 2;

					room->broadcastSkillInvoke(objectName(), index);
					player->setFlags("ZhenlieTarget");
					room->loseHp(HpLostStruct(player, 1, "zhenlie", player));
					if (player->isAlive() && player->hasFlag("ZhenlieTarget")) {
						use.nullified_list << player->objectName();
						data = QVariant::fromValue(use);
						if (player->canDiscard(use.from, "he")) {
							int id = room->askForCardChosen(player, use.from, "he", objectName(), false, Card::MethodDiscard);
							room->throwCard(id, use.from, player);
						}
					}
					player->setFlags("-ZhenlieTarget");
                }
            }
        }
        return false;
    }
};

class Miji : public TriggerSkill
{
public:
    Miji() : TriggerSkill("miji")
    {
        events << EventPhaseStart << ChoiceMade;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        if (TriggerSkill::triggerable(target) && triggerEvent == EventPhaseStart
            && target->getPhase() == Player::Finish && target->isWounded() && target->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());
            QStringList draw_num;
            for (int i = 1; i <= target->getLostHp(); draw_num << QString::number(i++)){}
            int num = room->askForChoice(target, "miji_draw", draw_num.join("+")).toInt();
            target->drawCards(num, objectName());
			QList<int> ids = target->handCards();
			target->assignmentCards(ids,objectName(),room->getOtherPlayers(target),num,num);
        } else if (triggerEvent == ChoiceMade) {
            QString str = data.toString();
            if (str.startsWith("Yiji:" + objectName()))
                target->addMark(objectName(), str.split(":").last().split("+").length());
        }
        return false;
    }
};

QiceCard::QiceCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool QiceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetFilter(targets, to_select, Self);
}

bool QiceCard::targetFixed() const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetFixed();
}

bool QiceCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetsFeasible(targets, Self);
}

const Card *QiceCard::validate(CardUseStruct &) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    use_card->addSubcards(subcards);
    use_card->setSkillName("qice");
    use_card->deleteLater();
    return use_card;
}

class Qice : public ViewAsSkill
{
public:
    Qice() : ViewAsSkill("qice")
    {
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("qice", false);
    }

    bool viewFilter(const QList<const Card *> &, const Card *) const
    {
        return false;
    }

    const Card *viewAs(const QList<const Card *> &) const
    {
        const Card *c = Self->getTag("qice").value<const Card *>();
        if (c) {
            QiceCard *card = new QiceCard;
            card->setUserString(c->objectName());
            card->addSubcards(Self->getHandcards());
            return card;
        }
		return nullptr;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->isKongcheng())
            return false;
        else {
            int n = player->getMark("SkillDescriptionArg1_qice");
			if (n <= 1) return !player->hasUsed("QiceCard");
            else return player->usedTimes("QiceCard") < n;
        }
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->isJieGeneral())
            index += 2;
        return index;
    }
};

class Zhiyu : public MasochismSkill
{
public:
    Zhiyu() : MasochismSkill("zhiyu")
    {
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        if (target->askForSkillInvoke(this, QVariant::fromValue(damage))) {
            target->drawCards(1, objectName());

            Room *room = target->getRoom();
            room->broadcastSkillInvoke(objectName());

            if (target->isKongcheng())
                return;
            room->showAllCards(target);

            QList<const Card *> cards = target->getHandcards();
            Card::Color color = cards.first()->getColor();
            foreach (const Card *card, cards) {
                if (card->getColor() != color)
                    return;
            }

            if (damage.from && damage.from->canDiscard(damage.from, "h"))
                room->askForDiscard(damage.from, objectName(), 1, 1);
        }
    }
};

class Jiangchi : public DrawCardsSkill
{
public:
    Jiangchi() : DrawCardsSkill("jiangchi")
    {
    }

    int getDrawNum(ServerPlayer *caozhang, int n) const
    {
        Room *room = caozhang->getRoom();
        QString choice = room->askForChoice(caozhang, objectName(), "jiang+chi+cancel");
        if (choice == "cancel")
            return n;

        LogMessage log;
        log.from = caozhang;
        log.arg = objectName();
        if (choice == "jiang") {
            log.type = "#Jiangchi1";
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName(), 1);
			room->notifySkillInvoked(caozhang, objectName());
            room->setPlayerCardLimitation(caozhang, "use,response", "Slash", true);
            return n + 1;
        } else {
            log.type = "#Jiangchi2";
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName(), 2);
			room->notifySkillInvoked(caozhang, objectName());
            room->setPlayerFlag(caozhang, "JiangchiInvoke");
            return n - 1;
        }
    }
};

class JiangchiTargetMod : public TargetModSkill
{
public:
    JiangchiTargetMod() : TargetModSkill("#jiangchi-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("JiangchiInvoke"))
            return 1;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("JiangchiInvoke"))
            return 1000;
        return 0;
    }
};

class Qianxi : public TriggerSkill
{
public:
    Qianxi() : TriggerSkill("qianxi")
    {
        events << EventPhaseStart << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && TriggerSkill::triggerable(target)
            && target->getPhase() == Player::Start) {
            if (room->askForSkillInvoke(target, objectName())) {
                room->broadcastSkillInvoke(objectName());
                JudgeStruct judge;
                judge.reason = objectName();
                judge.play_animation = false;
                judge.who = target;

                room->judge(judge);
                if (!target->isAlive()) return false;
                QString color = judge.pattern;
                QList<ServerPlayer *> to_choose;
                foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                    if (target->distanceTo(p) == 1)
                        to_choose << p;
                }
                if (to_choose.isEmpty())
                    return false;

                ServerPlayer *victim = room->askForPlayerChosen(target, to_choose, objectName());
                QString pattern = QString(".|%1|.|hand$0").arg(color);

                room->setPlayerFlag(victim, "QianxiTarget");
                room->addPlayerMark(victim, QString("@qianxi_%1").arg(color));
                room->setPlayerCardLimitation(victim, "use,response", pattern, false);

                LogMessage log;
                log.type = "#Qianxi";
                log.from = victim;
                log.arg = QString("no_suit_%1").arg(color);
                room->sendLog(log);
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason != objectName() || !target->isAlive()) return false;

            QString color = judge->card->isRed() ? "red" : "black";
            target->setTag(objectName(), QVariant::fromValue(color));
            judge->pattern = color;
        }
        return false;
    }
};

class QianxiClear : public TriggerSkill
{
public:
    QianxiClear() : TriggerSkill("#qianxi-clear")
    {
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return !target->getTag("qianxi").toString().isEmpty();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player)
                return false;
        }

        QString color = player->getTag("qianxi").toString();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->hasFlag("QianxiTarget")) {
                room->removePlayerCardLimitation(p, "use,response", QString(".|%1|.|hand$0").arg(color));
                room->setPlayerMark(p, QString("@qianxi_%1").arg(color), 0);
            }
        }
        return false;
    }
};

class Dangxian : public TriggerSkill
{
public:
    Dangxian() : TriggerSkill("dangxian")
    {
        frequency = Compulsory;
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liaohua, QVariant &) const
    {
        if (liaohua->getPhase() == Player::RoundStart) {
            int index = 1;
            if (liaohua->getGeneralName().contains("guansuo") || (!liaohua->getGeneralName().contains("liaohua") && liaohua->getGeneral2Name().contains("guansuo")))
                index = 2;
            room->sendCompulsoryTriggerLog(liaohua, this, index);

            liaohua->insertPhase(Player::Play);/*
            room->broadcastProperty(liaohua, "phase");
            RoomThread *thread = room->getThread();
            if (!thread->trigger(EventPhaseStart, room, liaohua))
                thread->trigger(EventPhaseProceeding, room, liaohua);
            thread->trigger(EventPhaseEnd, room, liaohua);

            liaohua->setPhase(Player::RoundStart);
            room->broadcastProperty(liaohua, "phase");*/
        }
        return false;
    }
};

class Fuli : public TriggerSkill
{
public:
    Fuli() : TriggerSkill("fuli")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@laoji";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && target->getMark("@laoji") > 0;
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
        if (dying_data.who != liaohua)
            return false;
        if (liaohua->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName());
            //room->doLightbox("$FuliAnimate", 3000);

            room->doSuperLightbox(liaohua, "fuli");

            room->removePlayerMark(liaohua, "@laoji");
            room->recover(liaohua, RecoverStruct(liaohua, nullptr, getKingdoms(room) - liaohua->getHp(), "fuli"));

            liaohua->turnOver();
        }
        return false;
    }
};

class Zishou : public DrawCardsSkill
{
public:
    Zishou() : DrawCardsSkill("zishou")
    {
    }

    int getDrawNum(ServerPlayer *liubiao, int n) const
    {
        Room *room = liubiao->getRoom();
        if (liubiao->isWounded() && room->askForSkillInvoke(liubiao, objectName())) {
            int losthp = liubiao->getLostHp();
            room->broadcastSkillInvoke(objectName(), qMin(3, losthp));
            liubiao->clearHistory();
            liubiao->skip(Player::Play);
            return n + losthp;
        } else
            return n;
    }
};

class Zongshi : public MaxCardsSkill
{
public:
    Zongshi() : MaxCardsSkill("zongshi")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill(objectName())){
			QSet<QString> kingdom_set;
            foreach(const Player *player, target->parent()->findChildren<const Player *>()) {
                if (player->isAlive()) kingdom_set << player->getKingdom();
            }
            return kingdom_set.size();
		}
        return 0;
    }
};

class NewZishou : public DrawCardsSkill
{
public:
    NewZishou() : DrawCardsSkill("newzishou")
    {

    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        if (player->askForSkillInvoke(this)) {
            Room *room = player->getRoom();
            room->broadcastSkillInvoke(objectName());

            room->setPlayerFlag(player, "newzishou");

            QSet<QString> kingdomSet;
            foreach(ServerPlayer *p, room->getAlivePlayers())
                kingdomSet.insert(p->getKingdom());

            return n + kingdomSet.count();
        }

        return n;
    }
};

class NewZishouProhibit : public ProhibitSkill
{
public:
    NewZishouProhibit() : ProhibitSkill("#newzishou")
    {

    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (card->isKindOf("SkillCard"))
            return false;
        return from != to && from->hasFlag("newzishou");
    }
};


class Shiyong : public TriggerSkill
{
public:
    Shiyong() : TriggerSkill("shiyong")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")
            && (damage.card->isRed() || damage.card->hasFlag("drank"))) {
            int index = 1;
            if (damage.from->getGeneralName().contains("guanyu"))
                index = 3;
            else if (damage.card->hasFlag("drank"))
                index = 2;
            room->broadcastSkillInvoke(objectName(), index);
            room->sendCompulsoryTriggerLog(player, objectName());

            room->loseMaxHp(player, 1, objectName());
        }
        return false;
    }
};

class FuhunViewAsSkill : public ViewAsSkill
{
public:
    FuhunViewAsSkill() : ViewAsSkill("fuhun")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getHandcardNum() >= 2 && Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return player->getHandcardNum() >= 2 && (pattern.contains("slash") || pattern.contains("Slash"));
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        Slash *slash = new Slash(Card::SuitToBeDecided, 0);
        slash->setSkillName(objectName());
        slash->addSubcards(cards);

        return slash;
    }
};

class Fuhun : public TriggerSkill
{
public:
    Fuhun() : TriggerSkill("fuhun")
    {
        events << Damage << EventPhaseChanging;
        view_as_skill = new FuhunViewAsSkill;
		waked_skills = "wusheng,paoxiao";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage && TriggerSkill::triggerable(player)) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && damage.card->getSkillNames().contains(objectName())
                && player->getPhase() == Player::Play) {
                room->handleAcquireDetachSkills(player, "wusheng|paoxiao");
                room->broadcastSkillInvoke(objectName(), 2);
                player->setFlags(objectName());
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive && player->hasFlag(objectName()))
                room->handleAcquireDetachSkills(player, "-wusheng|-paoxiao", true);
        }

        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 1;
    }
};

GongqiCard::GongqiCard()
{
    mute = true;
    target_fixed = true;
}

void GongqiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->setPlayerFlag(source, "InfinityAttackRange");
    const Card *cd = Sanguosha->getCard(subcards.first());
    if (cd->isKindOf("EquipCard")) {
        room->broadcastSkillInvoke("gongqi", 2);
        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getOtherPlayers(source))
            if (source->canDiscard(p, "he")) targets << p;
        if (!targets.isEmpty()) {
            ServerPlayer *to_discard = room->askForPlayerChosen(source, targets, "gongqi", "@gongqi-discard", true);
            if (to_discard)
                room->throwCard(room->askForCardChosen(source, to_discard, "he", "gongqi", false, Card::MethodDiscard), to_discard, source);
        }
    } else {
        room->broadcastSkillInvoke("gongqi", 1);
    }
}

class Gongqi : public OneCardViewAsSkill
{
public:
    Gongqi() : OneCardViewAsSkill("gongqi")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("GongqiCard");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        GongqiCard *card = new GongqiCard;
        card->addSubcard(originalcard->getId());
        card->setSkillName(objectName());
        return card;
    }
};

JiefanCard::JiefanCard()
{
    mute = true;
}

bool JiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void JiefanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(source, "@rescue");
    ServerPlayer *target = targets.first();
    source->setTag("JiefanTarget", QVariant::fromValue(target));
    room->broadcastSkillInvoke("jiefan");
    //room->doLightbox("$JiefanAnimate", 2500);
    room->doSuperLightbox(source, "jiefan");

    foreach (ServerPlayer *player, room->getAllPlayers()) {
        if (player->isAlive() && player->inMyAttackRange(target))
            room->cardEffect(this, source, player);
    }
    source->removeTag("JiefanTarget");
}

void JiefanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    ServerPlayer *target = effect.from->getTag("JiefanTarget").value<ServerPlayer *>();
    QVariant data = effect.from->getTag("JiefanTarget");
    if (target && !room->askForCard(effect.to, ".Weapon", "@jiefan-discard::" + target->objectName(), data))
        target->drawCards(1, "jiefan");
}

class Jiefan : public ZeroCardViewAsSkill
{
public:
    Jiefan() : ZeroCardViewAsSkill("jiefan")
    {
        frequency = Limited;
        limit_mark = "@rescue";
    }

    const Card *viewAs() const
    {
        return new JiefanCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@rescue") >= 1;
    }
};

AnxuCard::AnxuCard()
{
    mute = true;
}

bool AnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;
    if (targets.isEmpty())
        return true;
    else if (targets.length() == 1)
        return to_select->getHandcardNum() != targets.first()->getHandcardNum();
    else
        return false;
}

bool AnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void AnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QList<ServerPlayer *> selecteds = targets;
    ServerPlayer *from = selecteds.first()->getHandcardNum() < selecteds.last()->getHandcardNum() ? selecteds.takeFirst() : selecteds.takeLast();
    ServerPlayer *to = selecteds.takeFirst();
    if (from->getGeneralName().contains("sunquan"))
        room->broadcastSkillInvoke("anxu", 2);
    else
        room->broadcastSkillInvoke("anxu", 1);
    int id = room->askForCardChosen(from, to, "h", "anxu");
    const Card *cd = Sanguosha->getCard(id);
    from->obtainCard(cd);
    room->showCard(from, id);
    if (cd->getSuit() != Card::Spade)
        source->drawCards(1, "anxu");
}

class Anxu : public ZeroCardViewAsSkill
{
public:
    Anxu() : ZeroCardViewAsSkill("anxu")
    {
    }

    const Card *viewAs() const
    {
        return new AnxuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("AnxuCard");
    }
};

class Zhuiyi : public TriggerSkill
{
public:
    Zhuiyi() : TriggerSkill("zhuiyi")
    {
        events << Death;
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
        QList<ServerPlayer *> targets = (death.damage && death.damage->from) ? room->getOtherPlayers(death.damage->from) :
            room->getAlivePlayers();

        if (targets.isEmpty())
            return false;

        QString prompt = "zhuiyi-invoke";
        if (death.damage && death.damage->from && death.damage->from != player)
            prompt = QString("%1x:%2").arg(prompt).arg(death.damage->from->objectName());
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), prompt, true, true);
        if (!target) return false;

        if (target->getGeneralName().contains("sunquan"))
            room->broadcastSkillInvoke(objectName(), 2);
        else
            room->broadcastSkillInvoke(objectName(), 1);

        target->drawCards(3, objectName());
        room->recover(target, RecoverStruct("zhuiyi", player), true);
        return false;
    }
};

class LihuoViewAsSkill : public OneCardViewAsSkill
{
public:
    LihuoViewAsSkill() : OneCardViewAsSkill("lihuo")
    {
        filter_pattern = "%slash";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (pattern.contains("slash") || pattern.contains("Slash"));
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *acard = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->setSkillName(objectName());
        acard->addSubcard(originalCard);
        return acard;
    }
};

class Lihuo : public TriggerSkill
{
public:
    Lihuo() : TriggerSkill("lihuo")
    {
        events << DamageDone << CardFinished << ChangeSlash;
        view_as_skill = new LihuoViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && damage.card->getSkillNames().contains(objectName())) {
                QVariantList slash_list = damage.from->getTag("InvokeLihuo").toList();
                slash_list << QVariant::fromValue(damage.card);
                damage.from->setTag("InvokeLihuo", slash_list);
            }
        } else if (triggerEvent == ChangeSlash) {
            if (!TriggerSkill::triggerable(player)) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->objectName() != "slash") return false;
            bool has_changed = false;
            QString skill_name = use.card->getSkillName();
            if (!skill_name.isEmpty()) {
                const Skill *skill = Sanguosha->getSkill(skill_name);
                if (skill && !skill->inherits("FilterSkill") && !skill->objectName().contains("guhuo"))
                    has_changed = true;
            }
            if (!has_changed || (use.card->isVirtualCard() && use.card->subcardsLength() == 0)) {
                FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
                if (use.card->isVirtualCard())
                    fire_slash->addSubcards(use.card->getSubcards());
				else
                    fire_slash->addSubcard(use.card);
                fire_slash->setSkillName("lihuo");
                bool can_use = true;
                foreach (ServerPlayer *p, use.to) {
                    if (!player->canSlash(p, fire_slash, false)) {
                        can_use = false;
                        break;
                    }
                }
                if (can_use && room->askForSkillInvoke(player, "lihuo", data, false)) {
                    //room->broadcastSkillInvoke("lihuo");
                    use.changeCard(fire_slash);
                    data = QVariant::fromValue(use);
                }
            }
        } else if (TriggerSkill::triggerable(player) && !player->hasFlag("Global_ProcessBroken")) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash"))
                return false;

            bool can_invoke = false;
            QVariantList slash_list = use.from->getTag("InvokeLihuo").toList();
            foreach (QVariant card, slash_list) {
                if (card.value<const Card *>() == use.card) {
                    can_invoke = true;
                    slash_list.removeOne(card);
                    use.from->setTag("InvokeLihuo", QVariant::fromValue(slash_list));
                    break;
                }
            }
            if (!can_invoke) return false;

            int n = 2;
            if (player->isJieGeneral())
                n = 4;
            room->broadcastSkillInvoke("lihuo", n);
            room->sendCompulsoryTriggerLog(player, objectName());
            room->loseHp(HpLostStruct(player, 1, "lihuo", player));
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int n = 1;
        if (player->isJieGeneral())
            n = 3;
        return n;
    }
};

class LihuoTargetMod : public TargetModSkill
{
public:
    LihuoTargetMod() : TargetModSkill("#lihuo-target")
    {
        frequency = NotFrequent;
    }

    int getExtraTargetNum(const Player *from, const Card *card) const
    {
        if (card->isKindOf("FireSlash")&&from->hasSkill("lihuo"))
            return 1;
        return 0;
    }
};

ChunlaoCard::ChunlaoCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void ChunlaoCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("wine", this);
}

ChunlaoWineCard::ChunlaoWineCard()
{
    m_skillName = "chunlao";
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ChunlaoWineCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const
{
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    if (subcards.length() != 0) {
        room->throwCard(subcards, "chunlao", nullptr);
        Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
        analeptic->setSkillName("_chunlao");
		analeptic->deleteLater();
        room->useCard(CardUseStruct(analeptic, who, who, false));
    }
}

class ChunlaoViewAsSkill : public ViewAsSkill
{
public:
    ChunlaoViewAsSkill() : ViewAsSkill("chunlao")
    {
        expand_pile = "wine";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "@@chunlao"
            || (pattern.contains("peach") && !player->getPile("wine").isEmpty());
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@chunlao")
            return to_select->isKindOf("Slash");
        else {
            ExpPattern pattern(".|.|.|wine");
            if (!pattern.match(Self, to_select)) return false;
            return selected.length() == 0;
        }
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@chunlao") {
            if (cards.length() == 0) return nullptr;

            Card *acard = new ChunlaoCard;
            acard->addSubcards(cards);
            acard->setSkillName(objectName());
            return acard;
        } else {
            if (cards.length() != 1) return nullptr;
            Card *wine = new ChunlaoWineCard;
            wine->addSubcards(cards);
            wine->setSkillName(objectName());
            return wine;
        }
    }
};

class Chunlao : public TriggerSkill
{
public:
    Chunlao() : TriggerSkill("chunlao")
    {
        events << EventPhaseStart;
        view_as_skill = new ChunlaoViewAsSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *chengpu, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart && chengpu->getPhase() == Player::Finish
            && !chengpu->isKongcheng() && chengpu->getPile("wine").isEmpty()) {
            room->askForUseCard(chengpu, "@@chunlao", "@chunlao", -1, Card::MethodNone);
        }
        return false;
    }/*

    int getEffectIndex(const ServerPlayer *player, const Card *card) const
    {
        if (card->isKindOf("Analeptic")) {
            if (player->getGeneralName().contains("zhouyu"))
                return 3;
            else
                return 2;
        } else
            return 1;
    }*/
};

class OLZishou : public TriggerSkill
{
public:
    OLZishou() : TriggerSkill("olzishou")
    {
        events << DrawNCards << DamageCaused;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
			QSet<QString> kingdomSet;
            foreach(ServerPlayer *p, room->getAlivePlayers())
                kingdomSet.insert(p->getKingdom());

            int n = kingdomSet.count();
            if (!player->askForSkillInvoke(this, QString("olzishou:" + QString::number(n)))) return false;
            room->broadcastSkillInvoke(objectName());
            room->setPlayerFlag(player, "olzishou");
            draw.num += n;
			data = QVariant::fromValue(draw);
        } else if(player->hasFlag(objectName())){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->isAlive() && damage.to != player) {
                LogMessage log;
                log.type = "#OLzishouPrevent";
                log.from = player;
                log.to << damage.to;
                log.arg = objectName();
                log.arg2 = QString::number(damage.damage);
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(player, objectName());
                return true;
            }
        }
        return false;
    }
};

class SecondMiji : public PhaseChangeSkill
{
public:
    SecondMiji() : PhaseChangeSkill("secondmiji")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish || player->getLostHp() <= 0) return false;
        if (!player->askForSkillInvoke(this)) return false;

        int index = qrand() % 2 + 1;
        if (player->isJieGeneral())
            index += 2;
        room->broadcastSkillInvoke(this, index);

        int lost = player->getLostHp();
        player->drawCards(lost, objectName());

        if (player->isAlive() && !player->isKongcheng()){
			QList<int> ids = player->handCards();
			player->assignmentCards(ids,objectName(),room->getOtherPlayers(player),lost,-1);
		}
        return false;
    }
};

class NosFuhun : public TriggerSkill
{
public:
    NosFuhun() : TriggerSkill("nosfuhun")
    {
        events << EventPhaseStart << EventPhaseChanging;
		waked_skills = "wusheng,paoxiao";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *shuangying, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && shuangying->getPhase() == Player::Draw && TriggerSkill::triggerable(shuangying)) {
            if (shuangying->askForSkillInvoke(this)) {
                int card1 = room->drawCard();
                int card2 = room->drawCard();
                CardsMoveStruct move;
                move.card_ids << card1 << card2;
                bool diff = (Sanguosha->getCard(card1)->getColor() != Sanguosha->getCard(card2)->getColor());

                move.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, shuangying->objectName(), "fuhun", "");
                move.to_place = Player::PlaceTable;
                room->moveCardsAtomic(move, true);
                room->getThread()->delay();

                DummyCard *dummy = new DummyCard(move.card_ids);
                room->obtainCard(shuangying, dummy);
                delete dummy;

                if (diff) {
                    room->handleAcquireDetachSkills(shuangying, "wusheng|paoxiao");
                    room->broadcastSkillInvoke(objectName(), qrand() % 2 + 1);
                    shuangying->setFlags(objectName());
                } else
                    room->broadcastSkillInvoke(objectName(), 3);

                return true;
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive && shuangying->hasFlag(objectName()))
                room->handleAcquireDetachSkills(shuangying, "-wusheng|-paoxiao", true);
        }

        return false;
    }
};

class NosGongqi : public OneCardViewAsSkill
{
public:
    NosGongqi() : OneCardViewAsSkill("nosgongqi")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return (pattern.contains("slash") || pattern.contains("Slash"));
    }

    bool viewFilter(const Card *to_select) const
    {
        if (to_select->getTypeId() != Card::TypeEquip)
            return false;

        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            Slash *slash = new Slash(Card::SuitToBeDecided, -1);
            slash->addSubcard(to_select->getEffectiveId());
            slash->deleteLater();
            return slash->isAvailable(Self);
        }
        return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }
};

class NosGongqiTargetMod : public TargetModSkill
{
public:
    NosGongqiTargetMod() : TargetModSkill("#nosgongqi-target")
    {
        frequency = NotFrequent;
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "nosgongqi")
            return 1000;
        return 0;
    }
};

NosJiefanCard::NosJiefanCard()
{
    target_fixed = true;
    //mute = true;
}

void NosJiefanCard::use(Room *room, ServerPlayer *handang, QList<ServerPlayer *> &) const
{
    ServerPlayer *current = room->getCurrent();
    if (!current || current->isDead()) return;
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    room->setTag("NosJiefanTarget", QVariant::fromValue(who));
    if (room->askForUseSlashTo(handang, current, "nosjiefan-slash:" + current->objectName(), false,false,false,nullptr,nullptr,"NosJiefanUsed"))
        return;
}

class NosJiefanViewAsSkill : public ZeroCardViewAsSkill
{
public:
    NosJiefanViewAsSkill() : ZeroCardViewAsSkill("nosjiefan")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (!pattern.contains("peach")) return false;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->hasFlag("CurrentPlayer"))
                return true;
        }
        return false;
    }

    const Card *viewAs() const
    {
        return new NosJiefanCard;
    }
};

class NosJiefan : public TriggerSkill
{
public:
    NosJiefan() : TriggerSkill("nosjiefan")
    {
        events << DamageCaused;
        view_as_skill = new NosJiefanViewAsSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *handang, QVariant &data) const
    {
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("NosJiefanUsed")) {
                LogMessage log2;
                log2.type = "#NosJiefanPrevent";
                log2.from = handang;
                log2.to << damage.to;
                room->sendLog(log2);

                ServerPlayer *target = room->getTag("NosJiefanTarget").value<ServerPlayer *>();
				Peach *peach = new Peach(Card::NoSuit, 0);
				peach->setSkillName("_nosjiefan");
				peach->setFlags("YUANBEN");
				if(target&&handang->canUse(peach,target))
					room->useCard(CardUseStruct(peach, handang, target));
				peach->deleteLater();
                return true;
            }
        }
        return false;
    }
};

class NosQianxi : public TriggerSkill
{
public:
    NosQianxi() : TriggerSkill("nosqianxi")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        if (player->distanceTo(damage.to) == 1 && damage.card && damage.card->isKindOf("Slash")
            && damage.by_user && !damage.chain && !damage.transfer && player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), 1);
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = false;
            judge.who = player;
            judge.reason = objectName();

            room->judge(judge);
            if (judge.isGood()) {
                room->broadcastSkillInvoke(objectName(), 2);
                room->loseMaxHp(damage.to, 1, "nosqianxi");
                return true;
            } else
                room->broadcastSkillInvoke(objectName(), 3);
        }
        return false;
    }
};

class NosZhenlie : public RetrialSkill
{
public:
    NosZhenlie() : RetrialSkill("noszhenlie")
    {

    }

    const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
    {
        if (judge->who != player)
            return nullptr;

        if (player->askForSkillInvoke(this, QVariant::fromValue(judge))) {
            Room *room = player->getRoom();
            int card_id = room->drawCard();
            room->broadcastSkillInvoke(objectName(), room->getCurrent() == player ? 2 : 1);
            room->getThread()->delay();
            return Sanguosha->getCard(card_id);
        }

        return nullptr;
    }
};

class NosMiji : public PhaseChangeSkill
{
public:
    NosMiji() : PhaseChangeSkill("nosmiji")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *wangyi, Room *room) const
    {
        if (!wangyi->isWounded())
            return false;
        if (wangyi->getPhase() == Player::Start || wangyi->getPhase() == Player::Finish) {
            if (!wangyi->askForSkillInvoke(this))
                return false;
            room->broadcastSkillInvoke(objectName(), 1);
            JudgeStruct judge;
            judge.pattern = ".|black";
            judge.good = true;
            judge.reason = objectName();
            judge.who = wangyi;

            room->judge(judge);

            if (judge.isGood() && wangyi->isAlive()) {
                QList<int> pile_ids = room->getNCards(wangyi->getLostHp());
                room->fillAG(pile_ids, wangyi);
                ServerPlayer *target = room->askForPlayerChosen(wangyi, room->getAllPlayers(), objectName());
                room->clearAG(wangyi);
                if (target == wangyi)
                    room->broadcastSkillInvoke(objectName(), 2);
                else if (target->getGeneralName().contains("machao"))
                    room->broadcastSkillInvoke(objectName(), 4);
                else
                    room->broadcastSkillInvoke(objectName(), 3);

                DummyCard *dummy = new DummyCard(pile_ids);
                wangyi->setFlags("Global_GongxinOperator");
                target->obtainCard(dummy, false);
                wangyi->setFlags("-Global_GongxinOperator");
                delete dummy;
            }
        }
        return false;
    }
};

OlAnxuCard::OlAnxuCard()
{
    //mute = true;
    will_throw = true;
    target_fixed = false;
}

bool OlAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;

    if (targets.isEmpty())
        return true;

    if (targets.length() == 1)
        return to_select->getHandcardNum() != targets.first()->getHandcardNum();

    return false;
}

bool OlAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void OlAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *playerA = targets.first();
    ServerPlayer *playerB = targets.last();
    if (playerA->getHandcardNum() < playerB->getHandcardNum()) {
        playerA = targets.last();
        playerB = targets.first();
    }
    if (playerA->isKongcheng())
        return;

    room->setPlayerFlag(playerB, "olanxu_target"); // For AI
    const Card *card = room->askForExchange(playerA, "olanxu", 1, 1, false, QString("@olanxu:%1:%2").arg(source->objectName()).arg(playerB->objectName()));
    room->setPlayerFlag(playerB, "-olanxu_target"); // For AI
    if (!card) card = playerA->getRandomHandCard();

    room->obtainCard(playerB, card);

    if (playerA->getHandcardNum() == playerB->getHandcardNum()) {
        QString choices = source->getLostHp() > 0 ? "draw+recover" : "draw";
        QString choice = room->askForChoice(source, "olanxu", choices);
        if (choice == "draw")
            room->drawCards(source, 1, "olanxu");
        else if (choice == "recover") {
            RecoverStruct recover;
            recover.recover = 1;
            recover.who = source;
            recover.reason = "olanxu";
            room->recover(source, recover);
        }
    }
}

class OlAnxu : public ZeroCardViewAsSkill
{
public:
    OlAnxu() : ZeroCardViewAsSkill("olanxu") {
    }

    const Card *viewAs() const
    {
        return new OlAnxuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("OlAnxuCard") && player->getSiblings().length() >= 2;
    }
};

class OlMiji : public TriggerSkill
{
public:
    OlMiji() : TriggerSkill("olmiji")
    {
        events << EventPhaseStart << ChoiceMade;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        if (TriggerSkill::triggerable(target) && triggerEvent == EventPhaseStart
            && target->getPhase() == Player::Finish && target->isWounded() && target->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());
            QStringList draw_num;
            for (int i = 1; i <= target->getLostHp(); draw_num << QString::number(i++)){}
            int num = room->askForChoice(target, "olmiji_draw", draw_num.join("+")).toInt();
            target->drawCards(num, objectName());
			QList<int> ids = target->handCards();
			target->assignmentCards(ids,objectName(),room->getOtherPlayers(target),num,-1);
        } else if (triggerEvent == ChoiceMade) {
            QString str = data.toString();
            if (str.startsWith("Yiji:" + objectName()))
                target->addMark(objectName(), str.split(":").last().split("+").length());
        }
        return false;
    }
};

class OlQianxi : public PhaseChangeSkill
{
public:
    OlQianxi() : PhaseChangeSkill("olqianxi")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() == Player::Start && target->askForSkillInvoke(this)) {

            room->broadcastSkillInvoke(objectName());

            target->drawCards(1, objectName());

            if (target->isNude())
                return false;

            const Card *c = room->askForCard(target, "..!", "@olqianxi");
            if (c == nullptr) {
                c = target->getCards("he").at(qrand() % target->getCardCount());
                room->throwCard(c, target);
            }

            if (target->isDead())
                return false;

            QString color;
            if (c->isBlack())
                color = "black";
            else if (c->isRed())
                color = "red";
            else
                return false;
            QList<ServerPlayer *> to_choose;
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (target->distanceTo(p) == 1)
                    to_choose << p;
            }
            if (to_choose.isEmpty())
                return false;

            ServerPlayer *victim = room->askForPlayerChosen(target, to_choose, objectName());
            QString pattern = QString(".|%1|.|hand$0").arg(color);
            target->setTag(objectName(), QVariant::fromValue(color));

            room->setPlayerFlag(victim, "OlQianxiTarget");
            room->addPlayerMark(victim, QString("@qianxi_%1").arg(color));
            room->setPlayerCardLimitation(victim, "use,response", pattern, false);

            LogMessage log;
            log.type = "#Qianxi";
            log.from = victim;
            log.arg = QString("no_suit_%1").arg(color);
            room->sendLog(log);
        }
        return false;
    }
};

class OlQianxiClear : public TriggerSkill
{
public:
    OlQianxiClear() : TriggerSkill("#olqianxi-clear")
    {
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return !target->getTag("olqianxi").toString().isEmpty();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player)
                return false;
        }

        QString color = player->getTag("olqianxi").toString();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->hasFlag("OlQianxiTarget")) {
                room->removePlayerCardLimitation(p, "use,response", QString(".|%1|.|hand$0").arg(color));
                room->setPlayerMark(p, QString("@qianxi_%1").arg(color), 0);
            }
        }
        return false;
    }
};

YJCM2012Package::YJCM2012Package()
    : Package("YJCM2012")
{
    General *bulianshi = new General(this, "bulianshi", "wu", 3, false); // YJ 101
    bulianshi->addSkill(new Anxu);
    bulianshi->addSkill(new Zhuiyi);

    General *ol_bulianshi = new General(this, "ol_bulianshi", "wu", 3, false);
    ol_bulianshi->addSkill(new OlAnxu);
    ol_bulianshi->addSkill("zhuiyi");
    addMetaObject<OlAnxuCard>();

    General *caozhang = new General(this, "caozhang", "wei"); // YJ 102
    caozhang->addSkill(new Jiangchi);
    caozhang->addSkill(new JiangchiTargetMod);
    related_skills.insertMulti("jiangchi", "#jiangchi-target");

    General *chengpu = new General(this, "chengpu", "wu"); // YJ 103
    chengpu->addSkill(new Lihuo);
    chengpu->addSkill(new LihuoTargetMod);
    chengpu->addSkill(new Chunlao);
    related_skills.insertMulti("lihuo", "#lihuo-target");

    General *nos_guanxingzhangbao = new General(this, "nos_guanxingzhangbao", "shu");
    nos_guanxingzhangbao->addSkill(new NosFuhun);

    General *guanxingzhangbao = new General(this, "guanxingzhangbao", "shu"); // YJ 104
    guanxingzhangbao->addSkill(new Fuhun);

    /*General *ol_guanxingzhangbao = new General(this, "ol_guanxingzhangbao", "shu", 4, true);
    ol_guanxingzhangbao->addSkill("fuhun");*/

    General *nos_handang = new General(this, "nos_handang", "wu");
    nos_handang->addSkill(new NosGongqi);
    nos_handang->addSkill(new NosGongqiTargetMod);
    nos_handang->addSkill(new NosJiefan);
    related_skills.insertMulti("nosgongqi", "#nosgongqi-target");
    addMetaObject<NosJiefanCard>();
    General *handang = new General(this, "handang", "wu"); // YJ 105
    handang->addSkill(new Gongqi);
    handang->addSkill(new Jiefan);

    General *huaxiong = new General(this, "huaxiong", "qun", 6); // YJ 106
    huaxiong->addSkill(new Shiyong);

    General *liaohua = new General(this, "liaohua", "shu"); // YJ 107
    liaohua->addSkill(new Dangxian);
    liaohua->addSkill(new Fuli);

    General *liubiao = new General(this, "liubiao", "qun", 4); // YJ 108
    liubiao->addSkill(new Zishou);
    liubiao->addSkill(new Zongshi);

    General *new_liubiao = new General(this, "new_liubiao", "qun", 3);
    new_liubiao->addSkill(new NewZishou);
    new_liubiao->addSkill(new NewZishouProhibit);
    new_liubiao->addSkill("zongshi");
    related_skills.insertMulti("newzishou", "#newzishou");

    General *ol_liubiao = new General(this, "ol_liubiao", "qun", 3);
    ol_liubiao->addSkill(new OLZishou);
    ol_liubiao->addSkill("zongshi");

    General *nos_madai = new General(this, "nos_madai", "shu");
    nos_madai->addSkill("mashu");
    nos_madai->addSkill(new NosQianxi);

    General *madai = new General(this, "madai", "shu"); // YJ 109
    madai->addSkill("mashu");
    madai->addSkill(new Qianxi);
    madai->addSkill(new QianxiClear);
    related_skills.insertMulti("qianxi", "#qianxi-clear");

    General *ol_madai = new General(this, "ol_madai", "shu");
    ol_madai->addSkill("mashu");
    ol_madai->addSkill(new OlQianxi);
    ol_madai->addSkill(new OlQianxiClear);
    related_skills.insertMulti("olqianxi", "#olqianxi-clear");

    General *nos_wangyi = new General(this, "nos_wangyi", "wei", 3, false);
    nos_wangyi->addSkill(new NosZhenlie);
    nos_wangyi->addSkill(new NosMiji);
    General *wangyi = new General(this, "wangyi", "wei", 3, false); // YJ 110
    wangyi->addSkill(new Zhenlie);
    wangyi->addSkill(new Miji);

    General *ol_wangyi = new General(this, "ol_wangyi", "wei", 3, false);
    ol_wangyi->addSkill("zhenlie");
    ol_wangyi->addSkill(new OlMiji);

    General *second_wangyi = new General(this, "second_wangyi", "wei", 3, false);
    second_wangyi->addSkill("zhenlie");
    second_wangyi->addSkill(new SecondMiji);

    General *xunyou = new General(this, "xunyou", "wei", 3); // YJ 111
    xunyou->addSkill(new Qice);
    xunyou->addSkill(new Zhiyu);

    addMetaObject<QiceCard>();
    addMetaObject<ChunlaoCard>();
    addMetaObject<ChunlaoWineCard>();
    addMetaObject<GongqiCard>();
    addMetaObject<JiefanCard>();
    addMetaObject<AnxuCard>();
}

ADD_PACKAGE(YJCM2012)

