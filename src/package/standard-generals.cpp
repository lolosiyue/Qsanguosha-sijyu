//#include "general.h"
#include "standard.h"
//#include "skill.h"
#include "engine.h"
//#include "client.h"
//#include "serverplayer.h"
#include "room.h"
#include "standard-generals.h"
//#include "ai.h"
#include "settings.h"
#include "sp.h"
//#include "wind.h"
#include "mountain.h"
//#include "maneuvering.h"
//#include "json.h"
#include "clientplayer.h"
#include "clientstruct.h"
//#include "util.h"
#include "wrapped-card.h"
#include "roomthread.h"

ZhihengCard::ZhihengCard()
{
    target_fixed = true;
    mute = true;
}

void ZhihengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->hasInnateSkill("zhiheng") || !source->hasSkill("jilve"))
        room->broadcastSkillInvoke("zhiheng");
    else
        room->broadcastSkillInvoke("jilve", 4);
    if (source->isAlive())
        room->drawCards(source, subcards.length(), "zhiheng");
}

RendeCard::RendeCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    mute = true;
}

void RendeCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    int old_value = source->getMark("rende-PlayClear");
    if (old_value<1) source->peiyin("rende");

    ServerPlayer *target = targets.first();

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "rende", "");
    room->obtainCard(target, this, reason, false);

    int new_value = old_value + subcards.length();
    room->setPlayerMark(source, "rende-PlayClear", new_value);

    if (old_value < 2 && new_value >= 2)
        room->recover(source, RecoverStruct("rende", source));

    if (room->getMode() == "04_1v3" && source->getMark("rende") >= 2) return;
    if (source->isDead() || source->isKongcheng()) return;

    room->askForUseCard(source, "@@rende", "@rende-give", -1, Card::MethodNone,false);
}

YijueCard::YijueCard()
{
}

bool YijueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void YijueCard::use(Room *room, ServerPlayer *guanyu, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    bool success = guanyu->pindian(target, "yijue", nullptr);
    if (success) {
        target->addMark("yijue");
        room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true);
        room->addPlayerMark(target, "@skill_invalidity");

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);
        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    } else {
        if (!target->isWounded()) return;
        target->setFlags("YijueTarget");
        QString choice = room->askForChoice(guanyu, "yijue", "recover+cancel");
        target->setFlags("-YijueTarget");
        if (choice == "recover")
            room->recover(target, RecoverStruct("yijue", guanyu));
    }
}

JieyinCard::JieyinCard()
{
    mute = true;
}

bool JieyinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty())
        return false;

    return to_select->isMale() && to_select->isWounded() && to_select != Self;
}

void JieyinCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;

    int index = qrand() % 2 + 1;
    if (from->isMale()) {
        index = 4;
        if (from == to)
            index = 5;
        else if (from->getHp() >= to->getHp())
            index = 3;
    }
    from->peiyin("jieyin", index);

    Room *room = from->getRoom();
    RecoverStruct recover("jieyin", from);
    room->recover(from, recover, true);
    room->recover(to, recover, true);
}

TuxiCard::TuxiCard()
{
}

bool TuxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() >= Self->getMark("tuxi") || to_select->getHandcardNum() < Self->getHandcardNum() || to_select == Self)
        return false;

    return !to_select->isKongcheng();
}

void TuxiCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.from->isDead()) return;
    effect.from->addMark("TuxiTarget");
    Room *room = effect.to->getRoom();
	int id = room->askForCardChosen(effect.from,effect.to,"h","tuxi");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName(),effect.to->objectName(),"tuxi","");
    if(id>=0) room->obtainCard(effect.from,Sanguosha->getCard(id),reason,false);
}

FanjianCard::FanjianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void FanjianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhouyu = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhouyu->getRoom();
    Card::Suit suit = getSuit();

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, zhouyu->objectName(), target->objectName(), "fanjian", "");
    room->obtainCard(target, this, reason);

    target->setMark("FanjianSuit", int(suit)); // For AI
	if (!target->isNude()&&target->askForSkillInvoke("fanjian_discard", "prompt:::"+Card::Suit2String(suit))) {
		room->showAllCards(target);
		DummyCard *dummy = new DummyCard;
		foreach (const Card *card, target->getCards("he")) {
			if (card->getSuit() == suit)
				dummy->addSubcard(card);
		}
		if (dummy->subcardsLength() > 0)
			room->throwCard(dummy, target);
		delete dummy;
	} else
		room->loseHp(HpLostStruct(target, 1, "fanjian", zhouyu));
}

KurouCard::KurouCard()
{
    target_fixed = true;
}

void KurouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->loseHp(HpLostStruct(source, 1, "kurou", source));
}

LianyingCard::LianyingCard()
{
}

bool LianyingCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    return targets.length() < Self->getMark("lianying");
}

void LianyingCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->drawCards(1, "lianying");
}

LijianCard::LijianCard(bool cancelable) : duel_cancelable(cancelable)
{
}

bool LijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    if (!to_select->isMale()) return false;

	if(targets.length() == 1){
		Duel *duel = new Duel(Card::NoSuit, 0);
		duel->deleteLater();
		if(to_select->isCardLimited(duel,Card::MethodUse)||targets.first()->isProhibited(to_select,duel))
			return false;
	}
    return targets.length() < 2;
}

bool LijianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void LijianCard::onUse(Room *room, CardUseStruct &use) const
{
	use.from->setTag("LijianUse", QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void LijianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    CardUseStruct use = source->getTag("LijianUse").value<CardUseStruct>();
	ServerPlayer *to = use.to.at(0);
    ServerPlayer *from = use.to.at(1);

    Duel *duel = new Duel(Card::NoSuit, 0);
    duel->setCancelable(duel_cancelable);
	QString sn = getSkillName();
	if(sn.isEmpty()) sn = getClassName().remove("Card").toLower();
    duel->setSkillName("_"+sn);
    if (from->canUse(duel, to))
        room->useCard(CardUseStruct(duel, from, to));
    delete duel;
}

ChuliCard::ChuliCard()
{
}

bool ChuliCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self) return false;
    QSet<QString> kingdoms;
    foreach(const Player *p, targets)
        kingdoms << p->getKingdom();
    return Self->canDiscard(to_select, "he") && !kingdoms.contains(to_select->getKingdom());
}

void ChuliCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QList<ServerPlayer *> draw_card;
    if (Sanguosha->getCard(getEffectiveId())->getSuit() == Card::Spade)
        draw_card << source;
    foreach (ServerPlayer *target, targets) {
        if (!source->canDiscard(target, "he")) continue;
        int id = room->askForCardChosen(source, target, "he", "chuli", false, Card::MethodDiscard);
        room->throwCard(id, target, source);
        if (Sanguosha->getCard(id)->getSuit() == Card::Spade)
            draw_card << target;
    }

    foreach(ServerPlayer *p, draw_card)
        room->drawCards(p, 1, "chuli");
}

LiuliCard::LiuliCard()
{
}

bool LiuliCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty())
        return false;

    if (to_select->hasFlag("LiuliSlashSource") || to_select == Self)
        return false;

    const Player *from = nullptr;
    foreach (const Player *p, Self->getAliveSiblings()) {
        if (p->hasFlag("LiuliSlashSource")) {
            from = p;
            break;
        }
    }

    const Card *slash = Card::Parse(Self->property("liuli").toString());
    if (from && !from->canSlash(to_select, slash, false))
        return false;

    return Self->inMyAttackRange(to_select, subcards);
}

void LiuliCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->setFlags("LiuliTarget");
}

FenweiCard::FenweiCard()
{
}

bool FenweiCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    QStringList targetslist = Self->property("fenwei_targets").toString().split("+");
    return targetslist.contains(to_select->objectName());
}

void FenweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(source, "@fenwei");
    //room->doLightbox("$FenweiAnimate");
    room->doSuperLightbox(source, "fenwei");

    CardUseStruct use = source->getTag("fenwei").value<CardUseStruct>();
    foreach(ServerPlayer *p, targets)
        use.nullified_list << p->objectName();
    source->setTag("fenwei", QVariant::fromValue(use));
}

GuoseCard::GuoseCard()
{
    handling_method = Card::MethodNone;
}

bool GuoseCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length()>0) return false;
	
	if(to_select->containsTrick("indulgence")){
		if (Self->isJilei(Sanguosha->getCard(getEffectiveId())))
			return false;
		foreach (const Card *j, to_select->getJudgingArea()) {
			if (j->isKindOf("Indulgence") && Self->canDiscard(to_select, j->getEffectiveId()))
				return true;
		}
	}else{
		if (Self==to_select) return false;
		Indulgence *indulgence = new Indulgence(getSuit(), getNumber());
		indulgence->setSkillName("guose");
		indulgence->addSubcard(this);
		indulgence->deleteLater();
		return !Self->isLocked(indulgence)&&!Self->isProhibited(to_select, indulgence);
	}
    return false;
}

const Card *GuoseCard::validate(CardUseStruct &cardUse) const
{
    if (cardUse.to.first()->containsTrick("indulgence"))
		return this;
	Indulgence *indulgence = new Indulgence(getSuit(), getNumber());
	indulgence->setSkillName("guose");
	indulgence->addSubcard(this);
    indulgence->deleteLater();
	return indulgence;
}

void GuoseCard::onEffect(CardEffectStruct &effect) const
{
    foreach (const Card *judge, effect.to->getJudgingArea()) {
        if (judge->isKindOf("Indulgence") && effect.from->canDiscard(effect.to, judge->getEffectiveId())) {
            effect.from->getRoom()->throwCard(judge, nullptr, effect.from);
            break;
        }
    }
}

JijiangCard::JijiangCard(const QString &jijiang) : jijiang(jijiang)
{
    mute = true;
}

bool JijiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(NoSuit, 0);
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

const Card *JijiangCard::validate(CardUseStruct &cardUse) const
{
    cardUse.m_isOwnerUse = false;
    ServerPlayer *liubei = cardUse.from;
    Room *room = liubei->getRoom();

    if (!liubei->isLord() && liubei->hasSkill("weidi"))
        room->broadcastSkillInvoke("weidi");
    else {
        int r = 1 + qrand() % 2;
        if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
            r += 2;
        else if (liubei->isJieGeneral())
            r = qrand() % 2 + 5;
        room->broadcastSkillInvoke("jijiang", r);
    }

    LogMessage log;
    log.from = liubei;
    log.to = cardUse.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);
    room->notifySkillInvoked(liubei, jijiang);

	const Card *slash = nullptr;
    foreach(ServerPlayer *target, log.to)
        target->setFlags(jijiang == "jijiang" ? "JijiangTarget" : "OLJijiangTarget");
    foreach (ServerPlayer *liege, room->getLieges("shu", liubei)) {
        try {
            slash = room->askForCard(liege, "slash", "@" + jijiang + "-slash:" + liubei->objectName(),
                QVariant::fromValue(liubei), Card::MethodResponse, liubei, false, "", true);
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
                foreach(ServerPlayer *target, log.to)
                    target->setFlags(jijiang == "jijiang" ? "-JijiangTarget" : "-OLJijiangTarget");
            }
            throw triggerEvent;
        }

        if (slash) {
            foreach (ServerPlayer *target, log.to) {
                if (!liubei->canSlash(target, slash, false))
                    cardUse.to.removeOne(target);
            }
            if (cardUse.to.isEmpty()) slash = nullptr;
			else room->setCardFlag(slash,"YUANBEN");
            break;
        }
    }
    foreach(ServerPlayer *target, log.to)
        target->setFlags(jijiang == "jijiang" ? "-JijiangTarget" : "-OLJijiangTarget");
    room->setPlayerFlag(liubei, "Global_JijiangFailed");
    return slash;
}

YijiCard::YijiCard()
{
    mute = true;
}

bool YijiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self) return false;
    if (Self->getHandcardNum() == 1)
        return targets.isEmpty();
    else
        return targets.length() < 2;
}

void YijiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *target, targets) {
        if (!source->isAlive() || source->isKongcheng()) break;
        if (!target->isAlive()) continue;
        int max = qMin(2, source->getHandcardNum());
        if (source->getHandcardNum() == 2 && targets.length() == 2 && targets.last()->isAlive() && target == targets.first())
            max = 1;
        const Card *dummy = room->askForExchange(source, "yiji", max, 1, false, "YijiGive::" + target->objectName());
        target->addToPile("yiji", dummy, false);
    }
}

JianyanCard::JianyanCard()
{
    target_fixed = true;
}

void JianyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choice_list, pattern_list;
    choice_list << "basic" << "trick" << "equip" << "red" << "black";
    pattern_list << "BasicCard" << "TrickCard" << "EquipCard" << ".|red" << ".|black";

    QString choice = room->askForChoice(source, "jianyan", choice_list.join("+"));
    QString pattern = pattern_list.at(choice_list.indexOf(choice));

    LogMessage log;
    log.type = "#JianyanChoice";
    log.from = source;
    log.arg = choice;
    room->sendLog(log);

    QList<int> cardIds;
    while (true) {
        int id = room->drawCard();
        cardIds << id;
        CardsMoveStruct move(id, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "jianyan", ""));
        room->moveCardsAtomic(move, true);
        room->getThread()->delay();

        const Card *card = Sanguosha->getCard(id);
        if (Sanguosha->matchExpPattern(pattern, nullptr, card)) {
            QList<ServerPlayer *> males;
            foreach (ServerPlayer *player, room->getAlivePlayers()) {
                if (player->isMale())
                    males << player;
            }
            if (!males.isEmpty()) {
                QList<int> ids;
                ids << id;
                cardIds.removeOne(id);
                room->fillAG(ids, source);
                source->setMark("jianyan", id); // For AI
                ServerPlayer *target = room->askForPlayerChosen(source, males, "jianyan",
                    QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
                    .arg(card->getSuitString() + "_char")
                    .arg(card->getNumberString()));
                room->clearAG(source);
                room->obtainCard(target, card);
            }
            break;
        }
    }
    if (!cardIds.isEmpty()) {
        DummyCard *dummy = new DummyCard(cardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "jianyan", "");
        room->throwCard(dummy, reason, nullptr);
        delete dummy;
    }
}

class Jianxiong : public MasochismSkill
{
public:
    Jianxiong() : MasochismSkill("jianxiong")
    {
    }

