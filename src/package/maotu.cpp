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
        QString choice = Self->getTag("mtjieli").toString();

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
                    player->setTag("MTRenyuData", data);
                    bool invoke = player->askForSkillInvoke(this, p);
                    player->removeTag("MTRenyuData");

                    if (!invoke) continue;
                    player->peiyin(this);

                    int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                    room->throwCard(id, p, player);

                    use.nullified_list << p->objectName();
                    data = QVariant::fromValue(use);
                } else {
                    p->setTag("MTRenyuData", data);
                    bool invoke = p->askForSkillInvoke("mtrenyu_jin", "mtrenyu_jin");
                    p->removeTag("MTRenyuData");
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

        QStringList records = player->getTag("MTGuquRecord").toStringList();
        player->removeTag("MTGuquRecord");
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

    const Card *_card = Self->getTag("mtzhihe").value<const Card *>();
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

    const Card *_card = Self->getTag("mtzhihe").value<const Card *>();
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

    const Card *_card = Self->getTag("mtzhihe").value<const Card *>();
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

        const Card *c = Self->getTag("mtzhihe").value<const Card *>();
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


class MTYanyi : public TriggerSkill
{
public:
    MTYanyi() : TriggerSkill("mtyanyi")
    {
        events << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent) const
    {
        return -1;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMaxHp() <= 0 || player->getPhase() == Player::NotActive) return false;
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        Player::Phase phase = change.to;
        if (player->isSkipped(phase) || phase == Player::NotActive || phase == Player::RoundStart) return false;
        room->addPlayerMark(player, "mtyanyi_phase-Clear");
        if (!player->hasSkill(this) || player->getMark("mtyanyi_phase-Clear") != player->getMaxHp()) return false;

        QStringList phases;
        phases << "roundstart" << "start" << "judge" << "draw" << "play" << "discard" << "finish" << "notactive";

        LogMessage log;
        log.type = "#MTYanyiPhase";
        log.from = player;
        log.arg = objectName();
        log.arg2 = phases.at(int(phase));
        log.arg3 = "play";
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        player->peiyin(this);

        change.to = Player::Play;
        data = QVariant::fromValue(change);
        return false;
    }
};

MTJishiCard::MTJishiCard()
{
}

bool MTJishiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_mtjishi");
    slash->setFlags("mtjishi_user_" + Self->objectName());
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, slash, targets);
}

void MTJishiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QString choice = user_string;
    if (choice == "hp")
        room->loseHp(HpLostStruct(source, 1, "mtjishi", source));
    else
        room->loseMaxHp(source, 1, "mtjishi");
    if (source->isDead()) return;

    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_mtjishi");
    room->setCardFlag(slash, "mtjishi_user_" + source->objectName());
    slash->deleteLater();

    if (source->isLocked(slash)) return;

    QList<ServerPlayer *>tos;
    foreach (ServerPlayer *p, targets) {
        if (source->canSlash(p, slash, false))
            tos << p;
    }
    if (tos.isEmpty()) return;

    room->setCardFlag(slash, "SlashIgnoreArmor");
    room->useCard(CardUseStruct(slash, source, tos));
}

class MTJishiVS : public ZeroCardViewAsSkill
{
public:
    MTJishiVS() : ZeroCardViewAsSkill("mtjishi")
    {
    }

    const Card *viewAs() const
    {
        QString choice = Self->getTag("mtjishi").toString();
        if (choice.isEmpty()) return NULL;
        MTJishiCard *card = new MTJishiCard;
        card->setUserString(choice);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTJishiCard") && player->getKingdom() == "wei";
    }
};

class MTJishi : public TriggerSkill
{
public:
    MTJishi() : TriggerSkill("mtjishi")
    {
        events << Damage;
        view_as_skill = new MTJishiVS;
        waked_skills = "#mtjishi";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    QDialog *getDialog() const
    {
        return TiansuanDialog::getInstance("mtjishi", "hp,maxhp");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.card || !damage.card->isKindOf("Slash") || !damage.by_user) return false;
        if (damage.card->getSkillName() != objectName() && !damage.card->hasFlag("mtjishi_used_slash")) return false;

        ServerPlayer *user = NULL;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (damage.card->hasFlag("mtjishi_user_" + p->objectName())) {
                user = p;
                break;
            }
        }
        if (!user || user->isDead() || damage.from != user) return false;

        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(1, objectName());
        if (user->isDead()) return false;

        QStringList choices;
        choices << "add";
        if (user->isWounded())
            choices << "recover";
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));
        if (choice == "add")
            room->gainMaxHp(user, 1, objectName());
        else
            room->recover(user, RecoverStruct(objectName(), user, 1));
        return false;
    }
};

class MTJishiTargetMod : public TargetModSkill
{
public:
    MTJishiTargetMod() : TargetModSkill("#mtjishi")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (card->hasFlag("mtjishi_user_" + from->objectName()) && (card->getSkillName() == "mtjishi" || card->hasFlag("mtjishi_used_slash")))
            return 1000;
        else
            return 0;
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->hasFlag("mtjishi_user_" + from->objectName()) && (card->getSkillName() == "mtjishi" || card->hasFlag("mtjishi_used_slash")))
            return 1000;
        else
            return 0;
    }
};

MTYitaoCard::MTYitaoCard()
{
}

bool MTYitaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Duel *duel = new Duel(Card::NoSuit, 0);
    duel->setSkillName("_mtyitao");
    duel->deleteLater();
    return duel->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, duel, targets);
}

void MTYitaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QString choice = user_string;
    if (choice == "hp")
        room->loseHp(HpLostStruct(source, 1, "mtyitao", source));
    else
        room->loseMaxHp(source, 1, "mtyitao");
    if (source->isDead()) return;

    Duel *duel = new Duel(Card::NoSuit, 0);
    duel->setSkillName("_mtyitao");
    duel->deleteLater();

    if (source->isLocked(duel)) return;

    QList<ServerPlayer *>tos;
    foreach (ServerPlayer *p, targets) {
        if (source->canUse(duel, p, true))
            tos << p;
    }
    if (tos.isEmpty()) return;

    CardUseStruct use;
    use.card = duel;
    use.from = source;
    use.to = tos;
    use.no_offset_list << "_ALL_TARGETS";
    room->useCard(use);
}

class MTYitaoVS : public ZeroCardViewAsSkill
{
public:
    MTYitaoVS() : ZeroCardViewAsSkill("mtyitao")
    {
    }

    const Card *viewAs() const
    {
        QString choice = Self->getTag("mtyitao").toString();
        if (choice.isEmpty()) return NULL;
        MTYitaoCard *card = new MTYitaoCard;
        card->setUserString(choice);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTYitaoCard") && player->getKingdom() == "wu";
    }
};

class MTYitao : public TriggerSkill
{
public:
    MTYitao() : TriggerSkill("mtyitao")
    {
        events << Damage;
        view_as_skill = new MTYitaoVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    QDialog *getDialog() const
    {
        return TiansuanDialog::getInstance("mtyitao", "hp,maxhp");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.card || !damage.card->isKindOf("Duel") || !damage.by_user || !damage.to) return false;
        if (damage.card->getSkillName() != objectName()) return false;

        ServerPlayer *user = room->getCardUser(damage.card);
        if (!user || user->isDead() || damage.from != user) return false;

        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(1, objectName());
        if (user->isDead()) return false;

        int hp = damage.to->getHp(), hand = damage.to->getHandcardNum();
        hp = qMax(1, hp);
        hp = qMin(hp, 6);
        hand = qMax(1, hand);
        hand = qMin(hand, 6);

        QStringList choices;
        choices << QString("hp2=%1=%2").arg(damage.to->objectName()).arg(hp);
        choices << QString("hand=%1=%2").arg(damage.to->objectName()).arg(hand);
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));

        int maxhp = user->getMaxHp();
        if (choice.startsWith("hp2")) {
            //room->setPlayerProperty(user, "maxhp", hp);
            hp = damage.to->getHp();
            hp = qMax(1, hp);
            hp = qMin(hp, 6);
            if (maxhp < hp)
                room->gainMaxHp(user, hp - maxhp, objectName());
            else if (maxhp > hp)
                room->loseMaxHp(user, maxhp - hp, objectName());
         } else {
            //room->setPlayerProperty(user, "maxhp", hand);
            hand = damage.to->getHandcardNum();
            hand = qMax(1, hand);
            hand = qMin(hand, 6);
            if (maxhp < hand)
                room->gainMaxHp(user, hand - maxhp, objectName());
            else if (maxhp > hand)
                room->loseMaxHp(user, maxhp - hand, objectName());
        }
        return false;
    }
};

class MTJuyuan : public TriggerSkill
{
public:
    MTJuyuan() : TriggerSkill("mtjuyuan")
    {
        events << Dying;
        waked_skills = "#mtjuyuan-flag,#mtjuyuan-prohibit";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *who = dying.who;
        if (who->isDead() || who == player) return false;

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (who->inMyAttackRange(p))
                targets << p;
        }
        if (targets.isEmpty()) return false;

        targets = room->askForPlayersChosen(player, targets, objectName(), 0, targets.length(), "@mtjuyuan-draw:" + who->objectName());
        if (targets.isEmpty()) return false;
        player->peiyin(this);
        room->notifySkillInvoked(player, objectName());

