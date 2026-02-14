#include "thicket.h"
//#include "skill.h"
#include "room.h"
#include "maneuvering.h"
#include "clientplayer.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
//#include "json.h"
#include "roomthread.h"

class Xingshang : public TriggerSkill
{
public:
    Xingshang() : TriggerSkill("xingshang")
    {
        events << Death;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *caopi, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *player = death.who;
        if (player->isNude() || caopi == player)
            return false;
        if (caopi->isAlive() && room->askForSkillInvoke(caopi, objectName(), data)) {
            bool isCaoCao = player->getGeneralName().contains("caocao");
            room->broadcastSkillInvoke(objectName(), (isCaoCao ? 3 : (player->isMale() ? 1 : 2)));

            DummyCard *dummy = new DummyCard(player->handCards());
            QList <const Card *> equips = player->getEquips();
            foreach(const Card *card, equips)
                dummy->addSubcard(card);

            if (dummy->subcardsLength() > 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, caopi->objectName());
                room->obtainCard(caopi, dummy, reason, false);
            }
            delete dummy;
        }

        return false;
    }
};

class Fangzhu : public MasochismSkill
{
public:
    Fangzhu() : MasochismSkill("fangzhu")
    {
    }

    void onDamaged(ServerPlayer *caopi, const DamageStruct &) const
    {
        Room *room = caopi->getRoom();
        ServerPlayer *to = room->askForPlayerChosen(caopi, room->getOtherPlayers(caopi), objectName(),
            "fangzhu-invoke", caopi->getMark("JilveEvent") != int(Damaged), true);
        if (to) {
            if (caopi->hasInnateSkill("fangzhu") || !caopi->hasSkill("jilve")) {
                int index = to->faceUp() ? 1 : 2;
                if (to->getGeneralName().contains("caozhi") || (to->getGeneral2() && to->getGeneral2Name().contains("caozhi")))
                    index = 3;
                room->broadcastSkillInvoke("fangzhu", index);
            } else
                room->broadcastSkillInvoke("jilve", 2);

            to->drawCards(caopi->getLostHp(), objectName());
            to->turnOver();
        }
    }
};

class Songwei : public TriggerSkill
{
public:
    Songwei() : TriggerSkill("songwei$")
    {
        events << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getKingdom() == "wei";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();

        if (judge->card->isBlack()) {
            QList<ServerPlayer *> caopis;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasLordSkill(this)) caopis << p;
            }

            while (player->isAlive()) {
                ServerPlayer *caopi = room->askForPlayerChosen(player, caopis, objectName(), "@songwei-to", true);
                if (caopi) {
                    if (!caopi->isLord() && caopi->hasSkill("weidi"))
                        room->broadcastSkillInvoke("weidi");
                    else
                        room->broadcastSkillInvoke(objectName(), player->isMale() ? 1 : 2);

                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = player;
                    log.to << caopi;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(caopi, objectName());

                    caopi->drawCards(1, objectName());
                    caopis.removeOne(caopi);
                } else
                    break;
            }
        }

        return false;
    }
};

class Duanliang : public OneCardViewAsSkill
{
public:
    Duanliang() : OneCardViewAsSkill("duanliang")
    {
        filter_pattern = "BasicCard,EquipCard|black";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        shortage->setSkillName(objectName());
        shortage->addSubcard(originalCard);

        return shortage;
    }
};

class DuanliangTargetMod : public TargetModSkill
{
public:
    DuanliangTargetMod() : TargetModSkill("#duanliang-target")
    {
        frequency = NotFrequent;
        pattern = "SupplyShortage";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill("duanliang"))
            return 1;
        return 0;
    }
};

class Huoshou : public TriggerSkill
{
public:
    Huoshou() : TriggerSkill("huoshou")
    {
        events << TargetSpecified << ConfirmDamage << CardEffected;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SavageAssault")) {
				foreach (ServerPlayer *menghuo, room->findPlayersBySkillName(objectName())) {
					if (menghuo != use.from) {
						int index = qrand()%2+1;
						if (menghuo->isJieGeneral()) index += 2;
						room->sendCompulsoryTriggerLog(menghuo, this, index);
						use.card->setFlags("HuoshouDamage_" + menghuo->objectName());
					}
                }
            }
        } else if (triggerEvent == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.card->isKindOf("SavageAssault")&&effect.to->hasSkill(objectName())) {
				int index = qrand()%2+1;
				if (effect.to->isJieGeneral()) index += 2;
				room->sendCompulsoryTriggerLog(effect.to, this, index);
				effect.nullified = true;
	
				data = QVariant::fromValue(effect);
			}
        } else if (triggerEvent == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("SavageAssault"))
                return false;

            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (damage.card->hasFlag("HuoshouDamage_" + p->objectName())) {
					damage.from = p;
					data = QVariant::fromValue(damage);
                }
            }
        }
        return false;
    }
};