    void onDamaged(ServerPlayer *caocao, const DamageStruct &damage) const
    {
        Room *room = caocao->getRoom();
        QVariant data = QVariant::fromValue(damage);
        QStringList choices;
        choices << "draw" << "cancel";

        if (damage.card&&damage.card->getEffectiveId()>-1&&!room->getCardOwner(damage.card->getEffectiveId()))
            choices.prepend("obtain");

        QString choice = room->askForChoice(caocao, objectName(), choices.join("+"), data);
        if (choice != "cancel") {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = caocao;
            log.arg = objectName();
            room->sendLog(log);

            room->broadcastSkillInvoke(objectName(),qrand()%2+1,caocao);
            room->notifySkillInvoked(caocao, objectName());
            if (choice == "obtain")
                caocao->obtainCard(damage.card);
            else
                caocao->drawCards(1, objectName());
        }
    }
};

Hujia::Hujia(const QString &hujia) : TriggerSkill(hujia + "$"), hujia(hujia)
{
    events << CardAsked;
}

bool Hujia::triggerable(const ServerPlayer *target) const
{
    return target != nullptr && target->isAlive() && target->hasLordSkill(this);
}

bool Hujia::trigger(TriggerEvent, Room *room, ServerPlayer *caocao, QVariant &data) const
{
    QStringList patterns = data.toStringList();
    if (patterns.first() != "jink" || patterns.at(1).contains("hujia-jink"))
        return false;

    QList<ServerPlayer *> lieges = room->getLieges("wei", caocao);
    if (lieges.isEmpty())
        return false;

    if (!room->askForSkillInvoke(caocao, objectName(), data))
        return false;
    if (!caocao->isLord() && caocao->hasSkill("weidi"))
        room->broadcastSkillInvoke("weidi");
    else {
        int index = qrand() % 2 + 1;
        if (objectName() == "olhujia")
            room->broadcastSkillInvoke("hujia", index);
        else {
            if (Player::isNostalGeneral(caocao, "caocao"))
                index += 2;
            room->broadcastSkillInvoke(objectName(), index);
        }
    }
    foreach (ServerPlayer *liege, lieges) {
        const Card *jink = room->askForCard(liege, "jink", "@hujia-jink:" + caocao->objectName(),
            QVariant::fromValue(caocao), Card::MethodResponse, caocao, false, "", true);
        if (jink) {
			room->setCardFlag(jink,"YUANBEN");
            room->provide(jink);
            return true;
        }
    }
    return false;
}

class TuxiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    TuxiViewAsSkill() : ZeroCardViewAsSkill("tuxi")
    {
        response_pattern = "@@tuxi";
    }

    const Card *viewAs() const
    {
        return new TuxiCard;
    }
};

class Tuxi : public DrawCardsSkill
{
public:
    Tuxi() : DrawCardsSkill("tuxi")
    {
        view_as_skill = new TuxiViewAsSkill;
    }

    int getPriority(TriggerEvent) const
    {
        return 1;
    }

    int getDrawNum(ServerPlayer *zhangliao, int n) const
    {
        int num = 0;
        Room *room = zhangliao->getRoom();
        foreach(ServerPlayer *p, room->getOtherPlayers(zhangliao)){
            if (p->getHandcardNum() >= zhangliao->getHandcardNum())
                num++;
		}
		num = qMin(num, n);

        if (num > 0) {
            zhangliao->setMark("TuxiTarget",0);
			room->setPlayerMark(zhangliao, "tuxi", num);
            if (room->askForUseCard(zhangliao, "@@tuxi", "@tuxi-card:::" + QString::number(num)))
				n -= zhangliao->getMark("TuxiTarget");
        }
		return n;
    }
};

class Tiandu : public TriggerSkill
{
public:
    Tiandu() : TriggerSkill("tiandu")
    {
        frequency = Frequent;
        events << FinishJudge;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *guojia, QVariant &data) const
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        const Card *card = judge->card;

        QVariant data_card = QVariant::fromValue(card);
        if (room->getCardPlace(card->getEffectiveId()) == Player::PlaceJudge
            && guojia->askForSkillInvoke(this, data_card)) {
            int index = qrand() % 2 + 1;
            if (Player::isNostalGeneral(guojia, "guojia"))
                index += 2;
            else if (guojia->getGeneralName().contains("xizhicai") || (!guojia->getGeneralName().contains("guojia") && guojia->getGeneral2Name().contains("xizhicai")))
                index += 4;
            room->broadcastSkillInvoke(objectName(), index);
            guojia->obtainCard(judge->card);
            return false;
        }

        return false;
    }
};

class YijiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    YijiViewAsSkill() : ZeroCardViewAsSkill("yiji")
    {
        response_pattern = "@@yiji";
    }

    const Card *viewAs() const
    {
        return new YijiCard;
    }
};

class Yiji : public MasochismSkill
{
public:
    Yiji() : MasochismSkill("yiji")
    {
        view_as_skill = new YijiViewAsSkill;
        frequency = Frequent;
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        Room *room = target->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            if (target->isAlive() && room->askForSkillInvoke(target, objectName(), QVariant::fromValue(damage))) {
                room->broadcastSkillInvoke(objectName());
                target->drawCards(2, objectName());
                room->askForUseCard(target, "@@yiji", "@yiji");
            } else
                break;
        }
    }
};

class YijiObtain : public PhaseChangeSkill
{
public:
    YijiObtain() : PhaseChangeSkill("#yiji")
    {
    }

    int getPriority(TriggerEvent) const
    {
        return 4;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() == Player::Draw && !target->getPile("yiji").isEmpty()) {
            DummyCard *dummy = new DummyCard(target->getPile("yiji"));
            CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName(), "yiji", "");
            room->obtainCard(target, dummy, reason, false);
            delete dummy;
        }
        return false;
    }
};

class Ganglie : public TriggerSkill
{
public:
    Ganglie() : TriggerSkill("ganglie")
    {
        events << Damaged;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *xiahou, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();

            for (int i = 0; i < damage.damage; i++) {
                if (room->askForSkillInvoke(xiahou, "ganglie", data)) {
                    room->broadcastSkillInvoke(objectName());

                    JudgeStruct judge;
                    judge.pattern = ".";
                    judge.play_animation = false;
                    judge.reason = objectName();
                    judge.who = xiahou;

                    room->judge(judge);
                    if (!damage.from || damage.from->isDead()) continue;
                    if(judge.card->isRed()){
                        room->damage(DamageStruct(objectName(), xiahou, damage.from));
					}else if(judge.card->isBlack()){
                        if (xiahou->canDiscard(damage.from, "he")) {
                            int id = room->askForCardChosen(xiahou, damage.from, "he", objectName(), false, Card::MethodDiscard);
                            room->throwCard(id, damage.from, xiahou);
                        }
					}
                } else
                    break;
            }
        }
        return false;
    }
};

class Qingjian : public TriggerSkill
{
public:
    Qingjian() : TriggerSkill("qingjian")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!room->getTag("FirstRound").toBool() && player->getPhase() != Player::Draw && move.to == player && move.to_place == Player::PlaceHand) {
            QList<int> ids;
            foreach (int id, move.card_ids) {
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    ids << id;
            }
            if (ids.isEmpty())
                return false;
            player->setTag("QingjianCurrentMoveSkill", QVariant(move.reason.m_skillName));
            while (room->askForYiji(player, ids, objectName(), false, false, true, -1, QList<ServerPlayer *>(), CardMoveReason(), "@qingjian-distribute", true)) {
                if (player->isDead()) return false;
            }
        }
        return false;
    }
};

class Fankui : public MasochismSkill
{
public:
    Fankui() : MasochismSkill("fankui")
    {
    }

    void onDamaged(ServerPlayer *simayi, const DamageStruct &damage) const
    {
        ServerPlayer *from = damage.from;
        Room *room = simayi->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            QVariant data = QVariant::fromValue(from);
            if (from && !from->isNude() && room->askForSkillInvoke(simayi, "fankui", data)) {
                room->broadcastSkillInvoke(objectName());
                int card_id = room->askForCardChosen(simayi, from, "he", "fankui");
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, simayi->objectName());
                room->obtainCard(simayi, Sanguosha->getCard(card_id),
                    reason, room->getCardPlace(card_id) != Player::PlaceHand);
            } else {
                break;
            }
        }
    }
};

class Guicai : public RetrialSkill
{
public:
    Guicai() : RetrialSkill("guicai")
    {

    }

    const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
    {
        if (player->isNude())
            return nullptr;

        QStringList prompt_list;
        prompt_list << "@guicai-card" << judge->who->objectName()
            << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");
        bool forced = false;
        if (player->getMark("JilveEvent") == int(AskForRetrial))
            forced = true;

        Room *room = player->getRoom();

        const Card *card = room->askForCard(player, forced ? "..!" : "..", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);
        if (forced && card == nullptr) {
            QList<const Card *> c = player->getCards("he");
            card = c.at(qrand() % c.length());
        }

        if (card) {
            if (player->hasInnateSkill("guicai") || !player->hasSkill("jilve"))
                room->broadcastSkillInvoke(objectName());
            else
                room->broadcastSkillInvoke("jilve", 1);
        }

        return card;
    }
};

class LuoyiBuff : public TriggerSkill
{
public:
    LuoyiBuff() : TriggerSkill("#luoyi")
    {
        events << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getMark("&luoyi") > 0 && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *xuchu, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.chain || damage.transfer) return false;
        const Card *reason = damage.card;
        if (reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))) {
            LogMessage log;
            log.type = "#LuoyiBuff";
            log.from = xuchu;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data.setValue(damage);
        }

        return false;
    }
};

class Luoyi : public TriggerSkill
{
public:
    Luoyi() : TriggerSkill("luoyi")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseStart)
            return 4;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::RoundStart && player->getMark("&luoyi") > 0)
                room->setPlayerMark(player, "&luoyi", 0);
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (TriggerSkill::triggerable(player) && change.to == Player::Draw && !player->isSkipped(Player::Draw)
                && room->askForSkillInvoke(player, objectName())) {
                room->broadcastSkillInvoke(objectName());
                player->skip(Player::Draw, true);
                room->setPlayerMark(player, "&luoyi", 1);

                QList<int> ids = room->getNCards(3, false);
                CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "luoyi", ""));
                room->moveCardsAtomic(move, true);

                room->getThread()->delay();
                room->getThread()->delay();

                QList<int> card_to_throw;
                QList<int> card_to_gotback;
                for (int i = 0; i < 3; i++) {
                    const Card *card = Sanguosha->getCard(ids[i]);
                    if (card->getTypeId() == Card::TypeBasic || card->isKindOf("Weapon") || card->isKindOf("Duel"))
                        card_to_gotback << ids[i];
                    else
                        card_to_throw << ids[i];
                }
                if (!card_to_throw.isEmpty()) {
                    DummyCard *dummy = new DummyCard(card_to_throw);
                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "luoyi", "");
                    room->throwCard(dummy, reason, nullptr);
                    delete dummy;
                }
                if (!card_to_gotback.isEmpty()) {
                    DummyCard *dummy = new DummyCard(card_to_gotback);
                    room->obtainCard(player, dummy);
                    delete dummy;
                }
            }
        }
        return false;
    }
};

class Luoshen : public TriggerSkill
{
public:
    Luoshen() : TriggerSkill("luoshen")
    {
        events << EventPhaseStart << FinishJudge;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhenji, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && zhenji->getPhase() == Player::Start) {
            bool canRetrial = zhenji->hasSkills("guicai|nosguicai|guidao|huanshi");
            bool first = true;
            while (zhenji->askForSkillInvoke("luoshen")) {
                if (first) {
                    room->broadcastSkillInvoke(objectName());
                    first = false;
                }

                JudgeStruct judge;
                judge.pattern = ".|black";
                judge.good = true;
                judge.reason = objectName();
                //judge.play_animation = false;
                judge.who = zhenji;
                //judge.time_consuming = true;

                if (canRetrial)
                    zhenji->setFlags("LuoshenRetrial");
                try {
                    room->judge(judge);
                }
                catch (TriggerEvent triggerEvent) {
                    if ((triggerEvent == TurnBroken || triggerEvent == StageChange) && zhenji->hasFlag("LuoshenRetrial"))
                        zhenji->setFlags("-LuoshenRetrial");
                    throw triggerEvent;
                }

                if (judge.isBad())
                    break;
            }
            if (canRetrial && zhenji->getTag(objectName()).isValid()) {
                DummyCard *dummy = new DummyCard(ListV2I(zhenji->getTag(objectName()).toList()));
                if (dummy->subcardsLength() > 0)
                    zhenji->obtainCard(dummy);
                zhenji->removeTag(objectName());
                delete dummy;
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName()) {
                bool canRetrial = zhenji->hasFlag("LuoshenRetrial");
                if (judge->card->isBlack()) {
                    if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
                        if (canRetrial) {
                            CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE, zhenji->objectName(), "", judge->reason);
                            room->moveCardTo(judge->card, zhenji, nullptr, Player::PlaceTable, reason, true);
                            QVariantList luoshen_list = zhenji->getTag(objectName()).toList();
                            luoshen_list << judge->card->getEffectiveId();
                            zhenji->setTag(objectName(), luoshen_list);
                        } else {
                            zhenji->obtainCard(judge->card);
                        }
                    }
                } else {
                    if (canRetrial) {
                        DummyCard *dummy = new DummyCard(ListV2I(zhenji->getTag(objectName()).toList()));
                        if (dummy->subcardsLength() > 0)
                            zhenji->obtainCard(dummy);
                        zhenji->removeTag(objectName());
                        delete dummy;
                    }
                }
            }
        }

        return false;
    }
};

class Qingguo : public OneCardViewAsSkill
{
public:
    Qingguo() : OneCardViewAsSkill("qingguo")
    {
        filter_pattern = ".|black|.|hand";
        response_pattern = "jink";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
        jink->addSubcard(originalCard->getId());
        jink->setSkillName(objectName());
        return jink;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int n = qrand() % 2 + 1;
        if (player->getGeneralName().startsWith("tenyear_") || (!player->getGeneralName().startsWith("tenyear_") && player->getGeneral2() &&
                player->getGeneral2Name().startsWith("tenyear_")))
            n += 2;
        return n;
    }
};