        int n = targets.length();

        room->drawCards(targets, 1, objectName());

        foreach (ServerPlayer *p, targets) {
            if (p->isAlive())
                room->setPlayerFlag(p, "mtjuyuanPreventPeach_" + who->objectName());
        }

        DamageStruct *damage = dying.damage;
        QStringList tips = damage->tips;
        tips << "mtjuyuanNum_" + QString::number(n) << "mtjuyuanPlayer_" + player->objectName();
        damage->tips = tips;

        dying.damage = damage;
        data = QVariant::fromValue(dying);
        return false;
    }
};

class MTJuyuanFlag : public TriggerSkill
{
public:
    MTJuyuanFlag() : TriggerSkill("#mtjuyuan-flag")
    {
        events << AskForPeaches << AskForPeachesDone << QuitDying << EventPhaseChanging;
    }

    int getPriority(TriggerEvent event) const
    {
        if (event == AskForPeaches || event == AskForPeachesDone)
            return 5; // TriggerSkill::getPriority(event) + 1;
        return TriggerSkill::getPriority(event);
    }

    bool triggerable(const ServerPlayer *target, Room *, TriggerEvent event) const
    {
        if (event == AskForPeachesDone || event == EventPhaseChanging)
            return target != NULL;
        else
            return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == AskForPeachesDone) {
            DyingStruct dying = data.value<DyingStruct>();
            ServerPlayer *who = dying.who;
            QList<ServerPlayer *> targets = room->getAllPlayers(true);
            foreach (ServerPlayer *p, targets) {
                if (p->hasFlag("mtjuyuanPreventPeach"))
                    room->setPlayerFlag(p, "-mtjuyuanPreventPeach_" + who->objectName());
            }
        } else if (event == QuitDying) {
            int num = 0;
            ServerPlayer * from = NULL;

            QStringList tips = data.value<DyingStruct>().damage->tips;
            foreach (QString tip, tips) {
                if (tip.startsWith("mtjuyuanNum_")) {
                    int n = tip.split("_").last().toInt();
                    if (n <= 0) continue;
                    num = n;
                } else if (tip.startsWith("mtjuyuanPlayer_")) {
                    QString name = tip.split("_").last();
                    ServerPlayer *player = room->findChild<ServerPlayer *>(name);
                    if (!player || player->isDead()) continue;
                    from = player;
                }
            }

            if (num <= 0 || !from || from->isDead()) return false;

            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mtjuyuan");
            slash->deleteLater();
            try {
                for (int i = 0; i < num; i++) {
                    room->setCardFlag(slash, "SlashNoRespond");
                    if (player->isDead() || player->isLocked(slash) || from->isDead() || !player->canSlash(from,false))
                        break;
                    room->useCard(CardUseStruct(slash, player, from));
                }

                if (player->isAlive()) {//因为描述是“其于其下回合内不能使用牌指定除你与其外的角色为目标”，如果濒死的是孟达自己，这里就是当回合就加了标记而不是下回合，会导致bug
                    room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 1);
                    if (from->isAlive())
                        room->setPlayerMark(from, "mtjuyuanProhibited_" + player->objectName(), 1);
                }
            }
            catch (TriggerEvent triggerEvent) {
                if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
                    if (player->isAlive()) {   //因为描述是“其于其下回合内不能使用牌指定除你与其外的角色为目标”，如果濒死的是孟达自己，就是当回合不是下回合
                        room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 1);
                        if (from->isAlive())
                            room->setPlayerMark(from, "mtjuyuanProhibited_" + player->objectName(), 1);
                    }
                }
            }

        } else if (event == AskForPeaches) {  //防止其他技能的AskForPeaches时机return true，导致flag没被清除，所以在AskForPeachesDone再清除一次
            DyingStruct dying = data.value<DyingStruct>();
            ServerPlayer *who = dying.who;
            if (player->hasFlag("mtjuyuanPreventPeach_" + who->objectName())) {
                room->setPlayerFlag(player, "-mtjuyuanPreventPeach_" + who->objectName());
                return true;
            }
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            if (player->getMark("mtjuyuanProhibited-Keep") <= 0) return false;
            room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 0);
            QList<ServerPlayer *> targets = room->getAllPlayers(true);
            foreach (ServerPlayer *p, targets)
                room->setPlayerMark(p, "mtjuyuanProhibited_" + player->objectName(), 0);
        }

        return false;
    }
};

class MTJuyuanProhibit : public ProhibitSkill
{
public:
    MTJuyuanProhibit() : ProhibitSkill("#mtjuyuan-prohibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from->getPhase() == Player::NotActive || from->getMark("mtjuyuanProhibited-Keep") <= 0 || card->isKindOf("SkillCard")) return false;
        return from != to && to->getMark("mtjuyuanProhibited_" + from->objectName()) <= 0;
    }
};

class MTFupan : public MasochismSkill
{
public:
    MTFupan() : MasochismSkill("mtfupan")
    {
        frequency = Compulsory;
    }

    void MoveAndChange(Room *room, ServerPlayer *target, ServerPlayer *to, QList<int> ids) const
    {
        LogMessage log;
        log.type = "#ChoosePlayerWithSkill";
        log.from = target;
        log.to << to;
        log.arg = objectName();
        room->sendLog(log);
        room->doAnimate(1, target->objectName(), to->objectName());
        room->notifySkillInvoked(target, objectName());
        target->peiyin(this);

        room->giveCard(target, to, ids, objectName());
        if (target->isAlive()) {
            if (!room->canMoveField("ej")) return;
            room->moveField(target, objectName(), false, "ej");
            if (target->isDead() || target == to) return;
            room->changeKingdom(target, "wei");
        }
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        Room *room = target->getRoom();
        QString kim = target->getKingdom();
        ServerPlayer *from = damage.from;

        if (kim == "wei") {
            if (!from || from->isDead() || from->isAllNude()) return;
            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();
            QStringList areas;
            areas << "h" << "e" << "j";

            for (int i = 0; i < 3; i++) {
                if (!from || from->isDead() || from->getCards(areas.at(i)).isEmpty() || target->isDead()) break;
                int id = room->askForCardChosen(target, from, areas.at(i), objectName());
                if (id > 0)
                    dummy->addSubcard(id);
            }
            if (target->isDead() || dummy->subcardsLength() <= 0) return;
            room->obtainCard(target, dummy, objectName());

            if (target->isDead()) return;
            QList<int> ids, hands = target->handCards();
            foreach (int id, dummy->getSubcards()) {
                if (!hands.contains(id)) continue;
                ids << id;
            }
            if (ids.isEmpty()) return;
            QList<ServerPlayer *> targets = room->getAllPlayers();
            targets.removeOne(from);
            if (targets.isEmpty()) return;

            room->fillAG(ids, target);
            ServerPlayer *to = room->askForPlayerChosen(target, targets, objectName(), "@mtfupan-give:" + from->objectName());
            room->clearAG(target);
            if (target != to)
                room->giveCard(target, to, dummy, objectName());

            if (target->isAlive() && to != target)
                room->changeKingdom(target, "shu");
        } else if (kim == "shu") {
            if (target->isKongcheng()) return;
            QList<ServerPlayer *> targets = room->getOtherPlayers(target);
            if (from)
                targets.removeOne(from);
            if (targets.isEmpty()) return;

            QList<int> handcards = target->handCards();
            CardsMoveStruct move = room->askForYijiStruct(target, handcards, objectName(), false, false, false, 999, targets,
                            CardMoveReason(), from ? "@mtfupan-give2:" + from->objectName() : "@mtfupan-give3", false, false);

            if (move.to && !move.card_ids.isEmpty())
                MoveAndChange(room, target, (ServerPlayer *)move.to, move.card_ids);
            else {
                int id = target->getRandomHandCardId();
                ServerPlayer * to = targets.at(qrand() % targets.length());
                MoveAndChange(room, target, to, QList<int>() << id);
            }
        }
    }
};

class MTFeiyan : public TriggerSkill
{
public:
    MTFeiyan() : TriggerSkill("mtfeiyan")
    {
        events << TargetSpecified;
        waked_skills = "#mtfeiyan";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play || player->getMark("mtfeiyan-PlayClear") > 0) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.length() != 1) return false;
        ServerPlayer *to = use.to.first();
        if (to == player || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return false;

        int from_num = player->getEquips().length(), to_num = to->getEquips().length();
        if (from_num > to_num) return false;

        int num = qMax(1, to_num - from_num);
        QString kim = player->getKingdom();
        if (kim == "wei") {
            if (!player->askForSkillInvoke(this, QString("wei:%1::%2:%3").arg(to->objectName()).arg(use.card->objectName()).arg(num))) return false;
            player->peiyin(this);
            room->setCardFlag(use.card, QString("mtfeiyanEffect_%1_%2_%3").arg(player->objectName()).arg(to->objectName()).arg(num));
            room->addPlayerMark(player, "mtfeiyan-PlayClear");

            LogMessage log;
            log.type = "#TongliTimes";
            log.card_str = use.card->toString();
            log.arg = QString::number(num);
            log.to = use.to;
            room->sendLog(log);

            if (num > 2 && player->isAlive())
                player->setTag("mtfeiyan", "qun");  //不考虑如果发动多次“飞燕”，应该在出牌阶段结束时多次触发效果了
        } else if (kim == "qun") {
            if (to->isNude()) return false;
            if (!player->askForSkillInvoke(this, QString("qun:%1::%2").arg(to->objectName()).arg(num))) return false;
            player->peiyin(this);
            room->addPlayerMark(player, "mtfeiyan-PlayClear");

            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();
            for (int i = 0; i < num; i++) {
                int id = room->askForCardChosen(player, to, "he", objectName(), false, Card::MethodNone, dummy->getSubcards());
                if (id < 0) break; //id<0应该随机获得一张满足条件的牌，这里偷懒
                dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() <= 0) return false;
            room->obtainCard(player, dummy, objectName());

            if (num > 2 && player->isAlive())
                player->setTag("mtfeiyan", "wei");  //不考虑如果发动多次“飞燕”，应该在出牌阶段结束时多次触发效果了
        }
        return false;
    }
};

