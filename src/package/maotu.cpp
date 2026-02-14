#include "maotu.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "clientstruct.h"
#include "mobile.h"
#include "wind.h"

class MTLiaoshi : public TriggerSkill
{
public:
    MTLiaoshi() :TriggerSkill("mtliaoshi")
    {
        events << MarkChanged << CardsMoveOneTime << HpChanged;
        waked_skills = "#mtliaoshi";
    }

    static void DOSTH(ServerPlayer *player, QString choice, bool change_num, QString reason)
    {
        Room *room = player->getRoom();
        int num = player->getMark("&mtliaoshi_num");

        if (choice == "discard") {
            num++;
            if (num > 8) num = 1;
            if (player->canDiscard(player, "he"))
                room->askForDiscard(player, reason, 2, 2, false, true); //objectName()会报错
        } else if (choice == "lose") {
            num++;
            if (num > 8) num = 1;
            room->loseHp(HpLostStruct(player, 1, reason, player));
        } else if (choice == "draw") {
            num--;
            if (num < 1) num = 8;
            player->drawCards(2, reason);
        } else {
            num--;
            if (num < 1) num = 8;
            room->recover(player, RecoverStruct("mtliaoshi", player));
        }
        if (change_num && player->isAlive()) {  //复活后数字应该也变化了，这里为了ui，偷懒
            room->setPlayerMark(player, "&mtliaoshi_num", num);
            QVariant data = "mtliaoshi_choice_" + choice;
            room->getThread()->trigger(EventForDiy, room, player, data);
        }
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == MarkChanged) {
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name != "&mtliaoshi_num")
				return false;
			int num = player->getMark("&mtliaoshi_num");
			if(num <= 0) return false;
			if(num != player->getHp() && num != player->getHandcardNum())
				return false;
        } else {
            int num = player->getMark("&mtliaoshi_num");
            if (num <= 0) return false;

            if (event == HpChanged && num != player->getHp()) return false;

            if (event == CardsMoveOneTime) {
                bool ok = false;

                CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
                if (move.to == player && move.to_place == Player::PlaceHand && num == player->getHandcardNum())
                    ok = true;
                else if (move.from == player && move.from_places.contains(Player::PlaceHand) && num == player->getHandcardNum())
                    ok = true;

                if (!ok) return false;
            }
        }
        int dis = 0;
        QStringList choices;
        foreach (int id, player->handCards() + player->getEquipsId()) {
            if (player->canDiscard(player, id))
                dis++;
        }
        if (dis > 1) choices << "discard";
        choices << "lose" << "draw";
        if (player->isWounded()) choices << "recover";
		choices << "cancel";

        QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		if(choice=="cancel") return false;
		player->skillInvoked(this,0);
        DOSTH(player, choice, true, objectName());
        return false;
    }
};

class MTLiaoshiChoose : public GameStartSkill
{
public:
    MTLiaoshiChoose() : GameStartSkill("#mtliaoshi")
    {
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->sendCompulsoryTriggerLog(player, "mtliaoshi", true, true);
        QStringList choices;
        for (int i = 1; i < 9; i++) {
            if (i == player->getHp() || i == player->getHandcardNum()) continue;
            choices << QString::number(i);
        }
        if (choices.isEmpty()) return;
        QString choice = room->askForChoice(player, "mtliaoshi_num", choices.join("+"));
        int num = choice.toInt();
        if (num <= 0) num = 1;
        room->setPlayerMark(player, "&mtliaoshi_num", num);
    }
};

class MTTongyi : public TriggerSkill
{
public:
    MTTongyi() :TriggerSkill("mttongyi")
    {
        events << EventForDiy;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QString str = data.toString();
        if (!str.startsWith("mtliaoshi_choice_")) return false;

        QString choice = str.split("_").last();
        QList<ServerPlayer *> targets;

        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getMark("mttongyi_target-Keep") > 0) continue;
            if (choice == "recover" && !p->isWounded()) continue;
            if (choice == "discard" && p->getCardCount() < 2) continue;
            targets << p;
        }
        if (targets.isEmpty()) return false;

        ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mttongyi-invoke", true, true);
        if (!t) return false;
        player->peiyin(this);

        room->addPlayerMark(t, "mttongyi_target-Keep");
        MTLiaoshi::DOSTH(t, choice, false, objectName());
        return false;
    }
};

class MTXianzhengVS : public OneCardViewAsSkill
{
public:
    MTXianzhengVS() : OneCardViewAsSkill("mtxianzheng")
    {
        response_pattern = "@@mtxianzheng";
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(to_select);
        slash->deleteLater();
        slash->setSkillName(objectName());
        return slash->isAvailable(Self);
    }

    const Card *viewAs(const Card *to_select) const
    {
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(to_select);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTXianzheng : public TriggerSkill
{
public:
    MTXianzheng() :TriggerSkill("mtxianzheng")
    {
        events << EventPhaseStart << Damage;
        view_as_skill = new MTXianzhengVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start) return false;
            if (player->isNude() && player->getHandPile().isEmpty()) return false;
            room->askForUseCard(player, "@@mtxianzheng", "@mtxianzheng");
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || damage.to->isDead()) return false;
            if (damage.to->getEquips().isEmpty() && damage.to->getJudgingArea().isEmpty()) return false;
            QList<ServerPlayer *> players;
            players << damage.to;
            if (!room->canMoveField("ej", players, room->getOtherPlayers(damage.to))) return false;
            if (!player->askForSkillInvoke(this, "mtxianzheng:" + damage.to->objectName())) return false;
            player->peiyin(this);
            room->moveField(player, objectName(), false, "ej", players, room->getOtherPlayers(damage.to));
        }
        return false;
    }
};

class MTNianchou : public TriggerSkill
{
public:
    MTNianchou() :TriggerSkill("mtnianchou")
    {
        events << EventPhaseStart << Death;
        shiming_skill = true;
        waked_skills = "tenyearshensu,baobian,#mtnianchou";
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMark(objectName()) > 0) return false;

        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return false;
            ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtnianchou-target", true, true);
            if (!t) return false;
            player->peiyin(this);
            room->addPlayerMark(player, "mtnianchou_from-Clear");
            room->addPlayerMark(t, "mtnianchou_to-Clear");

            if (player->getHp() != 1) return false;
            room->sendShimingLog(player, this, false);
            room->handleAcquireDetachSkills(player, "-mtxianzheng|-mtnianchou|baobian");
        } else {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player || !death.who) return false;
            if (!death.damage || death.damage->from != player) return false;
            room->sendShimingLog(player, this);

            QString choices = "draw";
            if (player->isWounded()) choices = "recover+draw";
            QString choice = room->askForChoice(player, objectName(), choices);

            if (choice == "draw")
                player->drawCards(2, objectName());
            else
                room->recover(player, RecoverStruct("mtnianchou", player));

            room->handleAcquireDetachSkills(player, "tenyearshensu");
        }
        return false;
    }
};

class MTNianchouTargetMod : public TargetModSkill
{
public:
    MTNianchouTargetMod() : TargetModSkill("#mtnianchou")
    {
        frequency = NotFrequent;
        shiming_skill = true;
        pattern = "^SkillCard";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *to) const
    {
        if (from->getMark("mtnianchou_from-Clear") > 0 && to && to->getMark("mtnianchou_to-Clear") > 0)
            return 1000;
        return 0;
    }
};

MTJieliCard::MTJieliCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MTJieliCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Duel *duel = new Duel(Card::SuitToBeDecided, -1);
    duel->setSkillName("mtjieli");
    duel->addSubcards(subcards);
    duel->deleteLater();
    return duel->subcardsLength() > 0 && duel->targetFilter(targets, to_select, Self);
}

const Card *MTJieliCard::validate(CardUseStruct &) const
{
    Duel *duel = new Duel(Card::SuitToBeDecided, -1);
    duel->setSkillName("mtjieli");
    duel->addSubcards(subcards);
    duel->deleteLater();
    return duel;
}