class Rende : public ViewAsSkill
{
public:
    Rende() : ViewAsSkill("rende")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (ServerInfo.GameMode == "04_1v3" && selected.length() + Self->getMark("rende-PlayClear") >= 2)
            return false;
        else {
			return !to_select->isEquipped();
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (ServerInfo.GameMode == "04_1v3" && player->getMark("rende") >= 2)
            return false;
        return !player->hasUsed("RendeCard") && !player->isKongcheng();
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@rende";
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        RendeCard *rende_card = new RendeCard;
        rende_card->addSubcards(cards);
        return rende_card;
    }
};

JijiangViewAsSkill::JijiangViewAsSkill() : ZeroCardViewAsSkill("jijiang$")
{
}

bool JijiangViewAsSkill::isEnabledAtPlay(const Player *player) const
{
    return hasShuGenerals(player) && !player->hasFlag("Global_JijiangFailed") && Slash::IsAvailable(player);
}

bool JijiangViewAsSkill::isEnabledAtResponse(const Player *player, const QString &pattern) const
{
    return hasShuGenerals(player)
        && (pattern.contains("slash") || pattern.contains("Slash") || pattern == "@jijiang")
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && !player->hasFlag("Global_JijiangFailed");
}

const Card *JijiangViewAsSkill::viewAs() const
{
    return new JijiangCard;
}

bool JijiangViewAsSkill::hasShuGenerals(const Player *player)
{
    foreach(const Player *p, player->getAliveSiblings())
        if (p->getKingdom() == "shu")
            return true;
    return false;
}

class Jijiang : public TriggerSkill
{
public:
    Jijiang() : TriggerSkill("jijiang$")
    {
        events << CardAsked;
        view_as_skill = new JijiangViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->hasLordSkill("jijiang");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *liubei, QVariant &data) const
    {
        QStringList patterns = data.toStringList();
        if (patterns.first() != "slash" || patterns.at(2) != "response" || patterns.at(1).contains("jijiang-slash"))
            return false;
        QList<ServerPlayer *> lieges = room->getLieges("shu", liubei);
        if (lieges.isEmpty())
            return false;
        if (!liubei->hasFlag("qinwangjijiang") && !room->askForSkillInvoke(liubei, objectName(), data))
            return false;
        if (!liubei->isLord() && liubei->hasSkill("weidi"))
            room->broadcastSkillInvoke("weidi");
        else {
            int r = 1 + qrand() % 2;
            if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
                r += 2;
            else if (liubei->isJieGeneral())
                r = qrand() % 2 + 5;
            room->broadcastSkillInvoke("jijiang", r);
        }
        foreach (ServerPlayer *liege, lieges) {
            const Card *slash = room->askForCard(liege, "slash", "@jijiang-slash:" + liubei->objectName(),
                QVariant::fromValue(liubei), Card::MethodResponse, liubei, false, "", true);
            if (slash) {
                room->setCardFlag(slash,"YUANBEN");
				room->provide(slash);
                return true;
            }
        }
        return false;
    }
};

class Wusheng : public OneCardViewAsSkill
{
public:
    Wusheng() : OneCardViewAsSkill("wusheng")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.contains("slash") || pattern.contains("Slash");
    }

    bool viewFilter(const Card *card) const
    {
        if (!card->isRed()) return false;
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            Slash *slash = new Slash(Card::SuitToBeDecided, -1);
            slash->addSubcard(card->getEffectiveId());
            slash->deleteLater();
            return slash->isAvailable(Self);
        }
        return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard->getId());
        slash->setSkillName(objectName());
        return slash;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (Player::isNostalGeneral(player, "guanyu"))
            index += 2;
        else if (player->getGeneralName() == "jsp_guanyu" || (player->getGeneralName() != "guanyu" && player->getGeneral2Name() == "jsp_guanyu"))
            index += 4;
        else if (player->getGeneralName().contains("guansuo") || (player->getGeneralName() != "guanyu" && player->getGeneral2Name().contains("guansuo")))
            index = 7;
        return index;
    }
};

class YijueViewAsSkill : public ZeroCardViewAsSkill
{
public:
    YijueViewAsSkill() : ZeroCardViewAsSkill("yijue")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("YijueCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new YijueCard;
    }
};

class Yijue : public TriggerSkill
{
public:
    Yijue() : TriggerSkill("yijue")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new YijueViewAsSkill;
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
            if (death.who != target || target != room->getCurrent())
                return false;
        }
        QList<ServerPlayer *> players = room->getAllPlayers(true);
        foreach (ServerPlayer *player, players) {
            int mark = player->getMark("yijue");
            if (mark == 0) continue;
            player->removeMark("yijue", mark);
            room->removePlayerMark(player, "@skill_invalidity", mark);

            foreach(ServerPlayer *p, room->getAllPlayers())
                room->filterCards(p, p->getCards("he"), false);

            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

            room->removePlayerCardLimitation(player, "use,response", ".|.|.|hand$1");
        }
        return false;
    }
};

class NonCompulsoryInvalidity : public InvaliditySkill
{
public:
    NonCompulsoryInvalidity() : InvaliditySkill("#non-compulsory-invalidity")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("@skill_invalidity")<1 || skill->getFrequency(player) == Skill::Compulsory;
    }
};

class Paoxiao : public TargetModSkill
{
public:
    Paoxiao() : TargetModSkill("paoxiao")
    {
        frequency = NotCompulsory;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill(objectName()))
            return 1000;
        return 0;
    }
};

class Tishen : public TriggerSkill
{
public:
    Tishen() : TriggerSkill("tishen")
    {
        events << EventPhaseChanging << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@substitute";
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
                room->setPlayerProperty(player, "tishen_hp", QString::number(player->getHp()));
                room->setPlayerMark(player, "@substitute", player->getMark("@substitute")); // For UI coupling
            }
        } else if (triggerEvent == EventPhaseStart && TriggerSkill::triggerable(player)
            && player->getMark("@substitute") > 0 && player->getPhase() == Player::Start) {
            QString hp_str = player->property("tishen_hp").toString();
            if (hp_str.isEmpty()) return false;
            int hp = hp_str.toInt();
            int x = qMin(hp - player->getHp(), player->getMaxHp() - player->getHp());
            if (x > 0 && room->askForSkillInvoke(player, objectName(), QVariant::fromValue(x))) {
                room->removePlayerMark(player, "@substitute");
                room->broadcastSkillInvoke(objectName());
                //room->doLightbox("$TishenAnimate");
                room->doSuperLightbox(player, "tishen");

                room->recover(player, RecoverStruct(player, nullptr, x, objectName()));
                player->drawCards(x, objectName());
            }
        }
        return false;
    }
};

class Longdan : public OneCardViewAsSkill
{
public:
    Longdan() : OneCardViewAsSkill("longdan")
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        const Card *card = to_select;

        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return card->isKindOf("Jink");
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern.contains("slash") || pattern.contains("Slash"))
                return card->isKindOf("Jink");
            else if (pattern == "jink")
                return card->isKindOf("Slash");
            return false;
        }
        default:
            return false;
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "jink" || pattern.contains("slash") || pattern.contains("Slash");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (originalCard->isKindOf("Slash")) {
            Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
            jink->addSubcard(originalCard);
            jink->setSkillName(objectName());
            return jink;
        } else if (originalCard->isKindOf("Jink")) {
            Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
            slash->addSubcard(originalCard);
            slash->setSkillName(objectName());
            return slash;
        } else
            return nullptr;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (Player::isNostalGeneral(player, "zhaoyun"))
            index += 2;
        if (player->getGeneralName().contains("sp_tongyuan") || player->getGeneral2Name().contains("sp_tongyuan"))
            index = 5;
        return index;
    }
};

class Yajiao : public TriggerSkill
{
public:
    Yajiao() : TriggerSkill("yajiao")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->hasFlag("CurrentPlayer")) return false;
        const Card *cardstar = nullptr;
        bool isHandcard = false;
        if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            cardstar = use.card;
            isHandcard = use.m_isHandcard;
        } else {
            CardResponseStruct resp = data.value<CardResponseStruct>();
            cardstar = resp.m_card;
            isHandcard = resp.m_isHandcard;
        }
        if (isHandcard && room->askForSkillInvoke(player, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            QList<int> ids = room->getNCards(1, false);
            CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "yajiao", ""));
            room->moveCardsAtomic(move, true);
            int id = ids.first();

            const Card *card = Sanguosha->getCard(id);
            if (card->getTypeId() == cardstar->getTypeId()) {
                player->setMark("yajiao", id); // For AI
                ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                    QString("@yajiao-give:::%1:%2\\%3").arg(card->objectName())
                    .arg(card->getSuitString() + "_char")
                    .arg(card->getNumberString()),
                    true);
                if (target) {
                    //CardMoveReason reason(CardMoveReason::S_REASON_DRAW, target->objectName(), "yajiao", "");
                    //room->obtainCard(target, card, reason);
                    room->obtainCard(target, id, true);
					return false;
                }
            } else {
                if (room->askForChoice(player, objectName(), "throw+cancel", QVariant::fromValue(card)) == "throw") {
                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "yajiao", "");
                    room->throwCard(card, reason, nullptr);
					return false;
                }
            }
			if (room->getCardPlace(id) == Player::PlaceTable)
				room->returnToTopDrawPile(ids);
        }
        return false;
    }
};

class Tieji : public TriggerSkill
{
public:
    Tieji() : TriggerSkill("tieji")
    {
        events << TargetSpecified << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified && TriggerSkill::triggerable(player)) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash"))
                return false;
            QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
            int index = 0;
            QList<ServerPlayer *> tos;
            foreach (ServerPlayer *p, use.to) {
                if (!player->isAlive()) break;
                if (player->askForSkillInvoke(this, QVariant::fromValue(p))) {
                    room->broadcastSkillInvoke(objectName());
                    if (!tos.contains(p)) {
                        p->addMark("tieji");
                        room->addPlayerMark(p, "@skill_invalidity");
                        tos << p;

                        foreach(ServerPlayer *pl, room->getAllPlayers())
                            room->filterCards(pl, pl->getCards("he"), true);
                        JsonArray args;
                        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
                    }

                    JudgeStruct judge;
                    judge.pattern = ".";
                    judge.good = true;
                    judge.reason = objectName();
                    judge.who = player;
                    judge.play_animation = false;

                    room->judge(judge);

                    if ((p->isAlive() && !p->canDiscard(p, "he"))
                        || !room->askForCard(p, ".|" + judge.pattern, "@tieji-discard:::" + judge.pattern, data, Card::MethodDiscard)) {
                        LogMessage log;
                        log.type = "#NoJink";
                        log.from = p;
                        room->sendLog(log);
                        jink_list.replace(index, QVariant(0));
                    }
                }
                index++;
            }
            player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
            return false;
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName()) {
                judge->pattern = judge->card->getSuitString();
            }
        }
        return false;
    }
};

class TiejiClear : public TriggerSkill
{
public:
    TiejiClear() : TriggerSkill("#tieji-clear")
    {
        events << EventPhaseChanging << Death;
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
            if (death.who != target || target != room->getCurrent())
                return false;
        }
        QList<ServerPlayer *> players = room->getAllPlayers(true);
        foreach (ServerPlayer *player, players) {
            if (player->getMark("tieji") == 0) continue;
            room->removePlayerMark(player, "@skill_invalidity", player->getMark("tieji"));
            player->setMark("tieji", 0);

            foreach(ServerPlayer *p, room->getAllPlayers())
                room->filterCards(p, p->getCards("he"), false);
            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        }
        return false;
    }
};

class Guanxing : public PhaseChangeSkill
{
public:
    Guanxing() : PhaseChangeSkill("guanxing")
    {
        frequency = Frequent;
    }

    int getPriority(TriggerEvent) const
    {
        return 1;
    }

    bool onPhaseChange(ServerPlayer *zhuge, Room *room) const
    {
        if (zhuge->getPhase() == Player::Start && zhuge->askForSkillInvoke(this)) {
            int index = qrand() % 2 + 1;
            if (objectName() == "guanxing" && !zhuge->hasInnateSkill(this) && zhuge->hasSkill("zhiji"))
                index += 2;
            room->broadcastSkillInvoke(objectName(), index);
            QList<int> guanxing = room->getNCards(getGuanxingNum(room));
            LogMessage log;
            log.type = "$ViewDrawPile";
            log.from = zhuge;
            log.card_str = ListI2S(guanxing).join("+");
            room->sendLog(log, zhuge);
            room->askForGuanxing(zhuge, guanxing);
        }
        return false;
    }

    int getGuanxingNum(Room *room) const
    {
        if (objectName() == "super_guanxing")
            return 5;
        return qMin(5, room->alivePlayerCount());
    }
};

class Kongcheng : public ProhibitSkill
{
public:
    Kongcheng() : ProhibitSkill("kongcheng")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return (card->isKindOf("Slash") || card->isKindOf("Duel")) && to->isKongcheng() && to->hasSkill(objectName());
    }
};

class KongchengEffect : public TriggerSkill
{
public:
    KongchengEffect() :TriggerSkill("#kongcheng-effect")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->isKongcheng()) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.from_places.contains(Player::PlaceHand)) {
                int index = qrand() % 2 + 1;
                if (player->getGeneralName().startsWith("tenyear_") || (!player->getGeneralName().startsWith("tenyear_")
                   && player->getGeneral2() && player->getGeneral2Name().startsWith("tenyear_")))
                    index += 2;
                room->broadcastSkillInvoke("kongcheng", index);
            }
        }

        return false;
    }
};

class Jizhi : public TriggerSkill
{
public:
    Jizhi() : TriggerSkill("jizhi")
    {
        frequency = Frequent;
        events << CardUsed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *yueying, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();

        if (use.card->getTypeId() == Card::TypeTrick
            && (yueying->getMark("JilveEvent") > 0 || room->askForSkillInvoke(yueying, objectName()))) {
            if (yueying->getMark("JilveEvent") > 0)
                room->broadcastSkillInvoke("jilve", 5);
            else
                room->broadcastSkillInvoke(objectName());

            QList<int> ids = room->getNCards(1, false);
            CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, yueying->objectName(), "jizhi", ""));
            room->moveCardsAtomic(move, true);