class Lieren : public TriggerSkill
{
public:
    Lieren() : TriggerSkill("lieren")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhurong, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;
        if (target->isDead()) return false;
        if (damage.card && damage.card->isKindOf("Slash") && zhurong->canPindian(target) && !target->hasFlag("Global_DebutFlag") && !damage.chain && !damage.transfer
            && room->askForSkillInvoke(zhurong, objectName(), data)) {

            int index = qrand()%2+1;
            if (zhurong->isJieGeneral()) index += 2;
            room->broadcastSkillInvoke(objectName(), index);

            if (!zhurong->pindian(target, "lieren")) {/*
                if (!zhurong->isJieGeneral())
                    room->broadcastSkillInvoke(objectName(), 3);*/
                return false;
            }/*

            if (!zhurong->isJieGeneral())
                room->broadcastSkillInvoke(objectName(), 2);*/

            if (!target->isNude()) {
                int card_id = room->askForCardChosen(zhurong, target, "he", objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, zhurong->objectName());
                room->obtainCard(zhurong, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
            }
        }
        return false;
    }
};

class Zaiqi : public PhaseChangeSkill
{
public:
    Zaiqi() : PhaseChangeSkill("zaiqi")
    {
    }

    bool onPhaseChange(ServerPlayer *menghuo, Room *room) const
    {
        if (menghuo->getPhase() == Player::Draw && menghuo->isWounded()) {
            if (room->askForSkillInvoke(menghuo, objectName())) {
                room->broadcastSkillInvoke(objectName());

                //bool has_heart = false;
                int x = menghuo->getLostHp();
                QList<int> ids = room->getNCards(x, false);
                CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_TURNOVER, menghuo->objectName(), "zaiqi", ""));
                room->moveCardsAtomic(move, true);

                room->getThread()->delay();
                room->getThread()->delay();

                QList<int> card_to_throw;
                QList<int> card_to_gotback;
                for (int i = 0; i < x; i++) {
                    if (Sanguosha->getCard(ids[i])->getSuit() == Card::Heart)
                        card_to_throw << ids[i];
                    else
                        card_to_gotback << ids[i];
                }
                if (!card_to_throw.isEmpty()) {
                    int num = qMin(card_to_throw.length(), menghuo->getMaxHp() - menghuo->getHp());
                    room->recover(menghuo, RecoverStruct(menghuo, nullptr, num, "zaiqi"));

                    DummyCard *dummy = new DummyCard(card_to_throw);
                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, menghuo->objectName(), "zaiqi", "");
                    room->throwCard(dummy, reason, nullptr);
                    delete dummy;
                    //has_heart = true;
                }
                if (!card_to_gotback.isEmpty()) {
                    DummyCard *dummy2 = new DummyCard(card_to_gotback);
                    room->obtainCard(menghuo, dummy2);
                    delete dummy2;
                }/*

                if (has_heart)
                    room->broadcastSkillInvoke(objectName(), 2);
                else
                    room->broadcastSkillInvoke(objectName(), 3);*/

                return true;
            }
        }

        return false;
    }
};

class Juxiang : public TriggerSkill
{
public:
    Juxiang() : TriggerSkill("juxiang")
    {
        events << BeforeCardsMove << CardEffected;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from_places.contains(Player::PlaceTable) && move.to_place == Player::DiscardPile
                && (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) { //&& move.card_ids.length() == 1
				CardUseStruct use = move.reason.m_useStruct;
				if (!use.card || !use.card->isKindOf("SavageAssault")) return false;
				if (player!=use.from&&room->CardInTable(use.card)) {
					int index = qrand() % 2 + 1;
					if (player->isJieGeneral()) index += 2;
					room->sendCompulsoryTriggerLog(player, this, index);
	
					player->obtainCard(use.card);
					move.removeCardIds(move.card_ids);
					data = QVariant::fromValue(move);
				}
			}
        } else if (event == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.card->isKindOf("SavageAssault")) {
				room->sendCompulsoryTriggerLog(effect.to, this);
				effect.nullified = true;
	
				data = QVariant::fromValue(effect);
			}
        }
        return false;
    }
};