class MTJieliVS : public ZeroCardViewAsSkill
{
public:
    MTJieliVS() : ZeroCardViewAsSkill("mtjieli")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->isKongcheng())
            return false;
        else {
            if (player->getMark("SkillDescriptionArg1_mtjieli") <= 1)
                return !player->hasUsed("MTJieliCard");
            else
                return player->usedTimes("MTJieliCard") < player->getMark("SkillDescriptionArg1_mtjieli");
        }
    }

    const Card *viewAs() const
    {
        QString choice = Self->tag["mtjieli"].toString();

        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->setSkillName(objectName());
        duel->deleteLater();

        MTJieliCard *card = new MTJieliCard;

        foreach (const Card *c, Self->getHandcards()) {
            if ((c->isRed() && choice == "red") || (c->isBlack() && choice == "black")) {
                duel->addSubcard(c);
                card->addSubcard(c);
            }
        }

        if (duel->subcardsLength() > 0 && duel->isAvailable(Self))
            return card;
        return nullptr;
    }
};

class MTJieli : public TriggerSkill
{
public:
    MTJieli() :TriggerSkill("mtjieli")
    {
        events << CardFinished << DamageDone << EventPhaseChanging;
        view_as_skill = new MTJieliVS;
        waked_skills = "#mtjieli";
    }

    QDialog *getDialog() const
    {
        return TiansuanDialog::getInstance("mtjieli");
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardFinished) {
            if (!player->hasFlag("CurrentPlayer")) return false;

            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Duel")) return false;

            foreach (QString flag, use.card->getFlags()) {
                if (!flag.startsWith("mtjieli_damage_point_")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 4) continue;
                int damage = flags.last().toInt();
                if (damage < 2) continue;

                LogMessage log;
                log.type = "#MTJieliTimes";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(player, objectName());

                int mark = player->getMark("SkillDescriptionArg1_mtjieli");
                if (mark <= 0) mark = 1;
                room->setPlayerMark(player, "SkillDescriptionArg1_mtjieli", mark + 1);
				player->setSkillDescriptionSwap("mtjieli","%arg1",QString::number(mark+1));
                room->changeTranslation(player, "mtjieli", 1);
                break;
            }
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            room->setPlayerMark(player, "SkillDescriptionArg1_mtjieli", 0);
            room->changeTranslation(player, "mtjieli", 2);
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Duel")) return false;
            ServerPlayer *user = room->getCardUser(damage.card);
            if (!user || user->getPhase() == Player::NotActive) return false;

            int d = 0;

            foreach (QString flag, damage.card->getFlags()) {
                if (!flag.startsWith("mtjieli_damage_point_")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 4) continue;

                room->setCardFlag(damage.card, "-" + flag);

                int dd = flags.last().toInt();
                if (dd > d)
                    d = dd;
            }

            d += damage.damage;
            room->setCardFlag(damage.card, "mtjieli_damage_point_" + QString::number(d));
        }
        return false;
    }
};

class MTJieliTargetMod : public TargetModSkill
{
public:
    MTJieliTargetMod() : TargetModSkill("#mtjieli")
    {
        frequency = NotFrequent;
        pattern = "Duel";
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        if (from->hasSkill("mtjieli"))
            return 1;
        return 0;
    }
};

class MTFuyi : public TriggerSkill
{
public:
    MTFuyi() :TriggerSkill("mtfuyi")
    {
        events << Death;
        frequency = Wake;
        waked_skills = "#mtfuyi,#mtfuyi-turn";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player&&player->getMark(objectName())<1&&player->isAlive()&&player->hasSkill(objectName());
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.damage && death.damage->from && death.damage->from == player
			&&!player->canWake(objectName()))
			return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "mtfuyi");
        room->setPlayerMark(player, "mtfuyi", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
            room->recover(player, RecoverStruct(objectName(), player));
            room->addPlayerMark(player, "&mtfuyi_buff");
            room->addPlayerMark(player, "mtfuyi_extra_turn");
        }
        return false;
    }
};

class MTFuyiDamage : public TriggerSkill
{
public:
    MTFuyiDamage() :TriggerSkill("#mtfuyi")
    {
        events << ConfirmDamage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || damage.to->isDead()) return false;

        int d = damage.damage;

        if (damage.from && damage.from->getMark("&mtfuyi_buff") > 0) {
            d += damage.from->getMark("&mtfuyi_buff");

            LogMessage log;
            log.type = "#MTFuyiDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = "mtfuyi";
            log.arg2 = QString::number(d);
            room->sendLog(log);
            damage.from->peiyin("mtfuyi");
            room->notifySkillInvoked(damage.from, "mtfuyi");

            damage.damage = d;
            data = QVariant::fromValue(damage);
        }

        if (damage.to->getMark("&mtfuyi_buff") > 0) {
            d += damage.to->getMark("&mtfuyi_buff");

            LogMessage log;
            log.type = "#MTFuyiDamage";
            log.from = damage.to;
            log.to << damage.to;
            log.arg = "mtfuyi";
            log.arg2 = QString::number(d);
            room->sendLog(log);
            damage.to->peiyin("mtfuyi");
            room->notifySkillInvoked(damage.to, "mtfuyi");

            damage.damage = d;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class MTFuyiTurn : public PhaseChangeSkill
{
public:
    MTFuyiTurn() :PhaseChangeSkill("#mtfuyi-turn")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::NotActive;
    }

    bool onPhaseChange(ServerPlayer *, Room *room) const
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            int mark = p->getMark("mtfuyi_extra_turn");
            if (mark <= 0) continue;

            for (int i = 0; i < mark; i++) {
                if (p->isDead()) break;
                room->removePlayerMark(p, "mtfuyi_extra_turn");

                LogMessage log;
                log.type = "#MTFuyiTurn";
                log.from = p;
                log.arg = "mtfuyi";
                room->sendLog(log);

                p->gainAnExtraTurn();
            }
        }
        return false;
    }
};

class MTZhongyi : public TriggerSkill
{
public:
    MTZhongyi() :TriggerSkill("mtzhongyi")
    {
        events << TargetConfirming << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;

            ServerPlayer *current = room->getCurrent();
            if (!current || current->isDead() || current->isNude()) return false;

            foreach (ServerPlayer *p, room->getAllPlayers()) {
                foreach (ServerPlayer *pp, room->getAllPlayers()) {
                    if (pp->getHp() != pp->getMark("mtzhongyi_hp-Keep")) continue;
                    int mark = pp->getMark("mtzhongyi_" + p->objectName() + "-Keep");
                    if (mark<1||p->isDead()) continue;

                    for (int i = 0; i < mark; i++) {
                        if (current->isDead() || current->isNude()) break;
                        if (p == current && p->getEquips().isEmpty()) break;
                        if (!p->askForSkillInvoke(this, "current:" + current->objectName())) break;
                        int card_id = room->askForCardChosen(p, current, p == current ? "e" : "he", "mtzhongyi");
                        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                        room->obtainCard(p, Sanguosha->getCard(card_id), reason, false);
                    }

                }
            }
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.length() != 1) return false;
            if (use.card->isKindOf("Slash") || (use.card->isDamageCard() && !use.card->isKindOf("DelayedTrick"))) {
                ServerPlayer *t = use.to.first();
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (t->isDead()||p->isDead()||!p->hasSkill(objectName())) continue;
                    if (t == p || t->getHandcardNum() < p->getHandcardNum()) {
                        if (!p->askForSkillInvoke(this, "draw:" + t->objectName())) continue;
                        p->peiyin(this);
                        t->addMark("mtzhongyi_" + p->objectName() + "-Keep");
                        t->drawCards(1, objectName());
                    }
                }
            }
        }
        return false;
    }
};


MTWeiqieCard::MTWeiqieCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MTWeiqieCard::onUse(Room *, CardUseStruct &) const
{
}

class MTWeiqieVS : public ViewAsSkill
{
public:
    MTWeiqieVS() : ViewAsSkill("mtweiqie")
    {
        expand_pile = "#mtweiqie";
    }