class MTFeiyanEffect : public TriggerSkill
{
public:
    MTFeiyanEffect() : TriggerSkill("#mtfeiyan")
    {
        events << EventPhaseEnd << CardFinished << DamageInflicted;
    }

    bool triggerable(const ServerPlayer *target, Room *, TriggerEvent event) const
    {
        if (event == EventPhaseEnd)
            return target && target->isAlive() && target->getPhase() == Player::Play && !target->getTag("mtfeiyan").toString().isEmpty();
        else
            return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseEnd) {
            QString kim = player->getTag("mtfeiyan").toString();
            player->removeTag("mtfeiyan");
            /*if (kim == "qun" || kim == "wei")
                room->changeKingdom(player, kim);
            else {
                if (player->getKingdom() == "qun")
                    room->changeKingdom(player, "wei");
                else if (player->getKingdom() == "wei")
                    room->changeKingdom(player, "qun");
                else
                    room->changeKingdom(player, "wei");
            }*/
            if (kim == "qun" || kim == "wei") {
                LogMessage log;
                log.type = "#TriggerSkill";
                log.from = player;
                log.arg = "mtfeiyan";
                room->sendLog(log);
                player->peiyin("mtfeiyan");
                room->notifySkillInvoked(player, "mtfeiyan");
                room->changeKingdom(player, kim);

                room->addPlayerMark(player, "&mtfeiyanDamage");
            }
        } else if (event == DamageInflicted) {
            int n = player->getMark("&mtfeiyanDamage");
            if (n <= 0) return false;
            room->setPlayerMark(player, "&mtfeiyanDamage", 0);

            DamageStruct damage = data.value<DamageStruct>();
            int a = damage.damage, b = a + n;

            damage.damage = b;
            data = QVariant::fromValue(damage);

            LogMessage log;
            log.type = "#MTFeiyanDamage";
            log.from = player;
            log.arg = "mtfeiyan";
            log.arg2 = QString::number(a);
            log.arg3 = QString::number(b);
            room->sendLog(log);
            player->peiyin("mtfeiyan");
            room->notifySkillInvoked(player, "mtfeiyan");
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            const Card *card = use.card;
            if (!(card->isKindOf("Slash") || card->isNDTrick())) return false;

            ServerPlayer *from = NULL, *to = NULL;
            int num = 0;

            foreach (QString flag, card->getFlags()) {
                if (!flag.startsWith("mtfeiyanEffect")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 4) continue;
                from = room->findChild<ServerPlayer *>(flags.at(1));
                to = room->findChild<ServerPlayer *>(flags.at(2));
                num = flags.last().toInt();

                if (from && from->isAlive() && to && to->isAlive() && num > 0) break;
            }
            if (!from || from->isDead() || !to || to->isDead() || num <= 0) return false;

            for (int i = 0; i < num; i++) {
                if (from->isDead() || to->isDead()) break;
                card->use(room, from, QList<ServerPlayer *>() << to);
            }
        }
        return false;
    }
};

class MTJiukuang : public TriggerSkill
{
public:
    MTJiukuang() : TriggerSkill("mtjiukuang")
    {
        events << CardFinished;
        frequency = Compulsory;
        waked_skills = "#mtjiukuang";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Analeptic")) return false;

        room->sendCompulsoryTriggerLog(player, this);

        if (player->getLostHp() > 0)
            room->recover(player, RecoverStruct(objectName(), player, 1));
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

class MTJiukuangTMD : public TargetModSkill
{
public:
    MTJiukuangTMD() : TargetModSkill("#mtjiukuang")
    {
        pattern = "Analeptic";
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->getPhase() == Player::Play && from->hasSkill("mtjiukuang"))
            return 10000;
        return 0;
    }
};

MTZongqingCard::MTZongqingCard()
{
    target_fixed = true;
}

void MTZongqingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isKongcheng()) return;
    QList<int> types;
    QList<Card::Suit> suits;
    bool damage = false;
    foreach (int id, source->handCards()) {
        if (!source->canDiscard(source, id)) continue;
        const Card *c = Sanguosha->getCard(id);
        int type = c->getTypeId();
        Card::Suit suit = c->getSuit();
        if (!types.contains(type))
            types << type;
        if (!suits.contains(suit))
            suits << suit;
        if (c->isDamageCard())
            damage = true;
    }

    source->throwAllHandCards();
    if (source->isDead()) return;
    source->drawCards(1, "mtzongqing");
    if (source->isDead()) return;

    if (types.length() >= 2) {
        Analeptic *ana = new Analeptic(Card::NoSuit, 0);
        ana->setSkillName("_mtzongqing");
        ana->deleteLater();
        if (source->canUse(ana, source, true))
            room->useCard(CardUseStruct(ana, source, source));
    }
    if (suits.length() >= 2 && source->isAlive()) {
        ServerPlayer *to = room->askForPlayerChosen(source, room->getAllPlayers(), "mtzongqing", "@mtzongqing-draw");
        room->doAnimate(1, source->objectName(), to->objectName());
        to->drawCards(2, "mtzongqing");
    }
    if (damage && source->isAlive())
        room->addPlayerMark(source, "&mtzongqing");
        //room->addDistance(source, 1, false, false);
}

class MTZongqingVS : public ZeroCardViewAsSkill
{
public:
    MTZongqingVS() : ZeroCardViewAsSkill("mtzongqing")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTZongqingCard") && !player->isKongcheng();
    }

    const Card *viewAs() const
    {
        return new MTZongqingCard;
    }
};

class MTZongqing : public PhaseChangeSkill
{
public:
    MTZongqing() : PhaseChangeSkill("mtzongqing")
    {
        view_as_skill = new MTZongqingVS;
        waked_skills = "#mtzongqing";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && target->getPhase() == Player::RoundStart && target->getMark("&mtzongqing") > 0;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        room->setPlayerMark(target, "&mtzongqing", 0);
        return false;
    }
};

class MTZongqingDis : public DistanceSkill
{
public:
    MTZongqingDis() : DistanceSkill("#mtzongqing")
    {
        frequency = NotCompulsory;
    }

    int getCorrect(const Player *, const Player *to) const
    {
        return to->getMark("&mtzongqing");
    }
};

MTZanzhangCard::MTZanzhangCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MTZanzhangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int x = Self->getHandcardNum() - Self->getHp();
    if (x > 0)
        return targets.isEmpty() && to_select != Self;
    else if (x < 0)
        return targets.isEmpty() && !(to_select->getEquips().isEmpty() && to_select->getJudgingArea().isEmpty());
    return false;
}

void MTZanzhangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    if (getSubcards().isEmpty()) {
        int id = room->askForCardChosen(source, target, "ej", "mtzanzhang");
        room->obtainCard(source, id, "mtzanzhang");
    } else
        room->giveCard(source, target, this, "mtzanzhang");
}

class MTZanzhangVS : public ViewAsSkill
{
public:
    MTZanzhangVS() : ViewAsSkill("mtzanzhang")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        int x = Self->getHandcardNum() - Self->getHp();
        if (x > 0)
            return !to_select->isEquipped() && selected.length() < x;
        else
            return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int x = Self->getHandcardNum() - Self->getHp();
        if (x == 0 || (x > 0 && cards.isEmpty())) return NULL;
        MTZanzhangCard *c = new MTZanzhangCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MTZanzhangCard") && player->getHandcardNum() != player->getHp();
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mtzanzhang";
    }
};

class MTZanzhang : public PhaseChangeSkill
{
public:
    MTZanzhang() : PhaseChangeSkill("mtzanzhang")
    {
        view_as_skill = new MTZanzhangVS;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;
        int x = target->getHandcardNum() - target->getHp();
        if (x == 0 || (x > 0 && target->isKongcheng())) return false;
        QString pro = x > 0 ? "@mtzanzhang-give:" + QString::number(x) : "@mtzanzhang-get";
        room->askForUseCard(target, "@@mtzanzhang", pro, -1, Card::MethodNone);
        return false;
    }
};