            const Card *card = Sanguosha->getCard(ids.first());
            if (card->isKindOf("BasicCard")) {
                const Card *card_ex = nullptr;
                if (!yueying->isKongcheng())
                    card_ex = room->askForCard(yueying, ".", "@jizhi-exchange:::" + card->objectName(),
                    QVariant::fromValue(card), Card::MethodNone);
                if (card_ex) {
                    CardMoveReason reason1(CardMoveReason::S_REASON_PUT, yueying->objectName(), "jizhi", "");
                    CardMoveReason reason2(CardMoveReason::S_REASON_DRAW, yueying->objectName(), "jizhi", "");
                    CardsMoveStruct move1(card_ex->getEffectiveId(), yueying, nullptr, Player::PlaceUnknown, Player::DrawPile, reason1);
                    CardsMoveStruct move2(ids, yueying, yueying, Player::PlaceUnknown, Player::PlaceHand, reason2);

                    QList<CardsMoveStruct> moves;
                    moves.append(move1);
                    moves.append(move2);
                    room->moveCardsAtomic(moves, false);
                } else {
                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, yueying->objectName(), "jizhi", "");
                    room->throwCard(card, reason, nullptr);
                }
            } else {
                CardMoveReason reason(CardMoveReason::S_REASON_DRAW, yueying->objectName(), "jizhi", "");
                room->obtainCard(yueying, card, reason);
            }
        }

        return false;
    }
};

class Qicai : public TargetModSkill
{
public:
    Qicai() : TargetModSkill("qicai")
    {
        pattern = "TrickCard";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill(objectName()))
            return 1000;
        return 0;
    }
};

class QicaiLimit : public CardLimitSkill
{
public:
    QicaiLimit() : CardLimitSkill("#qicai-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "discard";//设置为限制弃置
    }

    QString limitPattern(const Player *target, const Card *card) const
    {
		if(card->isKindOf("Horse")) return "";
		foreach (const Player *p, target->getAliveSiblings()) {//获取其他角色
			if (p->getEquipsId().contains(card->getId())//这张牌在他的装备区
				&&p->hasSkill("qicai"))//且这个角色拥有奇才
				return card->toString();//则这张牌不能被target弃置
		}
		return "";
    }
};

class Zhuhai : public TriggerSkill
{
public:
    Zhuhai() : TriggerSkill("zhuhai")
    {
        events << EventPhaseStart << PreCardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("damage_point_round") > 0;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish) {
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName())) continue;
                if (p != player && p->canSlash(player, false)) {
                    p->setFlags("ZhuhaiSlash");
                    QString prompt = QString("@zhuhai-slash:%1:%2").arg(p->objectName()).arg(player->objectName());
                    if (!room->askForUseSlashTo(p, player, prompt, false))
                        p->setFlags("-ZhuhaiSlash");
                }
            }
        } else if (triggerEvent == PreCardUsed && player->hasFlag("ZhuhaiSlash")) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());

            player->setFlags("-ZhuhaiSlash");
        }
        return false;
    }
};

class Qianxin : public TriggerSkill
{
public:
    Qianxin() : TriggerSkill("qianxin")
    {
        events << Damage;
        frequency = Wake;
        waked_skills = "jianyan";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->isWounded()) {
            LogMessage log;
            log.type = "#QianxinWake";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
        }else if(!player->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        //room->doLightbox("$QianxinAnimate");

        room->doSuperLightbox(player, "qianxin");

        room->setPlayerMark(player, "qianxin", 1);
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->acquireSkill(player, "jianyan");

        return false;
    }
};

class Jianyan : public ZeroCardViewAsSkill
{
public:
    Jianyan() : ZeroCardViewAsSkill("jianyan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JianyanCard");
    }

    const Card *viewAs() const
    {
        return new JianyanCard;
    }
};

class Zhiheng : public ViewAsSkill
{
public:
    Zhiheng() : ViewAsSkill("zhiheng")
    {
		response_pattern = "@@zhiheng";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (ServerInfo.GameMode == "02_1v1" && ServerInfo.GameRuleMode != "Classical" && selected.length() >= 2) return false;
        return !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        ZhihengCard *zhiheng_card = new ZhihengCard;
        zhiheng_card->addSubcards(cards);
        zhiheng_card->setSkillName(objectName());
        return zhiheng_card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("ZhihengCard");
    }
};

class Jiuyuan : public TriggerSkill
{
public:
    Jiuyuan() : TriggerSkill("jiuyuan$")
    {
        events << TargetConfirmed << PreHpRecover;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->hasLordSkill("jiuyuan");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *sunquan, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Peach") && use.from && use.from->getKingdom() == "wu"
                && sunquan != use.from && sunquan->hasFlag("Global_Dying")) {
                room->setCardFlag(use.card, "jiuyuan");
            }
        } else if (triggerEvent == PreHpRecover) {
            RecoverStruct rec = data.value<RecoverStruct>();
            if (rec.card && rec.card->hasFlag("jiuyuan")) {
                if (!sunquan->isLord() && sunquan->hasSkill("weidi"))
                    room->broadcastSkillInvoke("weidi");
                else
                    room->broadcastSkillInvoke("jiuyuan", rec.who->isMale() ? 1 : 2);

                LogMessage log;
                log.type = "#JiuyuanExtraRecover";
                log.from = sunquan;
                log.to << rec.who;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(sunquan, "jiuyuan");

                rec.recover++;
                data.setValue(rec);
            }
        }

        return false;
    }
};

class Yingzi : public DrawCardsSkill
{
public:
    Yingzi() : DrawCardsSkill("yingzi")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *zhouyu, int n) const
    {
        Room *room = zhouyu->getRoom();
        int index = qrand() % 2 + 1;
        if (zhouyu->isJieGeneral("sunce"))
            index += 6;
        else {
            if (!zhouyu->hasInnateSkill(this)) {
                if (zhouyu->hasSkill("xiongyisy",true))
                    index = 9;
                else if (zhouyu->hasSkill("qizhou",true))
                    index = 10;
                else if (zhouyu->hasSkill("hunzi",true))
                    index += 4;
                else if (zhouyu->hasSkill("mouduan",true))
                    index += 2;
            }
        }
        room->broadcastSkillInvoke(objectName(), index);
        room->sendCompulsoryTriggerLog(zhouyu, objectName());
        return n + 1;
    }
};

class YingziMaxCards : public MaxCardsSkill
{
public:
    YingziMaxCards() : MaxCardsSkill("#yingzi")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasSkill("yingzi"))
            return target->getMaxHp();
        return -1;
    }
};

class Fanjian : public OneCardViewAsSkill
{
public:
    Fanjian() : OneCardViewAsSkill("fanjian")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && !player->hasUsed("FanjianCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FanjianCard *card = new FanjianCard;
        card->addSubcard(originalCard);
        card->setSkillName(objectName());
        return card;
    }
};

class Keji : public TriggerSkill
{
public:
    Keji() : TriggerSkill("keji")
    {
        events << PreCardUsed << CardResponded << EventPhaseChanging;
        frequency = Frequent;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lvmeng, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::Discard) {
                if (lvmeng->getMark("KejiSlashInPlayPhase-Clear")<1&&lvmeng->isAlive()
				&&lvmeng->hasSkill(objectName())&&lvmeng->askForSkillInvoke(this)) {
                    if (lvmeng->getHandcardNum() > lvmeng->getMaxCards()) {
                        int index = qrand() % 2 + 1;
                        if (!lvmeng->hasInnateSkill(this) && lvmeng->hasSkill("mouduan"))
                            index += 4;
                        else if (Player::isNostalGeneral(lvmeng, "lvmeng"))
                            index += 2;
                        room->broadcastSkillInvoke(objectName(), index);
                    }
                    lvmeng->skip(Player::Discard);
                }
            }
        } else if (lvmeng->getPhase() == Player::Play) {
            const Card *card = nullptr;
            if (triggerEvent == PreCardUsed)
                card = data.value<CardUseStruct>().card;
            else
                card = data.value<CardResponseStruct>().m_card;
            if (card && card->isKindOf("Slash"))
                lvmeng->addMark("KejiSlashInPlayPhase-Clear");
        }
        return false;
    }
};

class Qinxue : public PhaseChangeSkill
{
public:
    Qinxue() : PhaseChangeSkill("qinxue")
    {
        frequency = Wake;
        waked_skills = "gongxin";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *lvmeng, Room *room) const
    {
        int n = lvmeng->getHandcardNum() - lvmeng->getHp();
        int wake_lim = (Sanguosha->getPlayerCount(room->getMode()) >= 7) ? 2 : 3;
        if (n >= wake_lim){
			LogMessage log;
			log.type = "#QinxueWake";
			log.from = lvmeng;
			log.arg = QString::number(n);
			log.arg2 = "qinxue";
			room->sendLog(log);
		}else if(!lvmeng->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(lvmeng, objectName());
        //room->doLightbox("$QinxueAnimate");
        room->doSuperLightbox(lvmeng, "qinxue");

        room->setPlayerMark(lvmeng, "qinxue", 1);
        if (room->changeMaxHpForAwakenSkill(lvmeng, -1, objectName()))
            room->acquireSkill(lvmeng, "gongxin");

        return false;
    }
};

class Qixi : public OneCardViewAsSkill
{
public:
    Qixi() : OneCardViewAsSkill("qixi")
    {
        filter_pattern = ".|black";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Dismantlement *dismantlement = new Dismantlement(originalCard->getSuit(), originalCard->getNumber());
        dismantlement->addSubcard(originalCard->getId());
        dismantlement->setSkillName(objectName());
        return dismantlement;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (Player::isNostalGeneral(player, "ganning"))
            index += 2;
        return index;
    }
};

class FenweiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    FenweiViewAsSkill() :ZeroCardViewAsSkill("fenwei")
    {
        response_pattern = "@@fenwei";
    }

    const Card *viewAs() const
    {
        return new FenweiCard;
    }
};

class Fenwei : public TriggerSkill
{
public:
    Fenwei() : TriggerSkill("fenwei")
    {
        events << TargetSpecifying;
        view_as_skill = new FenweiViewAsSkill;
        frequency = Limited;
        limit_mark = "@fenwei";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        ServerPlayer *ganning = room->findPlayerBySkillName(objectName());
        if (!ganning || ganning->getMark("@fenwei") <= 0) return false;

        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.length() <= 1 || !use.card->isNDTrick())
            return false;

        QStringList target_list;
        foreach(ServerPlayer *p, use.to)
            target_list << p->objectName();
        room->setPlayerProperty(ganning, "fenwei_targets", target_list.join("+"));
        ganning->setTag("fenwei", data);
        room->askForUseCard(ganning, "@@fenwei", "@fenwei-card");
        data = ganning->getTag("fenwei");

        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->getGeneralName().contains("heqi") || (!player->getGeneralName().contains("ganning") && player->getGeneral2Name().contains("heqi")))
            index ++;
        return index;
    }
};

class Kurou : public OneCardViewAsSkill
{
public:
    Kurou() : OneCardViewAsSkill("kurou")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("KurouCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        KurouCard *card = new KurouCard;
        card->addSubcard(originalCard);
        card->setSkillName(objectName());
        return card;
    }
};

class Zhaxiang : public TriggerSkill
{
public:
    Zhaxiang() : TriggerSkill("zhaxiang")
    {
        events << HpLost << EventPhaseChanging;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseChanging)
            return 8;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == HpLost && TriggerSkill::triggerable(player)) {
            int lose = data.value<HpLostStruct>().lose;

            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());

            for (int i = 0; i < lose; i++) {
                player->drawCards(3, objectName());
                if (player->getPhase() == Player::Play)
                    room->addPlayerMark(player, objectName());
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive || change.to == Player::RoundStart)
                room->setPlayerMark(player, objectName(), 0);
        }
        return false;
    }
};

class ZhaxiangRedSlash : public TriggerSkill
{
public:
    ZhaxiangRedSlash() : TriggerSkill("#zhaxiang")
    {
        events << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive() && target->getMark("zhaxiang") > 0;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") || !use.card->isRed())
            return false;
        QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
        int index = 0;
        foreach (ServerPlayer *p, use.to) {
            LogMessage log;
            log.type = "#NoJink";
            log.from = p;
            room->sendLog(log);
            jink_list.replace(index, QVariant(0));
            index++;
        }
        player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
        return false;
    }
};

class ZhaxiangTargetMod : public TargetModSkill
{
public:
    ZhaxiangTargetMod() : TargetModSkill("#zhaxiang-target")
    {
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        return from->getMark("zhaxiang");
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->isRed() && from->getMark("zhaxiang") > 0)
            return 1000;
        return 0;
    }
};

class GuoseViewAsSkill : public OneCardViewAsSkill
{
public:
    GuoseViewAsSkill() : OneCardViewAsSkill("guose")
    {
        filter_pattern = ".|diamond";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("GuoseCard") && !(player->isNude() && player->getHandPile().isEmpty());
    }

    const Card *viewAs(const Card *originalCard) const
    {
        GuoseCard *card = new GuoseCard;
        card->addSubcard(originalCard);
        card->setSkillName(objectName());
        return card;
    }
};

class Guose : public TriggerSkill
{
public:
    Guose() : TriggerSkill("guose")
    {
        events << CardFinished;
        view_as_skill = new GuoseViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getSkillNames().contains(objectName()))
            player->drawCards(1, objectName());
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Indulgence") ? 1 : 2;
    }
};

class LiuliViewAsSkill : public OneCardViewAsSkill
{
public:
    LiuliViewAsSkill() : OneCardViewAsSkill("liuli")
    {
        filter_pattern = ".!";
        response_pattern = "@@liuli";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        LiuliCard *liuli_card = new LiuliCard;
        liuli_card->addSubcard(originalCard);
        return liuli_card;
    }
};