    int Judge(int a, int b) const
    {
        if (b == 0)
            return a;
        else
            return Judge(b, a%b);
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (!Self->getPile("#mtweiqie").contains(to_select->getEffectiveId())) return false;
        if (selected.isEmpty()) return true;

        int a = to_select->getNumber();

        foreach (const Card *c, selected) {
            int b = c->getNumber();
            if (Judge(a, b) != 1) return false;
        }
        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        MTWeiqieCard *c = new MTWeiqieCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mtweiqie";
    }
};

class MTWeiqie : public DrawCardsSkill
{
public:
    MTWeiqie() : DrawCardsSkill("mtweiqie")
    {
        view_as_skill = new MTWeiqieVS;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        if (!player->askForSkillInvoke(this)) return n;
        player->peiyin(this);

        Room *room = player->getRoom();
        QList<int> cards = room->showDrawPile(player, 4, objectName());
        room->notifyMoveToPile(player, cards, objectName(), Player::PlaceTable, true);
        const Card *c = room->askForUseCard(player, "@@mtweiqie", "@mtweiqie", -1, Card::MethodNone);
        room->notifyMoveToPile(player, cards, objectName(), Player::PlaceTable, false);

        if (c->subcardsLength() > 0) {
            QList<int> subcards = c->getSubcards();
            foreach (int id, subcards)
                cards.removeOne(id);

            DummyCard *dummy = new DummyCard(subcards);
            room->obtainCard(player, dummy);
            delete dummy;
        }

        if (!cards.isEmpty()) {
            DummyCard *dummy = new DummyCard(cards);
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "mtweiqie", "");
            room->throwCard(dummy, reason, nullptr);
            delete dummy;
        }
        return 0;
    }
};

class MTGuanda : public TriggerSkill
{
public:
    MTGuanda() :TriggerSkill("mtguanda")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile) return false;

        bool slash = false, red = false;

        foreach (int id, move.card_ids) {
            const Card *c = Sanguosha->getCard(id);
            if (c->isKindOf("Slash")) {
                slash = true;
                if (c->isRed())
                    red = true;
            }
            if (slash && red) break;
        }

        if (!slash || !player->askForSkillInvoke(this)) return false;
        player->peiyin(this);

        QList<int> two = room->getNCards(2);
        LogMessage log;
        log.from = player;
        log.type = "$ViewDrawPile";
        log.card_str = ListI2S(two).join("+");
        room->sendLog(log, player);

        log.type = "#ViewDrawPile";
        log.arg = "2";
        room->sendLog(log, room->getOtherPlayers(player, true));

        room->fillAG(two, player);
        int id = room->askForAG(player, two, true, objectName(), red ? "@mtguanda-get" : "@mtguanda-see");
        room->clearAG(player);

        room->returnToTopDrawPile(two);
        if (red && id > -1)
            room->obtainCard(player, id, false);
        return false;
    }
};

MTZhilieCard::MTZhilieCard()
{
}

bool MTZhilieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

void MTZhilieCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    if (to->isKongcheng()) return;

    Room *room = from->getRoom();

    int id = room->askForCardChosen(from, to, "h", "mtzhilie");
    room->showCard(to, id);
    const Card *show = Sanguosha->getCard(id);

    QStringList choices;
    int same = 0, dis = 0;

    foreach (const Card *c, from->getEquips()) {
        if (c->sameColorWith(show)) {
            same++;
            if (from->canDiscard(from, c->getEffectiveId()))
                dis++;
        }
    }
    if (same > 0 && dis > 0) {
        same = qMin(same, 2);
        choices << QString("damage=%1=%2=%3").arg(show->getColorString()).arg(to->objectName()).arg(same);
    }

    Card *usecard = Sanguosha->cloneCard(show->objectName(), Card::NoSuit, 0);
    if (usecard) {
        usecard->setSkillName("_mtzhilie");
        usecard->deleteLater();
		if ((usecard->isKindOf("BasicCard")||usecard->isNDTrick())&&from->canUse(usecard, to, true))
			choices << "use=" + to->objectName() + "=" + show->objectName();
    }

    choices << "cancel";

    CardUseStruct use;
    use.from = to;
    use.card = usecard;
    QVariant data = QVariant::fromValue(use);  //For AI

    QString choice = room->askForChoice(from, "mtzhilie", choices.join("+"), data);

    if (choice == "cancel")
        return;
     else if (choice.startsWith("damage")) {
        same = 0;
        DummyCard *discard = new DummyCard;

        foreach (const Card *c, from->getEquips()) {
            if (c->sameColorWith(show)) {
                same++;
                if (from->canDiscard(from, c->getEffectiveId()))
                    discard->addSubcard(c);
            }
        }

        same = qMin(same, 2);
        if (discard->subcardsLength() > 0) {
            room->throwCard(discard, from);
            room->damage(DamageStruct("mtzhilie", from, to, same));
        }
        delete discard;
    } else {
        if (usecard && from->canUse(usecard, to, true))
            room->useCard(CardUseStruct(usecard, from, to));
    }
}

class MTZhilie : public ZeroCardViewAsSkill
{
public:
    MTZhilie() : ZeroCardViewAsSkill("mtzhilie")
    {
    }

    const Card *viewAs() const
    {
        return new MTZhilieCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTZhilieCard");
    }
};

class MTChuanjiu : public TriggerSkill
{
public:
    MTChuanjiu() :TriggerSkill("mtchuanjiu")
    {
        events << CardUsed << EventPhaseStart;
        frequency = Compulsory;
        waked_skills = "#mtchuanjiu";
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            if (!player->hasFlag("CurrentPlayer")) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("TrickCard")) return false;

            Analeptic *ana = new Analeptic(Card::NoSuit, 0);
            ana->setSkillName("_mtchuanjiu");
            ana->deleteLater();

            if (!Analeptic::IsAvailable(player, ana)) return false;
            room->sendCompulsoryTriggerLog(player, this);
            room->useCard(CardUseStruct(ana, player), !player->isWounded());
        } else {
            if (player->getPhase() != Player::Finish) return false;
            if (!player->isWounded() || player->getMark("mtchuanjiu_Analeptic-Clear") <= 1) return false;
            room->sendCompulsoryTriggerLog(player, objectName());
            room->loseHp(HpLostStruct(player, 1, objectName(), player));
        }
        return false;
    }
};


class MTDianpei : public PhaseChangeSkill
{
public:
    MTDianpei() :PhaseChangeSkill("mtdianpei")
    {
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::RoundStart || player->getHandcardNum() > player->getHp()) return false;

        ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtdianpei-invoke", true, true);
        if (!t) return false;
        player->peiyin(this);

        if (!player->isKongcheng())
            t->addToPile(objectName(), player->handCards());

        if (player->isDead() || t->isDead()) return false;
        if (t->isNude() || player->getHandcardNum() >= player->getHp()) return false;

        int num = qMax(player->getLostHp(), 1), draw = qMax(player->getHp() - player->getHandcardNum(), 0);
        QString prompt = QString("@mtdianpei-give:%1:%2:%3").arg(player->objectName()).arg(num).arg(draw);
        const Card *ex = room->askForExchange(t, objectName(), num, num, true, prompt, true);
        if (!ex)
            player->drawCards(player->getHp() - player->getHandcardNum());
        else
            room->giveCard(t, player, ex, objectName());

        QList<int> pile = t->getPile(objectName());
        if (t->isAlive() && !pile.isEmpty()) {  //偷懒处理成获得全部，不管是不是player扣置的
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = t;
            log.arg = objectName();
            log.card_str = ListI2S(pile).join("+");
            room->sendLog(log, t);

            log.type = "#MTDianpeiGet";
            log.from = t;
            log.arg2 = QString::number(pile.length());
            room->sendLog(log, room->getOtherPlayers(t, true));

            DummyCard get(pile);
            room->obtainCard(t, &get, false);
        }
        return false;
    }
};

MTRenyiCard::MTRenyiCard()
{
    will_throw = false;
    target_fixed = false;
    handling_method = Card::MethodNone;
}

bool MTRenyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int num = subcardsLength();
    return targets.length() < num && to_select != Self;
}

bool MTRenyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcardsLength();
}

void MTRenyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->setPlayerMark(source, "mtrenyiShowNum-Clear", 0);

    QList<int> subcards = this->subcards;

    try {
        room->fillAG(subcards);
        foreach (ServerPlayer *p, targets) {
            if (subcards.isEmpty()) break;
            if (p->isDead()) continue;
            int id = room->askForAG(p, subcards, false, "mtrenyi");
            subcards.removeOne(id);
            room->takeAG(p, id, false);
            room->obtainCard(p, id);
        }
		room->getThread()->delay();
        room->clearAG();
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            room->clearAG();
    }

    if (source->isDead() || source->getPhase() == Player::NotActive) return;

    QList<int> list = room->getAvailableCardList(source, "basic", "mtrenyi");
    if (list.isEmpty()) return;

    room->fillAG(list, source);
    int id = room->askForAG(source, list, true, "mtrenyi", "@mtrenyi-use");
    room->clearAG(source);
    if (id < 0) return;

    QString name = Sanguosha->getEngineCard(id)->objectName();
    room->setPlayerMark(source, "mtrenyi_id-Clear", id + 1);
    Card *card = Sanguosha->cloneCard(name);
    if (!card) return;
    card->deleteLater();
    card->setSkillName("_mtrenyi");

    if (card->targetFixed()) {
        if (!source->askForSkillInvoke("mtrenyi", QString("mtrenyi_use:%1").arg(name), false)) return;
        room->useCard(CardUseStruct(card, source));
    } else
        room->askForUseCard(source, "@@mtrenyi2", "@mtrenyi2:" + name, 2, Card::MethodUse, false);
}

class MTRenyiVS : public ViewAsSkill
{
public:
    MTRenyiVS() : ViewAsSkill("mtrenyi")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern.endsWith("1")) {
            int mark = Self->getMark("mtrenyiShowNum-Clear");
            return selected.length() < mark;
        }
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.startsWith("@@mtrenyi");
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern.endsWith("1")) {
            if (cards.isEmpty() || cards.length() != Self->getMark("mtrenyiShowNum-Clear")) return nullptr;
            MTRenyiCard *c = new MTRenyiCard;
            c->addSubcards(cards);
            return c;
        } else if (pattern.endsWith("2")) {
            if (!cards.isEmpty()) return nullptr;
            int id = Self->getMark("mtrenyi_id-Clear") - 1;
            if (id < 0) return nullptr;
            QString name = Sanguosha->getEngineCard(id)->objectName();
            Card *card = Sanguosha->cloneCard(name);
            if (!card) return nullptr;
            card->setSkillName("_mtrenyi");
            return card;
        }
        return nullptr;
    }
};

class MTRenyi : public TriggerSkill
{
public:
    MTRenyi() :TriggerSkill("mtrenyi")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MTRenyiVS;
        waked_skills = "#mtrenyi";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (room->getTag("FirstRound").toBool()) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to != player || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile)) return false;

        int show = 0;
        for (int i = 0; i < move.card_ids.length(); i++) {
            int id = move.card_ids.at(i);
            if (!player->hasCard(id) || move.from_places.at(i) != Player::DrawPile) continue;
            show++;
        }
        if (show <= 0) return false;

        room->setPlayerMark(player, "mtrenyiShowNum-Clear", show);
        room->askForUseCard(player, "@@mtrenyi1", "@mtrenyi1:" + QString::number(show), 1, Card::MethodNone);
        room->setPlayerMark(player, "mtrenyiShowNum-Clear", 0);
        return false;
    }
};

class MTRenyiTargetMod : public TargetModSkill
{
public:
    MTRenyiTargetMod() : TargetModSkill("#mtrenyi")
    {
        pattern = "BasicCard";
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "mtrenyi")
            return 999;
        return 0;
    }
};

class MTFeirenVS : public OneCardViewAsSkill
{
public:
    MTFeirenVS() : OneCardViewAsSkill("mtfeiren")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return false;
        return (pattern.contains("slash") || pattern.contains("Slash"));
    }

    bool viewFilter(const Card *to_select) const
    {
        if (to_select->getTypeId() != Card::TypeEquip)
            return false;

        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(to_select->getEffectiveId());
        slash->deleteLater();

        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
            return slash->isAvailable(Self);
        return !Self->isLocked(slash);
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return nullptr;

        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTFeiren : public TriggerSkill
{
public:
    MTFeiren() :TriggerSkill("mtfeiren")
    {
        events << CardFinished << CardUsed;
        view_as_skill = new MTFeirenVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash") || (use.card->isDamageCard() && !use.card->isKindOf("DelayedTrick"))){
				room->setCardFlag(use.card,"mtfeirenBf");
			}
			return false;
		}
		ServerPlayer *current = room->getCurrent();
        if (!current||current->getPhase() >= Player::NotActive) return false;
        QString phase = QString::number(current->getPhase());
        if (player->isKongcheng() || player->getMark("mtfeiren_used-" + phase + "Clear") > 0) return false;

        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->hasFlag("mtfeirenBf")&&use.card->hasFlag("DamageDone")) {

            QList<int> ids;
            if (use.card->isVirtualCard())
                ids = use.card->getSubcards();
            else
                ids << use.card->getEffectiveId();

            room->fillAG(ids, player);
            const Card *c = room->askForCard(player, ".|.|.|hand", "@mtfeiren", data, Card::MethodNone);
            room->clearAG(player);
            if (!c) return false;
            player->addMark("mtfeiren_used-" + phase + "Clear");

            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            player->peiyin(this);
            room->notifySkillInvoked(player, objectName());

            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "mtfeiren", "");
            room->throwCard(c, reason, nullptr);
            if (player->isDead()) return false;
            room->obtainCard(player, use.card);
        }
        return false;
    }
};

class MTFeirenTargetMod : public TargetModSkill
{
public:
    MTFeirenTargetMod() : TargetModSkill("#mtfeiren")
    {
        frequency = NotFrequent;
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "mtfeiren")
            return 999;
        return 0;
    }
};

class MTFuzhan : public PhaseChangeSkill
{
public:
    MTFuzhan() :PhaseChangeSkill("mtfuzhan")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::Finish;
    }

    bool onPhaseChange(ServerPlayer *, Room *room) const
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
                if (p->getMark("mtfuzhanDamage_" + q->objectName() + "-Clear") <= 0 &&
                        q->getMark("mtfuzhanDamage_" + p->objectName() + "-Clear") <= 0) continue;
                if (!p->canPindian(q)) continue;
                targets << q;
            }

            ServerPlayer *t = room->askForPlayerChosen(p, targets, objectName(), "@mtfuzhan-pindian", true, true);
            if (!t) continue;
            p->peiyin(this);

            PindianStruct *pindian = p->PinDian(t, objectName());
            if (pindian->from_number == pindian->to_number) continue;

            ServerPlayer *winner = t, *loser = p;
            if (pindian->success) {
                winner = p;
                loser = t;
            }

            Slash *slash = new Slash(Card::SuitToBeDecided, -1);
            slash->addSubcard(pindian->from_card);
            if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
                slash->addSubcard(pindian->to_card);
            slash->setSkillName("_mtfuzhan");
            slash->deleteLater();

            Duel *duel = new Duel(Card::SuitToBeDecided, -1);
            duel->addSubcard(pindian->from_card);
            if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
                duel->addSubcard(pindian->to_card);
            duel->setSkillName("_mtfuzhan");
            duel->deleteLater();

            QStringList choices;
            if (winner->canSlash(loser, slash, false))
                choices << "slash=" + loser->objectName();
            if (winner->canUse(duel, loser, true))
                choices << "duel=" + loser->objectName();
            if (choices.isEmpty()) continue;

            QString choice = room->askForChoice(winner, objectName(), choices.join("+"), QVariant::fromValue(pindian));
            if (choice.startsWith("slash"))
                room->useCard(CardUseStruct(slash, winner, loser));
            else
                room->useCard(CardUseStruct(duel, winner, loser));
        }
        return false;
    }
};