class MTHongya : public TriggerSkill
{
public:
    MTHongya() : TriggerSkill("mthongya")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive || current->getPhase() == Player::PhaseNone) return false;
        QString mark = QString("mthongya-%1Clear").arg(current->getPhase());
        if (player->getMark(mark) > 0) return false;

        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == move.to) return false;

        bool flag = false;
        if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
            if(move.reason.m_reason == CardMoveReason::S_REASON_GIVE)
                flag = true;
        } else if (move.from && move.to == player) {
            if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip) {
                if (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                    flag = true;
            }
        }
        if (!flag) return false;

        if (player->getKingdom() == "wu") {
            if (!player->canDiscard(player, "he")) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->canDiscard(p, "he"))
                    targets << p;
            }
            if (targets.isEmpty()) return false;

            ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mthongya-wu", true, true);
            if (!t) return false;
            player->peiyin(this);
            room->setPlayerMark(player, mark, 1);

            QList<CardsMoveStruct> moves;
            if (player->isAlive() && player->canDiscard(player, "he")) {
                int id = room->askForCardChosen(player, player, "he", objectName(), true, Card::MethodDiscard);
                QList<int> ids;
                ids << id;
                CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), objectName(), "");
                LogMessage log;
                log.from = player;
                log.type = "$DiscardCard";
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);
                CardsMoveStruct move(ids, NULL, Player::DiscardPile, reason);
                moves << move;
            }
            if (player->isAlive() && t->isAlive() && player->canDiscard(t, "he")) {
                int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard);
                QList<int> ids;
                ids << id;
                CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), t->objectName(), objectName(), "");
                LogMessage log;
                log.from = player;
                log.type = "$DiscardCardByOther";
                log.card_str = ListI2S(ids).join("+");
                log.to << t;
                room->sendLog(log);
                CardsMoveStruct move(ids, NULL, Player::DiscardPile, reason);
                moves << move;
            }
            room->moveCardsAtomic(moves, true);
        } else if (player->getKingdom() == "shu") {
            ServerPlayer *t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@mthongya-shu", true, true);
            if (!t) return false;
            player->peiyin(this);
            room->setPlayerMark(player, mark, 1);
            t->drawCards(1, objectName());
        }
        return false;
    }
};

MTYinglveCard::MTYinglveCard()
{
    mute = true;
}

bool MTYinglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int id = Self->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return false;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return false;
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTYinglveCard::targetFixed() const
{
    int id = Self->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return false;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return false;
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool MTYinglveCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    int id = Self->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return false;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return false;
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

void MTYinglveCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *from = card_use.from;
    int id = from->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return;
    room->useCard(CardUseStruct(c, from, card_use.to));
    from->drawCards(1, "mtyinglve");
}

class MTYinglveVS : public ZeroCardViewAsSkill
{
public:
    MTYinglveVS() : ZeroCardViewAsSkill("mtyinglve")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mtyinglve";
    }

    const Card *viewAs() const
    {
        int id = Self->getMark("mtyinglve_cardID") - 1;
        if (id < 0) return NULL;
        const Card *card = Sanguosha->getCard(id);
        if (!card) return NULL;
        //return card;  这样的话，【铁索连环】可以重铸，而不是描述说的只能使用
        return new MTYinglveCard;
    }
};

class MTYinglve : public PhaseChangeSkill
{
public:
    MTYinglve() : PhaseChangeSkill("mtyinglve")
    {
        view_as_skill = new MTYinglveVS;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Play) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mtyinglve-target", true, true);
        if (!t) return false;
        player->peiyin(this);


        QList<int> hands = t->handCards();
        if (!hands.isEmpty()) {
            int id = room->doGongxin(player, t, hands, objectName());
            if (id < 0) id = t->getRandomHandCardId();
            room->showCard(t, id);

            const Card *card = Sanguosha->getCard(id);

            room->setPlayerMark(t, "mtyinglve_cardID", id + 1);
            if (!card->isAvailable(t)  || t->isLocked(card, false) ||
                    !room->askForUseCard(t, "@@mtyinglve", "@mtyinglve:" + card->objectName())) {
                QList<int> ids;
                foreach (int card_id, t->handCards()) {
                    if (card_id == id) continue;
                    ids << card_id;
                }
                if (ids.isEmpty()) return false;

                DummyCard *dummy = new DummyCard(ids);
                dummy->deleteLater();

                LogMessage log;
                log.type = "$RecastCard";
                log.from = t;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);

                room->moveCardTo(dummy, t, NULL, Player::DiscardPile,
                          CardMoveReason(CardMoveReason::S_REASON_RECAST, t->objectName(), objectName(), ""));
                t->drawCards(ids.length(), "recast");
            }
        }
        return false;
    }
};

class MTXianding : public TriggerSkill
{
public:
    MTXianding() : TriggerSkill("mtxianding")
    {
        events << DamageCaused;
        waked_skills = "#mtxianding";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive) return false;
        if (current->getMark("mtxiandingUseCard-Clear") > 1) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead()) break;
            if (p->isDead() || !p->hasSkill(this)) continue;
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *to = damage.to;
            int d = damage.damage + 1;
            if (!p->askForSkillInvoke(this, QString("damage:%1:%2:%3").arg(player->objectName()).arg(to->objectName()).arg(d))) continue;
            p->peiyin(this);
            damage.damage = d;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class MTXiandingRecord : public TriggerSkill
{
public:
    MTXiandingRecord() : TriggerSkill("#mtxianding")
    {
        events << PreCardUsed;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current != player || current->getPhase() == Player::NotActive) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return false;
        room->addPlayerMark(player, "mtxiandingUseCard-Clear");
        int mark = player->getMark("mtxiandingUseCard-Clear");
        if (mark == 2)
            room->setCardFlag(use.card, "mtbishiSecond");
        return false;
    }
};

class MTWangheVS : public ZeroCardViewAsSkill
{
public:
    MTWangheVS() : ZeroCardViewAsSkill("mtwanghe")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mtwanghe!";
    }

    const Card *viewAs() const
    {
        int id = Self->getMark("mtwanghe_id") - 1;
        if (id < 0) return NULL;
        QString name = Sanguosha->getEngineCard(id)->objectName();
        Card *c = Sanguosha->cloneCard(name);
        c->setSkillName(objectName());
        return c;
    }
};

class MTWanghe : public TriggerSkill
{
public:
    MTWanghe() : TriggerSkill("mtwanghe")
    {
        events << Damaged << CardsMoveOneTime << Death;
        view_as_skill = new MTWangheVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == move.to || move.from != player) return false;
            int num = 0;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)
                    num++;
            }
            if (num < 2) return false;
        }

        QString e;
        if (event == Damaged)
            e = "damaged";
        else if (event == CardsMoveOneTime)
            e = "cardsmove";
        else
            e = "death";
        QString delete_mark = QString("mtwangheDelete_%1-Keep").arg(e);
        if (player->getMark(delete_mark) > 0) return false;

        QString mark = QString("mtwangheEvent_%1_lun").arg(e);

        if (player->getMark(mark) > 0) {
            QStringList records = player->property("mtwangheEvent_Delete").toStringList();
            if (records.contains("mtwanghe:" + e)) return false;
            records << "mtwanghe:" + e << ",";
            room->setPlayerProperty(player, "mtwangheEvent_Delete", records.join("+"));
            player->setSkillDescriptionSwap(objectName(), "%arg1", records.join("+"));
            room->changeTranslation(player, objectName(), 1);
            room->setPlayerMark(player, delete_mark, 1);

            LogMessage log;
            log.type = "#MTWangheDelete";
            log.from = player;
            log.arg = objectName();
            log.arg2 = "mtwanghe:" + e;
            room->sendLog(log);
            player->peiyin(this);
            room->notifySkillInvoked(player, objectName());
            return false;
        }

        QList<int> list = room->getAvailableCardList(player, "basic", objectName());
        if (list.isEmpty()) return false;
        room->fillAG(list, player);
        int id = room->askForAG(player, list, true, objectName(), "@mtwanghe-basic");
        room->clearAG(player);
        if (id < 0) return false;

        QString name = Sanguosha->getEngineCard(id)->objectName();
        room->setPlayerMark(player, "mtwanghe_id", id + 1);
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return false;
        card->deleteLater();
        card->setSkillName("mtwanghe");

        room->setPlayerMark(player, mark, 1);
        if (card->targetFixed())
            room->useCard(CardUseStruct(card, player), true);
        else {
            if (!room->askForUseCard(player, "@@mtwanghe!", "@mtwanghe:" + name)) {
                QList<ServerPlayer *> targets = room->getCardTargets(player, card);
                if (targets.isEmpty()) return false;
                ServerPlayer *to = targets.at(qrand() % targets.length());
                room->useCard(CardUseStruct(card, player, to), true);
            }
        }
        return false;
    }
};

class MTChunzu : public TriggerSkill
{
public:
    MTChunzu() : TriggerSkill("mtchunzu")
    {
        events << BuryVictim << DrawNCards;
        frequency = Limited;
        limit_mark = "@mtchunzuMark";
        waked_skills = "#mtchunzu";
    }