class Liuli : public TriggerSkill
{
public:
    Liuli() : TriggerSkill("liuli")
    {
        events << TargetConfirming;
        view_as_skill = new LiuliViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *daqiao, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();

        if (use.card->isKindOf("Slash") && use.to.contains(daqiao) && daqiao->canDiscard(daqiao, "he")) {
            QList<ServerPlayer *> players = room->getOtherPlayers(daqiao);
            players.removeOne(use.from);

            bool can_invoke = false;
            foreach (ServerPlayer *p, players) {
                if (use.from->canSlash(p, use.card, false) && daqiao->inMyAttackRange(p)) {
                    can_invoke = true;
                    break;
                }
            }

            if (can_invoke) {
                QString prompt = "@liuli:" + use.from->objectName();
                room->setPlayerFlag(use.from, "LiuliSlashSource");
                // a temp nasty trick
                daqiao->setTag("liuli-card", QVariant::fromValue(use.card)); // for the server (AI)
                room->setPlayerProperty(daqiao, "liuli", use.card->toString()); // for the client (UI)
                if (room->askForUseCard(daqiao, "@@liuli", prompt, -1, Card::MethodDiscard)) {
                    daqiao->removeTag("liuli-card");
                    room->setPlayerProperty(daqiao, "liuli", "");
                    room->setPlayerFlag(use.from, "-LiuliSlashSource");
                    foreach (ServerPlayer *p, players) {
                        if (p->hasFlag("LiuliTarget")) {
                            p->setFlags("-LiuliTarget");
                            if (!use.from->canSlash(p, false))
                                return false;
                            use.to.removeOne(daqiao);
                            use.to.append(p);
                            room->sortByActionOrder(use.to);
                            data.setValue(use);
                            room->getThread()->trigger(TargetConfirming, room, p, data);
                            return false;
                        }
                    }
                } else {
                    daqiao->removeTag("liuli-card");
                    room->setPlayerProperty(daqiao, "liuli", "");
                    room->setPlayerFlag(use.from, "-LiuliSlashSource");
                }
            }
        }

        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (!player->hasInnateSkill(this) && player->hasSkills("luoyan|olluoyan"))
            index += 4;
        else if (Player::isNostalGeneral(player, "daqiao"))
            index += 2;

        return index;
    }
};

class Qianxun : public TriggerSkill
{
public:
    Qianxun() : TriggerSkill("qianxun")
    {
        events << CardOnEffect << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardOnEffect && TriggerSkill::triggerable(player) && !player->isKongcheng()) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.multiple && effect.card->getTypeId() == Card::TypeTrick
				&& effect.from != player && room->askForSkillInvoke(player, objectName(), data)) {
                room->broadcastSkillInvoke(objectName());
                player->setTag("QianxunEffectData", data);

                CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), "");
                QList<int> handcards = player->handCards();
                QList<ServerPlayer *> open;
                open << player;
                player->addToPile("qianxun", handcards, false, open, reason);
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (p->getPile("qianxun").length() > 0) {
                        DummyCard *dummy = new DummyCard(p->getPile("qianxun"));
                        CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, p->objectName(), "qianxun", "");
                        room->obtainCard(p, dummy, reason, false);
                        delete dummy;
                    }
                }
            }
        }
        return false;
    }
};

class LianyingViewAsSkill : public ZeroCardViewAsSkill
{
public:
    LianyingViewAsSkill() : ZeroCardViewAsSkill("lianying")
    {
        response_pattern = "@@lianying";
    }

    const Card *viewAs() const
    {
        return new LianyingCard;
    }
};

class Lianying : public TriggerSkill
{
public:
    Lianying() : TriggerSkill("lianying")
    {
        events << CardsMoveOneTime;
        view_as_skill = new LianyingViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *luxun, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == luxun && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
            luxun->setTag("LianyingMoveData", data);
            int count = 0;
            for (int i = 0; i < move.from_places.length(); i++) {
                if (move.from_places[i] == Player::PlaceHand) count++;
            }
            room->setPlayerMark(luxun, "lianying", count);
            room->askForUseCard(luxun, "@@lianying", "@lianying-card:::" + QString::number(count));
        }
        return false;
    }
};

class Jieyin : public ViewAsSkill
{
public:
    Jieyin() : ViewAsSkill("jieyin")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getHandcardNum() >= 2 && !player->hasUsed("JieyinCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() > 1 || Self->isJilei(to_select))
            return false;

        return !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        JieyinCard *jieyin_card = new JieyinCard();
        jieyin_card->addSubcards(cards);
        return jieyin_card;
    }
};

class Xiaoji : public TriggerSkill
{
public:
    Xiaoji() : TriggerSkill("xiaoji")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *sunshangxiang, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == sunshangxiang && move.from_places.contains(Player::PlaceEquip)) {
            for (int i = 0; i < move.card_ids.size(); i++) {
                if (!sunshangxiang->isAlive())
                    return false;
                if (move.from_places[i] == Player::PlaceEquip) {
                    if (room->askForSkillInvoke(sunshangxiang, objectName())) {
                        int index = qrand() % 2 + 1;
                        if (!sunshangxiang->hasInnateSkill(this) && sunshangxiang->getMark("fanxiang") > 0)
                            index += 2;
                        if (sunshangxiang->getGeneralName().startsWith("tenyear_") || (!sunshangxiang->getGeneralName().startsWith("tenyear_")
                           && sunshangxiang->getGeneral2() && sunshangxiang->getGeneral2Name().startsWith("tenyear_")))
                            index = qrand() % 2 + 5;
                        room->broadcastSkillInvoke(objectName(), index);

                        sunshangxiang->drawCards(2, objectName());
                    } else {
                        break;
                    }
                }
            }
        }
        return false;
    }
};

class Wushuang : public TriggerSkill
{
public:
    Wushuang() : TriggerSkill("wushuang")
    {
        events << TargetSpecified << CardEffected << CardResponded;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Duel")) {
				QStringList wushuang_tag;
                if (player->hasSkill(objectName())) {
					int n = qrand()%2+1;
					if(player->getGeneralName().startsWith("nos_")||player->getGeneral2Name().startsWith("nos_"))
						n++;
					room->sendCompulsoryTriggerLog(player, this, n);
                    foreach(ServerPlayer *to, use.to)
                        wushuang_tag << to->objectName();
                }
				foreach(ServerPlayer *to, use.to){
					if(to->hasSkill(objectName())){
						int n = qrand()%2+1;
						if(to->getGeneralName().startsWith("nos_")||to->getGeneral2Name().startsWith("nos_"))
							n++;
						room->sendCompulsoryTriggerLog(to, this, n);
						wushuang_tag << player->objectName();
					}
				}
				room->setTag("Wushuang_"+use.card->toString(), wushuang_tag);
            }else if(use.card->isKindOf("Slash")&&player->hasSkill(objectName())) {
                int n = qrand()%2+1;
				if(player->getGeneralName().startsWith("nos_")||player->getGeneral2Name().startsWith("nos_"))
					n++;
				room->sendCompulsoryTriggerLog(player, this, n);
				QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
                for (int i = 0; i < use.to.length(); i++) {
                    if (jink_list.at(i).toInt() == 1)
                        jink_list.replace(i, QVariant(2));
                }
                player->setTag("Jink_" + use.card->toString(), jink_list);
			}
        }else if (triggerEvent == CardEffected) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("Duel")){
				QStringList wushuang_tag = room->getTag("Wushuang_"+effect.card->toString()).toStringList();
				if(wushuang_tag.contains(effect.to->objectName())||wushuang_tag.contains(effect.from->objectName()))
					room->setTag("wushuangData",data);
			}
        } else if (triggerEvent == CardResponded) {
            CardResponseStruct resp = data.value<CardResponseStruct>();
			if(resp.m_toCard&&resp.m_toCard->isKindOf("Duel")&&!player->hasFlag("wushuangSlash")){
				QStringList wushuang_tag = room->getTag("Wushuang_"+resp.m_toCard->toString()).toStringList();
				if(wushuang_tag.contains(player->objectName())){
					room->setPlayerFlag(player,"wushuangSlash");
					if(!room->askForCard(player,"slash","duel-slash:"+resp.m_who->objectName(),room->getTag("wushuangData"),
						Card::MethodResponse,resp.m_who,false,"",false,resp.m_toCard)){
						resp.nullified = true;
						data.setValue(resp);
					}
					room->setPlayerFlag(player,"-wushuangSlash");
				}
			}
		}
        return false;
    }
};

class Liyu : public TriggerSkill
{
public:
    Liyu() : TriggerSkill("liyu")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isAlive() && player != damage.to && !damage.to->hasFlag("Global_DebutFlag") && !damage.to->isNude()
            && damage.card && damage.card->isKindOf("Slash")) {
            Duel *duel = new Duel(Card::NoSuit, 0);
            duel->setSkillName("_liyu");

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p != damage.to && player->canUse(duel, p))
                    targets << p;
            }
            ServerPlayer *target = room->askForPlayerChosen(damage.to, targets, objectName(), "@liyu:" + player->objectName(), true);
			if (target) {
				room->broadcastSkillInvoke(objectName());

				LogMessage log;
				log.type = "#InvokeOthersSkill";
				log.from = damage.to;
				log.to << player;
				log.arg = objectName();
				room->sendLog(log);
				room->notifySkillInvoked(player, objectName());

				int id = room->askForCardChosen(player, damage.to, "he", objectName());
				room->obtainCard(player, id);
				if (player->isAlive() && target->isAlive())
					room->useCard(CardUseStruct(duel, player, target));
            }
			delete duel;
        }
        return false;
    }
};

class Lijian : public OneCardViewAsSkill
{
public:
    Lijian() : OneCardViewAsSkill("lijian")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he") && !player->hasUsed("LijianCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        LijianCard *lijian_card = new LijianCard;
        lijian_card->addSubcard(originalCard);
        return lijian_card;
    }

    /*int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Duel") ? 0 : -1;
    }*/
};

class Biyue : public PhaseChangeSkill
{
public:
    Biyue() : PhaseChangeSkill("biyue")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *diaochan, Room *room) const
    {
        if (diaochan->getPhase() == Player::Finish) {
            if (room->askForSkillInvoke(diaochan, objectName())) {
                room->broadcastSkillInvoke(objectName());
                diaochan->drawCards(1, objectName());
            }
        }

        return false;
    }
};

class Chuli : public OneCardViewAsSkill
{
public:
    Chuli() : OneCardViewAsSkill("chuli")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("ChuliCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ChuliCard *chuli_card = new ChuliCard;
        chuli_card->addSubcard(originalCard->getId());
        return chuli_card;
    }
};

class Jijiu : public OneCardViewAsSkill
{
public:
    Jijiu() : OneCardViewAsSkill("jijiu")
    {
        filter_pattern = ".|red";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasFlag("CurrentPlayer");
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern.contains("peach") && player->getMark("Global_PreventPeach")<1
            && !player->hasFlag("CurrentPlayer");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
        peach->setSkillName(objectName());
        peach->addSubcard(originalCard);
        return peach;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (Player::isNostalGeneral(player, "huatuo"))
            index += 2;
        return index;
    }
};

class Mashu : public DistanceSkill
{
public:
    Mashu() : DistanceSkill("mashu")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasSkill(objectName()))
            return -1;
        return 0;
    }
};

class Xunxun : public PhaseChangeSkill
{
public:
    Xunxun() : PhaseChangeSkill("xunxun")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *lidian, Room *room) const
    {
        if (lidian->getPhase() == Player::Draw) {
            if (room->askForSkillInvoke(lidian, objectName())) {
                int index = qrand() % 2 + 1;
                if (lidian->getGeneralName().contains("tangzi") || (!lidian->getGeneralName().contains("lidian") && lidian->getGeneral2Name().contains("tangzi")))
                    index += 2;
                room->broadcastSkillInvoke(objectName(), index);
                QList<ServerPlayer *> p_list;
                p_list << lidian;
                QList<int> obtained,card_ids = room->getNCards(4);
                room->fillAG(card_ids, lidian);
                int id = room->askForAG(lidian, card_ids, false, objectName());
                card_ids.removeOne(id);
                obtained << id;
                room->takeAG(lidian, id, false, p_list);
                id = room->askForAG(lidian, card_ids, false, objectName());
                card_ids.removeOne(id);
                obtained << id;
                room->clearAG(lidian);

                room->askForGuanxing(lidian, card_ids, Room::GuanxingDownOnly);
                DummyCard *dummy = new DummyCard(obtained);
                lidian->obtainCard(dummy, false);
                delete dummy;

                return true;
            }
        }
        return false;
    }
};

class Wangxi : public TriggerSkill
{
public:
    Wangxi() : TriggerSkill("wangxi")
    {
        events << Damage << Damaged;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = nullptr;
        if (triggerEvent == Damage && !damage.to->hasFlag("Global_DebutFlag"))
            target = damage.to;
        else if (triggerEvent == Damaged)
            target = damage.from;
        if (!target || target == player) return false;
        QList<ServerPlayer *> players;
        players << player << target;
        room->sortByActionOrder(players);

        for (int i = 1; i <= damage.damage; i++) {
            if (!target->isAlive() || !player->isAlive())
                return false;
            if (room->askForSkillInvoke(player, objectName(), QVariant::fromValue(target))) {
                room->broadcastSkillInvoke(objectName(), (triggerEvent == Damaged) ? 1 : 2);
                room->drawCards(players, 1, objectName());
            } else
                break;
        }
        return false;
    }
};

class Wangzun : public PhaseChangeSkill
{
public:
    Wangzun() : PhaseChangeSkill("wangzun")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isLord() && target->getPhase() == Player::Start;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (!isNormalGameMode(room->getMode()))
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            if (!p->askForSkillInvoke(this, target)) continue;
            room->broadcastSkillInvoke(objectName());
            p->drawCards(1, objectName());
            room->addMaxCards(target, -1);
        }
        return false;
    }
};

class Tongji : public ProhibitSkill
{
public:
    Tongji() : ProhibitSkill("tongji")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (card->isKindOf("Slash")) {
            // get rangefix
            int rangefix = 0;
            if (card->isVirtualCard()) {
                QList<int> subcards = card->getSubcards();
				const Card *c = from->getWeapon();
                if (c && subcards.contains(c->getId())) {
                    const Weapon *weapon = qobject_cast<const Weapon *>(c->getRealCard());
                    rangefix += weapon->getRange(from) - from->getAttackRange(false);
                }
				c = from->getOffensiveHorse();
                if (c && subcards.contains(c->getId())) {
                    const Horse *horse = qobject_cast<const Horse *>(c->getRealCard());
                    rangefix -= horse->getCorrect(from);
                }
            }
            // find yuanshu
            foreach (const Player *p, from->getAliveSiblings()) {
                if (p!=to&&p->getHandcardNum()>p->getHp()&&p->hasSkill(objectName())&&from->inMyAttackRange(p,rangefix))
                    return true;
            }
        }
        return false;
    }
};

class Yaowu : public TriggerSkill
{
public:
    Yaowu() : TriggerSkill("yaowu")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash") && damage.card->isRed()
            && damage.from && damage.from->isAlive()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(damage.to, objectName());

            if (damage.from->isWounded() && room->askForChoice(damage.from, objectName(), "recover+draw", data) == "recover")
                room->recover(damage.from, RecoverStruct(objectName(), damage.to));
            else
                damage.from->drawCards(1, objectName());
        }
        return false;
    }
};