class Yinghun : public PhaseChangeSkill
{
public:
    Yinghun() : PhaseChangeSkill("yinghun")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->isWounded();
    }

    void broadcast(ServerPlayer *sunjian, int index) const
    {
        Room *room = sunjian->getRoom();
        if (!sunjian->isJieGeneral())
            room->broadcastSkillInvoke(objectName(), index);
        else {
            if(sunjian->hasSkill("mobilehunzi",true))
                room->broadcastSkillInvoke(objectName(), index + 11);
			else if (sunjian->isJieGeneral("sunce"))
                room->broadcastSkillInvoke(objectName(), index + 6);
            else
                room->broadcastSkillInvoke(objectName(), index + 4);
        }
    }

    bool onPhaseChange(ServerPlayer *sunjian, Room *room) const
    {
        ServerPlayer *to = room->askForPlayerChosen(sunjian, room->getOtherPlayers(sunjian), objectName(), "yinghun-invoke", true, true);
        if (to) {
            int x = sunjian->getLostHp();

            int index = qrand()%2+1;
            if (!sunjian->hasInnateSkill("yinghun")) {
                if (sunjian->hasSkill("xiongyisy",true))
                    index = 9;
                else if (sunjian->hasSkill("hunzi",true))
                    index += 2;
            }

            if (x == 1) {
                broadcast(sunjian, index);

                to->drawCards(1, objectName());
                room->askForDiscard(to, objectName(), 1, 1, false, true);
            } else {
                to->setFlags("YinghunTarget");
                QString choice = room->askForChoice(sunjian, objectName(), "d1tx+dxt1");
                to->setFlags("-YinghunTarget");
                if (choice == "d1tx") {
                    broadcast(sunjian, index + 1);

                    to->drawCards(1, objectName());
                    room->askForDiscard(to, objectName(), x, x, false, true);
                } else {
                    broadcast(sunjian, index);

                    to->drawCards(x, objectName());
                    room->askForDiscard(to, objectName(), 1, 1, false, true);
                }
            }
        }
        return false;
    }
};

HaoshiCard::HaoshiCard()
{
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
    m_skillName = "_haoshi";
}

bool HaoshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    return to_select->getHandcardNum() == Self->getMark("haoshi");
}

void HaoshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(),
        targets.first()->objectName(), "haoshi", "");
    room->moveCardTo(this, targets.first(), Player::PlaceHand, reason);
}

class HaoshiViewAsSkill : public ViewAsSkill
{
public:
    HaoshiViewAsSkill() : ViewAsSkill("haoshi")
    {
        response_pattern = "@@haoshi!";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped()) return false;

        return selected.length() < Self->getHandcardNum() / 2;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != Self->getHandcardNum() / 2)
            return nullptr;

        HaoshiCard *card = new HaoshiCard;
        card->addSubcards(cards);
        return card;
    }
};

class HaoshiGive : public TriggerSkill
{
public:
    HaoshiGive() : TriggerSkill("#haoshi-give")
    {
        events << AfterDrawNCards;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *lusu, QVariant &data) const
    {
        DrawStruct draw = data.value<DrawStruct>();
		if (draw.reason=="draw_phase"&&lusu->hasFlag("haoshi")) {
            lusu->setFlags("-haoshi");

            if (lusu->getHandcardNum() <= 5)
                return false;

            int least = 1000;
            foreach(ServerPlayer *player, room->getOtherPlayers(lusu))
                least = qMin(player->getHandcardNum(), least);
            room->setPlayerMark(lusu, "haoshi", least);

            if (!room->askForUseCard(lusu, "@@haoshi!", "@haoshi", -1, Card::MethodNone)) {
                // force lusu to give his half cards
                ServerPlayer *beggar = nullptr;
                foreach (ServerPlayer *player, room->getOtherPlayers(lusu)) {
                    if (player->getHandcardNum() == least) {
                        beggar = player;
                        break;
                    }
                }

                int n = lusu->getHandcardNum() / 2;
                QList<int> to_give = lusu->handCards().mid(0, n);
                HaoshiCard *haoshi_card = new HaoshiCard;
                haoshi_card->addSubcards(to_give);
                QList<ServerPlayer *> targets;
                targets << beggar;
                haoshi_card->use(room, lusu, targets);
                delete haoshi_card;
            }
        }

        return false;
    }
};