class MTRenyu : public TriggerSkill
{
public:
    MTRenyu() :TriggerSkill("mtrenyu")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash") || use.card->isNDTrick()) {
            foreach (ServerPlayer *p, use.to) {
                if (player->isDead() || !player->hasSkill(objectName())) break;
                if (p->isDead() || !use.to.contains(p) || p == player) continue;

                QString kingdom1 = player->getKingdom(), kingdom2 = p->getKingdom();
                if (kingdom1 == kingdom2) {
                    if (!player->canDiscard(p, "he")) continue;
                    player->tag["MTRenyuData"] = data;
                    bool invoke = player->askForSkillInvoke(this, p);
                    player->tag.remove("MTRenyuData");

                    if (!invoke) continue;
                    player->peiyin(this);

                    int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                    room->throwCard(id, p, player);

                    use.nullified_list << p->objectName();
                    data = QVariant::fromValue(use);
                } else {
                    p->tag["MTRenyuData"] = data;
                    bool invoke = p->askForSkillInvoke("mtrenyu_jin", "mtrenyu_jin");
                    p->tag.remove("MTRenyuData");
                    if (!invoke) continue;

                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = p;
                    log.to << player;
                    log.arg = objectName();
                    room->sendLog(log);
                    player->peiyin(this);
                    room->notifySkillInvoked(player, objectName());

                    p->drawCards(1, objectName());

                    log.type = "#ChangeKingdom2";
                    log.arg = p->getKingdom();
                    log.arg2 = "jin";
                    room->sendLog(log);
                    room->setPlayerProperty(p, "kingdom", "jin");

                    use.nullified_list << p->objectName();
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

class MTFengshang : public TriggerSkill
{
public:
    MTFengshang() :TriggerSkill("mtfengshang")
    {
        events << CardFinished;
        waked_skills = "#mtfengshang";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;

        QString kingdom = player->getKingdom();
        int mark = player->getMark("mtfengshang_times-PlayClear");
        int same = 0;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getKingdom() == kingdom)
                same++;
        }
        if (same <= mark) return false;

        ServerPlayer *t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@mtfengshang-invoke", true, true);
        if (!t) return false;
        room->addPlayerMark(player, "mtfengshang_times-PlayClear");
        player->peiyin(this);

        t->drawCards(1, objectName());

        if (player->isAlive() && t->getHandcardNum() > player->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(player, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

class MTFengshangKeep : public MaxCardsSkill
{
public:
    MTFengshangKeep() : MaxCardsSkill("#mtfengshang")
    {
        frequency = NotFrequent;
    }

    int getExtra(const Player *target) const
    {
        return -target->getMark("&mtfengshang_debuff-Self" + QString::number((int)Player::Finish) + "Clear");
    }
};

class MTJiawei : public TriggerSkill
{
public:
    MTJiawei() :TriggerSkill("mtjiawei$")
    {
        events << CardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getKingdom() == "jin" && target->getPhase() != Player::NotActive;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;

        QList<ServerPlayer *> simayans;
        foreach (ServerPlayer *simayan, room->getOtherPlayers(player)) {
            if (simayan->isDead() || !simayan->hasLordSkill(this) || simayan->getMark("mtjiawei_used-Clear") > 0) continue;
            bool can_be_user = true;
            foreach (ServerPlayer *p, use.to) {
                if (!use.card->isAvailable(simayan) || simayan->isLocked(use.card) || room->isProhibited(player, p, use.card) ||
                        !use.card->targetFilter(QList<const Player *>(), p, simayan)) {
                    can_be_user = false;
                    break;
                }
            }
            if (can_be_user)
                simayans << simayan;
        }
        if (simayans.isEmpty()) return false;

        ServerPlayer *simayan = room->askForPlayerChosen(player, simayans, objectName(), "@mtjiawei-invoke:" + use.card->objectName(), true);
        if (!simayan) return false;
        room->addPlayerMark(simayan, "mtjiawei_used-Clear");

        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << simayan;
        log.arg = simayan->isWeidi() ? "weidi" : objectName();
        room->sendLog(log);
        room->doAnimate(1, player->objectName(), simayan->objectName());
        if (simayan->isWeidi()) {
            simayan->peiyin("weidi");
            room->notifySkillInvoked(simayan, "weidi");
        } else {
            simayan->peiyin(this);
            room->notifySkillInvoked(simayan, objectName());
        }

        log.type = "#BecomeUser";
        log.from = simayan;
        log.card_str = use.card->toString();
        room->sendLog(log);

        use.from = simayan;
        data = QVariant::fromValue(use);

        if (simayan->isDead() || player->isDead()) return false;
        if (!simayan->askForSkillInvoke("mtjiawei", "mtjiawei:" + player->objectName(), false)) return false;

        if (simayan->getPhase() == Player::Play)
            room->addPlayerMark(simayan, "mtfengshang_times-PlayClear");

        log.type = "#ChoosePlayerWithSkill";
        log.from = simayan;
        log.to.clear();
        log.to << player;
        log.arg = "mtfengshang";
        room->sendLog(log);
        room->doAnimate(1, simayan->objectName(), player->objectName());
        simayan->peiyin("mtfengshang");
        room->notifySkillInvoked(simayan, "mtfengshang");

        player->drawCards(1, "mtfengshang");

        if (simayan->isAlive() && player->getHandcardNum() > simayan->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(simayan, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

MTGuzhaoCard::MTGuzhaoCard()
{
}

bool MTGuzhaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select->isKongcheng() || to_select == Self || targets.length() > 2) return false;
    if (targets.isEmpty()) return true;
    return to_select->isAdjacentTo(targets.last()) || to_select->isAdjacentTo(targets.first());
}

int MTGuzhaoCard::pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2) const
{
    if (!card2 || !from->canPindian(target, false)) return -2;

    Room *room = from->getRoom();

    PindianStruct *pindian_struct = new PindianStruct;
    pindian_struct->from = from;
    pindian_struct->to = target;
    pindian_struct->from_card = card1;
    pindian_struct->to_card = card2;
    pindian_struct->from_number = card1->getNumber();
    pindian_struct->to_number = card2->getNumber();
    pindian_struct->reason = "mtguzhao";
    QVariant data = QVariant::fromValue(pindian_struct);

    QList<CardsMoveStruct> moves;
    CardsMoveStruct move1;
    move1.card_ids << pindian_struct->from_card->getEffectiveId();
    move1.from = pindian_struct->from;
    move1.to = nullptr;
    move1.to_place = Player::PlaceTable;
    move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
        pindian_struct->to->objectName(), pindian_struct->reason, "");

    CardsMoveStruct move2;
    move2.card_ids << pindian_struct->to_card->getEffectiveId();
    move2.from = pindian_struct->to;
    move2.to = nullptr;
    move2.to_place = Player::PlaceTable;
    move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(),
		pindian_struct->reason, "");

    moves.append(move1);
    moves.append(move2);
    room->moveCardsAtomic(moves, true);

    LogMessage log;
    log.type = "$PindianResult";
    log.from = pindian_struct->from;
    log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
    room->sendLog(log);

    log.type = "$PindianResult";
    log.from = pindian_struct->to;
    log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
    room->sendLog(log);

    RoomThread *thread = room->getThread();
    thread->trigger(PindianVerifying, room, from, data);

	pindian_struct = data.value<PindianStruct *>();

    pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

    log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
    log.from = from;
    log.to.clear();
    log.to << target;
    log.card_str.clear();
    room->sendLog(log);

    JsonArray arg;
    arg << QSanProtocol::S_GAME_EVENT_REVEAL_PINDIAN << pindian_struct->from->objectName() << pindian_struct->from_card->getEffectiveId()
		<< target->objectName() << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << "mtguzhao";
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

    data = QVariant::fromValue(pindian_struct);
	thread->trigger(Pindian, room, from, data);

    moves.clear();
    if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == Player::PlaceTable) {
        CardsMoveStruct move1;
        move1.card_ids << pindian_struct->from_card->getEffectiveId();
        move1.from = pindian_struct->from;
        move1.to = nullptr;
        move1.to_place = Player::DiscardPile;
        move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
            pindian_struct->to->objectName(), pindian_struct->reason, "");
        moves.append(move1);
    }

    if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == Player::PlaceTable) {
        CardsMoveStruct move2;
        move2.card_ids << pindian_struct->to_card->getEffectiveId();
        move2.from = pindian_struct->to;
        move2.to = nullptr;
        move2.to_place = Player::DiscardPile;
        move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(),
			pindian_struct->reason, "");
        moves.append(move2);
    }
    if (!moves.isEmpty())
        room->moveCardsAtomic(moves, true);