    bool triggerable(const ServerPlayer *target, Room *, TriggerEvent event) const
    {
        if (event == DrawNCards)
            return target && target->isAlive() && target->getMark("mtchunzuUsed") > 0;
        else
            return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == BuryVictim) {
            DeathStruct de = data.value<DeathStruct>();
            if (de.who->getRole() != "rebel") return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(de.who)) {
                if (p->isDead() || !p->hasSkill(this) || p->getMark("@mtchunzuMark") < 1) continue;
                if (!p->askForSkillInvoke(this)) continue;
                p->peiyin(this);
                room->removePlayerMark(p, "@mtchunzuMark");
                room->doSuperLightbox(p, "mtchunzu");
                room->setPlayerMark(p, "mtchunzuUsed", 1);
                int x = p->getMaxHp() - p->getHp();
                if (x > 0)
                    room->recover(p, RecoverStruct(objectName(), p, x));
            }
        } else {
            DrawStruct draw = data.value<DrawStruct>();
            int num = draw.num;
            if(draw.reason == "InitialHandCards" || num < 1) return false;

            draw.num = INT_MIN;
            data = QVariant::fromValue(draw);

            QVariantList used = player->getTag("MTChunzuUsedIds").toList(), got = player->getTag("MTChunzuGotIds").toList();;

            QList<int> used_ids;
            foreach (QVariant id, used) {
                int idd = id.toInt();
                if (room->getCardPlace(idd) == Player::DiscardPile && !got.contains(idd))
                    used_ids << idd;
            }

            if (used_ids.isEmpty()) {
                LogMessage log;
                log.type = "#MTChunzuNone";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                player->peiyin(this);
                room->notifySkillInvoked(player, objectName());
                return false;
            }

            room->sendCompulsoryTriggerLog(player, this);

            DummyCard *dummy = new DummyCard();
            dummy->deleteLater();
            for (int i = 0; i < num; i++) {
                if (used_ids.isEmpty() || player->isDead()) break;
                room->fillAG(used_ids, player);
                int id = room->askForAG(player, used_ids, false, objectName(), "@mtchunzu");
                room->clearAG(player);
                used_ids.removeAll(id);
                got << id;
                dummy->addSubcard(id);
            }
            player->setTag("MTChunzuGotIds", got);
            player->obtainCard(dummy);
        }
        return false;
    }
};

class MTChunzuRecord : public TriggerSkill
{
public:
    MTChunzuRecord() : TriggerSkill("#mtchunzu")
    {
        events << CardFinished;
        frequency = Limited;
        global = true;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard") || use.card->isVirtualCard() || !use.card->getSkillName().isEmpty()) return false;
        QVariantList used = player->getTag("MTChunzuUsedIds").toList();
        int id = use.card->getEffectiveId();
        if (used.contains(id)) return false;
        used << id;
        player->setTag("MTChunzuUsedIds", used);
        return false;
    }
};

class MTBishi : public TriggerSkill
{
public:
    MTBishi() : TriggerSkill("mtbishi")
    {
        events << CardUsed;
        frequency = Compulsory;
        waked_skills = "#mtbishi";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard") || !use.card->hasFlag("mtbishiSecond")) return false;

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            if (use.from == p || use.to.contains(p)) {
                room->sendCompulsoryTriggerLog(p, this);
                p->drawCards(1, objectName());
                if (p->getKingdom() != "qun") continue;
                QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
                room->addPlayerMark(p, mark);
            }
        }
        return false;
    }
};

class MTBishiDis : public DistanceSkill
{
public:
    MTBishiDis() : DistanceSkill("#mtbishi")
    {
    }

    int getCorrect(const Player *, const Player *to) const
    {
        QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
        return to->getMark(mark);
    }
};

class MTChushi : public PhaseChangeSkill
{
public:
    MTChushi() : PhaseChangeSkill("mtchushi")
    {
        waked_skills = "olkanpo,bazhen";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        Player::Phase phase = player->getPhase();
        if (phase == Player::Finish) {
            if (!player->hasSkill(this)) return false;
            QList<ServerPlayer *> targets;
            int hand = player->getHandcardNum();
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() > hand)
                    targets << p;
            }
            if (targets.isEmpty() || !player->askForSkillInvoke(this)) return false;
            player->peiyin(this);

            targets.clear();
            hand = player->getHandcardNum();
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() > hand)
                    targets << p;
            }
            if (targets.isEmpty()) return false;

            QList<ServerPlayer *> players;
            QStringList player_names;
            foreach (ServerPlayer *p, targets) {
                if (player->isDead()) return false;
                if (p->isDead()) continue;
                const Card *c = room->askForCard(p, "..", "@mtchushi-give:" + player->objectName(), QVariant::fromValue(player), Card::MethodNone, player);
                if (!c) continue;
                players << p;
                player_names << p->objectName();
                if (p->isAlive() && player->isAlive())
                    room->giveCard(p, player, c, objectName());
            }

            if (player->isDead()) return false;
            if (player->getKingdom() == "shu") {
                room->acquireNextTurnSkills(player, QString(), "olkanpo");
                foreach (ServerPlayer *p, players) {
                    if (p->isDead() || p->hasSkill("bazhen", true)) continue;
                    p->setTag("MTChushi", QVariant::fromValue(player));
                    p->acquireSkill("bazhen");
                }
            } else {
                QString choice = "kanpo";
                if (!players.isEmpty())
                    choice = room->askForChoice(player, objectName(), "kanpo+bazhen", player_names.join("+"));

                if (choice == "kanpo")
                    room->acquireNextTurnSkills(player, QString(), "olkanpo");
                else {
                    foreach (ServerPlayer *p, players) {
                        if (p->isDead() || p->hasSkill("bazhen", true)) continue;
                        p->setTag("MTChushi", QVariant::fromValue(player));
                        room->acquireSkill(p, "bazhen");
                    }
                }
            }

        } else if (phase == Player::RoundStart) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                ServerPlayer *t = p->getTag("MTChushi").value<ServerPlayer *>();
                if (!t || t != player) continue;
                p->removeTag("MTChushi");

                LogMessage log;
                log.type = "#ZhafuEffect";
                log.from = p;
                log.arg = objectName();
                room->sendLog(log);

                if (p->hasSkill("bazhen", true))
                    room->detachSkillFromPlayer(p, "bazhen");
            }
        }
        return false;
    }
};

class MTJijing : public TriggerSkill
{
public:
    MTJijing() : TriggerSkill("mtjijing")
    {
        events << EventPhaseStart << Pindian;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Start) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill(this)) continue;
                int n = 1;
                if (p->hasSkill("mtbiancai") && p->getKingdom() == "shu")
                    n++;
                if (p->getMark("mtjijing_lun") >= n) continue;
                if (!p->askForSkillInvoke(this)) continue;
                p->peiyin(this);

                room->addPlayerMark(p, "mtjijing-Clear");
                room->addPlayerMark(p, "mtjijing_lun");

                QString mtjijing = p->getTag("mtjijing_list").toString();
                if (!mtjijing.isEmpty()) {
                    QList<int> ids = ListS2I(mtjijing.split("+"));
                    foreach (int id, p->handCards()) {
                        if (!ids.contains(id)) continue;
                        room->setCardTip(id, "mtjijing");
                    }
                }

                p->drawCards(1, objectName());
            }
        } else {
            PindianStruct *pindian = data.value<PindianStruct *>();
            ServerPlayer *from = pindian->from, *to = pindian->to;
            const Card *c = pindian->from_card, *cc = pindian->to_card;

            QList<ServerPlayer *> targets;
            if (from->isAlive() && from->getMark("mtjijing-Clear") > 0) {
                QString mtjijing = from->getTag("mtjijing_list").toString();
                if (mtjijing.split("+").contains(QString::number(c->getEffectiveId())))
                    targets << from;
            }
            if (to->isAlive() && to->getMark("mtjijing-Clear") > 0) {
                QString mtjijing = to->getTag("mtjijing_list").toString();
                if (mtjijing.split("+").contains(QString::number(cc->getEffectiveId())))
                    targets << to;
            }
            if (targets.isEmpty()) return false;
            room->sortByActionOrder(targets);

            foreach (ServerPlayer *p, targets) {
                if (p->isDead()) continue;
                room->sendCompulsoryTriggerLog(p, this);
                if (p == from && room->getCardPlace(c->getEffectiveId()) == Player::PlaceTable)
                    room->obtainCard(p, c);
                else if (p == to && room->getCardPlace(cc->getEffectiveId()) == Player::PlaceTable)
                    room->obtainCard(p, cc);
            }
        }
        return false;
    }
};

class MTJijingRecord : public TriggerSkill
{
public:
    MTJijingRecord() : TriggerSkill("#mtjijing")
    {
        events << EventPhaseChanging << CardsMoveOneTime << EventAcquireSkill;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                p->removeTag("mtjijing_list");
                foreach (int id, p->handCards() + p->getEquipsId())
                    room->setCardTip(id, "-mtjijing");
            }
        } else if(triggerEvent == CardsMoveOneTime) {
            if (!room->hasCurrent(true)) return false;

            QStringList mtjijinglist;
            QString mtjijing = player->getTag("mtjijing_list").toString();
            if (!mtjijing.isEmpty())
                mtjijinglist = mtjijing.split("+");

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!room->getTag("FirstRound").toBool() && move.to == player && move.to_place == Player::PlaceHand) {
                foreach (int id, move.card_ids) {
                    if (player->hasSkill("mtjijing", true) && player->getMark("mtjijing-Clear") > 0)
                        room->setCardTip(id, "mtjijing");
                    QString str = QString::number(id);
                    if (mtjijinglist.contains(str)) continue;
                    mtjijinglist << str;
                }
                player->setTag("mtjijing_list", mtjijinglist.join("+"));
            }
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != "mtjijing" || !player->hasSkill("mtjijing", true) || player->getMark("mtjijing-Clear") <= 0) return false;
            QString mtjijing = player->getTag("mtjijing_list").toString();
            if (mtjijing.isEmpty()) return false;
            QList<int> ids = ListS2I(mtjijing.split("+"));
            foreach (int id, player->handCards()) {
                if (!ids.contains(id)) continue;
                room->setCardTip(id, "mtjijing");
            }
        }
        return false;
    }
};