class Haoshi : public DrawCardsSkill
{
public:
    Haoshi() : DrawCardsSkill("haoshi")
    {
		view_as_skill = new HaoshiViewAsSkill;
    }

    int getDrawNum(ServerPlayer *lusu, int n) const
    {
        Room *room = lusu->getRoom();
        if (lusu->hasSkill("haoshi") && room->askForSkillInvoke(lusu, "haoshi")) {
            room->broadcastSkillInvoke("haoshi");
            lusu->setFlags("haoshi");
            n += 2;
        }
		return n;
    }
};

DimengCard::DimengCard()
{
}

bool DimengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;

    if (targets.isEmpty())
        return true;

    if (targets.length() == 1) {
        return qAbs(to_select->getHandcardNum() - targets.first()->getHandcardNum()) == subcardsLength();
    }

    return false;
}

bool DimengCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void DimengCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *a = targets.at(0);
    ServerPlayer *b = targets.at(1);
    a->setFlags("DimengTarget");
    b->setFlags("DimengTarget");

    int n1 = a->getHandcardNum();
    int n2 = b->getHandcardNum();

    try {/*
        QList<CardsMoveStruct> exchangeMove;
        CardsMoveStruct move1(a->handCards(), b, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, a->objectName(), b->objectName(), "dimeng", ""));
        CardsMoveStruct move2(b->handCards(), a, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, b->objectName(), a->objectName(), "dimeng", ""));
        exchangeMove.push_back(move1);
        exchangeMove.push_back(move2);
        room->moveCardsAtomic(exchangeMove, false);*/
		room->swapCards(a,b,"h","dimeng");

        LogMessage log;
        log.type = "#Dimeng";
        log.from = a;
        log.to << b;
        log.arg = QString::number(n1);
        log.arg2 = QString::number(n2);
        room->sendLog(log);
        room->getThread()->delay();

        a->setFlags("-DimengTarget");
        b->setFlags("-DimengTarget");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
            a->setFlags("-DimengTarget");
            b->setFlags("-DimengTarget");
        }
        throw triggerEvent;
    }
}

class Dimeng : public ViewAsSkill
{
public:
    Dimeng() : ViewAsSkill("dimeng")
    {
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        DimengCard *card = new DimengCard;
        foreach(const Card *c, cards)
            card->addSubcard(c);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("DimengCard");
    }
};

class Wansha : public TriggerSkill
{
public:
    Wansha() : TriggerSkill("wansha")
    {
        events << Dying;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event , Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Dying) {
			if (!player->hasFlag("CurrentPlayer")) return false;

			if (player->hasInnateSkill("wansha") || !player->hasSkill("jilve"))
				room->broadcastSkillInvoke(objectName());
			else
				room->broadcastSkillInvoke("jilve", 3);

			DyingStruct dying = data.value<DyingStruct>();

			LogMessage log;
			log.from = player;
			log.arg = objectName();
			log.type = "#WanshaOne";
			if (player != dying.who) {
				log.type = "#WanshaTwo";
				log.to << dying.who;
			}
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
        }
        return false;
    }
};

class WanshaLimit : public CardLimitSkill
{
public:
    WanshaLimit() : CardLimitSkill("#wansha-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->hasFlag("Global_Dying")) return "";
		foreach (const Player *p, target->getAliveSiblings()) {
			if (p->hasFlag("CurrentPlayer") && p->hasSkills("wansha|mobilemouwansha"))
				return "Peach";
		}
		return "";
    }
};

class Luanwu : public ZeroCardViewAsSkill
{
public:
    Luanwu() : ZeroCardViewAsSkill("luanwu")
    {
        frequency = Limited;
        limit_mark = "@chaos";
    }

    const Card *viewAs() const
    {
        return new LuanwuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@chaos") >= 1;
    }
};

LuanwuCard::LuanwuCard()
{
    target_fixed = true;
}

void LuanwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->removePlayerMark(source, "@chaos");
    QList<ServerPlayer *> players = room->getOtherPlayers(source);
    foreach (ServerPlayer *player, players)
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), player->objectName());
    room->doSuperLightbox(source, "luanwu");
    foreach (ServerPlayer *player, players) {
        if (player->isAlive())
            room->cardEffect(this, source, player);
        room->getThread()->delay();
    }
}

void LuanwuCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    QList<ServerPlayer *> players = room->getOtherPlayers(effect.to);
    QList<int> distance_list;
    int nearest = 1000;
    foreach (ServerPlayer *player, players) {
        int distance = effect.to->distanceTo(player);
        distance_list << distance;
        nearest = qMin(nearest, distance);
    }

    QList<ServerPlayer *> luanwu_targets;
    for (int i = 0; i < distance_list.length(); i++) {
        if (distance_list[i] == nearest && effect.to->canSlash(players[i], nullptr, false))
            luanwu_targets << players[i];
    }

    if (luanwu_targets.isEmpty() || !room->askForUseSlashTo(effect.to, luanwu_targets, "@luanwu-slash"))
        room->loseHp(HpLostStruct(effect.to, 1, "luanwu", effect.from));
}

class Weimu : public ProhibitSkill
{
public:
    Weimu() : ProhibitSkill("weimu")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("TrickCard")
            && card->isBlack() && to->hasSkill(objectName()); // Be care!!!!!!
    }
};

class Jiuchi : public OneCardViewAsSkill
{
public:
    Jiuchi() : OneCardViewAsSkill("jiuchi")
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

class Roulin : public TriggerSkill
{
public:
    Roulin() : TriggerSkill("roulin")
    {
        events << TargetConfirmed << TargetSpecified;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            QVariantList jink_list = use.from->tag["Jink_" + use.card->toString()].toList();
            int index = 0;
            bool play_effect = false;
            if (triggerEvent == TargetSpecified) {
                foreach (ServerPlayer *p, use.to) {
                    if (p->isFemale()) {
                        play_effect = true;
                        if (jink_list.at(index).toInt() == 1)
                            jink_list.replace(index, QVariant(2));
                    }
                    index++;
                }
                use.from->tag["Jink_" + use.card->toString()] = jink_list;
                if (play_effect) {
                    index = (qrand() % 2)+1;
                    if (use.from->isJieGeneral()) index += 2;
                    room->broadcastSkillInvoke(objectName(), index);
                    room->sendCompulsoryTriggerLog(use.from, objectName());
                }
            } else if (triggerEvent == TargetConfirmed && use.from->isFemale()) {
                foreach (ServerPlayer *p, use.to) {
                    if (p == player) {
                        if (jink_list.at(index).toInt() == 1) {
                            jink_list.replace(index, QVariant(2));
                            play_effect = true;
                        }
                    }
                    index++;
                }
                use.from->tag["Jink_" + use.card->toString()] = jink_list;

                if (play_effect) {
                    //bool drunk = (use.card->tag.value("drunk", 0).toInt() > 0);
                    index = (qrand() % 2)+1;
                    if (player->isJieGeneral()) index += 2;
                    room->broadcastSkillInvoke(objectName(), index);
                    room->sendCompulsoryTriggerLog(player, objectName());
                }
            }
        }

        return false;
    }
};

class Benghuai : public PhaseChangeSkill
{
public:
    Benghuai() : PhaseChangeSkill("benghuai")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *dongzhuo, Room *room) const
    {
        if (dongzhuo->getMark("benghuai_nullification-Clear") > 0) return false;
        bool trigger_this = false;

        if (dongzhuo->getPhase() == Player::Finish) {
            QList<ServerPlayer *> players = room->getOtherPlayers(dongzhuo);
            foreach (ServerPlayer *player, players) {
                if (dongzhuo->getHp() > player->getHp()) {
                    trigger_this = true;
                    break;
                }
            }
        }

        if (trigger_this) {
            room->sendCompulsoryTriggerLog(dongzhuo, objectName());

            QString result = room->askForChoice(dongzhuo, "benghuai", "hp+maxhp");
            int index = (dongzhuo->isFemale()) ? 2 : 1;
            if (dongzhuo->isJieGeneral("dongzhuo"))
                index = qrand() % 2 + 6;
            else {
                if (!dongzhuo->hasInnateSkill(this) && (dongzhuo->getMark("juyi") > 0 || dongzhuo->getMark("oljuyi") > 0))
                    index = 3;

                if (!dongzhuo->hasInnateSkill(this) && dongzhuo->getMark("baoling") > 0)
                    index = result == "hp" ? 4 : 5;

            }

            room->broadcastSkillInvoke(objectName(), index);
            if (result == "hp")
                room->loseHp(HpLostStruct(dongzhuo, 1, objectName(), dongzhuo));
            else
                room->loseMaxHp(dongzhuo, 1, objectName());
        }

        return false;
    }
};

