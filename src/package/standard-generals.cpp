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
#if !defined(QSAN_ENGINE_BUILD)
#include "clientstruct.h"
#endif
//#include "util.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "skill-instance-utils.h"

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

    int index = qsanRandomBounded(2) + 1;
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
		dummy->deleteLater();
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
    if (Config.EnableHegemony) {
        if (to_select == Self || targets.size() >= 3 || !Self->canDiscard(to_select, "he"))
            return false;
        if (!to_select->hasShownOneGeneral())
            return true;
        for (const Player *other : targets)
            if (to_select->isFriendWith(other))
                return false;
        return true;
    }

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
        int r = 1 + qsanRandomBounded(2);
        if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
            r += 2;
        else if (liubei->isJieGeneral())
            r = qsanRandomBounded(2) + 5;
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
        dummy->deleteLater();
    }
}

class Jianxiong : public TriggerSkillV2
{
public:
    Jianxiong() : TriggerSkillV2("jianxiong")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList choices;
        choices << "draw" << "cancel";
        if (canObtain(room, ctx.original_data->value<DamageStruct>().card))
            choices.prepend("obtain");
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
        return ctx.choice != "cancel";
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *caocao = ctx.owner;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = caocao;
        log.arg = objectName();
        room->sendLog(log);

        room->broadcastSkillInvoke(objectName(),qsanRandomBounded(2)+1,caocao);
        room->notifySkillInvoked(caocao, objectName());
        const Card *card = ctx.original_data->value<DamageStruct>().card;
        if (ctx.choice == "obtain") {
            // An earlier instance may already have taken the damage card.
            if (canObtain(room, card))
                caocao->obtainCard(card);
        } else
            caocao->drawCards(1, objectName());
        return false;
    }

private:
    static bool canObtain(Room *room, const Card *card)
    {
        return card && card->getEffectiveId() > -1 && !room->getCardOwner(card->getEffectiveId());
    }
};

Hujia::Hujia(const QString &hujia) : TriggerSkillV2(hujia + "$"), hujia(hujia)
{
    events << CardAsked;
}

TriggerList Hujia::triggerable(TriggerEvent, Room *room, ServerPlayer *caocao, QVariant &data) const
{
    const QStringList patterns = data.toStringList();
    return caocao && caocao->isAlive() && caocao->hasLordSkill(this)
        && patterns.first() == "jink" && !patterns.at(1).contains("hujia-jink")
        && !room->getLieges("wei", caocao).isEmpty()
        ? TriggerList{{caocao, {objectName()}}} : TriggerList();
}

bool Hujia::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
}

bool Hujia::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *caocao = ctx.owner;
    QList<ServerPlayer *> lieges = room->getLieges("wei", caocao);
    if (!caocao->isLord() && caocao->hasSkill("weidi"))
        room->broadcastSkillInvoke("weidi");
    else {
        int index = qsanRandomBounded(2) + 1;
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

class TuxiViewAsSkill : public ViewAsSkillV2
{
public:
    TuxiViewAsSkill() : ViewAsSkillV2("tuxi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@tuxi";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new TuxiCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "TuxiCard"; }
};

class Tuxi : public TriggerSkillV2
{
public:
    Tuxi() : TriggerSkillV2("tuxi")
    {
        events << DrawNCards;
        view_as_skill = new TuxiViewAsSkill;
    }

    int getPriority(TriggerEvent) const override
    {
        return 1;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *zhangliao, QVariant &data) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        return zhangliao && zhangliao->isAlive() && zhangliao->hasSkill(objectName())
            && draw.reason == "draw_phase" && targetCount(room, zhangliao, draw.num) > 0
            ? TriggerList{{zhangliao, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhangliao = ctx.owner;
        const int num = targetCount(room, zhangliao, ctx.original_data->value<DrawStruct>().num);
        if (num <= 0) return false;
        zhangliao->setMark("TuxiTarget",0);
        room->setPlayerMark(zhangliao, "tuxi", num);
        // The accepted TuxiCard owns target selection and extraction.
        return room->askForUseCard(zhangliao, "@@tuxi", "@tuxi-card:::" + QString::number(num));
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num -= ctx.owner->getMark("TuxiTarget");
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }

private:
    static int targetCount(Room *room, ServerPlayer *zhangliao, int n)
    {
        int num = 0;
        foreach(ServerPlayer *p, room->getOtherPlayers(zhangliao)){
            if (p->getHandcardNum() >= zhangliao->getHandcardNum())
                num++;
        }
        return qMin(num, n);
    }
};

class Tiandu : public TriggerSkillV2
{
public:
    Tiandu() : TriggerSkillV2("tiandu")
    {
        frequency = Frequent;
        events << FinishJudge;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        return player && player->isAlive() && player->hasSkill(objectName()) && judge && judge->card
            && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        return judge && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge
            && ctx.owner->askForSkillInvoke(this, QVariant::fromValue(judge->card));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *guojia = ctx.owner;
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        // A previous instance may already have taken this judgment card.
        if (judge && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
            int index = qsanRandomBounded(2) + 1;
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

class YijiViewAsSkill : public ViewAsSkillV2
{
public:
    YijiViewAsSkill() : ViewAsSkillV2("yiji") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@yiji";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new YijiCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "YijiCard"; }
};

class Yiji : public TriggerSkillV2
{
public:
    Yiji() : TriggerSkillV2("yiji")
    {
        events << Damaged;
        view_as_skill = new YijiViewAsSkill;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &) const override
    {
        return target && target->isAlive() && target->hasSkill(objectName())
            ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.owner;
        const int times = ctx.original_data->value<DamageStruct>().damage;
        // One damage event owns the continuation choices; a declined point ends it.
        for (int i = 0; i < times; i++) {
            if (!target->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(target, objectName(), *ctx.original_data)) break;
            room->broadcastSkillInvoke(objectName());
            target->drawCards(2, objectName());
            room->askForUseCard(target, "@@yiji", "@yiji");
        }
        return false;
    }
};

// Cards given by Yiji return to their holder even after the giver lost the skill.
class YijiObtain : public TriggerSkillV2
{
public:
    YijiObtain() : TriggerSkillV2("#yiji")
    {
        events << EventPhaseStart;
    }

    int getPriority(TriggerEvent) const override
    {
        return 4;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *target, QVariant &) const override
    {
        if (target && target->getPhase() == Player::Draw && !target->getPile("yiji").isEmpty()) {
            DummyCard *dummy = new DummyCard(target->getPile("yiji"));
            CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName(), "yiji", "");
            room->obtainCard(target, dummy, reason, false);
            dummy->deleteLater();
        }
        return true;
    }
};

class Ganglie : public TriggerSkillV2
{
public:
    Ganglie() : TriggerSkillV2("ganglie")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xiahou, QVariant &) const override
    {
        return xiahou && xiahou->isAlive() && xiahou->hasSkill(objectName())
            ? TriggerList{{xiahou, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, "ganglie", *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *xiahou = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        // One damage event owns the continuation choices; a declined point ends it.
        for (int i = 0; i < damage.damage; i++) {
            if (!isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(xiahou, "ganglie", *ctx.original_data)) break;
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
        }
        return false;
    }
};

class Qingjian : public TriggerSkillV2
{
public:
    Qingjian() : TriggerSkillV2("qingjian")
    {
        events << CardsMoveOneTime;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && !receivedIds(room, player, data).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // An earlier instance may already have distributed the received cards.
        QList<int> ids = receivedIds(room, player, *ctx.original_data);
        if (ids.isEmpty())
            return false;
        player->setTag("QingjianCurrentMoveSkill",
            QVariant(ctx.original_data->value<CardsMoveOneTimeStruct>().reason.m_skillName));
        while (room->askForYiji(player, ids, objectName(), false, false, true, -1, QList<ServerPlayer *>(), CardMoveReason(), "@qingjian-distribute", true)) {
            if (player->isDead()) return false;
        }
        return false;
    }

private:
    static QList<int> receivedIds(Room *room, ServerPlayer *player, const QVariant &data)
    {
        QList<int> ids;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!room->getTag("FirstRound").toBool() && player->getPhase() != Player::Draw && move.to == player && move.to_place == Player::PlaceHand) {
            foreach (int id, move.card_ids) {
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    ids << id;
            }
        }
        return ids;
    }
};

class Fankui : public TriggerSkillV2
{
public:
    Fankui() : TriggerSkillV2("fankui")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *simayi, QVariant &data) const override
    {
        ServerPlayer *from = data.value<DamageStruct>().from;
        return simayi && simayi->isAlive() && simayi->hasSkill(objectName()) && from && !from->isNude()
            ? TriggerList{{simayi, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        return from && !from->isNude() && room->askForSkillInvoke(ctx.owner, "fankui", QVariant::fromValue(from));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *simayi = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        // One damage event owns the continuation choices; a declined point ends it.
        for (int i = 0; i < damage.damage; i++) {
            if (!from || from->isNude() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(simayi, "fankui", QVariant::fromValue(from))) break;
            room->broadcastSkillInvoke(objectName());
            int card_id = room->askForCardChosen(simayi, from, "he", "fankui");
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, simayi->objectName());
            room->obtainCard(simayi, Sanguosha->getCard(card_id),
                reason, room->getCardPlace(card_id) != Player::PlaceHand);
        }
        return false;
    }
};

// Jilve calls the legacy trigger entry directly, so callbacks read `player`, not ctx.owner.
class Guicai : public TriggerSkillV2
{
public:
    Guicai() : TriggerSkillV2("guicai")
    {
        events << AskForRetrial;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && !player->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (player->isNude() || !judge)
            return false;

        QStringList prompt_list;
        prompt_list << "@guicai-card" << judge->who->objectName()
            << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");
        bool forced = false;
        if (player->getMark("JilveEvent") == int(AskForRetrial))
            forced = true;

        const Card *card = room->askForCard(player, forced ? "..!" : "..", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);
        if (forced && card == nullptr) {
            QList<const Card *> c = player->getCards("he");
            card = c.at(qsanRandomBounded(c.length()));
        }
        if (!card)
            return false;
        ctx.extra_data = QVariant::fromValue(card);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = ctx.extra_data.value<const Card *>();
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!card || !judge)
            return false;
        if (player->hasInnateSkill("guicai") || !player->hasSkill("jilve"))
            room->broadcastSkillInvoke(objectName());
        else
            room->broadcastSkillInvoke("jilve", 1);
        room->retrial(card, player, judge, objectName(), false);
        return false;
    }
};

class LuoyiBuff : public TriggerSkillV2
{
public:
    LuoyiBuff() : TriggerSkillV2("#luoyi")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xuchu, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        const Card *reason = damage.card;
        return xuchu && xuchu->isAlive() && xuchu->hasSkill(objectName()) && xuchu->getMark("&luoyi") > 0
            && !damage.chain && !damage.transfer && reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))
            ? TriggerList{{xuchu, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#LuoyiBuff";
        log.from = ctx.owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);

        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class Luoyi : public TriggerSkillV2
{
public:
    Luoyi() : TriggerSkillV2("luoyi")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    int getPriority(TriggerEvent triggerEvent) const override
    {
        if (triggerEvent == EventPhaseStart)
            return 4;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        // The buff expires even if Xu Chu lost the skill during the round.
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart && player->getMark("&luoyi") > 0)
            room->setPlayerMark(player, "&luoyi", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != EventPhaseChanging) return {};
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && change.to == Player::Draw && !player->isSkipped(Player::Draw)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return !ctx.owner->isSkipped(Player::Draw) && room->askForSkillInvoke(ctx.owner, objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
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
            dummy->deleteLater();
        }
        if (!card_to_gotback.isEmpty()) {
            DummyCard *dummy = new DummyCard(card_to_gotback);
            room->obtainCard(player, dummy);
            dummy->deleteLater();
        }
        return false;
    }
};

class Luoshen : public TriggerSkillV2
{
public:
    Luoshen() : TriggerSkillV2("luoshen")
    {
        events << EventPhaseStart << FinishJudge;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *zhenji, QVariant &data) const override
    {
        if (!zhenji || !zhenji->isAlive() || !zhenji->hasSkill(objectName())) return {};
        const bool matches = triggerEvent == EventPhaseStart ? zhenji->getPhase() == Player::Start
            : data.value<JudgeStruct *>()->reason == objectName();
        return matches ? TriggerList{{zhenji, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent triggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // The judgment follow-up belongs to an invocation already accepted.
        return triggerEvent == FinishJudge || ctx.owner->askForSkillInvoke("luoshen");
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhenji = ctx.owner;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == EventPhaseStart) {
            bool canRetrial = zhenji->hasSkills("guicai|nosguicai|guidao|huanshi");
            room->broadcastSkillInvoke(objectName());
            bool first = true;
            while (first || zhenji->askForSkillInvoke("luoshen")) {
                first = false;

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
                dummy->deleteLater();
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
                        dummy->deleteLater();
                    }
                }
            }
        }

        return false;
    }
};

class Qingguo : public ViewAsSkillV2
{
public:
    Qingguo() : ViewAsSkillV2("qingguo", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "jink"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card) || card->hasFlag("using")) return false;
        // OneCardViewAsSkill also admitted response-accessible hand piles.
        QStringList places{"hand"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        return Sanguosha->matchExpPattern(".|black|.|" + places.join(","), request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Jink"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
        jink->addSubcard(originalCard->getId());
        jink->setSkillName(objectName());
        return jink;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int n = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().startsWith("tenyear_") || (!player->getGeneralName().startsWith("tenyear_") && player->getGeneral2() &&
                player->getGeneral2Name().startsWith("tenyear_")))
            n += 2;
        return n;
    }
};