    QVariant decisionData = QVariant::fromValue(QString("pindian:%1:%2:%3:%4:%5")
        .arg("mtguzhao").arg(from->objectName()).arg(pindian_struct->from_card->getEffectiveId())
        .arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId()));
    thread->trigger(ChoiceMade, room, from, decisionData);

    if (pindian_struct->success) return 1;
    else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
    else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
    return -2;
}

void MTGuzhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QHash<ServerPlayer *, int> show;
    QList<ServerPlayer *> new_targets;
    foreach (ServerPlayer *target, targets) {
        if (target->isDead() || target->isKongcheng()) continue;
        int id = target->getRandomHandCardId();
        show[target] = id;
        new_targets << target;
        room->showCard(target, id, source, false);
    }

    if (new_targets.isEmpty() || !source->canPindian()) return;

    LogMessage log;
    log.type = "#Pindian";
    log.from = source;
    log.to = new_targets;
    room->sendLog(log);

    const Card *cardss = nullptr;
    QHash<ServerPlayer *, const Card *> hash;
    foreach (ServerPlayer *target, new_targets) {
        if (!source->canPindian(target, false)) continue;

        PindianStruct *pindian = new PindianStruct;
        pindian->from = source;
        pindian->to = target;
        pindian->from_card = cardss;
        pindian->to_card = nullptr;
        pindian->reason = "mtguzhao";

        RoomThread *thread = room->getThread();
        QVariant data = QVariant::fromValue(pindian);
        thread->trigger(AskforPindianCard, room, source, data);

        pindian = data.value<PindianStruct *>();

        if (!pindian->from_card && !pindian->to_card) {
            QList<const Card *> cards = room->askForPindianRace(source, target, "mtguzhao");
            pindian->from_card = cards.first();
            pindian->to_card = cards.last();
        } else if (!pindian->to_card) {
            if (pindian->from_card->isVirtualCard())
                pindian->from_card = Sanguosha->getCard(pindian->from_card->getEffectiveId());
            pindian->to_card = room->askForPindian(target, source, "mtguzhao");
        } else if (!pindian->from_card) {
            if (pindian->to_card->isVirtualCard())
                pindian->to_card = Sanguosha->getCard(pindian->to_card->getEffectiveId());
            pindian->from_card = room->askForPindian(source, source, "mtguzhao");
        }
        cardss = pindian->from_card;
        hash[target] = pindian->to_card;
    }

    if (!cardss) return;

    FireSlash *fire_slash = new FireSlash(Card::NoSuit, 0);
    fire_slash->deleteLater();
    fire_slash->setSkillName("_mtguzhao");
    if (source->isLocked(fire_slash) || !fire_slash->IsAvailable(source)) return;

    QList<ServerPlayer *> slash_targets;
    bool all_win = true;
    foreach (ServerPlayer *target, new_targets) {
        int n = pindian(source, target, cardss, hash[target]);
        if (n == -2) continue;
        if (n != 1) all_win = false;

        if (!show[target] || show[target] < 0 || show[target] == hash[target]->getEffectiveId() || !source->canSlash(target, fire_slash, false)) continue;
        slash_targets << target;
    }

    if (slash_targets.isEmpty()) return;
    if (all_win) room->setCardFlag(fire_slash, "mtguzhao_all_win");
    room->useCard(CardUseStruct(fire_slash, source, slash_targets));
}

class MTGuzhaoVS : public ZeroCardViewAsSkill
{
public:
    MTGuzhaoVS() : ZeroCardViewAsSkill("mtguzhao")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTGuzhaoCard");
    }

    const Card *viewAs() const
    {
        return new MTGuzhaoCard;
    }
};

class MTGuzhao : public TriggerSkill
{
public:
    MTGuzhao() :TriggerSkill("mtguzhao")
    {
        events << ConfirmDamage;
        view_as_skill = new MTGuzhaoVS;
        waked_skills = "#mtguzhao";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("FireSlash") || !damage.card->hasFlag("mtguzhao_all_win")) return false;
        ++damage.damage;
        data = QVariant::fromValue(damage);
        return false;
    }
};

class MTGuzhaoTargetMod : public TargetModSkill
{
public:
    MTGuzhaoTargetMod() : TargetModSkill("#mtguzhao")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "mtguzhao")
            return 1000;
        return 0;
    }
};

class MTGuquVS : public ViewAsSkill
{
public:
    MTGuquVS() : ViewAsSkill("mtguqu")
    {
        response_pattern = "@@mtguqu";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Self->isJilei(to_select) || !to_select->hasSuit()) return false;

        QString record = Self->property("MTGuquSuits").toString();
        QStringList records = record.isEmpty() ? QStringList() : record.split("+");
        if (!records.contains(to_select->getSuitString())) return false;

        foreach (const Card *c, selected) {
            if (c->getSuit() == to_select->getSuit())
                return false;
        }
        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;

        QString record = Self->property("MTGuquSuits").toString();
        QStringList records = record.isEmpty() ? QStringList() : record.split("+");
        if (cards.length() != records.length()) return nullptr;

        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(cards);
        return card;
    }
};

class MTGuqu : public PhaseChangeSkill
{
public:
    MTGuqu() :PhaseChangeSkill("mtguqu")
    {
        view_as_skill = new MTGuquVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::NotActive;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        QStringList all_suits;
        all_suits << "heart" << "diamond" << "spade" << "club";

        QStringList records = player->tag["MTGuquRecord"].toStringList();
        player->tag.remove("MTGuquRecord");
        foreach (QString suit, records)
            all_suits.removeOne(suit);
        if (all_suits.isEmpty()) return false;

        records.clear();
		records << QString("@mtguqu-discard1%1").arg(all_suits.length());
        foreach (QString suit, all_suits)
            records << "<img src='image/system/cardsuit/" + suit + ".png' height=17/>";

        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            if (p->getCardCount() < all_suits.length()) continue;

            room->setPlayerProperty(p, "MTGuquSuits", all_suits.join("+"));
            const Card *c = room->askForCard(p, "@@mtguqu", records.join(""), QVariant(), objectName());

            if (!c || p->isDead()) continue;
            p->peiyin(this);
            p->gainAnExtraTurn();
        }
        return false;
    }
};

class MTLunhuanVS : public ViewAsSkill
{
public:
    MTLunhuanVS() : ViewAsSkill("mtlunhuan")
    {
        response_pattern = "@@mtlunhuan";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Self->isJilei(to_select) || to_select->isEquipped() || !to_select->hasSuit()) return false;

        QString record = Self->property("MTLunhuanSuits").toString();
        QStringList records = record.isEmpty() ? QStringList() : record.split("+");

        if (records.isEmpty()) return false;
        int heart = 0, diamond = 0, spade = 0, club = 0;
        int _heart = 0, _diamond = 0, _spade = 0, _club = 0;

        foreach (QString suit, records) {
            if (suit == "heart")
                heart++;
            else if (suit == "diamond")
                diamond++;
            else if (suit == "spade")
                spade++;
            else if (suit == "club")
                club++;
        }
        foreach (const Card *c, selected) {
            QString suit = c->getSuitString();
            if (suit == "heart")
                _heart++;
            else if (suit == "diamond")
                _diamond++;
            else if (suit == "spade")
                _spade++;
            else if (suit == "club")
                _club++;
        }

        if (_heart >= heart && to_select->getSuitString() == "heart") return false;
        if (_diamond >= diamond && to_select->getSuitString() == "diamond") return false;
        if (_spade >= spade && to_select->getSuitString() == "spade") return false;
        if (_club >= club && to_select->getSuitString() == "club") return false;

        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;

        QString record = Self->property("MTLunhuanSuits").toString();
        QStringList records = record.isEmpty() ? QStringList() : record.split("+");