class Baonue : public TriggerSkill
{
public:
    Baonue() : TriggerSkill("baonue$")
    {
        events << Damage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive()&&target->getKingdom()=="qun";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
		QList<ServerPlayer *> dongzhuos;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->hasLordSkill(objectName())) dongzhuos << p;
		}
		while (player->isAlive()) {
			ServerPlayer *dongzhuo = room->askForPlayerChosen(player, dongzhuos, objectName(), "@baonue-to", true);
			if (!dongzhuo) break;
			dongzhuos.removeOne(dongzhuo);
			LogMessage log;
			log.type = "#InvokeOthersSkill";
			log.from = player;
			log.to << dongzhuo;
			log.arg = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(dongzhuo, objectName());
			JudgeStruct judge;
			judge.pattern = ".|spade";
			judge.good = true;
			judge.reason = objectName();
			judge.who = player;
			room->judge(judge);
			if (judge.isGood()) {
				if (!dongzhuo->isLord() && dongzhuo->hasSkill("weidi"))
					room->broadcastSkillInvoke("weidi");
				else
					room->broadcastSkillInvoke(objectName());
				room->recover(dongzhuo, RecoverStruct("baonue", player));
            }
        }
        return false;
    }
};

class Guixin : public MasochismSkill
{
public:
    Guixin() : MasochismSkill("guixin")
    {
    }

    void onDamaged(ServerPlayer *shencc, const DamageStruct &damage) const
    {
		Room *room = shencc->getRoom();
		int n = shencc->getMark(objectName() + "Times"); // mark for AI
		shencc->setMark(objectName() + "Times", 0);
		QVariant data = QVariant::fromValue(damage);
		QList<ServerPlayer *> players = room->getOtherPlayers(shencc);
		try {
			for (int i = 0; i < damage.damage; i++) {
				shencc->addMark(objectName() + "Times");
				if (shencc->askForSkillInvoke(this, data)) {
					room->broadcastSkillInvoke(objectName());
	
					shencc->setFlags(objectName() + "Using");
					/*
					if (players.length() >= 4 && (shencc->getGeneralName() == "shencaocao" || shencc->getGeneral2Name() == "shencaocao"))
					room->doLightbox("$GuixinAnimate");
					*/
					foreach (ServerPlayer *player, players){
						room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, shencc->objectName(), player->objectName());
					}
					room->doSuperLightbox(shencc, "newguixin");
	
					foreach (ServerPlayer *player, players) {
						if (player->isAlive() && !player->isAllNude()) {
							CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, shencc->objectName());
							int card_id = -1;
							if (objectName() == "guixin")
								card_id = room->askForCardChosen(shencc, player, "hej", objectName());
							else if (objectName() == "newguixin")
								card_id = player->getCards("hej").at(qrand() % player->getCards("hej").length())->getEffectiveId();
							if (card_id > 0)
								room->obtainCard(shencc, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
	
							if (shencc->isDead()) {
								shencc->setFlags("-" + objectName() + "Using");
								shencc->setMark(objectName() + "Times", 0);
								return;
							}
	
						}
					}
	
					shencc->turnOver();
					shencc->setFlags("-" + objectName() + "Using");
				} else
					break;
			}
			shencc->setMark(objectName() + "Times", n);
		}
		catch (TriggerEvent triggerEvent) {
			if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
				shencc->setFlags("-" + objectName() + "Using");
				shencc->setMark(objectName() + "Times", n);
			}
			throw triggerEvent;
		}
    }
};

class NewGuixin : public Guixin
{
public:
    NewGuixin() : Guixin()
    {
        setObjectName("newguixin");
    }
};