class RendeViewAsSkill : public ViewAsSkillV2 {
public:
    RendeViewAsSkill() : ViewAsSkillV2("rende") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->isKongcheng()
            && ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                    && !request.initiator->hasUsed("RendeCard")
                    && (ServerInfo.GameMode != "04_1v3" || request.initiator->getMark("rende") < 2))
                || (request.pattern == "@@rende"
                    && request.reason != CardUseStruct::CARD_USE_REASON_PLAY));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.initiator && !card->isEquipped()
            && (ServerInfo.GameMode != "04_1v3"
                || request.selectedCardIds.size() + request.initiator->getMark("rende-PlayClear") < 2)
            && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.selectedCardIds.contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && selected.isEmpty() && target && target->isAlive() && target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "RendeCard"; }

    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        // Preserve the chosen count if an interceptor explicitly waives payment.
        ctx.extra_data = request.selectedCardIds.size();
        return true;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.owner || !ctx.invoker || ctx.targets.size() != 1 || !cardSelectionFeasible(request)) return false;
        ServerPlayer *target = ctx.targets.first();
        if (!target || !target->isAlive() || target == ctx.invoker) return false;
        for (int id : request.selectedCardIds)
            if (!ctx.invoker->handCards().contains(id)) return false;
        // Giving is the cost; do not let the generic proxy discard these cards.
        DummyCard gift(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(),
                              target->objectName(), objectName(), QString());
        room->obtainCard(target, &gift, reason, false);
        ctx.extra_data = request.selectedCardIds.size();
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner) return FinishSkill;
        const int oldCount = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "given", 0).toInt();
        const int newCount = oldCount + ctx.extra_data.toInt();
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "given", newCount);
        // Keep the legacy AI's aggregate hint; authority is the selected instance.
        Room *room = owner->getRoom();
        room->setPlayerMark(owner, "rende-PlayClear", newCount);
        room->addPlayerMark(owner, objectName(), ctx.extra_data.toInt());
        if (oldCount < 2 && newCount >= 2 && owner->isAlive()) {
            // Standard Rende always heals; the basic-card reward belongs to tenyearrende.
            room->recover(owner, RecoverStruct("rende", owner));
        }
        if (owner->isAlive() && !owner->isKongcheng()
            && (room->getMode() != "04_1v3" || owner->getMark("rende") < 2))
            room->askForUseCard(owner, "@@rende", "@rende-give", -1, Card::MethodNone, false);
        return ContinueEffects;
    }
};

class Rende : public TriggerSkillV2 {
public:
    Rende() : TriggerSkillV2("rende")
    {
        events << EventPhaseChanging;
        view_as_skill = new RendeViewAsSkill;
    }
    void record(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || ctx.owner != player || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().from != Player::Play) return;
        player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "given");
        room->setPlayerMark(player, objectName(), 0);
        room->setPlayerMark(player, "rende-PlayClear", 0);
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