        if (records.isEmpty()) return nullptr;
        int heart = 0, diamond = 0, spade = 0, club = 0;
        int _heart = 0, _diamond = 0, _spade = 0, _club = 0;

        foreach (QString suit, records) {
            if (suit == "heart")
                heart++;
            else if (suit == "diamond")
                diamond++;
            else if (suit == "spade")
                spade++;
            else if (suit == "club")
                club++;
        }
        foreach (const Card *c, cards) {
            QString suit = c->getSuitString();
            if (suit == "heart")
                _heart++;
            else if (suit == "diamond")
                _diamond++;
            else if (suit == "spade")
                _spade++;
            else if (suit == "club")
                _club++;
        }

        if (_heart != heart || _diamond != diamond || _spade != spade || _club != club) return nullptr;

        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(cards);
        return card;
    }
};

class MTLunhuan : public TriggerSkill
{
public:
    MTLunhuan() :TriggerSkill("mtlunhuan")
    {
        events << EventPhaseEnd;
        waked_skills = "#mtlunhuan";
        view_as_skill = new MTLunhuanVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isKongcheng()) continue;
            targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mtlunhuan-invoke", true, true);
        if (!t) return false;
        player->peiyin(this);

        QList<int> show_ids;
        if (t->getHandcardNum() <= 4)
            show_ids = t->handCards();
        else {
            for (int i = 0; i < 4; ++i) {
                if (t->getHandcardNum()<=i) break;
                int id = room->askForCardChosen(player, t, "h", objectName(), false, Card::MethodNone, show_ids);
				if(id<0) break;
                show_ids << id;
            }
        }
        if (show_ids.isEmpty()) return false;
        room->showCard(t, show_ids);

        if (player->isDead() || player->getCardCount() < show_ids.length() || player->getMark("MTLunhuanDamage-Keep") > 0) return false;

        QStringList suits;
        foreach (int id, show_ids)
            suits << Sanguosha->getCard(id)->getSuitString();

        room->setPlayerProperty(player, "MTLunhuanSuits", suits.join("+"));
        if (!room->askForCard(player, "@@mtlunhuan", QString("@mtlunhuan-discard:%1:%2").arg(t->objectName()).arg(show_ids.length()),
			QVariant::fromValue(t), objectName())) return false;
        player->peiyin(this);
        room->damage(DamageStruct(objectName(), player, t, show_ids.length(), DamageStruct::Fire));
        return false;
    }
};

class MTLunhuanDamage : public TriggerSkill
{
public:
    MTLunhuanDamage() :TriggerSkill("#mtlunhuan")
    {
        events << DamageDone;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || damage.reason != "mtlunhuan") return false;
        room->setPlayerMark(damage.from, "MTLunhuanDamage-Keep", 1);
        return false;
    }
};

MTJiyeCard::MTJiyeCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MTJiyeCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("yhjyye", this);
}

class MTJiyeVS : public ViewAsSkill
{
public:
    MTJiyeVS() : ViewAsSkill("mtjiye")
    {
        response_pattern = "@@mtjiye";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (!to_select->hasSuit()) return false;
        QList<int> ye = Self->getPile("yhjyye");
        QList<Card::Suit> all_suits;
        all_suits << Card::Heart << Card::Diamond << Card::Spade << Card::Club;

        foreach (int id, ye) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!all_suits.contains(suit)) continue;
            all_suits.removeOne(suit);
        }
        if (!all_suits.contains(to_select->getSuit())) return false;

        foreach(const Card *c, selected) {
            if (c->getSuit() == to_select->getSuit())
                return false;
        }

        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        MTJiyeCard *c = new MTJiyeCard;
        c->addSubcards(cards);
        return c;
    }
};

class MTJiye : public TriggerSkill
{
public:
    MTJiye() :TriggerSkill("mtjiye")
    {
        events << RoundStart;
        frequency = Compulsory;
        view_as_skill = new MTJiyeVS;
    }

    static int getQueshaoSuitsNum(Player *player)
    {
        QList<int> ye = player->getPile("yhjyye");
        QList<Card::Suit> all_suits;
        all_suits << Card::Heart << Card::Diamond << Card::Spade << Card::Club;
        foreach (int id, ye) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!all_suits.contains(suit)) continue;
            all_suits.removeOne(suit);
        }
        return all_suits.length();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        int num = getQueshaoSuitsNum(player);
        if (num <= 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        player->drawCards(num, objectName());
        if (player->isNude()) return false;
        room->askForUseCard(player, "@@mtjiye", "@mtjiye", -1, Card::MethodNone);
        return false;
    }
};

MTZhiheCard::MTZhiheCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MTZhiheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mtzhihe");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

    const Card *_card = Self->tag.value("mtzhihe").value<const Card *>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mtzhihe");
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool MTZhiheCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
        return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetFixed();
	}

    const Card *_card = Self->tag.value("mtzhihe").value<const Card *>();
    if (_card == nullptr)
        return true;

    return _card->targetFixed();
}

bool MTZhiheCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mtzhihe");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

    const Card *_card = Self->tag.value("mtzhihe").value<const Card *>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mtzhihe");
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *MTZhiheCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    Room *room = player->getRoom();

    QList<int> ye = player->getPile("yhjyye");
    if (ye.isEmpty()) return nullptr;

    QString to_yizan = user_string;

    if ((user_string.contains("slash") || user_string.contains("Slash")) &&
            Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        foreach (int id, ye) {
            const Card *c = Sanguosha->getCard(id);
            QString name = c->objectName();
            if (c->isKindOf("Slash") && !guhuo_list.contains(name))
                guhuo_list << name;
        }
        if (guhuo_list.isEmpty()) return nullptr;
        to_yizan = room->askForChoice(player, "mtzhihe_slash", guhuo_list.join("+"));
    }

    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card *use_card = Sanguosha->cloneCard(to_yizan, Card::SuitToBeDecided, -1);
    use_card->setSkillName("mtzhihe");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mtzhihe");
	use_card->deleteLater();
    return use_card;
}

const Card *MTZhiheCard::validateInResponse(ServerPlayer *player) const
{
    QList<int> ye = player->getPile("yhjyye");
    if (ye.isEmpty()) return nullptr;

    Room *room = player->getRoom();

    QString to_yizan;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        foreach (int id, ye) {
            const Card *c = Sanguosha->getCard(id);
            QString name = c->objectName();
            if (c->isKindOf("Peach") && !guhuo_list.contains(name))
                guhuo_list << name;
            else if (c->isKindOf("Analeptic") && !guhuo_list.contains(name))
                guhuo_list << name;
        }
        if (guhuo_list.isEmpty()) return nullptr;
        to_yizan = room->askForChoice(player, "mtzhihe_saveself", guhuo_list.join("+"));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList guhuo_list;
        foreach (int id, ye) {
            const Card *c = Sanguosha->getCard(id);
            QString name = c->objectName();
            if (c->isKindOf("Slash") && !guhuo_list.contains(name))
                guhuo_list << name;
        }
        if (guhuo_list.isEmpty()) return nullptr;
        to_yizan = room->askForChoice(player, "mtzhihe_slash", guhuo_list.join("+"));
    } else
        to_yizan = user_string;

    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card *use_card = Sanguosha->cloneCard(to_yizan, Card::SuitToBeDecided, -1);
    use_card->setSkillName("mtzhihe");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mtzhihe");
	use_card->deleteLater();
    return use_card;
}

class MTZhiheVS : public ViewAsSkill
{
public:
    MTZhiheVS() : ViewAsSkill("mtzhihe")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->getPile("yhjyye").isEmpty();
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        QList<int> ye = player->getPile("yhjyye");
        if (ye.isEmpty()) return false;
        if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;

        bool can_use = false;
        QStringList patterns = pattern.split("+");