class Feiying : public DistanceSkill
{
public:
    Feiying() : DistanceSkill("feiying")
    {
    }

    int getCorrect(const Player *, const Player *to) const
    {
        if (to->hasSkill(objectName()))
            return 1;
        return 0;
    }
};

class Kuangbao : public TriggerSkill
{
public:
    Kuangbao() : TriggerSkill("kuangbao")
    {
        events << Damage << Damaged;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        LogMessage log;
        log.type = triggerEvent == Damage ? "#KuangbaoDamage" : "#KuangbaoDamaged";
        log.from = player;
        log.arg = QString::number(damage.damage);
        log.arg2 = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        room->addPlayerMark(player, "&wrath", damage.damage);
        return false;
    }
};

class Wumou : public TriggerSkill
{
public:
    Wumou() : TriggerSkill("wumou")
    {
        frequency = Compulsory;
        events << CardUsed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isNDTrick()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());

            if (player->getMark("&wrath") >= 1 && room->askForChoice(player, objectName(), "discard+losehp") == "discard") {
                player->loseMark("&wrath");
            } else
                room->loseHp(HpLostStruct(player, 1, "wumou", player));
        }

        return false;
    }
};

class Shenfen : public ZeroCardViewAsSkill
{
public:
    Shenfen() : ZeroCardViewAsSkill("shenfen")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("&wrath") >= 6 && !player->hasUsed("ShenfenCard");
    }

    const Card *viewAs() const
    {
        return new ShenfenCard;
    }
};

ShenfenCard::ShenfenCard()
{
    target_fixed = true;
}

void ShenfenCard::use(Room *room, ServerPlayer *shenlvbu, QList<ServerPlayer *> &) const
{
    shenlvbu->setFlags("ShenfenUsing");
	QList<ServerPlayer *> players = room->getOtherPlayers(shenlvbu);
	foreach (ServerPlayer *player, players)
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, shenlvbu->objectName(), player->objectName());
    room->doSuperLightbox(shenlvbu, "shenfen");
    shenlvbu->loseMark("&wrath", 6);
    try {
        foreach (ServerPlayer *player, players) {
            room->damage(DamageStruct("shenfen", shenlvbu, player));
			if(shenlvbu->isDead()) break;
            room->getThread()->delay();
        }

        foreach (ServerPlayer *player, players) {
            bool has = player->hasEquip();
            player->throwAllEquips();
            if (has) room->getThread()->delay();
        }

        foreach (ServerPlayer *player, players) {
            bool has = !player->isKongcheng();
            room->askForDiscard(player, "shenfen", 4, 4);
            if (has) room->getThread()->delay();
        }

        shenlvbu->turnOver();
        shenlvbu->setFlags("-ShenfenUsing");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            shenlvbu->setFlags("-ShenfenUsing");
        throw triggerEvent;
    }
}

WuqianCard::WuqianCard()
{
}

bool WuqianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void WuqianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    effect.from->loseMark("&wrath", 2);
    room->acquireSkill(effect.from, "wushuang");
    effect.from->setFlags("WuqianSource");
    effect.to->setFlags("WuqianTarget");
    room->addPlayerMark(effect.to, "Armor_Nullified");
}

class WuqianViewAsSkill : public ZeroCardViewAsSkill
{
public:
    WuqianViewAsSkill() : ZeroCardViewAsSkill("wuqian")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("&wrath") >= 2;
    }

    const Card *viewAs() const
    {
        return new WuqianCard;
    }
};

class Wuqian : public TriggerSkill
{
public:
    Wuqian() : TriggerSkill("wuqian")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new WuqianViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->hasFlag("WuqianSource");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        }
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player)
                return false;
        }

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->hasFlag("WuqianTarget")) {
                p->setFlags("-WuqianTarget");
                room->removePlayerMark(p, "Armor_Nullified");
            }
        }
        room->detachSkillFromPlayer(player, "wushuang", false, true);

        return false;
    }
};

class TenyearDuanliang : public OneCardViewAsSkill
{
public:
    TenyearDuanliang() : OneCardViewAsSkill("tenyearduanliang")
    {
        filter_pattern = "BasicCard,EquipCard|black";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        shortage->setSkillName(objectName());
        shortage->addSubcard(originalCard);
        return shortage;
    }
};