// Qinwang calls the legacy trigger entry directly, so callbacks read `player`, not ctx.owner.
class Jijiang : public TriggerSkillV2
{
public:
    Jijiang() : TriggerSkillV2("jijiang$")
    {
        events << CardAsked;
        view_as_skill = new JijiangViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *liubei, QVariant &data) const override
    {
        return liubei && liubei->hasLordSkill("jijiang") && canAsk(room, liubei, data)
            ? TriggerList{{liubei, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *liubei, SkillContext &ctx) const override
    {
        return canAsk(room, liubei, *ctx.original_data)
            && (liubei->hasFlag("qinwangjijiang") || room->askForSkillInvoke(liubei, objectName(), *ctx.original_data));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *liubei, SkillContext &) const override
    {
        QList<ServerPlayer *> lieges = room->getLieges("shu", liubei);
        if (!liubei->isLord() && liubei->hasSkill("weidi"))
            room->broadcastSkillInvoke("weidi");
        else {
            int r = 1 + qsanRandomBounded(2);
            if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
                r += 2;
            else if (liubei->isJieGeneral())
                r = qsanRandomBounded(2) + 5;
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

private:
    static bool canAsk(Room *room, ServerPlayer *liubei, const QVariant &data)
    {
        QStringList patterns = data.toStringList();
        return patterns.first() == "slash" && patterns.at(2) == "response" && !patterns.at(1).contains("jijiang-slash")
            && !room->getLieges("shu", liubei).isEmpty();
    }
};

class Wusheng : public ViewAsSkillV2 {
public:
    Wusheng() : ViewAsSkillV2("wusheng", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator)
            : (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !request.initiator
            || !card->isRed()) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        return slash.isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(original->getSuit(), original->getNumber());
        slash->addSubcard(original);
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (Player::isNostalGeneral(player, "guanyu"))
            index += 2;
        else if (player->getGeneralName() == "jsp_guanyu" || (player->getGeneralName() != "guanyu" && player->getGeneral2Name() == "jsp_guanyu"))
            index += 4;
        else if (player->getGeneralName().contains("guansuo") || (player->getGeneralName() != "guanyu" && player->getGeneral2Name().contains("guansuo")))
            index = 7;
        return index;
    }
};

class YijueViewAsSkill : public ViewAsSkillV2
{
public:
    YijueViewAsSkill() : ViewAsSkillV2("yijue") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("YijueCard") && request.initiator->canPindian();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new YijueCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "YijueCard"; }
};

// Yijue's limitations expire at turn end, independent of surviving skill instances.
class Yijue : public TriggerSkillV2
{
public:
    Yijue() : TriggerSkillV2("yijue")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new YijueViewAsSkill;
    }

    int getPriority(TriggerEvent) const override
    {
        return 5;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return true;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != target || target != room->getCurrent())
                return true;
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
        return true;
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

class Paoxiao : public TargetModSkillV2
{
public:
    Paoxiao() : TargetModSkillV2("paoxiao")
    {
        frequency = NotCompulsory;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The collector owns exact-source visibility and validity.
        return ctx.modType == TargetModSkill::Residue
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class Tishen : public TriggerSkillV2
{
public:
    Tishen() : TriggerSkillV2("tishen")
    {
        events << EventPhaseChanging << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@substitute";
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                room->setPlayerProperty(player, "tishen_hp", QString::number(player->getHp()));
                room->setPlayerMark(player, "@substitute", player->getMark("@substitute")); // For UI coupling
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return triggerEvent == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getMark("@substitute") > 0 && player->getPhase() == Player::Start && recoverNum(player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.extra_data = recoverNum(ctx.owner);
        return ctx.extra_data.toInt() > 0 && room->askForSkillInvoke(ctx.owner, objectName(), ctx.extra_data);
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The limited token is the cost; another instance may have spent the shared mark.
        if (ctx.owner->getMark("@substitute") <= 0) return false;
        room->removePlayerMark(ctx.owner, "@substitute");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int x = ctx.extra_data.toInt();
        room->broadcastSkillInvoke(objectName());
        //room->doLightbox("$TishenAnimate");
        room->doSuperLightbox(player, "tishen");

        room->recover(player, RecoverStruct(player, nullptr, x, objectName()));
        player->drawCards(x, objectName());
        return false;
    }

private:
    static int recoverNum(ServerPlayer *player)
    {
        QString hp_str = player->property("tishen_hp").toString();
        if (hp_str.isEmpty()) return 0;
        int hp = hp_str.toInt();
        return qMin(hp - player->getHp(), player->getMaxHp() - player->getHp());
    }
};

class Longdan : public ViewAsSkillV2
{
public:
    Longdan() : ViewAsSkillV2("longdan", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator)
            : !materialClass(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card) || card->hasFlag("using")) return false;
        const QString material = materialClass(request);
        if (material.isEmpty() || !card->isKindOf(material.toLatin1().constData())) return false;
        // OneCardViewAsSkill also admitted response-accessible hand piles.
        QStringList places{"hand"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        if (!Sanguosha->matchExpPattern(".|.|.|" + places.join(","), request.initiator, card)) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        return slash.isAvailable(request.initiator);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return materialClass(request) == "Slash" ? "Jink" : "Slash";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (originalCard->isKindOf("Slash")) {
            Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
            jink->addSubcard(originalCard);
            jink->setSkillName(objectName());
            return jink;
        }
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (Player::isNostalGeneral(player, "zhaoyun"))
            index += 2;
        if (player->getGeneralName().contains("sp_tongyuan") || player->getGeneral2Name().contains("sp_tongyuan"))
            index = 5;
        return index;
    }

private:
    static QString materialClass(const ActiveSkillRequest &request)
    {
        switch (request.reason) {
        case CardUseStruct::CARD_USE_REASON_PLAY:
            return "Jink";
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
            if (request.pattern.contains("slash") || request.pattern.contains("Slash"))
                return "Jink";
            if (request.pattern == "jink")
                return "Slash";
            return QString();
        default:
            return QString();
        }
    }
};

class Yajiao : public TriggerSkillV2
{
public:
    Yajiao() : TriggerSkillV2("yajiao")
    {
        events << CardUsed << CardResponded;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const bool isHandcard = triggerEvent == CardUsed ? data.value<CardUseStruct>().m_isHandcard
            : data.value<CardResponseStruct>().m_isHandcard;
        return player && player->isAlive() && player->hasSkill(objectName())
            && !player->hasFlag("CurrentPlayer") && isHandcard
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const Card *cardstar = triggerEvent == CardUsed ? ctx.original_data->value<CardUseStruct>().card
            : ctx.original_data->value<CardResponseStruct>().m_card;
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
        return false;
    }
};

class Tieji : public TriggerSkillV2
{
public:
    Tieji() : TriggerSkillV2("tieji")
    {
        events << TargetSpecified << FinishJudge;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        // A started judgment keeps its suit rule even if the skill is lost meanwhile.
        if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName()) {
                judge->pattern = judge->card->getSuitString();
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return triggerEvent == TargetSpecified && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<CardUseStruct>().card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Targets are offered in order; the first accepted one starts this invocation.
        const QList<ServerPlayer *> tos = ctx.original_data->value<CardUseStruct>().to;
        for (int index = 0; index < tos.length(); ++index) {
            if (!ctx.owner->isAlive()) break;
            if (ctx.owner->askForSkillInvoke(this, QVariant::fromValue(tos.at(index)))) {
                ctx.extra_data = index;
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
        QList<ServerPlayer *> tos;
        for (int index = ctx.extra_data.toInt(); index < use.to.length(); ++index) {
            ServerPlayer *p = use.to.at(index);
            if (!player->isAlive()) break;
            if (index > ctx.extra_data.toInt() && !player->askForSkillInvoke(this, QVariant::fromValue(p)))
                continue;
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
        player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
        return false;
    }
};

// Tieji's invalidity expires at turn end, independent of surviving skill instances.
class TiejiClear : public TriggerSkillV2
{
public:
    TiejiClear() : TriggerSkillV2("#tieji-clear")
    {
        events << EventPhaseChanging << Death;
    }

    int getPriority(TriggerEvent) const override
    {
        return 5;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return true;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != target || target != room->getCurrent())
                return true;
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
        return true;
    }
};

Guanxing::Guanxing(const QString &name) : TriggerSkillV2(name)
{
    events << EventPhaseStart;
    m_baseAmount = 5;
    frequency = name == "heg_yizhi" ? Compulsory : Frequent;
    if (name == "heg_yizhi") relate_to_place = "deputy";
}

bool Guanxing::canPreshow() const { return !Config.EnableHegemony; }

int Guanxing::getPriority(TriggerEvent) const { return 1; }

void Guanxing::record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const
{
    if (player && ctx.owner == player && player->getPhase() == Player::Start)
        player->removeTag("guanxing_consumed");
}

TriggerList Guanxing::triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
{
    if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
    const QStringList consumed = player->getTag("guanxing_consumed").toStringList();
    QStringList candidates;
    for (int id : player->getValidSkillInstanceIds(objectName())) {
        const QString key = SkillInstanceUtils::formatName(objectName(), id);
        if (!consumed.contains(key)) candidates << key;
    }
    return candidates.isEmpty() ? TriggerList() : TriggerList{{player, candidates}};
}

bool Guanxing::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
    int index = qsanRandomBounded(2) + 1;
    if (objectName() == "guanxing" && !ctx.owner->hasInnateSkill(this) && ctx.owner->hasSkill("zhiji")) index += 2;
    room->broadcastSkillInvoke(objectName(), index);
    if (!Config.EnableHegemony) return true;
    const QString other = objectName() == "heg_yizhi" ? "guanxing" : "heg_yizhi";
    // The trigger menu chooses the actual source (head or deputy). The other
    // source is optional and is revealed only during pay, never during selection.
    if (ctx.owner->hasSkill(other) && !ctx.owner->hasShownSkill(other)) {
        for (int id : ctx.owner->getValidSkillInstanceIds(other)) {
            SkillInstanceRef ref(ctx.owner->objectName(), SkillInstanceKey(other, id));
            if (!room->canShowGeneralForSkill(ref)) continue;
            ctx.extra_data = id;
            ctx.choice = room->askForChoice(ctx.owner, "GuanxingShowGeneral", "show_both_generals+cancel");
            break;
        }
    }
    return true;
}

bool Guanxing::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!Config.EnableHegemony || ctx.choice != "show_both_generals") return true;
    const QString other = objectName() == "heg_yizhi" ? "guanxing" : "heg_yizhi";
    return room->showGeneralForSkill(SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(other, ctx.extra_data.toInt())));
}

bool Guanxing::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *owner = ctx.owner;
    if (!owner) return false;
    // Guanxing and Yizhi are two reveal choices for the same observation. Pair
    // instances by stable ID order so additional acquired instances still work.
    const QString other = objectName() == "heg_yizhi" ? "guanxing" : "heg_yizhi";
    const int ordinal = owner->getValidSkillInstanceIds(objectName()).indexOf(ctx.instanceID);
    const QList<int> others = owner->getValidSkillInstanceIds(other);
    if (Config.EnableHegemony && ordinal >= 0 && ordinal < others.size()) {
        QStringList consumed = owner->getTag("guanxing_consumed").toStringList();
        consumed << SkillInstanceUtils::formatName(other, others.at(ordinal));
        owner->setTag("guanxing_consumed", consumed);
    }
    const int amount = qMax(0, getEffectiveAmount(ctx));
    const int count = objectName() == "super_guanxing"
        || (Config.EnableHegemony && owner->hasShownSkill("guanxing") && owner->hasShownSkill("heg_yizhi"))
        ? amount : qMin(amount, owner->aliveCount());
    if (count == 0) return false;
    const QList<int> cards = room->getNCards(count);
    LogMessage log;
    log.type = "$ViewDrawPile";
    log.from = owner;
    log.card_str = ListI2S(cards).join("+");
    room->sendLog(log, owner);
    room->askForGuanxing(owner, cards, Room::GuanxingBothSides);
    return false;
}

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

class KongchengEffect : public TriggerSkillV2
{
public:
    KongchengEffect() :TriggerSkillV2("#kongcheng-effect")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->isKongcheng()
            && move.from == player && move.from_places.contains(Player::PlaceHand)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().startsWith("tenyear_") || (!player->getGeneralName().startsWith("tenyear_")
           && player->getGeneral2() && player->getGeneral2Name().startsWith("tenyear_")))
            index += 2;
        room->broadcastSkillInvoke("kongcheng", index);
        return false;
    }
};