class Qiaomeng : public TriggerSkill
{
public:
    Qiaomeng() : TriggerSkill("qiaomeng")
    {
        events << Damage << BeforeCardsMove;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage && TriggerSkill::triggerable(player)) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->isAlive() && !damage.to->hasFlag("Global_DebutFlag")
                && damage.card && damage.card->isKindOf("Slash") && damage.card->isBlack()
                && player->canDiscard(damage.to, "e") && room->askForSkillInvoke(player, objectName(), data)) {
                room->broadcastSkillInvoke(objectName());
                int id = room->askForCardChosen(player, damage.to, "e", objectName(), false, Card::MethodDiscard);
                CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), damage.to->objectName(),
                    objectName(), "");
                room->throwCard(Sanguosha->getCard(id), reason, damage.to, player);
            }
        } else if (triggerEvent == BeforeCardsMove) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.reason.m_skillName == objectName() && move.reason.m_playerId == player->objectName()
                && move.card_ids.length() > 0) {
                const Card *card = Sanguosha->getCard(move.card_ids.first());
                if (card->isKindOf("Horse")) {
                    move.card_ids.clear();
                    data.setValue(move);
                    room->obtainCard(player, card);
                }
            }
        }
        return false;
    }
};

class Xiaoxi : public TriggerSkill
{
public:
    Xiaoxi() : TriggerSkill("xiaoxi")
    {
        events << Debut;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *opponent = player->getNext();
        if (!opponent->isAlive())
            return false;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_xiaoxi");
				slash->deleteLater();
        if (player->isLocked(slash) || !player->canSlash(opponent, slash, false)) {
            return false;
        }
        if (room->askForSkillInvoke(player, objectName()))
            room->useCard(CardUseStruct(slash, player, opponent));
        return false;
    }
};




class NosJianxiong : public MasochismSkill
{
public:
    NosJianxiong() : MasochismSkill("nosjianxiong")
    {
    }

    void onDamaged(ServerPlayer *caocao, const DamageStruct &damage) const
    {
        Room *room = caocao->getRoom();
        const Card *card = damage.card;
        if (!card) return;

        QList<int> ids;
        if (card->isVirtualCard())
            ids = card->getSubcards();
        else
            ids << card->getEffectiveId();

        if (ids.isEmpty()) return;
        foreach (int id, ids) {
            if (room->getCardPlace(id) != Player::PlaceTable) return;
        }
        QVariant data = QVariant::fromValue(damage);
        if (room->askForSkillInvoke(caocao, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            caocao->obtainCard(card);
        }
    }
};

class NosFankui : public MasochismSkill
{
public:
    NosFankui() : MasochismSkill("nosfankui")
    {
    }

    void onDamaged(ServerPlayer *simayi, const DamageStruct &damage) const
    {
        ServerPlayer *from = damage.from;
        Room *room = simayi->getRoom();
        QVariant data = QVariant::fromValue(from);
        if (from && !from->isNude() && room->askForSkillInvoke(simayi, "nosfankui", data)) {
            room->broadcastSkillInvoke(objectName());
            int card_id = room->askForCardChosen(simayi, from, "he", "nosfankui");
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, simayi->objectName());
            room->obtainCard(simayi, Sanguosha->getCard(card_id),
                reason, room->getCardPlace(card_id) != Player::PlaceHand);
        }
    }
};

class NosGuicai : public RetrialSkill
{
public:
    NosGuicai() : RetrialSkill("nosguicai")
    {

    }

    const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
    {
        if (player->isKongcheng())
            return nullptr;

        QStringList prompt_list;
        prompt_list << "@nosguicai-card" << judge->who->objectName()
            << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");

        Room *room = player->getRoom();
        const Card *card = room->askForCard(player, ".", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);
        if (card) room->broadcastSkillInvoke(objectName());

        return card;
    }
};

class NosGanglie : public MasochismSkill
{
public:
    NosGanglie() : MasochismSkill("nosganglie")
    {
    }

    void onDamaged(ServerPlayer *xiahou, const DamageStruct &damage) const
    {
        ServerPlayer *from = damage.from;
        Room *room = xiahou->getRoom();
        QVariant data = QVariant::fromValue(damage);

        if (room->askForSkillInvoke(xiahou, "nosganglie", data)) {
            room->broadcastSkillInvoke("nosganglie");

            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = false;
            judge.reason = objectName();
            judge.who = xiahou;

            room->judge(judge);
            if (!from || from->isDead()) return;
            if (judge.isGood()) {
                if (from->getHandcardNum() < 2 || !room->askForDiscard(from, objectName(), 2, 2, true))
                    room->damage(DamageStruct(objectName(), xiahou, from));
            }
        }
    }
};

NosTuxiCard::NosTuxiCard()
{
}

bool NosTuxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() >= 2 || to_select == Self)
        return false;

    return !to_select->isKongcheng();
}

void NosTuxiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.from->isAlive() && !effect.to->isKongcheng()) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "h", "nostuxi");
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName(),effect.to->objectName(),"nostuxi","");
        room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, false);
    }
}

class NosTuxiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    NosTuxiViewAsSkill() : ZeroCardViewAsSkill("nostuxi")
    {
        response_pattern = "@@nostuxi";
    }

    const Card *viewAs() const
    {
        return new NosTuxiCard;
    }
};

class NosTuxi : public PhaseChangeSkill
{
public:
    NosTuxi() : PhaseChangeSkill("nostuxi")
    {
        view_as_skill = new NosTuxiViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *zhangliao, Room *room) const
    {
        if (zhangliao->getPhase() == Player::Draw) {
            foreach (ServerPlayer *player, room->getOtherPlayers(zhangliao)) {
                if (!player->isKongcheng()) {
					if (room->askForUseCard(zhangliao, "@@nostuxi", "@nostuxi-card"))
						return true;
                    break;
                }
            }
        }
        return false;
    }
};

class NosLuoyiBuff : public TriggerSkill
{
public:
    NosLuoyiBuff() : TriggerSkill("#nosluoyi")
    {
        events << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->hasFlag("nosluoyi") && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *xuchu, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.chain || damage.transfer || !damage.by_user) return false;
        const Card *reason = damage.card;
        if (reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))) {
            LogMessage log;
            log.type = "#LuoyiBuff";
            log.from = xuchu;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data.setValue(damage);
        }

        return false;
    }
};

class NosLuoyi : public DrawCardsSkill
{
public:
    NosLuoyi() : DrawCardsSkill("nosluoyi")
    {
    }

    int getDrawNum(ServerPlayer *xuchu, int n) const
    {
        Room *room = xuchu->getRoom();
        if (room->askForSkillInvoke(xuchu, objectName())) {
            room->broadcastSkillInvoke(objectName());
            xuchu->setFlags(objectName());
            return n - 1;
        } else
            return n;
    }
};

NosYiji::NosYiji() : MasochismSkill("nosyiji")
{
    frequency = Frequent;
    n = 2;
}

void NosYiji::onDamaged(ServerPlayer *guojia, const DamageStruct &damage) const
{
    Room *room = guojia->getRoom();
    int x = damage.damage;
    for (int i = 0; i < x; i++) {
        if (!guojia->isAlive() || !room->askForSkillInvoke(guojia, objectName()))
            return;
        room->broadcastSkillInvoke("nosyiji");

        QList<int> yiji_cards = room->getNCards(n);
		guojia->assignmentCards(yiji_cards,objectName());
		if(!yiji_cards.isEmpty()){
			DummyCard *dummy = new DummyCard(yiji_cards);
			guojia->obtainCard(dummy, false);
			delete dummy;
		}

        /*CardsMoveStruct move(yiji_cards, nullptr, guojia, Player::PlaceTable, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_PREVIEW, guojia->objectName(), objectName(), ""));
        QList<CardsMoveStruct> moves;
        moves.append(move);
        QList<ServerPlayer *> _guojia;
        _guojia.append(guojia);
        room->notifyMoveCards(true, moves, false, _guojia);
        room->notifyMoveCards(false, moves, false, _guojia);

        QList<int> origin_yiji = yiji_cards;
        QHash<ServerPlayer *, QStringList> hash;

        while (guojia->isAlive()) {
            CardsMoveStruct yiji_move = room->askForYijiStruct(guojia, origin_yiji, objectName(), true, false, true, -1,
                                        room->getAlivePlayers(), CardMoveReason(), "", false, false);
            if (!yiji_move.to || yiji_move.card_ids.isEmpty()) break;
            QStringList id_strings = hash[(ServerPlayer *)yiji_move.to];
            foreach (int id, yiji_move.card_ids) {
                id_strings << QString::number(id);
                origin_yiji.removeOne(id);
            }
            hash[(ServerPlayer *)yiji_move.to] = id_strings;
            if (origin_yiji.isEmpty()) break;
        }

        CardsMoveStruct move2(yiji_cards, guojia, nullptr, Player::PlaceHand, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_PREVIEW, guojia->objectName(), objectName(), ""));
        moves.clear();
        moves.append(move2);
        room->notifyMoveCards(true, moves, false, _guojia);
        room->notifyMoveCards(false, moves, false, _guojia);

        if (!origin_yiji.isEmpty()) {
            QStringList id_strings = hash[guojia];
            foreach (int id, origin_yiji)
                id_strings << QString::number(id);
            hash[guojia] = id_strings;
        }

        moves.clear();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead()) continue;
            QList<int> ids = ListS2I(hash[p]);
            if (ids.isEmpty()) continue;
            hash.remove(p);
            CardsMoveStruct move(ids, nullptr, p, Player::DrawPile, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_PREVIEWGIVE, guojia->objectName(), p->objectName(), "nosyiji", ""));
            moves.append(move);
        }
        if (moves.isEmpty()) return;
        room->moveCardsAtomic(moves, false);*/
    }
}

NosRendeCard::NosRendeCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool NosRendeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(Self->getAcquiredSkills().contains("nosrende")){
		QString ww = Self->property("manweiwoFrom").toString();
		if(!ww.isEmpty()&&to_select->objectName()!=ww) return false;
	}
    return to_select!=Self&&targets.isEmpty();
}

void NosRendeCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();

    QDateTime dtbefore = source->getTag("nosrende", QDateTime(QDate::currentDate(), QTime(0, 0, 0))).toDateTime();
    QDateTime dtafter = QDateTime::currentDateTime();

    if (dtbefore.secsTo(dtafter) > 3 * Config.AIDelay / 1000)
        room->broadcastSkillInvoke("rende",qrand()%2+1);

    source->setTag("nosrende", QDateTime::currentDateTime());

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "nosrende", "");
    room->obtainCard(target, this, reason, false);

    int old_value = source->getMark("nosrende");
    int new_value = old_value + subcards.length();
    room->setPlayerMark(source, "nosrende", new_value);

    if (old_value < 2 && new_value >= 2)
        room->recover(source, RecoverStruct("nosrende", source));
}

class NosRendeViewAsSkill : public ViewAsSkill
{
public:
    NosRendeViewAsSkill() : ViewAsSkill("nosrende")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (ServerInfo.GameMode == "04_1v3" && selected.length() + Self->getMark("nosrende") >= 2)
            return false;
        else
            return !to_select->isEquipped();
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (ServerInfo.GameMode == "04_1v3" && player->getMark("nosrende") >= 2)
            return false;
        return !player->isKongcheng();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        NosRendeCard *rende_card = new NosRendeCard;
        rende_card->addSubcards(cards);
        return rende_card;
    }
};

class NosRende : public TriggerSkill
{
public:
    NosRende() : TriggerSkill("nosrende")
    {
        events << EventPhaseChanging;
        view_as_skill = new NosRendeViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getMark("nosrende") > 0;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;
        room->setPlayerMark(player, "nosrende", 0);
        return false;
    }
};

class NosTieji : public TriggerSkill
{
public:
    NosTieji() : TriggerSkill("nostieji")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash"))
            return false;
        QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
        int index = 0;
        foreach (ServerPlayer *p, use.to) {
            if (!player->isAlive()) break;
            if (player->askForSkillInvoke(this, QVariant::fromValue(p))) {
                room->broadcastSkillInvoke(objectName());

                p->setFlags("NosTiejiTarget"); // For AI

                JudgeStruct judge;
                judge.pattern = ".|red";
                judge.good = true;
                judge.reason = objectName();
                judge.who = player;

                try {
                    room->judge(judge);
                }
                catch (TriggerEvent triggerEvent) {
                    if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                        p->setFlags("-NosTiejiTarget");
                    throw triggerEvent;
                }

                if (judge.isGood()) {
                    LogMessage log;
                    log.type = "#NoJink";
                    log.from = p;
                    room->sendLog(log);
                    jink_list.replace(index, QVariant(0));
                }

                p->setFlags("-NosTiejiTarget");
            }
            index++;
        }
        player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
        return false;
    }
};

class NosJizhi : public TriggerSkill
{
public:
    NosJizhi() : TriggerSkill("nosjizhi")
    {
        frequency = Frequent;
        events << CardUsed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *yueying, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();

        if (use.card->isNDTrick() && room->askForSkillInvoke(yueying, objectName())) {
            room->broadcastSkillInvoke("jizhi");
            yueying->drawCards(1, objectName());
        }

        return false;
    }
};

class NosQicai : public TargetModSkill
{
public:
    NosQicai() : TargetModSkill("nosqicai")
    {
        pattern = "TrickCard";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill(objectName()))
            return 1000;
        return 0;
    }
};

NosKurouCard::NosKurouCard()
{
    target_fixed = true;
}

void NosKurouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->loseHp(HpLostStruct(source, 1, "noskurou", source));
    if (source->isAlive())
        room->drawCards(source, 2, "noskurou");
}

class NosKurou : public ZeroCardViewAsSkill
{
public:
    NosKurou() : ZeroCardViewAsSkill("noskurou")
    {
    }

    const Card *viewAs() const
    {
        return new NosKurouCard;
    }
};

class NosYingzi : public DrawCardsSkill
{
public:
    NosYingzi() : DrawCardsSkill("nosyingzi")
    {
        frequency = Frequent;
    }

    int getDrawNum(ServerPlayer *zhouyu, int n) const
    {
        Room *room = zhouyu->getRoom();
        if (room->askForSkillInvoke(zhouyu, objectName())) {
            room->broadcastSkillInvoke("nosyingzi");
            return n + 1;
        } else
            return n;
    }
};