class MTBiancai : public TriggerSkill
{
public:
    MTBiancai() : TriggerSkill("mtbiancai")
    {
        events << EventPhaseStart << CardUsed;
        waked_skills = "#mtbiancai";
    }

    bool triggerable(const ServerPlayer *target, Room *, TriggerEvent event) const
    {
        if (event == EventPhaseStart)
            return target != NULL && target->isAlive() && target->hasSkill(this) && target->getPhase() == Player::Play &&
                    target->canPindian() && target->getKingdom() == "shu";
        else
            return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->canPindian(p))
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mtbiancai", true, true);
            if (!t) return false;
            player->peiyin(this);

            PindianStruct *pindian = player->PinDian(t, objectName());

            if (pindian->success) {
                int from_id = pindian->from_card->getTypeId(), to_id = pindian->to_card->getTypeId();
                room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(from_id).arg(t->objectName()), 1);
                room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(to_id).arg(t->objectName()), 1);
            } else {
                QString from_type = pindian->from_card->getType(), to_type = pindian->to_card->getType();
                room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(from_type), 1);
                room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(to_type), 1);
            }
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->isKindOf("SkillCard")) return false;
            int id = use.card->getTypeId();
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (player->getMark(QString("mtbiancai_win_%1_%2-Clear").arg(id).arg(p->objectName())) > 0)
                    use.no_respond_list << p->objectName();
            }
            data = QVariant::fromValue(use);
        }
        return false;
    }
};

class MTBiancaiLimit : public CardLimitSkill
{
public:
    MTBiancaiLimit() : CardLimitSkill("#mtbiancai")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        QStringList trs;
        foreach (QString mark, target->getMarkNames()) {
            if (!mark.startsWith("mtbiancai_notwin_") || target->getMark(mark) < 1) continue;
            QStringList marks = mark.split("_");
            if (marks.length() != 3) continue;
            QString type = marks.last().split("-").first();
            trs << type;
        }
        return trs.join(",");
    }
};

class MTChenxiao : public PhaseChangeSkill
{
public:
    MTChenxiao() : PhaseChangeSkill("mtchenxiao")
    {
        waked_skills = "#mtchenxiao";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && target->getPhase() == Player::Play;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        QString phase = QString::number((int)Player::RoundStart);
        QString mark = "&mtchenxiao-Self" + phase + "Clear";

        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(this) || p->getKingdom() != "jin" || !p->canPindian(player) || p->getMark(mark) > 0) continue;
            if (!p->askForSkillInvoke(this, player)) continue;
            p->peiyin(this);

            if (p->pindian(player, objectName())) {
                room->setPlayerMark(player, QString("MTChenxiao_%1-Clear").arg(p->objectName()), 1);
                room->setPlayerMark(p, "mtjijing_lun", 0);
            } else
                room->setPlayerMark(p, mark, 1);
        }
        return false;
    }
};

class MTChenxiaoProhibit : public ProhibitSkill
{
public:
    MTChenxiaoProhibit() : ProhibitSkill("#mtchenxiao")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return from->getMark(QString("MTChenxiao_%1-Clear").arg(to->objectName())) > 0 && !card->isKindOf("SkillCard");
    }
};

MTZhuluCard::MTZhuluCard()
{
     mute = true;
}

bool MTZhuluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Self->isKongcheng()) return false;
    Slash *slash = new Slash(Card::SuitToBeDecided, -1);
    slash->setSkillName("mtzhulu");
    slash->addSubcards(Self->handCards());
    slash->deleteLater();
    if (Self->isCardLimited(slash, Card::MethodUse, true)) return false;
    return slash->targetFilter(targets, to_select, Self) && to_select->getMark("mtzhuluTarget-Clear") > 0;
}

void MTZhuluCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *from = card_use.from;
    if (from->isKongcheng()) return;
    Slash *slash = new Slash(Card::SuitToBeDecided, -1);
    slash->setSkillName("mtzhulu");
    slash->addSubcards(from->handCards());
    slash->deleteLater();
    if (from->isCardLimited(slash, Card::MethodUse, true)) return;
    room->useCard(CardUseStruct(slash, from, card_use.to));
}

class MTZhuluVS : public ZeroCardViewAsSkill
{
public:
    MTZhuluVS() : ZeroCardViewAsSkill("mtzhulu")
    {
        response_pattern = "@@mtzhulu";
    }

    const Card *viewAs() const
    {
        return new MTZhuluCard;
    }
};

class MTZhulu : public TriggerSkill
{
public:
    MTZhulu() : TriggerSkill("mtzhulu")
    {
        events << CardFinished << EventPhaseChanging;
        view_as_skill = new MTZhuluVS;
        waked_skills = "#mtzhulu-slash-ndl,#mtzhulu";
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardFinished) {
            if (player->getPhase() != Player::Play || player->getMark("mtzhuluWuxiao-Clear") > 0) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;

            QList<ServerPlayer *> players;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->isKongcheng()) continue;
                players << p;
            }
            if (players.isEmpty()) return false;
            ServerPlayer *t = room->askForPlayerChosen(player, players, objectName(), "@mtzhulu-show", true, true);
            if (!t) return false;
            player->peiyin(this);

            room->setPlayerMark(t, "mtzhuluTarget-Clear", 1);
            if (t->isKongcheng()) return false;
            int id = room->askForCardChosen(player, t, "h", objectName());

            /*QVariantList ids = t->getTag("MTZhuluShow").toList();
            if (!ids.contains(QVariant(id))) {
                ids << id;
                t->setTag("MTZhuluShow", ids);
            }*/

            room->setPlayerMark(t, QString("MTZhuluShow_%1-Clear").arg(id), 1);
            room->showCard(t, id);

            QString suit = Sanguosha->getCard(id)->getSuitString() + "_char";
            QStringList suits;
            foreach (QString mark, player->getMarkNames()) {
                if (!mark.startsWith("&mtzhuluSuit+") || !mark.endsWith("-PlayClear") || player->getMark(mark) < 1) continue;
                QStringList marks = mark.split("+");
                foreach (QString s, marks) {
                    if (s == "&mtzhuluSuit") continue;
                    QString ss = s.split("-").first();
                    if (!suits.contains(ss))
                        suits << ss;
                }
                room->setPlayerMark(player, mark, 0);
            }

            if (suits.contains(suit)) {
                room->setPlayerMark(player, "&mtzhuluSuit+" + suits.join("+") + "-PlayClear", 1);
                room->setPlayerMark(player, "mtzhuluWuxiao-Clear", 1);
            } else {
                suits << suit;
                room->setPlayerMark(player, "&mtzhuluSuit+" + suits.join("+") + "-PlayClear", 1);
                player->drawCards(1, objectName());
            }
        } else {
            if (player->isKongcheng() || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

            bool flag = false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("mtzhuluTarget-Clear") > 0) {
                    flag = true;
                    break;
                }
            }
            if (!flag) return false;

            Slash *slash = new Slash(Card::SuitToBeDecided, -1);
            slash->setSkillName("mtzhulu");
            slash->addSubcards(player->handCards());
            slash->deleteLater();
            if (player->isCardLimited(slash, Card::MethodUse, true)) return false;
            room->askForUseCard(player, "@@mtzhulu", "@mtzhulu");
        }
        return false;
    }
};

class MTZhuluDamage : public TriggerSkill
{
public:
    MTZhuluDamage() : TriggerSkill("#mtzhulu")
    {
        events << Damage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct d = data.value<DamageStruct>();
        if (!d.card || !d.card->isKindOf("Slash") || !d.card->getSkillNames().contains("mtzhulu")) return false;
        if (d.chain || d.transfer || d.to->isKongcheng()) return false;

        ServerPlayer *user = room->getCardUser(d.card);
        if (!user || user->isDead()) return false;

        DummyCard *dummy = new DummyCard();
        dummy->deleteLater();
        foreach(int id, d.to->handCards()) {
            if (d.to->getMark(QString("MTZhuluShow_%1-Clear").arg(id)) > 0)
                dummy->addSubcard(id);
        }
        if (dummy->subcardsLength() < 1) return false;
        room->obtainCard(user, dummy, objectName());
        return false;
    }
};

class MTZhengwang : public TriggerSkill
{
public:
    MTZhengwang() : TriggerSkill("mtzhengwang")
    {
        events << EventPhaseStart << EventPhaseEnd << DamageDone;
        waked_skills = "#mtzhengwang";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getRole() != "lord" || player->getPhase() != Player::NotActive) return false;
            if (room->getTag("Global_ExtraTurn" + player->objectName()).toBool()) return false;