// Jilve and Five Lines call the legacy trigger entry directly, so callbacks read `player`.
class Jizhi : public TriggerSkillV2
{
public:
    Jizhi() : TriggerSkillV2("jizhi")
    {
        frequency = Frequent;
        events << CardUsed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *yueying, QVariant &data) const override
    {
        return yueying && yueying->isAlive() && yueying->hasSkill(objectName())
            && data.value<CardUseStruct>().card->getTypeId() == Card::TypeTrick
            ? TriggerList{{yueying, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &ctx) const override
    {
        return ctx.original_data->value<CardUseStruct>().card->getTypeId() == Card::TypeTrick
            && (yueying->getMark("JilveEvent") > 0 || room->askForSkillInvoke(yueying, objectName()));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &) const override
    {
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

        return false;
    }
};

class Qicai : public TargetModSkillV2
{
public:
    Qicai() : TargetModSkillV2("qicai", "TrickCard") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The collector owns exact-source visibility and validity.
        return ctx.modType == TargetModSkill::DistanceLimit
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
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

class Zhuhai : public TriggerSkillV2
{
public:
    Zhuhai() : TriggerSkillV2("zhuhai")
    {
        events << EventPhaseStart << PreCardUsed;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        // Announce the accepted Zhuhai slash when it is actually used.
        if (triggerEvent == PreCardUsed && player->hasFlag("ZhuhaiSlash")) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());

            player->setFlags("-ZhuhaiSlash");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (triggerEvent != EventPhaseStart || !player || !player->isAlive()
            || player->getMark("damage_point_round") <= 0 || player->getPhase() != Player::Finish) return list;
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (p->isAlive() && p->hasSkill(objectName()) && p != player && p->canSlash(player, false))
                list[p] << objectName();
        }
        return list;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *p = ctx.owner, *player = ctx.invoker;
        if (player->isDead() || !p->canSlash(player, false)) return false;
        p->setFlags("ZhuhaiSlash");
        QString prompt = QString("@zhuhai-slash:%1:%2").arg(p->objectName()).arg(player->objectName());
        // The accepted slash is the whole effect; it is announced at PreCardUsed.
        if (room->askForUseSlashTo(p, player, prompt, false))
            return true;
        p->setFlags("-ZhuhaiSlash");
        return false;
    }
};

class Qianxin : public TriggerSkillV2
{
public:
    Qianxin() : TriggerSkillV2("qianxin")
    {
        events << Damage;
        frequency = Wake;
        waked_skills = "jianyan";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive()
            && player->getMark(objectName())<1 && player->hasSkill(objectName())
            // canWake() consumes its grant, so selection only peeks at it.
            && (player->isWounded() || !player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // Another instance may already have awakened this player.
        if (player->getMark(objectName()) > 0) return false;
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

class Jianyan : public ViewAsSkillV2
{
public:
    Jianyan() : ViewAsSkillV2("jianyan") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("JianyanCard");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new JianyanCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JianyanCard"; }
};

// Jilve answers "@@zhiheng" through a Room::BorrowedSkillScope instance.
class Zhiheng : public ViewAsSkillV2
{
public:
    Zhiheng() : ViewAsSkillV2("zhiheng") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return request.pattern == "@@zhiheng";
        return player->canDiscard(player, "he") && canUseAtPlay(player);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using")) return false;
        if (ServerInfo.GameMode == "02_1v1" && ServerInfo.GameRuleMode != "Classical"
            && request.selectedCardIds.size() >= 2) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && (player->handCards().contains(id) || player->getEquipsId().contains(id))
            && !player->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name and material payment.
        auto *card = new ZhihengCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhihengCard"; }

protected:
    virtual bool canUseAtPlay(const Player *player) const { return !player->hasUsed("ZhihengCard"); }
};

class Jiuyuan : public TriggerSkillV2
{
public:
    Jiuyuan() : TriggerSkillV2("jiuyuan$")
    {
        events << TargetConfirmed << PreHpRecover;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *sunquan, QVariant &data) const override
    {
        bool matches = false;
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            matches = use.card->isKindOf("Peach") && use.from && use.from->getKingdom() == "wu"
                && sunquan != use.from && sunquan->hasFlag("Global_Dying");
        } else if (triggerEvent == PreHpRecover) {
            RecoverStruct rec = data.value<RecoverStruct>();
            matches = rec.card && rec.card->hasFlag("jiuyuan");
        }
        return matches && sunquan && sunquan->hasLordSkill("jiuyuan")
            ? TriggerList{{sunquan, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *sunquan = ctx.owner;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == TargetConfirmed) {
            room->setCardFlag(data.value<CardUseStruct>().card, "jiuyuan");
        } else if (triggerEvent == PreHpRecover) {
            RecoverStruct rec = data.value<RecoverStruct>();
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

        return false;
    }
};

class Yingzi : public TriggerSkillV2
{
public:
    Yingzi() : TriggerSkillV2("yingzi")
    {
        events << DrawNCards;
        frequency = Compulsory;
        m_baseAmount = 1;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && (ctx.owner->hasShownSkill(this) || ctx.owner->askForSkillInvoke(this));
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhouyu = ctx.invoker;
        if (!zhouyu || !ctx.original_data) return false;
        int index = qsanRandomBounded(2) + 1;
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
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class YingziMaxCards : public MaxCardsSkillV2
{
public:
    YingziMaxCards() : MaxCardsSkillV2("#yingzi")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
    {
        return CorrectSkillResult::noEffect();
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder && ctx.holder->hasSkill("yingzi")
            ? CorrectSkillResult::useAmount(ctx.holder->getMaxHp()) : CorrectSkillResult::noEffect();
    }
};

class Fanjian : public ViewAsSkillV2
{
public:
    Fanjian() : ViewAsSkillV2("fanjian", 1) {  }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng() && !request.initiator->hasUsed("FanjianCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 1) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && player->handCards().contains(id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new FanjianCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "FanjianCard"; }
};

class Keji : public TriggerSkillV2
{
public:
    Keji() : TriggerSkillV2("keji")
    {
        events << PreCardUsed << CardResponded << EventPhaseChanging;
        frequency = Frequent;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Track all players once per event, including before they acquire Keji.
        if (player && player->getPhase() == Player::Play && event != EventPhaseChanging) {
            const Card *card = event == PreCardUsed ? data.value<CardUseStruct>().card
                : data.value<CardResponseStruct>().m_card;
            if (card && card->isKindOf("Slash")) player->addMark("KejiSlashInPlayPhase-Clear");
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Discard
            && player->getMark("KejiSlashInPlayPhase-Clear") < 1
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.invoker->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lvmeng = ctx.invoker;
        if (!lvmeng || !lvmeng->isAlive()) return false;
        if (lvmeng->getHandcardNum() > lvmeng->getMaxCards()) {
            int index = qsanRandomBounded(2) + 1;
            if (!lvmeng->hasInnateSkill(this) && lvmeng->hasSkill("mouduan")) index += 4;
            else if (Player::isNostalGeneral(lvmeng, "lvmeng")) index += 2;
            room->broadcastSkillInvoke(objectName(), index);
        }
        lvmeng->skip(Player::Discard);
        return false;
    }
};

class Qinxue : public TriggerSkillV2
{
public:
    Qinxue() : TriggerSkillV2("qinxue")
    {
        events << EventPhaseStart;
        frequency = Wake;
        waked_skills = "gongxin";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
            && player->getMark(objectName())<1 && player->hasSkill(objectName())
            // canWake() consumes its grant, so selection only peeks at it.
            && (meetsCondition(room, player) || !player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lvmeng = ctx.owner;
        // Another instance may already have awakened this player.
        if (lvmeng->getMark(objectName()) > 0) return false;
        int n = lvmeng->getHandcardNum() - lvmeng->getHp();
        if (meetsCondition(room, lvmeng)){
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

private:
    static bool meetsCondition(Room *room, ServerPlayer *lvmeng)
    {
        int n = lvmeng->getHandcardNum() - lvmeng->getHp();
        int wake_lim = (Sanguosha->getPlayerCount(room->getMode()) >= 7) ? 2 : 3;
        return n >= wake_lim;
    }
};

class Qixi : public ViewAsSkillV2
{
public:
    Qixi() : ViewAsSkillV2("qixi", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && card->isBlack() && (request.initiator->handCards().contains(id)
            || request.initiator->getEquipsId().contains(id) || request.initiator->getHandPile().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Dismantlement *dismantlement = new Dismantlement(originalCard->getSuit(), originalCard->getNumber());
        dismantlement->addSubcard(originalCard->getId());
        dismantlement->setSkillName(objectName());
        return dismantlement;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (Player::isNostalGeneral(player, "ganning"))
            index += 2;
        return index;
    }
};

class FenweiViewAsSkill : public ViewAsSkillV2
{
public:
    FenweiViewAsSkill() : ViewAsSkillV2("fenwei") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@fenwei";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new FenweiCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "FenweiCard"; }
};

class Fenwei : public TriggerSkillV2
{
public:
    Fenwei() : TriggerSkillV2("fenwei")
    {
        events << TargetSpecifying;
        view_as_skill = new FenweiViewAsSkill;
        frequency = Limited;
        limit_mark = "@fenwei";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.length() <= 1 || !use.card->isNDTrick())
            return list;
        foreach (ServerPlayer *ganning, room->findPlayersBySkillName(objectName())) {
            if (ganning->isAlive() && ganning->hasSkill(objectName()) && ganning->getMark("@fenwei") > 0)
                list[ganning] << objectName();
        }
        return list;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *ganning = ctx.owner;
        if (ganning->getMark("@fenwei") <= 0) return false;
        QStringList target_list;
        foreach(ServerPlayer *p, ctx.original_data->value<CardUseStruct>().to)
            target_list << p->objectName();
        room->setPlayerProperty(ganning, "fenwei_targets", target_list.join("+"));
        ganning->setTag("fenwei", *ctx.original_data);
        // The accepted FenweiCard spends the limited mark and records the nullified targets.
        return room->askForUseCard(ganning, "@@fenwei", "@fenwei-card");
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        *ctx.original_data = ctx.owner->getTag("fenwei");
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().contains("heqi") || (!player->getGeneralName().contains("ganning") && player->getGeneral2Name().contains("heqi")))
            index ++;
        return index;
    }
};

class Kurou : public ViewAsSkillV2
{
public:
    Kurou() : ViewAsSkillV2("kurou", 1) {  }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("KurouCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 1) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && (player->handCards().contains(id) || player->getEquipsId().contains(id)) && !player->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new KurouCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "KurouCard"; }
};

class Zhaxiang : public TriggerSkillV2
{
public:
    Zhaxiang() : TriggerSkillV2("zhaxiang")
    {
        events << HpLost << EventPhaseChanging;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent triggerEvent) const override
    {
        if (triggerEvent == EventPhaseChanging)
            return 8;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // The turn bonus expires even if the skill was lost meanwhile.
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive || change.to == Player::RoundStart)
                room->setPlayerMark(player, objectName(), 0);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return triggerEvent == HpLost && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        int lose = ctx.original_data->value<HpLostStruct>().lose;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());

        for (int i = 0; i < lose; i++) {
            player->drawCards(3, objectName());
            if (player->getPhase() == Player::Play)
                room->addPlayerMark(player, objectName());
        }
        return false;
    }
};

class ZhaxiangRedSlash : public TriggerSkillV2
{
public:
    ZhaxiangRedSlash() : TriggerSkillV2("#zhaxiang")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getMark("zhaxiang") > 0
            && use.card->isKindOf("Slash") && use.card->isRed()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
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

class ZhaxiangTargetMod : public TargetModSkillV2
{
public:
    ZhaxiangTargetMod() : TargetModSkillV2("#zhaxiang-target")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int mark = ctx.primary ? ctx.primary->getMark("zhaxiang") : 0;
        if (mark <= 0) return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::Residue)
            return CorrectSkillResult::useAmount(mark);
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->isRed())
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class GuoseViewAsSkill : public ViewAsSkillV2
{
public:
    GuoseViewAsSkill() : ViewAsSkillV2("guose", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("GuoseCard") && !(request.initiator->isNude() && request.initiator->getHandPile().isEmpty());
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 1) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && candidate->getSuit() == Card::Diamond && (player->handCards().contains(id) || player->getEquipsId().contains(id) || player->getHandPile().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new GuoseCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "GuoseCard"; }
};

// CardFinished observes one completed card, even if its skill was lost during use.
class Guose : public TriggerSkillV2
{
public:
    Guose() : TriggerSkillV2("guose")
    {
        events << CardFinished;
        view_as_skill = new GuoseViewAsSkill;
    }

    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && use.card->getSkillNames().contains(objectName()))
            player->drawCards(1, objectName());
        return true;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Indulgence") ? 1 : 2;
    }
};

class LiuliViewAsSkill : public ViewAsSkillV2
{
public:
    LiuliViewAsSkill() : ViewAsSkillV2("liuli", 1) {  }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@liuli"
            && true;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 1) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && (player->handCards().contains(id) || player->getEquipsId().contains(id)) && !player->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new LiuliCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LiuliCard"; }
};

// The trigger only prompts; the accepted V2 response owns activation and payment.
class Liuli : public TriggerSkillV2
{
public:
    Liuli() : TriggerSkillV2("liuli")
    {
        events << TargetConfirming;
        view_as_skill = new LiuliViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *daqiao, QVariant &data) const override
    {
        return daqiao && daqiao->isAlive() && daqiao->hasSkill(objectName())
            && canRedirect(room, daqiao, data.value<CardUseStruct>())
            ? TriggerList{{daqiao, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *daqiao = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!canRedirect(room, daqiao, use)) return false;
        QString prompt = "@liuli:" + use.from->objectName();
        room->setPlayerFlag(use.from, "LiuliSlashSource");
        // a temp nasty trick
        daqiao->setTag("liuli-card", QVariant::fromValue(use.card)); // for the server (AI)
        room->setPlayerProperty(daqiao, "liuli", use.card->toString()); // for the client (UI)
        const bool used = room->askForUseCard(daqiao, "@@liuli", prompt, -1, Card::MethodDiscard);
        daqiao->removeTag("liuli-card");
        room->setPlayerProperty(daqiao, "liuli", "");
        room->setPlayerFlag(use.from, "-LiuliSlashSource");
        return used;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *daqiao = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        QList<ServerPlayer *> players = room->getOtherPlayers(daqiao);
        players.removeOne(use.from);
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
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (!player->hasInnateSkill(this) && player->hasSkills("luoyan|olluoyan"))
            index += 4;
        else if (Player::isNostalGeneral(player, "daqiao"))
            index += 2;

        return index;
    }

private:
    static bool canRedirect(Room *room, ServerPlayer *daqiao, const CardUseStruct &use)
    {
        if (!use.card->isKindOf("Slash") || !use.to.contains(daqiao) || !daqiao->canDiscard(daqiao, "he"))
            return false;
        QList<ServerPlayer *> players = room->getOtherPlayers(daqiao);
        players.removeOne(use.from);
        foreach (ServerPlayer *p, players) {
            if (use.from->canSlash(p, use.card, false) && daqiao->inMyAttackRange(p))
                return true;
        }
        return false;
    }
};

class Qianxun : public TriggerSkillV2
{
public:
    Qianxun() : TriggerSkillV2("qianxun") { events << CardOnEffect << EventPhaseChanging; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        // Returning stored cards is cleanup, independent of surviving skill instances.
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getPile("qianxun").length() > 0) {
                    DummyCard *dummy = new DummyCard(p->getPile("qianxun"));
                    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, p->objectName(), "qianxun", "");
                    room->obtainCard(p, dummy, reason, false);
                    dummy->deleteLater();
                }
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardOnEffect || !player || !player->isAlive()
            || !player->hasSkill(objectName()) || player->isKongcheng()) return {};
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return effect.card && !effect.multiple && effect.card->getTypeId() == Card::TypeTrick
            && effect.from != player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.original_data
            && room->askForSkillInvoke(ctx.invoker, objectName(), *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        room->broadcastSkillInvoke(objectName());
        player->setTag("QianxunEffectData", *ctx.original_data);
        CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), "");
        player->addToPile("qianxun", player->handCards(), false, QList<ServerPlayer *>{player}, reason);
        return false;
    }
};

class LianyingViewAsSkill : public ViewAsSkillV2
{
public:
    LianyingViewAsSkill() : ViewAsSkillV2("lianying") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@lianying";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new LianyingCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LianyingCard"; }
};

class Lianying : public TriggerSkillV2
{
public:
    Lianying() : TriggerSkillV2("lianying")
    {
        events << CardsMoveOneTime;
        view_as_skill = new LianyingViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *luxun, QVariant &data) const override
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return luxun && luxun->isAlive() && luxun->hasSkill(objectName())
            && move.from == luxun && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard
            ? TriggerList{{luxun, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *luxun = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        luxun->setTag("LianyingMoveData", *ctx.original_data);
        int count = 0;
        for (int i = 0; i < move.from_places.length(); i++) {
            if (move.from_places[i] == Player::PlaceHand) count++;
        }
        room->setPlayerMark(luxun, "lianying", count);
        // The accepted LianyingCard owns target selection and drawing.
        return room->askForUseCard(luxun, "@@lianying", "@lianying-card:::" + QString::number(count));
    }
};

class Jieyin : public ViewAsSkillV2
{
public:
    Jieyin() : ViewAsSkillV2("jieyin", 2) {  }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHandcardNum() >= 2 && !request.initiator->hasUsed("JieyinCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 2) return false;
        const int id = candidate->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && player->handCards().contains(id) && !player->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new JieyinCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JieyinCard"; }
};

class Xiaoji : public TriggerSkillV2
{
public:
    Xiaoji() : TriggerSkillV2("xiaoji") { events << CardsMoveOneTime; frequency = Frequent; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.from != player) return {};
        const int count = move.from_places.count(Player::PlaceEquip);
        return count > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !ctx.original_data) return false;
        ctx.extra_data = ctx.original_data->value<CardsMoveOneTimeStruct>().from_places.count(Player::PlaceEquip);
        return ctx.extra_data.toInt() > 0 && room->askForSkillInvoke(ctx.invoker, objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *sunshangxiang = ctx.invoker;
        if (!sunshangxiang || !sunshangxiang->isAlive()) return false;
        // One declined choice stops the remaining equipment draws for this move.
        // Keep the move-local count in context so nested card moves cannot overwrite it.
        for (int i = 0; i < ctx.extra_data.toInt(); ++i) {
            if (!sunshangxiang->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(sunshangxiang, objectName())) break;
            int index = qsanRandomBounded(2) + 1;
            if (!sunshangxiang->hasInnateSkill(this) && sunshangxiang->getMark("fanxiang") > 0)
                index += 2;
            if (sunshangxiang->getGeneralName().startsWith("tenyear_") || (!sunshangxiang->getGeneralName().startsWith("tenyear_")
               && sunshangxiang->getGeneral2() && sunshangxiang->getGeneral2Name().startsWith("tenyear_")))
                index = qsanRandomBounded(2) + 5;
            room->broadcastSkillInvoke(objectName(), index);

            sunshangxiang->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};

class Wushuang : public TriggerSkillV2
{
public:
    Wushuang() : TriggerSkillV2("wushuang")
    {
        events << TargetSpecified << CardEffected << CardResponded;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // Duel obligations bind the holder's opponents, who do not hold the skill.
        if (!player || !player->isAlive()) return true;
        if (triggerEvent == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            // Each holder triggered below appends its obligations for this Duel.
            if (use.card->isKindOf("Duel"))
                room->setTag("Wushuang_"+use.card->toString(), QStringList());
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
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList list;
        if (triggerEvent != TargetSpecified || !player || !player->isAlive()) return list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Duel")) {
            // The user and each Duel target may hold the skill independently.
            if (player->hasSkill(objectName()))
                list[player] << objectName();
            foreach (ServerPlayer *to, use.to) {
                if (to != player && to->hasSkill(objectName()))
                    list[to] << objectName();
            }
        } else if (use.card->isKindOf("Slash") && player->hasSkill(objectName()))
            list[player] << objectName();
        return list;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner, *player = ctx.invoker;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        int n = qsanRandomBounded(2)+1;
        if(owner->getGeneralName().startsWith("nos_")||owner->getGeneral2Name().startsWith("nos_"))
            n++;
        room->sendCompulsoryTriggerLog(owner, this, n);
        if (use.card->isKindOf("Duel")) {
            QStringList wushuang_tag = room->getTag("Wushuang_"+use.card->toString()).toStringList();
            if (owner == player) {
                foreach(ServerPlayer *to, use.to)
                    wushuang_tag << to->objectName();
            } else
                wushuang_tag << player->objectName();
            room->setTag("Wushuang_"+use.card->toString(), wushuang_tag);
        } else {
            QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
            for (int i = 0; i < use.to.length(); i++) {
                if (jink_list.at(i).toInt() == 1)
                    jink_list.replace(i, QVariant(2));
            }
            player->setTag("Jink_" + use.card->toString(), jink_list);
        }
        return false;
    }
};

class Liyu : public TriggerSkillV2
{
public:
    Liyu() : TriggerSkillV2("liyu")
    {
        events << Damage;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && damage.to->isAlive() && player != damage.to && !damage.to->hasFlag("Global_DebutFlag") && !damage.to->isNude()
            && damage.card && damage.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to->isAlive() || damage.to->isNude()) return false;
        Duel duel(Card::NoSuit, 0);
        duel.setSkillName("_liyu");

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p != damage.to && player->canUse(&duel, p))
                targets << p;
        }
        // The damaged player decides; declining leaves the skill unused.
        ServerPlayer *target = room->askForPlayerChosen(damage.to, targets, objectName(), "@liyu:" + player->objectName(), true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());

        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = damage.to;
        log.to << player;
        log.arg = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());

        if (!damage.to->isNude()) {
            int id = room->askForCardChosen(player, damage.to, "he", objectName());
            room->obtainCard(player, id);
        }
        if (player->isAlive() && target->isAlive()) {
            Duel *duel = new Duel(Card::NoSuit, 0);
            duel->setSkillName("_liyu");
            room->useCard(CardUseStruct(duel, player, target));
            delete duel;
        }
        return false;
    }
};

class Lijian : public ViewAsSkillV2
{
public:
    Lijian() : ViewAsSkillV2("lijian", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he") && !player->hasUsed("LijianCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        return player && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (player->handCards().contains(card->getEffectiveId()) || player->hasEquip(card))
            && player->canDiscard(player, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Preserve the shared card's wire identity, ordered targets and AI entry.
        auto *card = new LijianCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LijianCard"; }
};

class Biyue : public TriggerSkillV2
{
public:
    Biyue() : TriggerSkillV2("biyue") { events << EventPhaseStart; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish)
            return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The dispatcher binds this activation to one exact owning instance.
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Chuli : public ViewAsSkillV2
{
public:
    Chuli() : ViewAsSkillV2("chuli", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->canDiscard(player, "he") && !player->hasUsed("ChuliCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        const int id = card ? card->getEffectiveId() : -1;
        // Keep the V2 selection bound to the same hand/equipment cards as the legacy skill.
        return player && card && id >= 0 && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (player->handCards().contains(id) || player->hasEquip(card))
            && player->canDiscard(player, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ChuliCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ChuliCard"; }
};

class Jijiu : public ViewAsSkillV2
{
public:
    Jijiu() : ViewAsSkillV2("jijiu", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->hasFlag("CurrentPlayer")) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || ((request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                 || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
                && request.pattern.contains("peach") && player->getMark("Global_PreventPeach") < 1);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        // Retain response-accessible hand piles as well as hand/equipment cards.
        QStringList places{"hand", "equipped"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        return Sanguosha->matchExpPattern(".|red|.|" + places.join(","), request.initiator, card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
        peach->setSkillName(objectName());
        peach->addSubcard(originalCard);
        return peach;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Peach"; }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (Player::isNostalGeneral(player, "huatuo"))
            index += 2;
        return index;
    }
};

class Mashu : public DistanceSkillV2
{
public:
    Mashu() : DistanceSkillV2("mashu")
    {
        // The engine gates each source's reveal/validity and applies its amount.
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_Primary);
    }
};

class Xunxun : public TriggerSkillV2
{
public:
    Xunxun() : TriggerSkillV2("xunxun")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName(), QVariant(), false);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lidian = ctx.invoker;
        if (!lidian || !lidian->isAlive()) return false;
        room->notifySkillInvoked(lidian, objectName());
        int index = qsanRandomBounded(2) + 1;
        if (lidian->getGeneralName().contains("tangzi") || (!lidian->getGeneralName().contains("lidian") && lidian->getGeneral2Name().contains("tangzi")))
            index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        const QList<ServerPlayer *> viewers{lidian};
        QList<int> obtained, card_ids = room->getNCards(4);
        room->fillAG(card_ids, lidian);
        for (int i = 0; i < 2 && !card_ids.isEmpty(); ++i) {
            const int id = room->askForAG(lidian, card_ids, false, objectName());
            card_ids.removeOne(id);
            obtained << id;
            if (i == 0) room->takeAG(lidian, id, false, viewers);
        }
        room->clearAG(lidian);
        room->askForGuanxing(lidian, card_ids, Room::GuanxingDownOnly);
        DummyCard *dummy = new DummyCard(obtained);
        dummy->deleteLater();
        lidian->obtainCard(dummy, false);
        // Resolving Xunxun replaces the normal draw phase; declining cost does not.
        return true;
    }
};

class Wangxi : public TriggerSkillV2
{
public:
    Wangxi() : TriggerSkillV2("wangxi")
    {
        events << Damage << Damaged;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        const ServerPlayer *target = otherPlayer(event, damage);
        return player && player->isAlive() && player->hasSkill(objectName()) && target
            && target->isAlive() && target != player && damage.damage > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *target = otherPlayer(event, damage);
        if (!target || !target->isAlive() || target == ctx.owner || damage.damage <= 0
            || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(target), false)) return false;
        ctx.targets = {target};
        ctx.extra_data = damage.damage;
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !target || target == player) return false;
        QList<ServerPlayer *> players{player, target};
        room->sortByActionOrder(players);
        // One damage event owns the continuation choices. A declined point ends
        // this execution instead of creating more independently prompted tickets.
        for (int i = 0; i < ctx.extra_data.toInt(); ++i) {
            if (!player->isAlive() || !target->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(player, objectName(), QVariant::fromValue(target), false)) break;
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName(), event == Damaged ? 1 : 2);
            room->drawCards(players, getEffectiveAmount(ctx), objectName());
        }
        return false;
    }

private:
    static ServerPlayer *otherPlayer(TriggerEvent event, const DamageStruct &damage)
    {
        if (event == Damage && damage.to && !damage.to->hasFlag("Global_DebutFlag")) return damage.to;
        return event == Damaged ? damage.from : nullptr;
    }
};

class Wangzun : public TriggerSkillV2
{
public:
    Wangzun() : TriggerSkillV2("wangzun")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *target, QVariant &) const override
    {
        TriggerList list;
        if (!target || !target->isLord() || target->getPhase() != Player::Start || !isNormalGameMode(room->getMode()))
            return list;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()))
                list[p] << objectName();
        }
        return list;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->askForSkillInvoke(this, ctx.invoker);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(1, objectName());
        room->addMaxCards(ctx.invoker, -1);
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

class Yaowu : public TriggerSkillV2
{
public:
    Yaowu() : TriggerSkillV2("yaowu")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && damage.card && damage.card->isKindOf("Slash") && damage.card->isRed()
            && damage.from && damage.from->isAlive()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || damage.from->isDead()) return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(damage.to, objectName());