        foreach (QString pat, patterns) {
            QStringList names = pat.split(",");
            foreach (QString name, names) {
                name = name.toLower();
                Card *card = Sanguosha->cloneCard(name);
                if (!card) continue;
                card->deleteLater();

                foreach (int id, ye) {
                    const Card *c = Sanguosha->getCard(id);
                    if (c->sameNameWith(card)) {  //如果要求的是【火杀】，而c是【杀】，就不能这么判断。这里偷懒不管了
                        can_use = true;
                        break;
                    }
                }
                if (can_use) break;
            }
            if (can_use) break;
        }
        return can_use;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        QList<int> ye = player->getPile("yhjyye");
        if (ye.isEmpty()) return false;
        foreach (int id, ye) {
            const Card *c = Sanguosha->getCard(id);
            if (c->isKindOf("Nullification"))
                return true;
        }
        return false;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped()) return false;

        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
            if (Self->isCardLimited(to_select, Card::MethodResponse))
                return false;
        } else {
            if (Self->isLocked(to_select))
                return false;
        }

        int num = MTJiye::getQueshaoSuitsNum(Self);
        num = qMax(num, 1);
        if (selected.length() >= num) return false;
        if (selected.isEmpty()) return true;
        return selected.first()->getSuit() == to_select->getSuit();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int num = MTJiye::getQueshaoSuitsNum(Self);
        num = qMax(num, 1);
        if (cards.length() != num || cards.isEmpty()) return nullptr;

        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            MTZhiheCard *card = new MTZhiheCard;
            card->setUserString(Sanguosha->getCurrentCardUsePattern());
            card->addSubcards(cards);
            return card;
        }

        const Card *c = Self->tag.value("mtzhihe").value<const Card *>();
        if (c && c->isAvailable(Self)) {
            MTZhiheCard *card = new MTZhiheCard;
            card->setUserString(c->objectName());
            card->addSubcards(cards);
            return card;
        }
        return nullptr;
    }
};

class MTZhihe : public TriggerSkill
{
public:
    MTZhihe() : TriggerSkill("mtzhihe")
    {
        events << CardFinished;
        view_as_skill = new MTZhiheVS;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("mtzhihe", true, true, true);
    }

    int getPriority(TriggerEvent) const
    {
        return 0;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = nullptr;
        if (event == CardFinished)
            card = data.value<CardUseStruct>().card;
        if (!card || card->isKindOf("SkillCard") || (!card->hasFlag("mtzhihe") && !card->getSkillNames().contains(objectName()))) return false;

        QList<int> remove;
        foreach (int id, player->getPile("yhjyye")) {
            if (Sanguosha->getCard(id)->sameNameWith(card, true))
                remove << id;
        }
        if (remove.isEmpty()) return false;

        DummyCard remove_card(remove);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), objectName(), "");
        room->throwCard(&remove_card, reason, nullptr);
        return false;
    }
};

class MTWenqi : public MasochismSkill
{
public:
    MTWenqi() : MasochismSkill("mtwenqi")
    {
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        if (!player->hasTurn()) return;
        ServerPlayer *from = damage.from, *to = damage.to;
        if (!from || from == to) return;

        Room *room = player->getRoom();

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (from->isDead()) return;
            if (p->isDead() || !p->hasSkill(objectName()) || p->getMark("mtwenqi_Used-Clear") > 0) continue;
            if (from == p || to == p) {
                room->sendCompulsoryTriggerLog(p, this);
                room->addPlayerMark(p, "mtwenqi_Used-Clear");

                QStringList choices;
                int num = MTJiye::getQueshaoSuitsNum(p);

                if (!p->getPile("yhjyye").isEmpty())
                    choices << "get=" + QString::number(num + 1);
                choices << "draw=" + QString::number(num);

                QString choice = room->askForChoice(from, objectName(), choices.join("+"), QVariant::fromValue(p));

                if (choice.startsWith("get")) {
                    QList<int> ye = p->getPile("yhjyye");
                    if (!ye.isEmpty()) {
                        room->fillAG(ye, from);
                        int id = room->askForAG(from, ye, false, objectName(), "@mtwenqi-get");
                        room->clearAG(from);

                        if (from == p) {
                            LogMessage log;
                            log.type = "$KuangbiGet";
                            log.from = from;
                            log.arg = "yhjyye";
                            log.card_str = QString::number(id);
                            room->sendLog(log);
                        }
                        room->obtainCard(from, id);

                        num = MTJiye::getQueshaoSuitsNum(p);
                        if (num > 0 && from->isAlive() && !from->isNude())
                            room->askForDiscard(from, objectName(), num, num, false, true);
                    }
                } else {
                    num = MTJiye::getQueshaoSuitsNum(p);
                    from->drawCards(num, objectName());
                    if (from->isAlive())
                        from->turnOver();
                }
            }
        }
    }
};

MaotuPackage::MaotuPackage()
    : Package("maotu")
{
    General *mt_wenhui = new General(this, "mt_wenhui", "wei", 3);
    mt_wenhui->addSkill(new MTLiaoshi);
    mt_wenhui->addSkill(new MTLiaoshiChoose);
    mt_wenhui->addSkill(new MTTongyi);

    General *mt_xiahouba = new General(this, "mt_xiahouba", "wei", 4);
    mt_xiahouba->addSkill(new MTXianzheng);
    mt_xiahouba->addSkill(new MTNianchou);
    mt_xiahouba->addSkill(new MTNianchouTargetMod);

    General *mt_zhugeshang = new General(this, "mt_zhugeshang", "shu", 4);
    mt_zhugeshang->addSkill(new MTJieli);
    mt_zhugeshang->addSkill(new MTJieliTargetMod);
    mt_zhugeshang->addSkill(new MTFuyi);
    mt_zhugeshang->addSkill(new MTFuyiDamage);
    mt_zhugeshang->addSkill(new MTFuyiTurn);

    General *mt_luoxian = new General(this, "mt_luoxian", "shu", 4);
    mt_luoxian->addSkill(new MTZhongyi);

    General *mt_zhaoshuang = new General(this, "mt_zhaoshuang", "wu", 3);
    mt_zhaoshuang->addSkill(new MTWeiqie);
    mt_zhaoshuang->addSkill(new MTGuanda);

    General *mt_weizhao = new General(this, "mt_weizhao", "wu", 3);
    mt_weizhao->addSkill(new MTZhilie);
    mt_weizhao->addSkill(new MTChuanjiu);

    General *mt_liubei = new General(this, "mt_liubei", "qun", 3);
    mt_liubei->addSkill(new MTDianpei);
    mt_liubei->addSkill(new MTRenyi);
    mt_liubei->addSkill(new MTRenyiTargetMod);

    General *mt_zhurong = new General(this, "mt_zhurong", "qun", 4, false);
    mt_zhurong->addSkill(new MTFeiren);
    mt_zhurong->addSkill(new MTFeirenTargetMod);
    mt_zhurong->addSkill(new MTFuzhan);

    General *mt_simayan = new General(this, "mt_simayan$", "jin", 4);
    mt_simayan->addSkill(new MTRenyu);
    mt_simayan->addSkill(new MTFengshang);
    mt_simayan->addSkill(new MTFengshangKeep);
    mt_simayan->addSkill(new MTJiawei);

    General *mt_wangjun = new General(this, "mt_wangjun", "jin", 4);
    mt_wangjun->addSkill(new MTGuzhao);
    mt_wangjun->addSkill(new MTGuzhaoTargetMod);

    General *mt_shenzhouyu = new General(this, "mt_shenzhouyu", "god", 4);
    mt_shenzhouyu->addSkill(new MTGuqu);
    mt_shenzhouyu->addSkill(new MTLunhuan);
    mt_shenzhouyu->addSkill(new MTLunhuanDamage);
    mt_shenzhouyu->addSkill("yingzi");

    General *mt_shencaopi = new General(this, "mt_shencaopi", "god", 3);
    mt_shencaopi->addSkill(new MTJiye);
    mt_shencaopi->addSkill(new MTZhihe);
    mt_shencaopi->addSkill(new MTWenqi);

    addMetaObject<MTJieliCard>();
    addMetaObject<MTWeiqieCard>();
    addMetaObject<MTZhilieCard>();
    addMetaObject<MTRenyiCard>();
    addMetaObject<MTGuzhaoCard>();
    addMetaObject<MTJiyeCard>();
    addMetaObject<MTZhiheCard>();
}

ADD_PACKAGE(Maotu)