NosFanjianCard::NosFanjianCard()
{
}

void NosFanjianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhouyu = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhouyu->getRoom();

    Card::Suit suit = room->askForSuit(target, "nosfanjian");

    LogMessage log;
    log.type = "#ChooseSuit";
    log.from = target;
    log.arg = Card::Suit2String(suit);
    room->sendLog(log);

    int card_id = room->askForCardChosen(target, zhouyu, "h", "nosfanjian");
    const Card *card = Sanguosha->getCard(card_id);
    target->obtainCard(card);
    room->showCard(target, card_id);

    if (card->getSuit() != suit)
        room->damage(DamageStruct("nosfanjian", zhouyu, target));
}

class NosFanjian : public ZeroCardViewAsSkill
{
public:
    NosFanjian() : ZeroCardViewAsSkill("nosfanjian")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && !player->hasUsed("NosFanjianCard");
    }

    const Card *viewAs() const
    {
        return new NosFanjianCard;
    }
};

class NosGuose : public OneCardViewAsSkill
{
public:
    NosGuose() : OneCardViewAsSkill("nosguose")
    {
        filter_pattern = ".|diamond";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Indulgence *indulgence = new Indulgence(originalCard->getSuit(), originalCard->getNumber());
        indulgence->addSubcard(originalCard->getId());
        indulgence->setSkillName(objectName());
        return indulgence;
    }
};

class NosQianxun : public ProhibitSkill
{
public:
    NosQianxun() : ProhibitSkill("nosqianxun")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return to->hasSkill(objectName()) && (card->isKindOf("Snatch") || card->isKindOf("Indulgence"));
    }
};

class NosLianying : public TriggerSkill
{
public:
    NosLianying() : TriggerSkill("noslianying")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *luxun, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == luxun && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
            if (room->askForSkillInvoke(luxun, objectName(), data)) {
                room->broadcastSkillInvoke(objectName());
                luxun->drawCards(1, objectName());
            }
        }
        return false;
    }
};

QingnangCard::QingnangCard()
{
}

bool QingnangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(Self->getAcquiredSkills().contains("qingnang")){
		QString ww = Self->property("manweiwoFrom").toString();
		if(!ww.isEmpty()&&to_select->objectName()!=ww) return false;
	}
    return targets.isEmpty() && to_select->isWounded();
}

bool QingnangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if(Self->getAcquiredSkills().contains("qingnang")){
		QString ww = Self->property("manweiwoFrom").toString();
		if(!ww.isEmpty()&&(targets.isEmpty()||targets.first()->objectName()!=ww)) return false;
	}
    return targets.value(0, Self)->isWounded();
}

void QingnangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->cardEffect(this, source, targets.value(0, source));
}

void QingnangCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->getRoom()->recover(effect.to, RecoverStruct("qingnang", effect.from));
}

class Qingnang : public OneCardViewAsSkill
{
public:
    Qingnang() : OneCardViewAsSkill("qingnang")
    {
        filter_pattern = ".|.|.|hand!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "h") && !player->hasUsed("QingnangCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        QingnangCard *qingnang_card = new QingnangCard;
        qingnang_card->addSubcard(originalCard->getId());
        return qingnang_card;
    }
};

NosLijianCard::NosLijianCard() : LijianCard(false)
{
}

class NosLijian : public OneCardViewAsSkill
{
public:
    NosLijian() : OneCardViewAsSkill("noslijian")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he") && !player->hasUsed("NosLijianCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        NosLijianCard *lijian_card = new NosLijianCard;
        lijian_card->addSubcard(originalCard->getId());
        return lijian_card;
    }/*

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Duel") ? 0 : -1;
    }*/
};

class MobileWangzun : public PhaseChangeSkill
{
public:
    MobileWangzun() : PhaseChangeSkill("mobilewangzun")
    {
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getPhase() == Player::RoundStart;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(objectName()) || player->getHp() <= p->getHp()) continue;
            room->sendCompulsoryTriggerLog(p, objectName(), true, true);
            if (player->isLord()) {
                p->drawCards(2, objectName());
                room->addMaxCards(player, -1);
            } else
                p->drawCards(1, objectName());
        }
        return false;
    }
};

MobileTongjiCard::MobileTongjiCard()
{
    mute = true;
}

bool MobileTongjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    if (to_select->hasFlag("MobileTongjiSlashSource") || to_select == Self) return false;

    const Player *from = nullptr;
    foreach (const Player *p, Self->getAliveSiblings()) {
        if (p->hasFlag("MobileTongjiSlashSource")) {
            from = p;
            break;
        }
    }
    const Card *slash = Card::Parse(Self->property("mobiletongji").toString());
    if (from && !from->canSlash(to_select, slash, false)) return false;
    return to_select->hasSkill("mobiletongji") && Self->inMyAttackRange(to_select, subcards);
}

void MobileTongjiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    QVariant data = QVariant::fromValue(use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, card_use.from, data);
    use = data.value<CardUseStruct>();

    room->broadcastSkillInvoke("mobiletongji");

    LogMessage log;
    log.from = card_use.from;
    log.to << card_use.to;
    log.type = "$MobileTongjiUse";
    log.card_str = ListI2S(subcards).join("+");
    log.arg = "mobiletongji";
    room->sendLog(log);
    room->doAnimate(1, card_use.from->objectName(), card_use.to.first()->objectName());
    room->notifySkillInvoked(card_use.to.first(), "mobiletongji");

    CardMoveReason reason(CardMoveReason::S_REASON_THROW, card_use.from->objectName(), "", "mobiletongji", "");
    room->moveCardTo(this, card_use.from, nullptr, Player::DiscardPile, reason, true);

    thread->trigger(CardUsed, room, card_use.from, data);
    use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, card_use.from, data);
}

void MobileTongjiCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->setFlags("MobileTongjiTarget");
}

class MobileTongjiVS : public OneCardViewAsSkill
{
public:
    MobileTongjiVS() : OneCardViewAsSkill("mobiletongji")
    {
        filter_pattern = ".";
        response_pattern = "@@mobiletongji";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileTongjiCard *c = new MobileTongjiCard;
        c->addSubcard(originalCard);
        return c;
    }
};

class MobileTongji : public TriggerSkill
{
public:
    MobileTongji() : TriggerSkill("mobiletongji")
    {
        events << TargetConfirming;
        view_as_skill = new MobileTongjiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        if (!use.to.contains(player) || !player->canDiscard(player, "he")) return false;

        bool yuanshu = false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName()) || p == use.from) continue;
            if (!player->inMyAttackRange(p) || !use.from->canSlash(p, use.card, false)) continue;
            yuanshu = true;
            break;
        }
        if (!yuanshu) return false;

        QString prompt = "@mobiletongji:" + use.from->objectName();
        room->setPlayerFlag(use.from, "MobileTongjiSlashSource");
        player->setTag("mobiletongji-card", QVariant::fromValue(use.card)); // for the server (AI)
        room->setPlayerProperty(player, "mobiletongji", use.card->toString()); // for the client (UI)

        if (room->askForUseCard(player, "@@mobiletongji", prompt, -1, Card::MethodDiscard)) {
            player->removeTag("mobiletongji-card");
            room->setPlayerProperty(player, "mobiletongji", "");
            room->setPlayerFlag(use.from, "-MobileTongjiSlashSource");
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasFlag("MobileTongjiTarget")) {
                    p->setFlags("-MobileTongjiTarget");
                    if (!use.from->canSlash(p, false))
                        return false;
                    use.to.removeOne(player);
                    use.to.append(p);
                    room->sortByActionOrder(use.to);
                    data = QVariant::fromValue(use);
                    room->getThread()->trigger(TargetConfirming, room, p, data);
                    return false;
                }
            }
        } else {
            player->removeTag("mobiletongji-card");
            room->setPlayerProperty(player, "mobiletongji", "");
            room->setPlayerFlag(use.from, "-MobileTongjiSlashSource");
        }
        return false;
    }
};




void StandardPackage::addGenerals()
{
    // Wei
    General *nos_caocao = new General(this, "nos_caocao$", "wei");
    nos_caocao->addSkill(new NosJianxiong);
    nos_caocao->addSkill("hujia");
	
    General *nos_simayi = new General(this, "nos_simayi", "wei", 3);
    nos_simayi->addSkill(new NosFankui);
    nos_simayi->addSkill(new NosGuicai);
	
    General *nos_xiahoudun = new General(this, "nos_xiahoudun", "wei");
    nos_xiahoudun->addSkill(new NosGanglie);
	
    General *nos_zhangliao = new General(this, "nos_zhangliao", "wei");
    nos_zhangliao->addSkill(new NosTuxi);
	
    General *nos_xuchu = new General(this, "nos_xuchu", "wei");
    nos_xuchu->addSkill(new NosLuoyi);
    nos_xuchu->addSkill(new NosLuoyiBuff);
    related_skills.insertMulti("nosluoyi", "#nosluoyi");
	
    General *nos_guojia = new General(this, "nos_guojia", "wei", 3);
    nos_guojia->addSkill("tiandu");
    nos_guojia->addSkill(new NosYiji);
	
    General *zhenji = new General(this, "zhenji", "wei", 3, false); // WEI 007
    zhenji->addSkill(new Qingguo);
    zhenji->addSkill(new Luoshen);

    // Shu
    General *nos_liubei = new General(this, "nos_liubei$", "shu");
    nos_liubei->addSkill(new NosRende);
    nos_liubei->addSkill("jijiang");

    General *nos_guanyu = new General(this, "nos_guanyu", "shu");
    nos_guanyu->addSkill("wusheng");

    General *nos_zhangfei = new General(this, "nos_zhangfei", "shu");
    nos_zhangfei->addSkill("paoxiao");

    General *zhugeliang = new General(this, "zhugeliang", "shu", 3); // SHU 004
    zhugeliang->addSkill(new Guanxing);
    zhugeliang->addSkill(new Kongcheng);
    zhugeliang->addSkill(new KongchengEffect);
    related_skills.insertMulti("kongcheng", "#kongcheng-effect");

    General *nos_zhaoyun = new General(this, "nos_zhaoyun", "shu");
    nos_zhaoyun->addSkill("longdan");

    General *nos_machao = new General(this, "nos_machao", "shu");
    nos_machao->addSkill("mashu");
    nos_machao->addSkill(new NosTieji);

    General *nos_huangyueying = new General(this, "nos_huangyueying", "shu", 3, false);
    nos_huangyueying->addSkill(new NosJizhi);
    nos_huangyueying->addSkill(new NosQicai);

    // Wu
    General *sunquan = new General(this, "sunquan$", "wu"); // WU 001
    sunquan->addSkill(new Zhiheng);
    sunquan->addSkill(new Jiuyuan);

    General *nos_ganning = new General(this, "nos_ganning", "wu");
    nos_ganning->addSkill("qixi");

    General *nos_lvmeng = new General(this, "nos_lvmeng", "wu");
    nos_lvmeng->addSkill("keji");

    General *nos_huanggai = new General(this, "nos_huanggai", "wu");
    nos_huanggai->addSkill(new NosKurou);

    General *nos_zhouyu = new General(this, "nos_zhouyu", "wu", 3);
    nos_zhouyu->addSkill(new NosYingzi);
    nos_zhouyu->addSkill(new NosFanjian);

    General *nos_daqiao = new General(this, "nos_daqiao", "wu", 3, false);
    nos_daqiao->addSkill(new NosGuose);
    nos_daqiao->addSkill("liuli");

    General *nos_luxun = new General(this, "nos_luxun", "wu", 3);
    nos_luxun->addSkill(new NosQianxun);
    nos_luxun->addSkill(new NosLianying);

    General *sunshangxiang = new General(this, "sunshangxiang", "wu", 3, false); // WU 008
    sunshangxiang->addSkill(new Jieyin);
    sunshangxiang->addSkill(new Xiaoji);

    // Qun
    General *nos_huatuo = new General(this, "nos_huatuo", "qun", 3);
    nos_huatuo->addSkill(new Qingnang);
    nos_huatuo->addSkill("jijiu");

    General *nos_lvbu = new General(this, "nos_lvbu", "qun");
    nos_lvbu->addSkill("wushuang");

    General *nos_diaochan = new General(this, "nos_diaochan", "qun", 3, false);
    nos_diaochan->addSkill(new NosLijian);
    nos_diaochan->addSkill("biyue");

    // for skill cards
    addMetaObject<ZhihengCard>();
    addMetaObject<JieyinCard>();
    addMetaObject<NosTuxiCard>();
    addMetaObject<NosRendeCard>();
    addMetaObject<NosKurouCard>();
    addMetaObject<NosFanjianCard>();
    addMetaObject<NosLijianCard>();
    addMetaObject<QingnangCard>();
}