        if (damage.from->isWounded() && room->askForChoice(damage.from, objectName(), "recover+draw", *ctx.original_data) == "recover")
            room->recover(damage.from, RecoverStruct(objectName(), damage.to));
        else
            damage.from->drawCards(1, objectName());
        return false;
    }
};

class Qiaomeng : public TriggerSkillV2
{
public:
    Qiaomeng() : TriggerSkillV2("qiaomeng")
    {
        events << Damage << BeforeCardsMove;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // A started Qiaomeng discard keeps its horse even if the skill was lost meanwhile.
        if (triggerEvent == BeforeCardsMove) {
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
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != Damage || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        DamageStruct damage = data.value<DamageStruct>();
        return damage.to->isAlive() && !damage.to->hasFlag("Global_DebutFlag")
            && damage.card && damage.card->isKindOf("Slash") && damage.card->isBlack()
            && player->canDiscard(damage.to, "e")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return ctx.owner->canDiscard(damage.to, "e") && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());
        int id = room->askForCardChosen(player, damage.to, "e", objectName(), false, Card::MethodDiscard);
        CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), damage.to->objectName(),
            objectName(), "");
        room->throwCard(Sanguosha->getCard(id), reason, damage.to, player);
        return false;
    }
};

class Xiaoxi : public TriggerSkillV2
{
public:
    Xiaoxi() : TriggerSkillV2("xiaoxi")
    {
        events << Debut;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && canSlash(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return canSlash(ctx.owner) && room->askForSkillInvoke(ctx.owner, objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_xiaoxi");
        slash->deleteLater();
        room->useCard(CardUseStruct(slash, player, player->getNext()));
        return false;
    }

private:
    static bool canSlash(ServerPlayer *player)
    {
        ServerPlayer *opponent = player->getNext();
        if (!opponent->isAlive())
            return false;
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_xiaoxi");
        return !player->isLocked(&slash) && player->canSlash(opponent, &slash, false);
    }
};




class NosJianxiong : public TriggerSkillV2
{
public:
    NosJianxiong() : TriggerSkillV2("nosjianxiong") { events << Damaged; }

    static bool available(Room *room, const Card *card)
    {
        if (!card) return false;
        const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        if (ids.isEmpty()) return false;
        for (int id : ids)
            if (room->getCardPlace(id) != Player::PlaceTable) return false;
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && available(room, data.value<DamageStruct>().card)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return available(room, ctx.original_data->value<DamageStruct>().card)
            && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = ctx.original_data->value<DamageStruct>().card;
        // Nested movement or another instance must not reclaim cards from a new owner.
        if (available(room, card)) {
            room->broadcastSkillInvoke(objectName());
            ctx.owner->obtainCard(card);
        }
        return false;
    }
};

class NosFankui : public TriggerSkillV2
{
public:
    NosFankui() : TriggerSkillV2("nosfankui") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *from = data.value<DamageStruct>().from;
        return player && player->isAlive() && player->hasSkill(objectName()) && from && !from->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || from->isNude() || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(from)))
            return false;
        ctx.targets = {from};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Recheck after target interception and earlier instances' extraction.
        if (!target || target->isNude()) return false;
        room->broadcastSkillInvoke(objectName());
        const int id = room->askForCardChosen(ctx.owner, target, "he", objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName());
        room->obtainCard(ctx.owner, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
        return false;
    }
};