class TenyearDuanliangTargetMod : public TargetModSkill
{
public:
    TenyearDuanliangTargetMod() : TargetModSkill("#tenyearduanliang-target")
    {
        frequency = NotFrequent;
        pattern = "SupplyShortage";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *to) const
    {
        if (to&&from->getHandcardNum()<=to->getHandcardNum()&&from->hasSkill("tenyearduanliang"))
            return 1000;
        return 0;
    }
};

class TenyearJiezi : public TriggerSkill
{
public:
    TenyearJiezi() : TriggerSkill("tenyearjiezi")
    {
        events << EventPhaseSkipped;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Draw) return false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isAlive() && p->hasSkill(objectName())) {
                room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                p->drawCards(1, objectName());
            }
        }
        return false;
    }
};

ThicketPackage::ThicketPackage()
    : Package("thicket")
{
    General *xuhuang = new General(this, "xuhuang", "wei"); // WEI 010
    xuhuang->addSkill(new Duanliang);
    xuhuang->addSkill(new DuanliangTargetMod);
    related_skills.insertMulti("duanliang", "#duanliang-target");

    General *tenyear_xuhuang = new General(this, "tenyear_xuhuang", "wei", 4);
    tenyear_xuhuang->addSkill(new TenyearDuanliang);
    tenyear_xuhuang->addSkill(new TenyearDuanliangTargetMod);
    tenyear_xuhuang->addSkill(new TenyearJiezi);
    related_skills.insertMulti("tenyearduanliang", "#tenyearduanliang-target");

    General *caopi = new General(this, "caopi$", "wei", 3); // WEI 014
    caopi->addSkill(new Xingshang);
    caopi->addSkill(new Fangzhu);
    caopi->addSkill(new Songwei);

    General *menghuo = new General(this, "menghuo", "shu"); // SHU 014
    menghuo->addSkill(new Huoshou);
    menghuo->addSkill(new Zaiqi);

    General *zhurong = new General(this, "zhurong", "shu", 4, false); // SHU 015
    zhurong->addSkill(new Juxiang);
    zhurong->addSkill(new Lieren);

    General *sunjian = new General(this, "sunjian", "wu"); // WU 009
    sunjian->addSkill(new Yinghun);

    General *lusu = new General(this, "lusu", "wu", 3); // WU 014
    lusu->addSkill(new Haoshi);
    lusu->addSkill(new HaoshiViewAsSkill);
    lusu->addSkill(new HaoshiGive);
    lusu->addSkill(new Dimeng);
    related_skills.insertMulti("haoshi", "#haoshi-give");

    General *dongzhuo = new General(this, "dongzhuo$", "qun", 8); // QUN 006
    dongzhuo->addSkill(new Jiuchi);
    dongzhuo->addSkill(new Roulin);
    dongzhuo->addSkill(new Benghuai);
    dongzhuo->addSkill(new Baonue);

    General *jiaxu = new General(this, "jiaxu", "qun", 3); // QUN 007
    jiaxu->addSkill(new Wansha);
    jiaxu->addSkill(new WanshaLimit);
    jiaxu->addSkill(new Luanwu);
    jiaxu->addSkill(new Weimu);
    related_skills.insertMulti("wansha", "#wansha-limit");

    General *shencaocao = new General(this, "shencaocao", "god", 3); // LE 005
    shencaocao->addSkill(new Guixin);
    shencaocao->addSkill(new Feiying);

    General *new_shencaocao = new General(this, "new_shencaocao", "god", 3);
    new_shencaocao->addSkill(new NewGuixin);
    new_shencaocao->addSkill("feiying");

    General *shenlvbu = new General(this, "shenlvbu", "god", 5); // LE 006
    shenlvbu->addSkill(new Kuangbao);
    shenlvbu->addSkill(new MarkAssignSkill("&wrath", 2));
    shenlvbu->addSkill(new Wumou);
    shenlvbu->addSkill(new Wuqian);
    shenlvbu->addSkill(new Shenfen);
    related_skills.insertMulti("kuangbao", "#&wrath-2");
    addMetaObject<ShenfenCard>();
    addMetaObject<WuqianCard>();

    addMetaObject<DimengCard>();
    addMetaObject<LuanwuCard>();
    addMetaObject<HaoshiCard>();
}
ADD_PACKAGE(Thicket)