            int mark = player->getMark("MTZhengwangDamage-Keep");
            room->setPlayerMark(player, "MTZhengwangDamage-Keep", 0);

            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill(this) || p->getKingdom() != "jin") continue;
                if (!p->askForSkillInvoke(this, QString("mtzhengwang:%1:%2").arg(player->objectName()).arg(mark))) continue;
                p->peiyin(this);

                room->setPlayerMark(p, "&mtzhengwangDamage-Clear", mark + 1);
                p->drawCards(2, objectName());
                p->gainAnExtraTurn();
            }
        } else if (event == EventPhaseEnd) {
            if (player->isDead() || player->getPhase() != Player::Play || player->getMark("&mtzhengwangDamage-Clear") < 1 ||
                    player->isKongcheng()) return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->throwAllHandCards();
        } else {
            DamageStruct d = data.value<DamageStruct>();
            if (d.damage < 1 || !d.from) return false;
            room->removePlayerMark(d.from, "&mtzhengwangDamage-Clear", d.damage);
        }
        return false;
    }
};

class MTZhengwangRecord : public TriggerSkill
{
public:
    MTZhengwangRecord() : TriggerSkill("#mtzhengwang")
    {
        events << DamageDone;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct d = data.value<DamageStruct>();
        if (d.damage < 1) return false;
        ServerPlayer *from = d.from;
        if (!from) return false;
        if (from->getRole() != "lord" || from->getPhase() == Player::NotActive) return false;
        if (room->getTag("Global_ExtraTurn" + from->objectName()).toBool()) return false;
        room->addPlayerMark(from, "MTZhengwangDamage-Keep", d.damage);
        return false;
    }
};

MTZhuizunCard::MTZhuizunCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MTZhuizunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), QString(), "mtzhuizun", "");
    room->throwCard(this, reason, NULL);

    QStringList choices;
    if (!source->hasSkill("tenyearrende1", true))
        choices << "tenyearrende1";
    if (!source->hasSkill("olsishu1", true))
        choices << "olsishu1";
    if (choices.isEmpty()) return;

    QStringList skills = source->getTag("MTZhuizunSkills").toStringList();
    QString choice = room->askForChoice(source, "mtzhuizun", choices.join("+"));
    choice.chop(1);
    if (!skills.contains(choice)) {
        skills << choice;
        source->setTag("MTZhuizunSkills", skills);
    }

    room->acquireSkill(source, choice);
}

class MTZhuizunVS : public ViewAsSkill
{
public:
    MTZhuizunVS() : ViewAsSkill("mtzhuizun")
    {
        expand_pile = "mtzhuizunde";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && Self->getPile("mtzhuizunde").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2) return NULL;
        MTZhuizunCard *c = new MTZhuizunCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getPile("mtzhuizunde").length() > 1;
    }
};

class MTZhuizun : public PhaseChangeSkill
{
public:
    MTZhuizun() : PhaseChangeSkill("mtzhuizun")
    {
        view_as_skill = new MTZhuizunVS;
        waked_skills = "#mtzhuizun";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && target->getPhase() == Player::RoundStart && !target->isKongcheng();
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead() || player->isKongcheng()) break;
            if (p->isDead() || !p->hasSkill(this) || p->getKingdom() != "qun") continue;

            QList<Card::Suit> suits;
            foreach (int id, p->getPile("mtzhuizunde")) {
                Card::Suit suit = Sanguosha->getCard(id)->getSuit();
                if (!suits.contains(suit))
                    suits << suit;
            }

            QStringList ids;
            foreach (int id, player->handCards()) {
                if (!suits.contains(Sanguosha->getCard(id)->getSuit()))
                    ids << QString::number(id);
            }
            if (ids.isEmpty()) continue;

            const Card *c = room->askForCard(player, ids.join(","), "@mtzhuizun:" + p->objectName(), QVariant::fromValue(p), Card::MethodNone);
            if (!c) continue;

            LogMessage log;
            log.type = player == p ? "#InvokeSkill" : "#InvokeOthersSkill";
            log.arg = objectName();
            log.from = player;
            log.to << p;
            room->sendLog(log);
            p->peiyin(this);
            room->notifySkillInvoked(p, objectName());

            if (p->isAlive())
                p->addToPile("mtzhuizunde", c);
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class MTZhuizunEffect : public TriggerSkill
{
public:
    MTZhuizunEffect() : TriggerSkill("#mtzhuizun")
    {
        events << RoundEnd;
    }

    bool triggerable(const ServerPlayer *) const
    {
        return true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            QStringList skills = p->getTag("MTZhuizunSkills").toStringList();
            if (skills.isEmpty()) continue;
            p->removeTag("MTZhuizunSkills");
            QStringList loses;
            foreach (QString sk, skills) {
                if (!loses.contains("-" + sk) && p->hasSkill(sk, true))
                    loses << "-" + sk;
            }
            room->handleAcquireDetachSkills(p, loses.join("|"));
        }
        return false;
    }
};

class MTZhanhua : public TriggerSkill
{
public:
    MTZhanhua() : TriggerSkill("mtzhanhua")
    {
        events << EventPhaseStart << Dying;
        waked_skills = "#mtzhanhua";
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play || player->getMark("MTZhanhuaEventPhaseStart") > 0) return false;
            int num = player->getTag("MTZhanhuaHandcardNum").toInt(), hand = player->getHandcardNum();
            if (hand > num) {
                num = hand;
                player->setTag("MTZhanhuaHandcardNum", hand);
                player->setSkillDescriptionSwap(objectName(), "%arg2", QString::number(hand));
                room->changeTranslation(player, objectName(), 1);
            }

            num++;

            if (!player->askForSkillInvoke(this, QString("handcardnum:%1").arg(num))) return false;
            player->peiyin(this);
            room->setPlayerMark(player, "MTZhanhuaEventPhaseStart", 1);
            player->drawCards(num - player->getHandcardNum(), objectName());
        } else {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who != player || player->getMark("MTZhanhuaDying") > 0) return false;

            int max = player->getTag("MTZhanhuaMaxHp").toInt(), maxhp = player->getMaxHp();
            if (maxhp > max) {
                max = maxhp;
                player->setTag("MTZhanhuaMaxHp", maxhp);
                player->setSkillDescriptionSwap(objectName(), "%arg3", QString::number(maxhp));
                room->changeTranslation(player, objectName(), 1);
            }

            int hp = player->getTag("MTZhanhuaHp").toInt(), hpp = player->getHp();
            if (hpp > hp) {
                hp = hpp;
                player->setTag("MTZhanhuaHp", hpp);
                player->setSkillDescriptionSwap(objectName(), "%arg4", QString::number(hpp));
                room->changeTranslation(player, objectName(), 1);
            }

            max++;
            hp++;
            hp = qMin(max, hp);

            if (!player->askForSkillInvoke(this, QString("dying:%1:%2").arg(max).arg(hp))) return false;
            player->peiyin(this);
            room->setPlayerMark(player, "MTZhanhuaDying", 1);
            room->setPlayerProperty(player, "maxhp", max);
            //room->setPlayerProperty(player, "hp", hp);
            room->recover(player, RecoverStruct(objectName(), player, hp - player->getHp()));
        }
        return false;
    }
};

class MTZhanhuaRecord : public TriggerSkill
{
public:
    MTZhanhuaRecord() : TriggerSkill("#mtzhanhua")
    {
        events << DamageCaused << CardsMoveOneTime << GameReady << MaxHpChanged << HpChanged;
        global = true;
    }

    int getPriority(TriggerEvent event) const
    {
        if (event == DamageCaused)
            return TriggerSkill::getPriority(event) - 1;
        return TriggerSkill::getPriority(event) + 1;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            int d = player->getTag("MTZhanhuaDamage").toInt(), dd = damage.damage;
            if (dd > d) {
                player->setTag("MTZhanhuaDamage", dd);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(dd));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
            if (!player->hasSkill(this) || player->getMark("MTZhanhuaDamageCaused") > 0) return false;

            player->setTag("MTZhanhuaDamageCaused", data);
            bool invoke = player->askForSkillInvoke("mtzhanhua", QString("damage:%1:%2").arg(damage.to->objectName()).arg(qMax(dd, d) + 1));
            player->removeTag("MTZhanhuaDamageCaused");
            if (!invoke) return false;

            player->peiyin("mtzhanhua");
            room->setPlayerMark(player, "MTZhanhuaDamageCaused", 1);
            d = qMax(dd, d) + 1;
            player->setTag("MTZhanhuaDamage", d);
            player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(d));
            room->changeTranslation(player, "mtzhanhua", 1);