StrengthenPackage::StrengthenPackage()
    : Package("strengthen")
{
    General *caocao = new General(this, "caocao$*standard", "wei"); // WEI 001
    caocao->addSkill(new Jianxiong);
    caocao->addSkill(new Hujia);

    General *simayi = new General(this, "simayi*standard", "wei", 3); // WEI 002
    simayi->addSkill(new Fankui);
    simayi->addSkill(new Guicai);

    General *xiahoudun = new General(this, "xiahoudun*standard", "wei"); // WEI 003
    xiahoudun->addSkill(new Ganglie);
    xiahoudun->addSkill(new Qingjian);

    General *zhangliao = new General(this, "zhangliao*standard", "wei"); // WEI 004
    zhangliao->addSkill(new Tuxi);

    General *xuchu = new General(this, "xuchu*standard", "wei"); // WEI 005
    xuchu->addSkill(new Luoyi);
    xuchu->addSkill(new LuoyiBuff);
    related_skills.insertMulti("luoyi", "#luoyi");

    General *guojia = new General(this, "guojia*standard", "wei", 3); // WEI 006
    guojia->addSkill(new Tiandu);
    guojia->addSkill(new Yiji);
    guojia->addSkill(new YijiObtain);
    related_skills.insertMulti("yiji", "#yiji");
	
    General *lidian = new General(this, "lidian*standard", "wei", 3); // WEI 017
    lidian->addSkill(new Xunxun);
    lidian->addSkill(new Wangxi);

    General *liubei = new General(this, "liubei$*standard", "shu"); // SHU 001
    liubei->addSkill(new Rende);
    liubei->addSkill(new Jijiang);

    General *guanyu = new General(this, "guanyu*standard", "shu"); // SHU 002
    guanyu->addSkill(new Wusheng);
    guanyu->addSkill(new Yijue);

    General *zhangfei = new General(this, "zhangfei*standard", "shu"); // SHU 003
    zhangfei->addSkill(new Paoxiao);
    zhangfei->addSkill(new Tishen);


    General *zhaoyun = new General(this, "zhaoyun*standard", "shu"); // SHU 005
    zhaoyun->addSkill(new Longdan);
    zhaoyun->addSkill(new Yajiao);

    General *machao = new General(this, "machao*standard", "shu"); // SHU 006
    machao->addSkill(new Mashu);
    machao->addSkill(new Tieji);
    machao->addSkill(new TiejiClear);
    related_skills.insertMulti("tieji", "#tieji-clear");

    General *huangyueying = new General(this, "huangyueying*standard", "shu", 3, false); // SHU 007
    huangyueying->addSkill(new Jizhi);
    huangyueying->addSkill(new Qicai);
    huangyueying->addSkill(new QicaiLimit);
    related_skills.insertMulti("qicai", "#qicai-limit");

    General *st_xushu = new General(this, "st_xushu*standard", "shu"); // SHU 017
    st_xushu->addSkill(new Zhuhai);
    st_xushu->addSkill(new Qianxin);
    st_xushu->addRelateSkill("jianyan");


    General *ganning = new General(this, "ganning*standard", "wu"); // WU 002
    ganning->addSkill(new Qixi);
    ganning->addSkill(new Fenwei);

    General *lvmeng = new General(this, "lvmeng*standard", "wu"); // WU 003
    lvmeng->addSkill(new Keji);
    lvmeng->addSkill(new Qinxue);

    General *huanggai = new General(this, "huanggai*standard", "wu"); // WU 004
    huanggai->addSkill(new Kurou);
    huanggai->addSkill(new Zhaxiang);
    huanggai->addSkill(new ZhaxiangRedSlash);
    huanggai->addSkill(new ZhaxiangTargetMod);
    related_skills.insertMulti("zhaxiang", "#zhaxiang");
    related_skills.insertMulti("zhaxiang", "#zhaxiang-target");

    General *zhouyu = new General(this, "zhouyu*standard", "wu", 3); // WU 005
    zhouyu->addSkill(new Yingzi);
    zhouyu->addSkill(new YingziMaxCards);
    zhouyu->addSkill(new Fanjian);
    related_skills.insertMulti("yingzi", "#yingzi");

    General *daqiao = new General(this, "daqiao*standard", "wu", 3, false); // WU 006
    daqiao->addSkill(new Guose);
    daqiao->addSkill(new Liuli);

    General *luxun = new General(this, "luxun*standard", "wu", 3); // WU 007
    luxun->addSkill(new Qianxun);
    luxun->addSkill(new Lianying);


    General *huatuo = new General(this, "huatuo*standard", "qun", 3); // QUN 001
    huatuo->addSkill(new Chuli);
    huatuo->addSkill(new Jijiu);

    General *lvbu = new General(this, "lvbu*standard", "qun", 5); // QUN 002
    lvbu->addSkill(new Wushuang);
    lvbu->addSkill(new Liyu);

    General *diaochan = new General(this, "diaochan*standard", "qun", 3, false); // QUN 003
    diaochan->addSkill(new Lijian);
    diaochan->addSkill(new Biyue);

    General *st_huaxiong = new General(this, "st_huaxiong*standard", "qun", 6); // QUN 019
    st_huaxiong->addSkill(new Yaowu);

    General *st_yuanshu = new General(this, "st_yuanshu*standard", "qun"); // QUN 021
    st_yuanshu->addSkill(new Wangzun);
    st_yuanshu->addSkill(new Tongji);

    General *mobile_yuanshu = new General(this, "mobile_yuanshu", "qun");
    mobile_yuanshu->addSkill(new MobileWangzun);
    mobile_yuanshu->addSkill(new MobileTongji);
    addMetaObject<MobileTongjiCard>();

    General *st_gongsunzan = new General(this, "st_gongsunzan*standard", "qun"); // QUN 026
    st_gongsunzan->addSkill(new Qiaomeng);
    st_gongsunzan->addSkill("yicong");

    addMetaObject<RendeCard>();
    addMetaObject<YijueCard>();
    addMetaObject<TuxiCard>();
    addMetaObject<KurouCard>();
    addMetaObject<LijianCard>();
    addMetaObject<FanjianCard>();
    addMetaObject<LiuliCard>();
    addMetaObject<LianyingCard>();
    addMetaObject<JijiangCard>();
    addMetaObject<YijiCard>();
    addMetaObject<FenweiCard>();
    addMetaObject<ChuliCard>();
    addMetaObject<JianyanCard>();
    addMetaObject<GuoseCard>();
    skills << new Xiaoxi << new NonCompulsoryInvalidity << new Jianyan;
}
ADD_PACKAGE(Strengthen)

class SuperZhiheng : public Zhiheng
{
public:
    SuperZhiheng() :Zhiheng()
    {
        setObjectName("super_zhiheng");
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && player->usedTimes("ZhihengCard") < (player->getLostHp() + 1);
    }
};

class SuperGuanxing : public Guanxing
{
public:
    SuperGuanxing() : Guanxing()
    {
        setObjectName("super_guanxing");
    }
};

class SuperMaxCards : public MaxCardsSkill
{
public:
    SuperMaxCards() : MaxCardsSkill("super_max_cards")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill(objectName()))
            return target->getMark("@max_cards_test");
        return 0;
    }
};

class SuperOffensiveDistance : public DistanceSkill
{
public:
    SuperOffensiveDistance() : DistanceSkill("super_offensive_distance")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        int n = from->getMark("@offensive_distance_test");
		if (n>0&&from->hasSkill(objectName()))
            return -n;
        return 0;
    }
};

class SuperDefensiveDistance : public DistanceSkill
{
public:
    SuperDefensiveDistance() : DistanceSkill("super_defensive_distance")
    {
    }

    int getCorrect(const Player *, const Player *to) const
    {
        int n = to->getMark("@defensive_distance_test");
		if (n>0&&to->hasSkill(objectName()))
            return n;
        return 0;
    }
};

class SuperYongsi : public Yongsi
{
public:
    SuperYongsi() : Yongsi()
    {
        setObjectName("super_yongsi");
    }

    int getKingdoms(ServerPlayer *yuanshu) const
    {
        return yuanshu->getMark("@yongsi_test");
    }
};

class SuperJushou : public Jushou
{
public:
    SuperJushou() : Jushou()
    {
        setObjectName("super_jushou");
    }

    int getJushouDrawNum(ServerPlayer *caoren) const
    {
        return caoren->getMark("@jushou_test");
    }
};

class GdJuejing : public TriggerSkill
{
public:
    GdJuejing() : TriggerSkill("gdjuejing")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gaodayihao, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != gaodayihao && move.to != gaodayihao)
                return false;
            if (move.to_place != Player::PlaceHand && !move.from_places.contains(Player::PlaceHand))
                return false;
        }
        if (gaodayihao->getHandcardNum() == 4)
            return false;
        int diff = abs(gaodayihao->getHandcardNum() - 4);
        if (gaodayihao->getHandcardNum() < 4) {
            room->sendCompulsoryTriggerLog(gaodayihao, objectName());
            gaodayihao->drawCards(diff, objectName());
        } else if (gaodayihao->getHandcardNum() > 4) {
            room->sendCompulsoryTriggerLog(gaodayihao, objectName());
            room->askForDiscard(gaodayihao, objectName(), diff, diff);
        }

        return false;
    }
};

class GdJuejingSkipDraw : public DrawCardsSkill
{
public:
    GdJuejingSkipDraw() : DrawCardsSkill("#gdjuejing")
    {
    }

    int getPriority(TriggerEvent) const
    {
        return 1;
    }

    int getDrawNum(ServerPlayer *gaodayihao, int) const
    {
        LogMessage log;
        log.type = "#GdJuejing";
        log.from = gaodayihao;
        log.arg = "gdjuejing";
        gaodayihao->getRoom()->sendLog(log);

        return 0;
    }
};

class GdLonghun : public Longhun
{
public:
    GdLonghun() : Longhun()
    {
        setObjectName("gdlonghun");
    }

    int getEffHp(const Player *) const
    {
        return 1;
    }
};

class GdLonghunDuojian : public TriggerSkill
{
public:
    GdLonghunDuojian() : TriggerSkill("#gdlonghun-duojian")
    {
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *gaodayihao, QVariant &) const
    {
        if (gaodayihao->getPhase() == Player::Start) {
            foreach (ServerPlayer *p, room->getOtherPlayers(gaodayihao)) {
                if (p->getWeapon() && p->getWeapon()->isKindOf("QinggangSword")) {
                    if (room->askForSkillInvoke(gaodayihao, "gdlonghun")) {
                        room->broadcastSkillInvoke("gdlonghun", 5);
                        gaodayihao->obtainCard(p->getWeapon());
                    }
                    break;
                }
            }
        }

        return false;
    }
};

class Gepi : public TriggerSkill
{
public:
    Gepi() : TriggerSkill("gepi")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Start) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p == player || !TriggerSkill::triggerable(p) || !player->canDiscard(p, "he"))
                    continue;

                if (p->askForSkillInvoke(objectName(), QVariant::fromValue(player))) {
                    int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                    room->throwCard(id, p, p == player ? nullptr : player);
                    
                    QList<const Skill *> skills = player->getVisibleSkillList();
                    QList<const Skill *> skills_canselect;
                    foreach (const Skill *s, skills) {
                        if (!s->isLordSkill() && s->getFrequency() != Skill::Wake && !s->inherits("SPConvertSkill") && !s->isAttachedLordSkill())
                            skills_canselect << s;
                    }
                    if (!skills_canselect.isEmpty()) {
                        QStringList l;
                        foreach (const Skill *s, skills_canselect)
                            l << s->objectName();

                        QString skill_lose = room->askForChoice(p, objectName(), l.join("+"));

                        Q_ASSERT(player->hasSkill(skill_lose, true));

                        LogMessage log;
                        log.type = "$GepiNullify";
                        log.from = p;
                        log.to << player;
                        log.arg = skill_lose;
                        room->sendLog(log);

                        room->setPlayerMark(player, "gepi_" + skill_lose, 1);
                        QStringList gepi_list = player->getTag("gepi").toStringList();
                        gepi_list << skill_lose;
                        player->setTag("gepi", gepi_list);

                        foreach (ServerPlayer *ap, room->getAllPlayers())
                            room->filterCards(ap, ap->getCards("he"), true);

                        JsonArray args;
                        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
                    }

                    player->drawCards(3, objectName());
                }
            }
        }
        return false;
    }
};

class GepiReset : public TriggerSkill
{
public:
    GepiReset() : TriggerSkill("#gepi")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *target, QVariant &) const
    {
        if (target->getPhase() == Player::NotActive) {
            foreach (ServerPlayer *player, room->getAllPlayers()) {
                QStringList gepi_list = player->getTag("gepi").toStringList();
                if (gepi_list.isEmpty()) continue;
                foreach (QString skill_name, gepi_list) {
                    room->setPlayerMark(player, "gepi_" + skill_name, 0);
                    if (player->hasSkill(skill_name)) {
                        LogMessage log;
                        log.type = "$GepiReset";
                        log.from = player;
                        log.arg = skill_name;
                        room->sendLog(log);
                    }
                }
                player->removeTag("gepi");
                foreach (ServerPlayer *p, room->getAllPlayers())
                    room->filterCards(p, p->getCards("he"), true);

                JsonArray args;
                args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
            }
        }
        return false;
    }
};

class GepiInv : public InvaliditySkill
{
public:
    GepiInv() : InvaliditySkill("#gepi-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("gepi_" + skill->objectName())<1;
    }
};

TestPackage::TestPackage()
    : Package("~test")
{
    // for test only
    General *zhiba_sunquan = new General(this, "zhiba_sunquan$", "wu", 4, true, true);
    zhiba_sunquan->addSkill(new SuperZhiheng);
    zhiba_sunquan->addSkill("jiuyuan");

    General *wuxing_zhuge = new General(this, "wuxing_zhugeliang", "shu", 3, true, true);
    wuxing_zhuge->addSkill(new SuperGuanxing);
    wuxing_zhuge->addSkill("kongcheng");

    General *gaodayihao = new General(this, "gaodayihao", "god", 1, true, true);
    gaodayihao->addSkill(new GdJuejing);
    gaodayihao->addSkill(new GdJuejingSkipDraw);
    gaodayihao->addSkill(new GdLonghun);
    gaodayihao->addSkill(new GdLonghunDuojian);
    related_skills.insertMulti("gdjuejing", "#gdjuejing");
    related_skills.insertMulti("gdlonghun", "#gdlonghun-duojian");

    General *super_yuanshu = new General(this, "super_yuanshu", "qun", 4, true, true);
    super_yuanshu->addSkill(new SuperYongsi);
    super_yuanshu->addSkill(new MarkAssignSkill("@yongsi_test", 4));
    related_skills.insertMulti("super_yongsi", "#@yongsi_test-4");
    super_yuanshu->addSkill("weidi");

    General *super_caoren = new General(this, "super_caoren", "wei", 4, true, true);
    super_caoren->addSkill(new SuperJushou);
    super_caoren->addSkill(new MarkAssignSkill("@jushou_test", 5));
    related_skills.insertMulti("super_jushou", "#@jushou_test-5");

    General *nobenghuai_dongzhuo = new General(this, "nobenghuai_dongzhuo$", "qun", 4, true, true);
    nobenghuai_dongzhuo->addSkill("jiuchi");
    nobenghuai_dongzhuo->addSkill("roulin");
    nobenghuai_dongzhuo->addSkill("baonue");

    new General(this, "sujiang", "god", 5, true, true);
    new General(this, "sujiangf", "god", 5, false, true);

    new General(this, "anjiang", "god", 4, true, true, true);

    skills << new SuperMaxCards << new SuperOffensiveDistance << new SuperDefensiveDistance;
    skills << new Gepi << new GepiReset << new GepiInv;
    related_skills.insertMulti("gepi", "#gepi");
    related_skills.insertMulti("gepi", "#gepi-inv");
}
ADD_PACKAGE(Test)