// Jilve calls the legacy trigger entry directly, so callbacks read `player`, not ctx.owner.
class NosGuicai : public TriggerSkillV2
{
public:
    NosGuicai() : TriggerSkillV2("nosguicai")
    {
        events << AskForRetrial;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (player->isKongcheng() || !judge)
            return false;

        QStringList prompt_list;
        prompt_list << "@nosguicai-card" << judge->who->objectName()
            << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");

        const Card *card = room->askForCard(player, ".", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);
        if (!card)
            return false;
        ctx.extra_data = QVariant::fromValue(card);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = ctx.extra_data.value<const Card *>();
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!card || !judge)
            return false;
        room->broadcastSkillInvoke(objectName());
        room->retrial(card, player, judge, objectName(), false);
        return false;
    }
};

class NosGanglie : public TriggerSkillV2
{
public:
    NosGanglie() : TriggerSkillV2("nosganglie")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xiahou, QVariant &) const override
    {
        return xiahou && xiahou->isAlive() && xiahou->hasSkill(objectName())
            ? TriggerList{{xiahou, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, "nosganglie", *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *xiahou = ctx.owner;
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        room->broadcastSkillInvoke("nosganglie");

        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.reason = objectName();
        judge.who = xiahou;

        room->judge(judge);
        if (!from || from->isDead()) return false;
        if (judge.isGood()) {
            if (from->getHandcardNum() < 2 || !room->askForDiscard(from, objectName(), 2, 2, true))
                room->damage(DamageStruct(objectName(), xiahou, from));
        }
        return false;
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

class NosTuxiViewAsSkill : public ViewAsSkillV2
{
public:
    NosTuxiViewAsSkill() : ViewAsSkillV2("nostuxi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@nostuxi";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new NosTuxiCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosTuxiCard"; }
};

class NosTuxi : public TriggerSkillV2
{
public:
    NosTuxi() : TriggerSkillV2("nostuxi")
    {
        events << EventPhaseStart;
        view_as_skill = new NosTuxiViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *zhangliao, QVariant &) const override
    {
        if (!zhangliao || !zhangliao->isAlive() || !zhangliao->hasSkill(objectName())
            || zhangliao->getPhase() != Player::Draw) return {};
        foreach (ServerPlayer *player, room->getOtherPlayers(zhangliao)) {
            if (!player->isKongcheng())
                return {{zhangliao, {objectName()}}};
        }
        return {};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The accepted NosTuxiCard owns target selection and extraction.
        return room->askForUseCard(ctx.owner, "@@nostuxi", "@nostuxi-card");
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
    {
        // Resolving Tuxi replaces the normal draw phase.
        return true;
    }
};

class NosLuoyiBuff : public TriggerSkillV2
{
public:
    NosLuoyiBuff() : TriggerSkillV2("#nosluoyi")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xuchu, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        const Card *reason = damage.card;
        return xuchu && xuchu->isAlive() && xuchu->hasSkill(objectName()) && xuchu->hasFlag("nosluoyi")
            && !damage.chain && !damage.transfer && damage.by_user
            && reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))
            ? TriggerList{{xuchu, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#LuoyiBuff";
        log.from = ctx.owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);

        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class NosLuoyi : public TriggerSkillV2
{
public:
    NosLuoyi() : TriggerSkillV2("nosluoyi")
    {
        events << DrawNCards;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xuchu, QVariant &data) const override
    {
        return xuchu && xuchu->isAlive() && xuchu->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{xuchu, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->setFlags(objectName());
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num--;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

NosYiji::NosYiji() : TriggerSkillV2("nosyiji")
{
    events << Damaged;
    frequency = Frequent;
    n = 2;
}

TriggerList NosYiji::triggerable(TriggerEvent, Room *, ServerPlayer *guojia, QVariant &) const
{
    return guojia && guojia->isAlive() && guojia->hasSkill(objectName())
        ? TriggerList{{guojia, {objectName()}}} : TriggerList();
}

bool NosYiji::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    return room->askForSkillInvoke(ctx.owner, objectName());
}

bool NosYiji::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *guojia = ctx.owner;
    int x = ctx.original_data->value<DamageStruct>().damage;
    // One damage event owns the continuation choices; a declined point ends it.
    for (int i = 0; i < x; i++) {
        if (!guojia->isAlive() || !isSourceAvailable(room, ctx)) break;
        if (i > 0 && !room->askForSkillInvoke(guojia, objectName()))
            break;
        room->broadcastSkillInvoke("nosyiji");

        QList<int> yiji_cards = room->getNCards(n);
		guojia->assignmentCards(yiji_cards,objectName());
		if(!yiji_cards.isEmpty()){
			DummyCard *dummy = new DummyCard(yiji_cards);
			guojia->obtainCard(dummy, false);
			dummy->deleteLater();
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
    return false;
}

NosRendeCard::NosRendeCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool NosRendeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(Self->hasAcquiredSkill("nosrende")){
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
        room->broadcastSkillInvoke("rende",qsanRandomBounded(2)+1);

    source->setTag("nosrende", QDateTime::currentDateTime());

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "nosrende", "");
    room->obtainCard(target, this, reason, false);

    int old_value = source->getMark("nosrende");
    int new_value = old_value + subcards.length();
    room->setPlayerMark(source, "nosrende", new_value);

    if (old_value < 2 && new_value >= 2)
        room->recover(source, RecoverStruct("nosrende", source));
}

class NosRendeViewAsSkill : public ViewAsSkillV2
{
public:
    NosRendeViewAsSkill() : ViewAsSkillV2("nosrende") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !player->isKongcheng()
            && (ServerInfo.GameMode != "04_1v3" || player->getMark("nosrende") < 2);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        return player && card && !card->hasFlag("using") && !card->isEquipped()
            && (ServerInfo.GameMode != "04_1v3"
                || request.selectedCardIds.size() + player->getMark("nosrende") < 2)
            && player->handCards().contains(card->getEffectiveId())
            && !request.selectedCardIds.contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and giving effect.
        auto *card = new NosRendeCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosRendeCard"; }
};

class NosRende : public TriggerSkillV2
{
public:
    NosRende() : TriggerSkillV2("nosrende")
    {
        events << EventPhaseChanging;
        view_as_skill = new NosRendeViewAsSkill;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // Five Lines also registers this cleanup for players without the skill.
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive && player && player->getMark("nosrende") > 0)
            room->setPlayerMark(player, "nosrende", 0);
        return true;
    }
};

class NosTieji : public TriggerSkillV2
{
public:
    NosTieji() : TriggerSkillV2("nostieji")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<CardUseStruct>().card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Targets are offered in order; the first accepted one starts this invocation.
        const QList<ServerPlayer *> tos = ctx.original_data->value<CardUseStruct>().to;
        for (int index = 0; index < tos.length(); ++index) {
            if (!ctx.owner->isAlive()) break;
            if (ctx.owner->askForSkillInvoke(this, QVariant::fromValue(tos.at(index)))) {
                ctx.extra_data = index;
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
        for (int index = ctx.extra_data.toInt(); index < use.to.length(); ++index) {
            ServerPlayer *p = use.to.at(index);
            if (!player->isAlive()) break;
            if (index > ctx.extra_data.toInt() && !player->askForSkillInvoke(this, QVariant::fromValue(p)))
                continue;
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
        player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
        return false;
    }
};

// Jilve calls the legacy trigger entry directly, so callbacks read `player`, not ctx.owner.
class NosJizhi : public TriggerSkillV2
{
public:
    NosJizhi() : TriggerSkillV2("nosjizhi")
    {
        frequency = Frequent;
        events << CardUsed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *yueying, QVariant &data) const override
    {
        return yueying && yueying->isAlive() && yueying->hasSkill(objectName())
            && data.value<CardUseStruct>().card->isNDTrick()
            ? TriggerList{{yueying, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &ctx) const override
    {
        return ctx.original_data->value<CardUseStruct>().card->isNDTrick()
            && room->askForSkillInvoke(yueying, objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &) const override
    {
        room->broadcastSkillInvoke("jizhi");
        yueying->drawCards(1, objectName());
        return false;
    }
};

class NosQicai : public TargetModSkillV2
{
public:
    NosQicai() : TargetModSkillV2("nosqicai", "TrickCard") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The collector owns exact-source visibility, validity and hidden-use previews.
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.primary && ctx.card
            && Sanguosha->matchExpPattern("TrickCard", ctx.primary, ctx.card)
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
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

class NosKurou : public ViewAsSkillV2
{
public:
    NosKurou() : ViewAsSkillV2("noskurou") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new NosKurouCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosKurouCard"; }
};

class NosYingzi : public TriggerSkillV2
{
public:
    NosYingzi() : TriggerSkillV2("nosyingzi")
    {
        events << DrawNCards;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *zhouyu, QVariant &data) const override
    {
        return zhouyu && zhouyu->isAlive() && zhouyu->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{zhouyu, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke("nosyingzi");
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num++;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
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

class NosFanjian : public ViewAsSkillV2
{
public:
    NosFanjian() : ViewAsSkillV2("nosfanjian") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng() && !request.initiator->hasUsed("NosFanjianCard");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new NosFanjianCard;
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosFanjianCard"; }
};

class NosGuose : public ViewAsSkillV2
{
public:
    NosGuose() : ViewAsSkillV2("nosguose", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        if (!player || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && card->getSuit() == Card::Diamond && (player->handCards().contains(id)
            || player->getEquipsId().contains(id) || player->getHandPile().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Indulgence"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
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

class NosLianying : public TriggerSkillV2
{
public:
    NosLianying() : TriggerSkillV2("noslianying")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *luxun, QVariant &data) const override
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return luxun && luxun->isAlive() && luxun->hasSkill(objectName())
            && move.from == luxun && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard
            ? TriggerList{{luxun, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

QingnangCard::QingnangCard()
{
}

bool QingnangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(Self->hasAcquiredSkill("qingnang")){
		QString ww = Self->property("manweiwoFrom").toString();
		if(!ww.isEmpty()&&to_select->objectName()!=ww) return false;
	}
    return targets.isEmpty() && to_select->isWounded();
}

bool QingnangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if(Self->hasAcquiredSkill("qingnang")){
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

class Qingnang : public ViewAsSkillV2
{
public:
    Qingnang() : ViewAsSkillV2("qingnang", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->canDiscard(player, "h") && !player->hasUsed("QingnangCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        return player && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && player->handCards().contains(card->getEffectiveId()) && !player->isJilei(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Keep the established card wire name, target rules and material payment.
        auto *card = new QingnangCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "QingnangCard"; }
};

NosLijianCard::NosLijianCard() : LijianCard(false)
{
}

class NosLijian : public ViewAsSkillV2
{
public:
    NosLijian() : ViewAsSkillV2("noslijian", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he") && !player->hasUsed("NosLijianCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        return player && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (player->handCards().contains(card->getEffectiveId()) || player->hasEquip(card))
            && player->canDiscard(player, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Preserve the shared card's wire identity, ordered targets and AI entry.
        auto *card = new NosLijianCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosLijianCard"; }
    /*

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Duel") ? 0 : -1;
    }*/
};

class MobileWangzun : public TriggerSkillV2
{
public:
    MobileWangzun() : TriggerSkillV2("mobilewangzun")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart) return list;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isAlive() && p->hasSkill(objectName()) && player->getHp() > p->getHp())
                list[p] << objectName();
        }
        return list;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *p = ctx.owner, *player = ctx.invoker;
        if (player->isDead() || player->getHp() <= p->getHp()) return false;
        room->sendCompulsoryTriggerLog(p, objectName(), true, true);
        if (player->isLord()) {
            p->drawCards(2, objectName());
            room->addMaxCards(player, -1);
        } else
            p->drawCards(1, objectName());
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

// Stays legacy: the slash target uses it without owning a Tongji instance.
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

class MobileTongji : public TriggerSkillV2
{
public:
    MobileTongji() : TriggerSkillV2("mobiletongji")
    {
        events << TargetConfirming;
        view_as_skill = new MobileTongjiVS;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // The slash target decides once; the first eligible Yuan Shu anchors the source.
        ServerPlayer *yuanshu = player && player->isAlive() ? firstYuanshu(room, player, data.value<CardUseStruct>()) : nullptr;
        return yuanshu ? TriggerList{{yuanshu, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!firstYuanshu(room, player, use)) return false;

        QString prompt = "@mobiletongji:" + use.from->objectName();
        room->setPlayerFlag(use.from, "MobileTongjiSlashSource");
        player->setTag("mobiletongji-card", QVariant::fromValue(use.card)); // for the server (AI)
        room->setPlayerProperty(player, "mobiletongji", use.card->toString()); // for the client (UI)

        const bool used = room->askForUseCard(player, "@@mobiletongji", prompt, -1, Card::MethodDiscard);
        player->removeTag("mobiletongji-card");
        room->setPlayerProperty(player, "mobiletongji", "");
        room->setPlayerFlag(use.from, "-MobileTongjiSlashSource");
        return used;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
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
        return false;
    }

private:
    ServerPlayer *firstYuanshu(Room *room, ServerPlayer *player, const CardUseStruct &use) const
    {
        if (!use.card->isKindOf("Slash")) return nullptr;
        if (!use.to.contains(player) || !player->canDiscard(player, "he")) return nullptr;

        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName()) || p == use.from) continue;
            if (!player->inMyAttackRange(p) || !use.from->canSlash(p, use.card, false)) continue;
            return p;
        }
        return nullptr;
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
    related_skills.insert("nosluoyi", "#nosluoyi");
	
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
    related_skills.insert("kongcheng", "#kongcheng-effect");

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
    related_skills.insert("luoyi", "#luoyi");

    General *guojia = new General(this, "guojia*standard", "wei", 3); // WEI 006
    guojia->addSkill(new Tiandu);
    guojia->addSkill(new Yiji);
    guojia->addSkill(new YijiObtain);
    related_skills.insert("yiji", "#yiji");
	
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
    related_skills.insert("tieji", "#tieji-clear");

    General *huangyueying = new General(this, "huangyueying*standard", "shu", 3, false); // SHU 007
    huangyueying->addSkill(new Jizhi);
    huangyueying->addSkill(new Qicai);
    huangyueying->addSkill(new QicaiLimit);
    related_skills.insert("qicai", "#qicai-limit");

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
    related_skills.insert("zhaxiang", "#zhaxiang");
    related_skills.insert("zhaxiang", "#zhaxiang-target");

    General *zhouyu = new General(this, "zhouyu*standard", "wu", 3); // WU 005
    zhouyu->addSkill(new Yingzi);
    zhouyu->addSkill(new YingziMaxCards);
    zhouyu->addSkill(new Fanjian);
    related_skills.insert("yingzi", "#yingzi");

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

protected:
    bool canUseAtPlay(const Player *player) const override
    {
        return player->usedTimes("ZhihengCard") < (player->getLostHp() + 1);
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

class SuperMaxCards : public MaxCardsSkillV2
{
public:
    SuperMaxCards() : MaxCardsSkillV2("super_max_cards")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getMark("@max_cards_test"))
            : CorrectSkillResult::noEffect();
    }
};

class SuperOffensiveDistance : public DistanceSkillV2
{
public:
    SuperOffensiveDistance() : DistanceSkillV2("super_offensive_distance")
    {
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        int n = ctx.holder ? ctx.holder->getMark("@offensive_distance_test") : 0;
        return n > 0 ? CorrectSkillResult::useAmount(-n) : CorrectSkillResult::noEffect();
    }
};

class SuperDefensiveDistance : public DistanceSkillV2
{
public:
    SuperDefensiveDistance() : DistanceSkillV2("super_defensive_distance")
    {
        setHolderSelector(CorrectSkill_Secondary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        int n = ctx.holder ? ctx.holder->getMark("@defensive_distance_test") : 0;
        return n > 0 ? CorrectSkillResult::useAmount(n) : CorrectSkillResult::noEffect();
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

class GdJuejing : public TriggerSkillV2
{
public:
    GdJuejing() : TriggerSkillV2("gdjuejing")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *gaodayihao, QVariant &data) const override
    {
        if (!gaodayihao || !gaodayihao->isAlive() || !gaodayihao->hasSkill(objectName())) return {};
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != gaodayihao && move.to != gaodayihao)
            return {};
        if (move.to_place != Player::PlaceHand && !move.from_places.contains(Player::PlaceHand))
            return {};
        return gaodayihao->getHandcardNum() != 4 ? TriggerList{{gaodayihao, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *gaodayihao = ctx.owner;
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

class GdJuejingSkipDraw : public TriggerSkillV2
{
public:
    GdJuejingSkipDraw() : TriggerSkillV2("#gdjuejing")
    {
        events << DrawNCards;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent) const override
    {
        return 1;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *gaodayihao, QVariant &data) const override
    {
        return gaodayihao && gaodayihao->isAlive() && gaodayihao->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{gaodayihao, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        LogMessage log;
        log.type = "#GdJuejing";
        log.from = ctx.owner;
        log.arg = "gdjuejing";
        room->sendLog(log);

        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = 0;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
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

class GdLonghunDuojian : public TriggerSkillV2
{
public:
    GdLonghunDuojian() : TriggerSkillV2("#gdlonghun-duojian")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *gaodayihao, QVariant &) const override
    {
        return gaodayihao && gaodayihao->isAlive() && gaodayihao->hasSkill(objectName())
            && gaodayihao->getPhase() == Player::Start && swordHolder(room, gaodayihao)
            ? TriggerList{{gaodayihao, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return swordHolder(room, ctx.owner) && room->askForSkillInvoke(ctx.owner, "gdlonghun");
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *p = swordHolder(room, ctx.owner);
        if (!p) return false;
        room->broadcastSkillInvoke("gdlonghun", 5);
        ctx.owner->obtainCard(p->getWeapon());
        return false;
    }

private:
    static ServerPlayer *swordHolder(Room *room, ServerPlayer *gaodayihao)
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(gaodayihao)) {
            if (p->getWeapon() && p->getWeapon()->isKindOf("QinggangSword"))
                return p;
        }
        return nullptr;
    }
};

class Gepi : public TriggerSkillV2
{
public:
    Gepi() : TriggerSkillV2("gepi")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start) return list;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p != player && p->isAlive() && p->hasSkill(objectName()) && player->canDiscard(p, "he"))
                list[p] << objectName();
        }
        return list;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.invoker->canDiscard(ctx.owner, "he")
            && ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *p = ctx.owner, *player = ctx.invoker;
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
        return false;
    }
};

// Gepi's invalidity expires at turn end, independent of surviving skill instances.
class GepiReset : public TriggerSkillV2
{
public:
    GepiReset() : TriggerSkillV2("#gepi")
    {
        events << EventPhaseStart;
    }

    int getPriority(TriggerEvent) const override
    {
        return 6;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *target, QVariant &) const override
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
        return true;
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
    related_skills.insert("gdjuejing", "#gdjuejing");
    related_skills.insert("gdlonghun", "#gdlonghun-duojian");

    General *super_yuanshu = new General(this, "super_yuanshu", "qun", 4, true, true);
    super_yuanshu->addSkill(new SuperYongsi);
    super_yuanshu->addSkill(new MarkAssignSkill("@yongsi_test", 4));
    related_skills.insert("super_yongsi", "#@yongsi_test-4");
    super_yuanshu->addSkill("weidi");

    General *super_caoren = new General(this, "super_caoren", "wei", 4, true, true);
    super_caoren->addSkill(new SuperJushou);
    super_caoren->addSkill(new MarkAssignSkill("@jushou_test", 5));
    related_skills.insert("super_jushou", "#@jushou_test-5");

    General *nobenghuai_dongzhuo = new General(this, "nobenghuai_dongzhuo$", "qun", 4, true, true);
    nobenghuai_dongzhuo->addSkill("jiuchi");
    nobenghuai_dongzhuo->addSkill("roulin");
    nobenghuai_dongzhuo->addSkill("baonue");

    new General(this, "sujiang", "god", 5, true, true);
    new General(this, "sujiangf", "god", 5, false, true);

    new General(this, "anjiang", "god", 4, true, true, true);

    skills << new SuperMaxCards << new SuperOffensiveDistance << new SuperDefensiveDistance;
    skills << new Gepi << new GepiReset << new GepiInv;
    related_skills.insert("gepi", "#gepi");
    related_skills.insert("gepi", "#gepi-inv");
}
ADD_PACKAGE(Test)