            damage.damage = d;
            data = QVariant::fromValue(damage);
        } else if (event == GameReady) {
            int d = player->getTag("MTZhanhuaDamage").toInt(), num = player->getTag("MTZhanhuaHandcardNum").toInt(),
                max = player->getTag("MTZhanhuaMaxHp").toInt(), hp = player->getTag("MTZhanhuaHp").toInt();
            num = qMax(num, player->getHandcardNum());
            max = qMax(num, player->getMaxHp());
            hp = qMax(num, player->getHp());

            player->setTag("MTZhanhuaHandcardNum", num);
            player->setTag("MTZhanhuaMaxHp", max);
            player->setTag("MTZhanhuaHp", hp);

            player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(d));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg2", QString::number(num));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg3", QString::number(max));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg4", QString::number(hp));
            room->changeTranslation(player, "mtzhanhua", 1);
        } else if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand) {
                int num = player->getTag("MTZhanhuaHandcardNum").toInt(), hand = player->getHandcardNum();
                if (hand > num) {
                    player->setTag("MTZhanhuaHandcardNum", hand);
                    player->setSkillDescriptionSwap("mtzhanhua", "%arg2", QString::number(hand));
                    room->changeTranslation(player, "mtzhanhua", 1);
                }
            }
        } else if (event == MaxHpChanged) {
            int max = player->getTag("MTZhanhuaMaxHp").toInt(), maxhp = player->getMaxHp();
            if (maxhp > max) {
                player->setTag("MTZhanhuaMaxHp", maxhp);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg3", QString::number(maxhp));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
        } else if (event == HpChanged) {
            int hp = player->getTag("MTZhanhuaHp").toInt(), hpp = player->getMaxHp();
            if (hpp > hp) {
                player->setTag("MTZhanhuaHp", hpp);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg4", QString::number(hpp));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
        }
        return false;
    }
};

MTHongwuCard::MTHongwuCard()
{
    mute = true;
}

bool MTHongwuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTHongwuCard::targetFixed() const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool MTHongwuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

void MTHongwuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *from = card_use.from;
    const Card *c = Sanguosha->getCard(subcards.first());
    room->useCard(CardUseStruct(c, from, card_use.to));
}

class MTHongwuVS :public OneCardViewAsSkill
{
public:
    MTHongwuVS() :OneCardViewAsSkill("mthongwu")
    {
        response_pattern = "@@mthongwu";
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isDamageCard() && Self->getHandcards().contains(to_select) && to_select->isAvailable(Self);
    }

    const Card *viewAs(const Card *originalCard) const
    {
        //MTHongwuCard *card = new MTHongwuCard;  //防止重铸，不然直接return originalCard了;
        //card->addSubcard(originalCard);
        return originalCard;
    }
};

class MTHongwu : public TriggerSkill
{
public:
    MTHongwu() : TriggerSkill("mthongwu")
    {
        events << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new MTHongwuVS;
        waked_skills = "#mthongwu";
    }

    bool triggerable(const ServerPlayer *target, Room *, TriggerEvent event) const
    {
        if (event == EventPhaseStart)
            return target != NULL && target->isAlive() && target->getPhase() == Player::RoundStart;
        else
            return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(this) || p->getMark("hongwuWuxiao_lun") > 0) continue;
                if (!p->askForSkillInvoke(this, player)) continue;
                p->peiyin(this);

                QList<ServerPlayer *> targets;
                targets << p << player;
                room->sortByActionOrder(targets);
                room->drawCards(targets, 1, objectName());

                Card::Suit suit = room->askForSuit(player, objectName());
                QString suit_str = Card::Suit2String(suit);
                p->setTag("MTHongwu_" + player->objectName(), suit_str);

                LogMessage log;
                log.type = "#ChooseSuit";
                log.from = player;
                log.arg = suit_str;
                room->sendLog(log, player);
                room->setPlayerCardLimitation(player, "use,response,discard", QString(".|%1|.|.").arg(suit_str), true);
            }
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->isDead()) continue;
                QString suit = p->getTag("MTHongwu_" + player->objectName()).toString();
                if (suit.isEmpty()) continue;

                Card::Suit suitt = room->askForSuit(p, objectName());
                QString suit_str = Card::Suit2String(suitt);
                bool b = suit == suit_str;

                LogMessage log;
                log.type = b ? "#MTHongwuRight" : "#MTHongwuWrong";
                log.from = p;
                log.arg = suit_str;
                log.arg2 = suit;
                room->sendLog(log);

                if (player->isDead()) continue;

                if (b) {
                    DummyCard *dummy = new DummyCard;
                    dummy->deleteLater();
                    QList<int> jilei_list;

                    foreach (const Card *c, player->getCards("he")) {
                        if (c->isDamageCard()) continue;
                        int id = c->getEffectiveId();
                        if (player->canDiscard(player, id))
                            dummy->addSubcard(c);
                        else
                            jilei_list << id;
                    }

                    if (!jilei_list.isEmpty()) {
                        log.type = "$JileiShowAllCards";
                        log.from = player;
                        log.card_str = ListI2S(jilei_list).join("+");
                        room->sendLog(log);
                        JsonArray gongxinArgs;
                        gongxinArgs << player->objectName() << false << JsonUtils::toJsonArray(jilei_list);
                        room->doBroadcastNotify(QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
                        QVariant data = log.card_str;
                        room->getThread()->trigger(ShowCards, room, player, data);
                    }

                    if (dummy->subcardsLength() > 0)
                        room->throwCard(dummy, objectName(), player);
                } else {
                    room->setPlayerMark(p, "hongwuWuxiao_lun", 1);
                    while (player->isAlive() && !player->isKongcheng()) {
                        const Card *c = room->askForUseCard(player, "@@mthongwu", "@mthongwu");
                        if (!c) break;
                    }
                }
            }
        }
        return false;
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

    General *mt_zhugedan = new General(this, "mt_zhugedan", "wei+wu", 4);
    mt_zhugedan->addSkill(new MTYanyi);
    mt_zhugedan->addSkill(new MTJishi);
    mt_zhugedan->addSkill(new MTJishiTargetMod);
    mt_zhugedan->addSkill(new MTYitao);

    General *mt_mengda = new General(this, "mt_mengda", "wei+shu", 4);
    mt_mengda->addSkill(new MTJuyuan);
    mt_mengda->addSkill(new MTJuyuanFlag);
    mt_mengda->addSkill(new MTJuyuanProhibit);
    mt_mengda->addSkill(new MTFupan);

    General *mt_zhangyan = new General(this, "mt_zhangyan", "wei+qun", 4);
    mt_zhangyan->addSkill(new MTFeiyan);
    mt_zhangyan->addSkill(new MTFeiyanEffect);

    General *mt_liuling = new General(this, "mt_liuling", "wei+jin", 3);
    mt_liuling->addSkill(new MTJiukuang);
    mt_liuling->addSkill(new MTJiukuangTMD);
    mt_liuling->addSkill(new MTZongqing);
    mt_liuling->addSkill(new MTZongqingDis);

    General *mt_zhangwen = new General(this, "mt_zhangwen", "wu+shu", 3);
    mt_zhangwen->addSkill(new MTZanzhang);
    mt_zhangwen->addSkill(new MTHongya);

    General *mt_sunfuren = new General(this, "mt_sunfuren", "wu+qun", 3, false);
    mt_sunfuren->addSkill(new MTYinglve);
    mt_sunfuren->addSkill(new MTXianding);
    mt_sunfuren->addSkill(new MTXiandingRecord);

    General *mt_luji = new General(this, "mt_luji", "wu+jin", 3);
    mt_luji->addSkill(new MTWanghe);
    mt_luji->addSkill(new MTChunzu);
    mt_luji->addSkill(new MTChunzuRecord);

    General *mt_zhugejun = new General(this, "mt_zhugejun", "shu+qun", 3);
    mt_zhugejun->addSkill(new MTBishi);
    mt_zhugejun->addSkill(new MTBishiDis);
    mt_zhugejun->addSkill(new MTChushi);

    General *mt_limi = new General(this, "mt_limi", "shu+jin", 3);
    mt_limi->addSkill(new MTJijing);
    mt_limi->addSkill(new MTJijingRecord);
    mt_limi->addSkill(new MTBiancai);
    mt_limi->addSkill(new MTBiancaiLimit);
    mt_limi->addSkill(new MTChenxiao);
    mt_limi->addSkill(new MTChenxiaoProhibit);

    General *mt_liuyuan = new General(this, "mt_liuyuan", "qun+jin", 4);
    mt_liuyuan->addSkill(new MTZhulu);
    mt_liuyuan->addSkill(new MTZhuluDamage);
    mt_liuyuan->addSkill(new SlashNoDistanceLimitSkill("mtzhulu"));
    mt_liuyuan->addSkill(new MTZhengwang);
    mt_liuyuan->addSkill(new MTZhengwangRecord);
    mt_liuyuan->addSkill(new MTZhuizun);
    mt_liuyuan->addSkill(new MTZhuizunEffect);

    General *mt_shendiaochan = new General(this, "mt_shendiaochan", "god", 3, false);
    mt_shendiaochan->addSkill(new MTZhanhua);
    mt_shendiaochan->addSkill(new MTZhanhuaRecord);
    mt_shendiaochan->addSkill(new MTHongwu);

    addMetaObject<MTJieliCard>();
    addMetaObject<MTWeiqieCard>();
    addMetaObject<MTZhilieCard>();
    addMetaObject<MTRenyiCard>();
    addMetaObject<MTGuzhaoCard>();
    addMetaObject<MTJiyeCard>();
    addMetaObject<MTZhiheCard>();
    addMetaObject<MTJishiCard>();
    addMetaObject<MTYitaoCard>();
    addMetaObject<MTZongqingCard>();
    addMetaObject<MTZanzhangCard>();
    addMetaObject<MTYinglveCard>();
    addMetaObject<MTZhuluCard>();
    addMetaObject<MTZhuizunCard>();
    addMetaObject<MTHongwuCard>();
}
ADD_PACKAGE(Maotu)
