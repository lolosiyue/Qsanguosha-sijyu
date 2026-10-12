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
#include "card-lifetime-manager.h"
#include "room-state.h"
#include <QScopeGuard>

namespace {
// Nested response requests must not replace the enclosing ordinary card-use reason.
auto standardRequestScope()
{
    RoomState *state = Sanguosha->currentRoomState();
    const auto reason = state->getCurrentCardUseReason();
    const QString pattern = state->getCurrentCardUsePattern();
    return qScopeGuard([=] {
        state->setCurrentCardUseReason(reason);
        state->setCurrentCardUsePattern(pattern);
    });
}
// Pin nested responses to the invoking instance and unwind only transient prompt state.
auto standardPromptScope(Room *room, ServerPlayer *player, const SkillInstanceRef &entry, const QVariantMap &prompt)
{
    const QString name = entry.key.skillName;
    const int id = entry.key.instanceID;
    const QString selector = ViewAsSkillV2::borrowedActivationMarkName(name);
    const int previousSelector = player->getMark(selector);
    RoomState *state = Sanguosha->currentRoomState();
    const auto previousReason = state->getCurrentCardUseReason();
    const QString previousPattern = state->getCurrentCardUsePattern();
    const QVariant previousPrompt = player->getSkillInstanceStateValue(name, id, "prompt");
    room->setPlayerMark(player, selector, id);
    player->setSkillInstanceStateValue(name, id, "prompt", prompt);
    return qScopeGuard([=] {
        state->setCurrentCardUseReason(previousReason);
        state->setCurrentCardUsePattern(previousPattern);
        room->setPlayerMark(player, selector, previousSelector);
        if (!player->findSkillInstance(name, id)) return;
        if (previousPrompt.isValid()) player->setSkillInstanceStateValue(name, id, "prompt", previousPrompt);
        else player->removeSkillInstanceStateValue(name, id, "prompt");
    });
}
QVariantMap standardPrompt(const Player *player, const SkillInstanceRef &entry)
{
    return player ? player->getSkillInstanceStateValue(entry.key.skillName, entry.key.instanceID, "prompt").toMap() : QVariantMap();
}
QString standardReceiptProperty(const QString &name) { return name + "_v2_effects"; }
int nextStandardReceiptId(Room *room, ServerPlayer *recipient, const QString &name)
{
    // Source instance numbers are owner-local; continuations need their own stable key.
    const QByteArray key = (name + "_v2_receipt_sequence").toUtf8();
    const int id = recipient->property(key.constData()).toInt() + 1;
    room->setPlayerProperty(recipient, key.constData(), id);
    return id;
}
SkillInstanceRef standardReceiptRef(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("owner").toString(),
        SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
}
void addStandardReceipt(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx, int amount)
{
    if (!recipient || !ctx.sourceRef.isValid() || amount <= 0) return;
    const QString property = standardReceiptProperty(name);
    QVariantList receipts = recipient->property(property.toUtf8().constData()).toList();
    for (int i = 0; i < receipts.size(); ++i) {
        QVariantMap receipt = receipts.at(i).toMap();
        if (standardReceiptRef(receipt) != ctx.sourceRef) continue;
        receipt.insert("amount", receipt.value("amount").toInt() + amount);
        receipts[i] = receipt;
        room->setPlayerProperty(recipient, property.toUtf8().constData(), receipts);
        return;
    }
    receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
        {"instance", ctx.sourceRef.key.instanceID}, {"actor", recipient->objectName()}, {"amount", amount},
        {"dispatch", nextStandardReceiptId(room, recipient, name)}};
    room->setPlayerProperty(recipient, property.toUtf8().constData(), receipts);
}
bool hasStandardReceipt(const SkillContext &ctx, const QString &name)
{
    if (!ctx.owner || !ctx.owner->isAlive()) return false;
    for (const QVariant &entry : ctx.owner->property(standardReceiptProperty(name).toUtf8().constData()).toList()) {
        const QVariantMap receipt = entry.toMap();
        if (standardReceiptRef(receipt) == ctx.sourceRef && receipt.value("actor").toString() == ctx.owner->objectName()
            && receipt.value("amount").toInt() > 0) return true;
    }
    return false;
}
void collectStandardReceipts(const QString &definition, const QString &name, TriggerEvent event,
    ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts)
{
    if (!player || !player->isAlive()) return;
    for (const QVariant &entry : player->property(standardReceiptProperty(name).toUtf8().constData()).toList()) {
        const QVariantMap receipt = entry.toMap();
        const SkillInstanceRef source = standardReceiptRef(receipt);
        if (!source.isValid() || receipt.value("actor").toString() != player->objectName()
            || receipt.value("amount").toInt() <= 0) continue;
        SkillContext ctx;
        ctx.skill_name = definition;
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = source;
        // This is a previously applied effect, not a new activation of a removed skill.
        ctx.instanceID = receipt.value("dispatch").toInt();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
    }
}

}

ZhihengCard::ZhihengCard()
{
    setSkillName("zhiheng");
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
    setSkillName("yijue");
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
    setSkillName("jieyin");
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
    setSkillName("tuxi");
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
    setSkillName("fanjian");
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
    setSkillName("kurou");
    target_fixed = true;
}

void KurouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->loseHp(HpLostStruct(source, 1, "kurou", source));
}

LianyingCard::LianyingCard()
{
    setSkillName("lianying");
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
    setSkillName("lijian");
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
    setSkillName("chuli");
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
    setSkillName("liuli");
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
    CardLifetimeManager &manager = globalCardLifetimeManager();
    CardLifetimeLease lease(manager, manager.observeCard(const_cast<Card *>(slash)));
    const auto retire = qScopeGuard([slash] {
        if (slash && slash->isVirtualCard()) const_cast<Card *>(slash)->deleteLater();
    });
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
    setSkillName("fenwei");
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
    setSkillName("guose");
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
    setSkillName(jijiang);
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
    setSkillName("yiji");
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
    setSkillName("jianyan");
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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
            caocao->drawCards(getEffectiveAmount(ctx), objectName());
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
        && patterns.size() >= 2 && patterns.first() == "jink" && !patterns.at(1).contains("hujia-jink")
        && !room->getLieges("wei", caocao).isEmpty()
        ? TriggerList{{caocao, {objectName()}}} : TriggerList();
}

bool Hujia::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
}

bool Hujia::effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
    ctx.manual_effect = true;
    return skillEffect(event, room, player, ctx, ctx.owner);
}

bool Hujia::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const
{
    const auto requestScope = standardRequestScope();
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "TuxiCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target != request.initiator && !target->isKongcheng()
            && selected.size() < standardPrompt(request.initiator, request.activationRef).value("limit").toInt()
            && target->getHandcardNum() >= request.initiator->getHandcardNum();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "obtain") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("id").toInt();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
            if (!from || !from->handCards().contains(id)) return ContinueEffects;
            room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,
                target->objectName(), from->objectName(), objectName(), QString()), false);
            if (ctx.initiator) {
                QVariantMap prompt = standardPrompt(ctx.initiator, ctx.activationRef);
                prompt.insert("obtained", prompt.value("obtained").toInt() + 1);
                ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "prompt", prompt);
            }
            return ContinueEffects;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && !target->isKongcheng(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName());
            if (!target->handCards().contains(id)) continue;
            ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}};
            ctx.choice = "obtain";
            skillEffect(ctx, ctx.invoker);
            ctx.choice.clear();
        }
        return ContinueEffects;
    }
};

class Tuxi : public TriggerSkillV2
{
public:
    Tuxi() : TriggerSkillV2("tuxi")
    {
        events << DrawNCards;
        view_as_skill = new TuxiViewAsSkill;
    }

    bool usesEventPriority() const override { return true; }

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

    bool resolvePrompt(Room *room, SkillContext &ctx) const
    {
        ServerPlayer *zhangliao = ctx.owner;
        const int num = targetCount(room, zhangliao, ctx.original_data->value<DrawStruct>().num);
        if (num <= 0) return false;
        const auto prompt = standardPromptScope(room, zhangliao, ctx.activationRef, {{"limit", num}});
        const bool used = room->askForUseCard(zhangliao, "@@tuxi", "@tuxi-card:::" + QString::number(num));
        ctx.extra_data = standardPrompt(zhangliao, ctx.activationRef).value("obtained", 0);
        return used;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        // The nested active declaration pays only after the parent's effect interception.
        if (!resolvePrompt(room, ctx)) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = qMax(0, draw.num - ctx.extra_data.toInt());
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "YijiCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target != request.initiator
            && selected.size() < qMin(2, request.initiator->getHandcardNum());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || ctx.invoker->isKongcheng()) return ContinueEffects;
        int maximum = qMin(2, ctx.invoker->getHandcardNum());
        if (maximum == 2 && ctx.invoker->getHandcardNum() == 2 && ctx.targets.size() == 2
            && target == ctx.targets.first() && ctx.targets.last()->isAlive()) maximum = 1;
        const Card *cards = ctx.invoker->getRoom()->askForExchange(ctx.invoker, objectName(), maximum, 1,
            false, "YijiGive::" + target->objectName());
        if (cards) target->addToPile("yiji", cards, false);
        return ContinueEffects;
    }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *target = ctx.owner;
        const int times = ctx.original_data->value<DamageStruct>().damage;
        // One damage event owns the continuation choices; a declined point ends it.
        for (int i = 0; i < times; i++) {
            if (!target->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(target, objectName(), *ctx.original_data)) break;
            room->broadcastSkillInvoke(objectName());
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
            const auto prompt = standardPromptScope(room, target, ctx.activationRef, {});
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

    bool usesEventPriority() const override { return true; }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
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
            ctx.choice = judge.card->isRed() ? "red" : judge.card->isBlack() ? "black" : QString();
            skillEffect(event, room, callback, ctx, damage.from);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "red") room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        else if (ctx.choice == "black") {
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(id) == target && ctx.owner->canDiscard(target, id))
                    room->throwCard(id, target, ctx.owner);
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.owner;
        QList<int> ids = receivedIds(room, player, *ctx.original_data);
        const QVariant previous = player->getTag("QingjianCurrentMoveSkill");
        player->setTag("QingjianCurrentMoveSkill", ctx.original_data->value<CardsMoveOneTimeStruct>().reason.m_skillName);
        const auto restore = qScopeGuard([=] {
            if (previous.isValid()) player->setTag("QingjianCurrentMoveSkill", previous);
            else player->removeTag("QingjianCurrentMoveSkill");
        });
        while (player->isAlive() && !ids.isEmpty()) {
            const CardsMoveStruct move = room->askForYijiStruct(player, ids, objectName(), false, false, true, -1,
                {}, CardMoveReason(), "@qingjian-distribute", true, false);
            if (!move.to || move.card_ids.isEmpty()) break;
            ctx.extra_data = QVariant::fromValue(move);
            skillEffect(event, room, callback, ctx, room->findPlayerByObjectName(move.to->objectName()));
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardsMoveStruct move = ctx.extra_data.value<CardsMoveStruct>();
        QList<int> ids;
        for (int id : move.card_ids)
            if (room->getCardOwner(id) == ctx.owner && room->getCardPlace(id) == Player::PlaceHand) ids << id;
        if (ids.isEmpty()) return false;
        move.card_ids = ids;
        move.to = target;
        room->moveCardsAtomic(move, false);
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *simayi = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        // One damage event owns the continuation choices; a declined point ends it.
        for (int i = 0; i < damage.damage; i++) {
            if (!from || from->isNude() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(simayi, "fankui", QVariant::fromValue(from))) break;
            room->broadcastSkillInvoke(objectName());
            skillEffect(event, room, callback, ctx, from);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toInt();
            ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
            if (room->getCardOwner(id) == from
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->obtainCard(target, Sanguosha->getCard(id),
                    CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName()),
                    room->getCardPlace(id) != Player::PlaceHand);
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && !target->isNude(); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName());
            if (room->getCardOwner(id) != target) continue;
            ctx.extra_data = id;
            ctx.choice = "obtain";
            skillEffect(event, room, callback, ctx, ctx.owner);
            ctx.choice.clear();
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
        const auto requestScope = standardRequestScope();
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
            if (!c.isEmpty()) card = c.at(qsanRandomBounded(c.length()));
        }
        if (!card)
            return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        return ctx.extra_data.isValid() && id >= 0 && !Sanguosha->getCard(id)->hasFlag("using")
            && room->getCardOwner(id) == player
            && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        return judge && skillEffect(event, room, player, ctx, judge->who);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.extra_data.isValid()) return false;
        const int id = ctx.extra_data.toInt();
        if (id < 0 || Sanguosha->getCard(id)->hasFlag("using") || room->getCardOwner(id) != player
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        const Card *card = Sanguosha->getCard(id);
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
    explicit LuoyiBuff(const QString &name = "#luoyi", const QString &receipt = "luoyi", bool byUser = false)
        : TriggerSkillV2(name), receiptName(receipt), requireUserDamage(byUser)
    {
        events << DamageCaused;
        frequency = Compulsory;
        global = true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.chain && !damage.transfer && (!requireUserDamage || damage.by_user) && damage.card
            && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel")))
            collectStandardReceipts(objectName(), receiptName, event, player, data, contexts);
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override { return hasStandardReceipt(ctx, receiptName); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.original_data->value<DamageStruct>().to);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#LuoyiBuff";
        log.from = ctx.owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
private:
    const QString receiptName;
    const bool requireUserDamage;
};

class Luoyi : public TriggerSkillV2
{
public:
    Luoyi() : TriggerSkillV2("luoyi")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    bool usesEventPriority() const override { return true; }

    int getPriority(TriggerEvent triggerEvent) const override
    {
        if (triggerEvent == EventPhaseStart)
            return 4;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        // The buff expires even if Xu Chu lost the skill during the round.
        if (triggerEvent == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            room->setPlayerMark(player, "&luoyi", 0);
            room->setPlayerProperty(player, standardReceiptProperty(objectName()).toUtf8().constData(), QVariantList());
        }
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        player->skip(Player::Draw, true);
        addStandardReceipt(room, player, objectName(), ctx, getEffectiveAmount(ctx));
        room->setPlayerMark(player, "&luoyi", 1);

        QList<int> ids = room->getNCards(3 * getEffectiveAmount(ctx), false);
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "luoyi", ""));
        room->moveCardsAtomic(move, true);

        room->getThread()->delay();
        room->getThread()->delay();

        QList<int> card_to_throw;
        QList<int> card_to_gotback;
        for (int i = 0; i < ids.size(); i++) {
            if (room->getCardPlace(ids[i]) != Player::PlaceTable) continue;
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
        for (int i = card_to_gotback.size() - 1; i >= 0; --i)
            if (room->getCardPlace(card_to_gotback.at(i)) != Player::PlaceTable) card_to_gotback.removeAt(i);
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
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const JudgeStruct *judge = event == FinishJudge ? data.value<JudgeStruct *>() : nullptr;
        const bool matches = event == EventPhaseStart ? player->getPhase() == Player::Start
            : judge && judge->reason == objectName();
        return matches ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // FinishJudge belongs only to the exact Luoshen currently judging.
        return event == FinishJudge
            ? ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "judging", false).toBool()
            : ctx.owner->askForSkillInvoke(objectName());
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == FinishJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || !judge->card || !judge->card->isBlack()
                || room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge) return false;
            if (player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred", false).toBool()) {
                const QString key = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred_key").toString();
                if (key.isEmpty()) return false;
                QVariantList cards = player->getTag(key).toList();
                cards << judge->card->getEffectiveId();
                // Retain the applied selection before movement can remove the granting instance.
                player->setTag(key, cards);
                room->moveCardTo(judge->card, player, nullptr, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_JUDGEDONE, player->objectName(), "", objectName()), true);
            } else player->obtainCard(judge->card);
            return false;
        }
        const QVariant oldJudging = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "judging");
        const QVariant oldDeferred = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred");
        const QVariant oldKey = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred_key");
        const QString pendingKey = QString("luoshen_pending_%1_%2").arg(ctx.instanceID).arg(ctx.executionID);
        const QVariant previousPending = player->getTag(pendingKey);
        player->setTag(pendingKey, QVariantList());
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred_key", pendingKey);
        const bool deferred = player->hasSkills("guicai|nosguicai|guidao|huanshi");
        const bool oldFlag = player->hasFlag("LuoshenRetrial");
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "judging", true);
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred", deferred);
        if (deferred) player->setFlags("LuoshenRetrial"); // scoped AI hint
        const auto restore = qScopeGuard([=] {
            if (previousPending.isValid()) player->setTag(pendingKey, previousPending);
            else player->removeTag(pendingKey);
            if (!oldFlag) player->setFlags("-LuoshenRetrial");
            if (!player->findSkillInstance(objectName(), ctx.instanceID)) return;
            if (oldKey.isValid()) player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred_key", oldKey);
            else player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred_key");
            if (oldJudging.isValid()) player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "judging", oldJudging);
            else player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "judging");
            if (oldDeferred.isValid()) player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred", oldDeferred);
            else player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "deferred");
        });
        room->broadcastSkillInvoke(objectName());
        const auto settleDeferred = [&](bool grant) {
            QList<int> remaining;
            for (const QVariant &entry : player->getTag(pendingKey).toList()) {
                const int id = entry.toInt();
                if (room->getCardPlace(id) == Player::PlaceTable && !remaining.contains(id)) remaining << id;
            }
            if (remaining.isEmpty()) return;
            DummyCard cards(remaining);
            if (grant && player->isAlive()) player->obtainCard(&cards);
            else room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                player->objectName(), objectName(), QString()), nullptr);
        };
        try {
            bool first = true;
            while (player->isAlive() && isSourceAvailable(room, ctx)
                && (first || player->askForSkillInvoke(objectName()))) {
                first = false;
                JudgeStruct judge;
                judge.pattern = ".|black";
                judge.good = true;
                judge.reason = objectName();
                judge.who = player;
                room->judge(judge);
                if (judge.isBad()) break;
            }
        } catch (...) {
            // A turn interruption must not strand a deferred black judgment on the table.
            settleDeferred(false);
            throw;
        }
        settleDeferred(true);
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

RendeCard::RendeCard()
{
    setSkillName("rende");
    will_throw = false;
    handling_method = Card::MethodNone;
}

class RendeViewAsSkill : public ViewAsSkillV2 {
public:
    explicit RendeViewAsSkill(const QString &name = "rende") : ViewAsSkillV2(name) {}

    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->isKongcheng()) return false;
        const int given = player->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "given", 0).toInt();
        if (ServerInfo.GameMode == "04_1v3" && given >= 2) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return objectName() == "nosrende" || !player->getSkillInstanceStateValue(objectName(),
                request.activationRef.key.instanceID, "started", false).toBool();
        return objectName() == "rende" && request.pattern == "@@rende"
            && standardPrompt(player, request.activationRef).value("continuation").toBool();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return false;
        const int given = ctx.initiator->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "given", 0).toInt();
        if (ctx.initiator->getRoom()->getMode() == "04_1v3" && given >= 2) return false;
        return objectName() == "nosrende" || !ctx.initiator->getSkillInstanceStateValue(objectName(),
            ctx.activationRef.key.instanceID, "started", false).toBool()
            || standardPrompt(ctx.initiator, ctx.activationRef).value("continuation").toBool();
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return;
        const int id = ctx.activationRef.key.instanceID;
        const int given = ctx.initiator->getSkillInstanceStateValue(objectName(), id, "given", 0).toInt();
        ctx.initiator->setSkillInstanceStateValue(objectName(), id, "given", given + ctx.use_card->subcardsLength());
        ctx.initiator->setSkillInstanceStateValue(objectName(), id, "started", true);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using")) return false;
        const int given = request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "given", 0).toInt();
        return (ServerInfo.GameMode != "04_1v3" || request.selectedCardIds.size() + given < 2)
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
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "nosrende" ? "NosRendeCard" : "RendeCard"; }

    void commitAccepted(SkillContext &ctx) const
    {
        if (!ctx.initiator || !ctx.use_card) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        if (receipt.value("committed").toBool()) return;
        if (!receipt.contains("given")) receipt.insert("given",
            ctx.initiator->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "given", 0));
        receipt.insert("committed", true);
        ctx.extra_data = receipt;
        addUsage(ctx);
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !ctx.initiator) return false;
        ctx.extra_data = QVariantMap{{"given", ctx.initiator->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "given", 0)},
            {"committed", false}};
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.invoker || ctx.targets.size() != 1 || !cardSelectionFeasible(request)) return false;
        ServerPlayer *target = ctx.targets.first();
        if (!target || !target->isAlive()) return false;
        // Reserve the custom quota before the giving move can trigger another activation.
        commitAccepted(ctx);
        DummyCard gift(request.selectedCardIds);
        room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(),
            target->objectName(), objectName(), QString()), false);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.initiator || !ctx.use_card) return FinishSkill;
        // Custom quotas are not committed by the core; waived payment still consumes this activation.
        commitAccepted(ctx);
        const int oldCount = ctx.extra_data.toMap().value("given").toInt();
        const int newCount = oldCount + ctx.use_card->subcardsLength();
        if (oldCount < 2 && newCount >= 2 && ctx.invoker->isAlive()) {
            ctx.manual_effect = true;
            skillEffect(ctx, ctx.invoker);
        }
        if (objectName() == "rende" && ctx.initiator->isAlive() && !ctx.initiator->isKongcheng()
            && ctx.initiator->findSkillInstance(objectName(), ctx.activationRef.key.instanceID)
            && (ctx.initiator->getRoom()->getMode() != "04_1v3" || newCount < 2)) {
            const auto prompt = standardPromptScope(ctx.initiator->getRoom(), ctx.initiator,
                ctx.activationRef, {{"continuation", true}});
            ctx.initiator->getRoom()->askForUseCard(ctx.initiator, "@@rende", "@rende-give", -1, Card::MethodNone, false);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        RecoverStruct recover(objectName(), ctx.invoker);
        recover.recover = getEffectiveAmount(ctx);
        target->getRoom()->recover(target, recover);
        return ContinueEffects;
    }
};

class Rende : public TriggerSkillV2 {
public:
    Rende() : TriggerSkillV2("rende")
    {
        events << EventPhaseChanging << EventSkillInvoking;
        global = true;
        view_as_skill = new RendeViewAsSkill;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext active = data.value<SkillContext>();
            if (active.activationRef.key.skillName == objectName() && active.use_card
                && active.use_card->getTypeId() == Card::TypeSkill) {
                static_cast<const RendeViewAsSkill *>(view_as_skill)->commitAccepted(active);
                data = QVariant::fromValue(active);
            }
            return true;
        }
        return false;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging) return;
        if (!player || ctx.owner != player || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().from != Player::Play) return;
        player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "given");
        player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "started");
        room->setPlayerMark(player, objectName(), 0);
        room->setPlayerMark(player, "rende-PlayClear", 0);
    }
};

JijiangViewAsSkill::JijiangViewAsSkill() : ViewAsSkillV2("jijiang$")
{
}

bool JijiangViewAsSkill::isEnabledAtPlay(const Player *player) const
{
    return player && hasShuGenerals(player) && Slash::IsAvailable(player);
}

bool JijiangViewAsSkill::isEnabledAtResponse(const Player *player, const QString &pattern) const
{
    return player && hasShuGenerals(player)
        && (pattern.contains("slash") || pattern.contains("Slash") || pattern == "@jijiang")
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
}

bool JijiangViewAsSkill::canActivate(const ActiveSkillRequest &request) const
{
    if (!request.initiator || request.initiator->getSkillInstanceStateValue(objectName(),
        request.activationRef.key.instanceID, "failed", false).toBool()) return false;
    if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return isEnabledAtPlay(request.initiator);
    return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && hasShuGenerals(request.initiator)
        && (request.pattern.contains("slash", Qt::CaseInsensitive) || request.pattern == "@jijiang");
}

const Card *JijiangViewAsSkill::createCard(const ActiveSkillRequest &request) const
{
    if (!cardSelectionFeasible(request)) return nullptr;
    // The ordinary preview supplies Slash targeting; liege responses are paid later.
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName(objectName());
    return slash;
}

bool JijiangViewAsSkill::pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const
{
    const auto requestScope = standardRequestScope();
    ServerPlayer *liubei = ctx.invoker;
    if (!liubei || !liubei->isAlive()) return false;
    QList<ServerPlayer *> marked;
    for (ServerPlayer *target : ctx.targets) {
        if (!target->hasFlag("JijiangTarget")) {
            target->setFlags("JijiangTarget");
            marked << target;
        }
    }
    const auto restoreHints = qScopeGuard([marked] {
        for (ServerPlayer *target : marked) target->setFlags("-JijiangTarget");
    });
    for (ServerPlayer *liege : room->getLieges("shu", liubei)) {
        const Card *provided = room->askForCard(liege, "slash", "@jijiang-slash:" + liubei->objectName(),
            QVariant::fromValue(liubei), Card::MethodResponse, liubei, false, "", true);
        if (!provided) continue;
        Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
        if (!slash) return false;
        // Own a copy beyond the nested response; provenance stays with this activation.
        slash->addSubcard(provided);
        slash->setSkillName(objectName());
        slash->setFlags("YUANBEN");
        slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        slash->deleteLater();
        ctx.updated_card = slash;
        if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            use.m_isOwnerUse = false;
            *ctx.original_data = QVariant::fromValue(use);
        }
        return true;
    }
    if (ctx.initiator)
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "failed", true);
    return false;
}

bool JijiangViewAsSkill::hasShuGenerals(const Player *player)
{
    foreach(const Player *p, player->getAliveSiblings())
        if (p->getKingdom() == "shu")
            return true;
    return false;
}

// CardAsked provision uses the same recipient boundary as ordinary Jijiang uses.
class Jijiang : public TriggerSkillV2
{
public:
    Jijiang() : TriggerSkillV2("jijiang$")
    {
        events << CardAsked << EventPhaseChanging;
        view_as_skill = new JijiangViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *liubei, QVariant &data) const override
    {
        return event == CardAsked && liubei && liubei->hasLordSkill("jijiang") && canAsk(room, liubei, data)
            ? TriggerList{{liubei, {objectName()}}} : TriggerList();
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && player == ctx.owner)
            ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "failed");
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *liubei, SkillContext &ctx) const override
    {
        return canAsk(room, liubei, *ctx.original_data)
            && room->askForSkillInvoke(liubei, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *liubei, SkillContext &, ServerPlayer *) const override
    {
        const auto requestScope = standardRequestScope();
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
        return patterns.size() >= 3 && patterns.first() == "slash" && patterns.at(2) == "response" && !patterns.at(1).contains("jijiang-slash")
            && !room->getLieges("shu", liubei).isEmpty();
    }
};

class Wusheng : public ViewAsSkillV2 {
public:
    Wusheng() : ViewAsSkillV2("wusheng", 1) { setResponseOrUse(true); }
    QStringList aiConversionStages() const override
    { return {"material", "output", "targets"}; }
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "YijueCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && selected.isEmpty() && request.initiator->canPindian(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || !from->canPindian(target)) return ContinueEffects;
        Room *room = from->getRoom();
        if (from->pindian(target, objectName(), nullptr)) {
            // These public receipts survive the source's removal until the turn expires.
            target->addMark("yijue");
            room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true, objectName());
            room->addPlayerMark(target, "@skill_invalidity");
            for (ServerPlayer *p : room->getAllPlayers()) room->filterCards(p, p->getCards("he"), true);
            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        } else if (target->isWounded()) {
            const bool oldFlag = target->hasFlag("YijueTarget");
            target->setFlags("YijueTarget");
            const auto restore = qScopeGuard([target, oldFlag] { if (!oldFlag) target->setFlags("-YijueTarget"); });
            if (room->askForChoice(from, objectName(), "recover+cancel") == "recover") {
                RecoverStruct recover(objectName(), from);
                recover.recover = getEffectiveAmount(ctx);
                room->recover(target, recover);
            }
        }
        return ContinueEffects;
    }

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

    bool usesEventPriority() const override { return true; }

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

            room->removePlayerCardLimitationByReason(player, "yijue");
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
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};

class Tishen : public TriggerSkillV2
{
public:
    Tishen() : TriggerSkillV2("tishen")
    {
        events << EventPhaseStart << EventSkillInvoking;
        global = true;
        frequency = Limited;
        limit_mark = "@substitute";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventSkillInvoking) {
            SkillContext active = data.value<SkillContext>();
            if (active.activationRef.key.skillName == objectName() && !active.use_card) {
                commitAccepted(active);
                data = QVariant::fromValue(active);
            }
            return true;
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return triggerEvent == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && recoverNum(player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.extra_data = recoverNum(ctx.owner);
        return ctx.extra_data.toInt() > 0 && room->askForSkillInvoke(ctx.owner, objectName(), ctx.extra_data);
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Each copy owns a game quota; the public token remains a presentation hint.
        if (!isUsable(ctx)) return false;
        commitAccepted(ctx);
        if (ctx.owner->getMark("@substitute") > 0) room->removePlayerMark(ctx.owner, "@substitute");
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        commitAccepted(ctx);
        if (!ctx.extra_data.isValid()) ctx.extra_data = recoverNum(ctx.owner);
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.owner;
        const int x = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        room->broadcastSkillInvoke(objectName());
        //room->doLightbox("$TishenAnimate");
        room->doSuperLightbox(player, "tishen");

        qint64 recoveryId = 0;
        room->recover(player, RecoverStruct(player, nullptr, x, objectName()), true, &recoveryId);
        if (recoveryId <= 0 || !player->isAlive()) return false;
        const QVariantMap history = room->queryHistoryFacts({{"kind", "actual_recover"},
            {"event_id", recoveryId}, {"to", player->objectName()}});
        if (!history.value("complete").toBool() || history.value("has_more").toBool()) return false;
        int recovered = 0;
        for (const QVariant &entry : history.value("items").toList())
            recovered += qMax(0, entry.toMap().value("data").toMap().value("amount").toInt());
        // The exact setter receipt excludes recoveries nested in HpChanged callbacks.
        if (recovered > 0) player->drawCards(recovered, objectName());
        return false;
    }

private:
    void commitAccepted(SkillContext &ctx) const
    {
        QVariantMap receipt = ctx.interceptor_data.value("tishen_quota");
        if (receipt.value("committed").toBool()) return;
        receipt.insert("committed", true);
        ctx.interceptor_data.insert("tishen_quota", receipt);
        addUsage(ctx);
    }
    static int recoverNum(ServerPlayer *player)
    {
        if (!player) return 0;
        Room *room = player->getRoom();
        const qint64 currentTurn = room->historyScopes().value("turn_id").toLongLong();
        if (currentTurn <= 0) return 0;
        qint64 previousTurn = 0;
        QVariantMap turns{{"kind", "turn"}, {"player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryEvents(turns);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return 0;
            for (const QVariant &entry : page.value("items").toList()) {
                const qint64 id = entry.toMap().value("id").toLongLong();
                if (id > previousTurn && id < currentTurn) previousTurn = id;
            }
            if (!page.value("has_more").toBool()) break;
            turns.insert("after", page.value("next_after"));
            turns.insert("watermark", page.value("watermark"));
        }
        if (previousTurn <= 0) return 0;
        bool found = false;
        int previousHp = 0;
        QVariantMap facts{{"kind", "turn_hp_snapshot"}, {"event_id", previousTurn}, {"player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(facts);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return 0;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap().value("data").toMap();
                if (fact.value("boundary").toString() != "end"
                    || fact.value("turn_owner").toString() != player->objectName() || !fact.contains("hp")) continue;
                previousHp = fact.value("hp").toInt();
                found = true;
            }
            if (!page.value("has_more").toBool()) break;
            facts.insert("after", page.value("next_after"));
            facts.insert("watermark", page.value("watermark"));
        }
        // Missing the previous own turn's endpoint is unknown, never an older snapshot fallback.
        return found ? qMax(0, qMin(previousHp - player->getHp(), player->getMaxHp() - player->getHp())) : 0;
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

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.owner;
        const Card *cardstar = triggerEvent == CardUsed ? ctx.original_data->value<CardUseStruct>().card
            : ctx.original_data->value<CardResponseStruct>().m_card;
        if (!cardstar) return false;
        const int type = cardstar->getTypeId();
        room->broadcastSkillInvoke(objectName());
        QList<int> ids = room->getNCards(1, false);
        if (ids.isEmpty()) return false;
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "yajiao", ""));
        room->moveCardsAtomic(move, true);
        int id = ids.first();

        const Card *card = Sanguosha->getCard(id);
        if (room->getCardPlace(id) != Player::PlaceTable) return false;
        if (card->getTypeId() == type) {
            const int oldMark = player->getMark("yajiao");
            player->setMark("yajiao", id); // Scoped AI projection.
            const auto restore = qScopeGuard([=] { player->setMark("yajiao", oldMark); });
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                QString("@yajiao-give:::%1:%2\\%3").arg(card->objectName())
                .arg(card->getSuitString() + "_char")
                .arg(card->getNumberString()),
                true);
            if (target) {
                ctx.extra_data = id;
                skillEffect(triggerEvent, room, callback, ctx, target);
                if (room->getCardPlace(id) != Player::PlaceTable) return false;
            }
        } else {
            if (room->askForChoice(player, objectName(), "throw+cancel", QVariant::fromValue(card)) == "throw"
                && room->getCardPlace(id) == Player::PlaceTable) {
                CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "yajiao", "");
                room->throwCard(card, reason, nullptr);
                return false;
            }
        }
        if (room->getCardPlace(id) == Player::PlaceTable)
            room->returnToTopDrawPile(ids);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardPlace(id) == Player::PlaceTable) room->obtainCard(target, id, true);
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const int first = ctx.extra_data.toInt();
        for (int index = first; index < use.to.size(); ++index) {
            ServerPlayer *target = use.to.at(index);
            if (!ctx.owner->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (!target->isAlive()) continue;
            if (index > first && !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) continue;
            skillEffect(event, room, callback, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.to.contains(target)) return false;
        room->broadcastSkillInvoke(objectName());
        target->addMark("tieji");
        room->addPlayerMark(target, "@skill_invalidity");
        for (ServerPlayer *p : room->getAllPlayers()) room->filterCards(p, p->getCards("he"), true);
        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        JudgeStruct judge;
        judge.pattern = ".";
        judge.good = true;
        judge.reason = objectName();
        judge.who = player;
        judge.play_animation = false;
        room->judge(judge);
        const auto requestScope = standardRequestScope();
        if (target->isAlive() && (!target->canDiscard(target, "he")
            || !room->askForCard(target, ".|" + judge.pattern, "@tieji-discard:::" + judge.pattern,
                *ctx.original_data, Card::MethodDiscard))) {
            LogMessage log;
            log.type = "#NoJink";
            log.from = target;
            room->sendLog(log);
            // Nested callbacks may modify other targets' requirements; merge this one only.
            QVariantList jinks = player->getTag("Jink_" + use.card->toString()).toList();
            const int index = ctx.original_data->value<CardUseStruct>().to.indexOf(target);
            if (index >= 0 && index < jinks.size()) jinks[index] = 0;
            player->setTag("Jink_" + use.card->toString(), jinks);
        }
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

    bool usesEventPriority() const override { return true; }

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

bool Guanxing::effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
    ctx.manual_effect = true;
    return skillEffect(event, room, player, ctx, ctx.owner);
}

bool Guanxing::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *yueying, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, yueying, ctx, yueying);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &ctx, ServerPlayer *) const override
    {
        if (yueying->getMark("JilveEvent") > 0)
            room->broadcastSkillInvoke("jilve", 5);
        else
            room->broadcastSkillInvoke(objectName());

        const auto requestScope = standardRequestScope();
        for (int i = 0; i < getEffectiveAmount(ctx) && yueying->isAlive(); ++i) {
        QList<int> ids = room->getNCards(1, false);
        if (ids.isEmpty()) break;
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, yueying->objectName(), "jizhi", ""));
        room->moveCardsAtomic(move, true);

        if (room->getCardPlace(ids.first()) != Player::PlaceTable) continue;
        const Card *card = Sanguosha->getCard(ids.first());
        if (card->isKindOf("BasicCard")) {
            const Card *card_ex = nullptr;
            if (!yueying->isKongcheng())
                card_ex = room->askForCard(yueying, ".", "@jizhi-exchange:::" + card->objectName(),
                QVariant::fromValue(card), Card::MethodNone);
            if (room->getCardPlace(ids.first()) != Player::PlaceTable) continue;
            if (card_ex && room->getCardOwner(card_ex->getEffectiveId()) == yueying
                && room->getCardPlace(card_ex->getEffectiveId()) == Player::PlaceHand) {
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
        return "discard";// Set the discard limitation.
    }

    QString limitPattern(const Player *target, const Card *card) const
    {
		if(card->isKindOf("Horse")) return "";
		foreach (const Player *p, target->getAliveSiblings()) {// Iterate over other players.
			if (p->getEquipsId().contains(card->getId())// The card is in this player's equipment area.
				&&p->hasSkill("qicai"))// This player has Qicai.
				return card->toString();// Therefore, the target cannot discard this card.
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
            || !dealtDamageThisTurn(room, player) || player->getPhase() != Player::Finish) return list;
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (p->isAlive() && p->hasSkill(objectName()) && p != player && p->canSlash(player, false))
                list[p] << objectName();
        }
        return list;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const auto requestScope = standardRequestScope();
        ServerPlayer *p = ctx.owner, *player = ctx.invoker;
        if (player->isDead() || !p->canSlash(player, false)) return false;
        const bool oldFlag = p->hasFlag("ZhuhaiSlash");
        p->setFlags("ZhuhaiSlash");
        const auto restore = qScopeGuard([p, oldFlag] { if (!oldFlag) p->setFlags("-ZhuhaiSlash"); });
        QString prompt = QString("@zhuhai-slash:%1:%2").arg(p->objectName()).arg(player->objectName());
        // The accepted slash is the whole effect; it is announced at PreCardUsed.
        room->askForUseSlashTo(p, target, prompt, false);
        p->setFlags("-ZhuhaiSlash");
        return false;
    }
private:
    static bool dealtDamageThisTurn(Room *room, ServerPlayer *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() == 0) return false;
        QVariantMap filter{{"turn_id", turn}, {"from", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList())
                if (entry.toMap().value("data").toMap().value("amount").toInt() > 0) return true;
            if (!page.value("has_more").toBool()) return false;
            filter.insert("after", page.value("next_after"));
            filter.insert("watermark", page.value("watermark"));
        }
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

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive()
            && player->hasSkill(objectName())
            // canWake() consumes its grant, so selection only peeks at it.
            && (player->isWounded() || !player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->isWounded()) {
            LogMessage log;
            log.type = "#QianxinWake";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
        }else if(!player->canWake(objectName()))
			return false;
        addUsage(ctx);
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
           ;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JianyanCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), "basic+trick+equip+red+black");
        return QStringList{"basic", "trick", "equip", "red", "black"}.contains(ctx.choice);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || !from->isAlive()) return FinishSkill;
        Room *room = from->getRoom();
        const QStringList choices{"basic", "trick", "equip", "red", "black"};
        const QStringList patterns{"BasicCard", "TrickCard", "EquipCard", ".|red", ".|black"};
        const int choice = choices.indexOf(ctx.choice);
        if (choice < 0) return FinishSkill;
        QList<int> revealed;
        while (from->isAlive()) {
            const int id = room->drawCard();
            if (id < 0) break;
            revealed << id;
            room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, from->objectName(), objectName(), QString())), true);
            const Card *card = Sanguosha->getCard(id);
            if (!Sanguosha->matchExpPattern(patterns.at(choice), nullptr, card)) continue;
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *p : room->getAlivePlayers()) if (p->isMale()) candidates << p;
            if (!candidates.isEmpty()) {
                const int oldHint = from->getMark("jianyan");
                from->setMark("jianyan", id);
                const auto restore = qScopeGuard([from, oldHint] { from->setMark("jianyan", oldHint); });
                ServerPlayer *target = room->askForPlayerChosen(from, candidates, objectName(),
                    QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
                        .arg(card->getSuitString() + "_char").arg(card->getNumberString()));
                if (target && target->isAlive() && room->getCardPlace(id) == Player::PlaceTable) {
                    ctx.extra_data = id;
                    skillEffect(ctx, target);
                }
            }
            break;
        }
        QList<int> remaining;
        for (int id : revealed) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        if (!remaining.isEmpty()) {
            DummyCard discard(remaining);
            room->throwCard(&discard, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                from->objectName(), objectName(), QString()), nullptr);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (target->getRoom()->getCardPlace(id) == Player::PlaceTable)
            target->obtainCard(Sanguosha->getCard(id));
        return ContinueEffects;
    }

};

// Jilve answers "@@zhiheng" through a Room::BorrowedSkillScope instance.
class Zhiheng : public ViewAsSkillV2
{
public:
    Zhiheng() : ViewAsSkillV2("zhiheng") {}

    LimitScope getLimitScope() const override { return Limit_Phase; }

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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhihengCard"; }

protected:
    virtual bool canUseAtPlay(const Player *) const { return true; }
public:
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            ctx.invoker->drawCards(ctx.use_card->subcardsLength() * getEffectiveAmount(ctx), objectName());
        return FinishSkill;
    }

};

class Jiuyuan : public TriggerSkillV2
{
public:
    Jiuyuan() : TriggerSkillV2("jiuyuan$")
    {
        events << TargetConfirmed << PreHpRecover << CardFinished;
        frequency = Compulsory;
        global = true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *sunquan, QVariant &data) const override
    {
        if (event != TargetConfirmed || !sunquan || !sunquan->hasLordSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.card->isKindOf("Peach") && use.from && use.from != sunquan
            && use.from->getKingdom() == "wu" && use.to.contains(sunquan) && sunquan->hasFlag("Global_Dying")
            ? TriggerList{{sunquan, {objectName()}}} : TriggerList();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && !receiptKey(room).isEmpty()) use.card->removeTag(receiptKey(room));
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != PreHpRecover) return false;
        const RecoverStruct recover = data.value<RecoverStruct>();
        if (!player || !player->isAlive() || !recover.card || receiptKey(room).isEmpty()) return true;
        for (const QVariant &entry : recover.card->getTag(receiptKey(room)).toList()) {
            const QVariantMap receipt = entry.toMap();
            const SkillInstanceRef source = standardReceiptRef(receipt);
            if (receipt.value("actor").toString() != player->objectName() || !source.isValid()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = source;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
        if (!recover.card || receiptKey(room).isEmpty()) return false;
        for (const QVariant &entry : recover.card->getTag(receiptKey(room)).toList()) {
            const QVariantMap receipt = entry.toMap();
            if (standardReceiptRef(receipt) == ctx.sourceRef && receipt.value("actor").toString() == ctx.owner->objectName())
                return true;
        }
        return false;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (receiptKey(room).isEmpty()) return false;
        if (event == TargetConfirmed) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            QVariantList receipts = use.card->getTag(receiptKey(room)).toList();
            bool merged = false;
            for (int i = 0; i < receipts.size(); ++i) {
                QVariantMap receipt = receipts.at(i).toMap();
                if (standardReceiptRef(receipt) != ctx.sourceRef
                    || receipt.value("actor").toString() != ctx.owner->objectName()) continue;
                receipt.insert("amount", receipt.value("amount").toInt() + getEffectiveAmount(ctx));
                receipts[i] = receipt;
                merged = true;
                break;
            }
            if (!merged) receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"actor", ctx.owner->objectName()}, {"amount", getEffectiveAmount(ctx)},
                {"dispatch", nextStandardReceiptId(room, ctx.owner, objectName())}};
            use.card->setTag(receiptKey(room), receipts);
        } else {
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(ctx.owner, objectName());
            recover.recover += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(recover);
        }
        return false;
    }
private:
    static QString receiptKey(Room *room)
    {
        const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        return id > 0 ? "jiuyuan:" + QString::number(id) : QString();
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
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "FanjianCard"; }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.initiator || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (!ctx.initiator->handCards().contains(id)) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const Card::Suit suit = Sanguosha->getCard(id)->getSuit();
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), QString());
        room->obtainCard(target, Sanguosha->getCard(id), reason);
        const int oldSuit = target->getMark("FanjianSuit");
        target->setMark("FanjianSuit", int(suit)); // Temporary AI projection only.
        const auto restore = qScopeGuard([target, oldSuit] { target->setMark("FanjianSuit", oldSuit); });
        if (!target->isNude() && target->askForSkillInvoke("fanjian_discard", "prompt:::" + Card::Suit2String(suit))) {
            room->showAllCards(target);
            DummyCard discard;
            for (const Card *card : target->getCards("he"))
                if (card->getSuit() == suit && target->canDiscard(target, card->getEffectiveId())) discard.addSubcard(card);
            if (discard.subcardsLength() > 0) room->throwCard(&discard, target);
        } else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return ContinueEffects;
    }

};

class Keji : public TriggerSkillV2
{
public:
    Keji() : TriggerSkillV2("keji")
    {
        events << EventPhaseChanging;
        frequency = Frequent;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Discard
            // Include both uses and pure responses in every Play phase of this turn.
            // An incomplete journal returns -1 and cannot prove that no Slash occurred.
            && room->countHistoryCards(player, "turn", "Slash", true, true) == 0
            && room->countHistoryCards(player, "turn", "Slash", false, true) == 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *lvmeng = ctx.owner;
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

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
            && player->hasSkill(objectName())
            // canWake() consumes its grant, so selection only peeks at it.
            && (meetsCondition(room, player) || !player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *lvmeng = ctx.owner;
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
        addUsage(ctx);
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "FenweiCard"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &, const Player *target) const override
    {
        return target && standardPrompt(request.initiator, request.activationRef).value("candidates").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator) return false;
        // The common limited mark is only a presentation hint; quota belongs to this instance.
        if (ctx.initiator->getMark("@fenwei") > 0) room->removePlayerMark(ctx.initiator, "@fenwei");
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator) return ContinueEffects;
        QVariantMap prompt = standardPrompt(ctx.initiator, ctx.activationRef);
        QStringList chosen = prompt.value("chosen").toStringList();
        chosen << target->objectName();
        prompt.insert("chosen", chosen);
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "prompt", prompt);
        return ContinueEffects;
    }
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

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.length() <= 1 || !use.card->isNDTrick())
            return list;
        foreach (ServerPlayer *ganning, room->findPlayersBySkillName(objectName())) {
            if (ganning->isAlive() && ganning->hasSkill(objectName()))
                list[ganning] << objectName();
        }
        return list;
    }

    bool resolvePrompt(Room *room, SkillContext &ctx) const
    {
        ServerPlayer *ganning = ctx.owner;
        QStringList target_list;
        foreach(ServerPlayer *p, ctx.original_data->value<CardUseStruct>().to)
            target_list << p->objectName();
        const auto prompt = standardPromptScope(room, ganning, ctx.activationRef, {{"candidates", target_list}});
        const QVariant previousProperty = ganning->property("fenwei_targets");
        const QVariant previousTag = ganning->getTag("fenwei");
        room->setPlayerProperty(ganning, "fenwei_targets", target_list.join("+"));
        ganning->setTag("fenwei", *ctx.original_data); // scoped AI projection only
        const auto restore = qScopeGuard([=] {
            room->setPlayerProperty(ganning, "fenwei_targets", previousProperty);
            if (previousTag.isValid()) ganning->setTag("fenwei", previousTag);
            else ganning->removeTag("fenwei");
        });
        const bool used = room->askForUseCard(ganning, "@@fenwei", "@fenwei-card");
        ctx.extra_data = standardPrompt(ganning, ctx.activationRef).value("chosen");
        return used;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The nested active declaration pays only after the parent's effect interception.
        if (!resolvePrompt(room, ctx)) return false;
        ctx.manual_effect = true;
        for (const QString &name : ctx.extra_data.toStringList())
            skillEffect(event, room, ctx.owner, ctx, room->findPlayerByObjectName(name));
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
           ;
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "KurouCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            ctx.invoker->getRoom()->loseHp(HpLostStruct(ctx.invoker, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return FinishSkill;
    }

};

class Zhaxiang : public TriggerSkillV2
{
public:
    Zhaxiang() : TriggerSkillV2("zhaxiang")
    {
        events << HpLost << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool usesEventPriority() const override { return true; }

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
            if (player && (change.to == Player::NotActive || change.to == Player::RoundStart)) {
                room->setPlayerMark(player, objectName(), 0);
                room->setPlayerProperty(player, standardReceiptProperty(objectName()).toUtf8().constData(), QVariantList());
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return triggerEvent == HpLost && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.owner;
        int lose = ctx.original_data->value<HpLostStruct>().lose;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());

        for (int i = 0; i < lose; i++) {
            player->drawCards(3 * getEffectiveAmount(ctx), objectName());
            if (player->getPhase() == Player::Play) {
                addStandardReceipt(room, player, objectName(), ctx, getEffectiveAmount(ctx));
                room->addPlayerMark(player, objectName(), getEffectiveAmount(ctx));
            }
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
        global = true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->isKindOf("Slash") && use.card->isRed())
            collectStandardReceipts(objectName(), "zhaxiang", event, player, data, contexts);
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override { return hasStandardReceipt(ctx, "zhaxiang"); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const QList<ServerPlayer *> targets = ctx.original_data->value<CardUseStruct>().to;
        for (ServerPlayer *target : targets) skillEffect(event, room, player, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QVariantList jinks = ctx.owner->getTag("Jink_" + use.card->toString()).toList();
        const int index = use.to.indexOf(target);
        if (index >= 0 && index < jinks.size()) jinks[index] = 0;
        ctx.owner->setTag("Jink_" + use.card->toString(), jinks);
        return false;
    }
};

class ZhaxiangTargetMod : public TargetModSkillV2
{
public:
    ZhaxiangTargetMod() : TargetModSkillV2("#zhaxiang-target")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        int mark = 0;
        if (ctx.primary)
            for (const QVariant &entry : ctx.primary->property("zhaxiang_v2_effects").toList())
                mark += qMax(0, entry.toMap().value("amount").toInt());
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !(request.initiator->isNude() && request.initiator->getHandPile().isEmpty());
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
        return ViewAsSkillV2::createCard(request);
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "GuoseCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || !selected.isEmpty() || request.selectedCardIds.size() != 1) return false;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        if (target->containsTrick("indulgence")) {
            if (!request.initiator->canDiscard(request.initiator, material->getEffectiveId())) return false;
            for (const Card *judge : target->getJudgingArea())
                if (judge->isKindOf("Indulgence") && request.initiator->canDiscard(target, judge->getEffectiveId())) return true;
            return false;
        }
        Indulgence indulgence(material->getSuit(), material->getNumber());
        indulgence.addSubcard(material);
        indulgence.setSkillName(objectName());
        return target != request.initiator && !request.initiator->isLocked(&indulgence)
            && !request.initiator->isProhibited(target, &indulgence);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (ctx.targets.size() != 1 || !cardSelectionFeasible(request)) return false;
        if (ctx.targets.first()->containsTrick("indulgence")) return true;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Indulgence *indulgence = new Indulgence(material->getSuit(), material->getNumber());
        indulgence->addSubcard(material);
        indulgence->setSkillName(objectName());
        indulgence->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        indulgence->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        indulgence->deleteLater();
        ctx.updated_card = indulgence;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        // Applying Indulgence pays its diamond through the ordinary card pipeline.
        if (ctx.updated_card && ctx.updated_card->isKindOf("Indulgence")) return true;
        if (!ctx.initiator || request.selectedCardIds.size() != 1
            || !ctx.initiator->canDiscard(ctx.initiator, request.selectedCardIds.first())) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker) return ContinueEffects;
        for (const Card *judge : target->getJudgingArea()) {
            if (judge->isKindOf("Indulgence") && ctx.invoker->canDiscard(target, judge->getEffectiveId())) {
                target->getRoom()->throwCard(judge, nullptr, ctx.invoker);
                break;
            }
        }
        return ContinueEffects;
    }
};

// CardFinished observes one completed card, even if its skill was lost during use.
class Guose : public TriggerSkillV2
{
public:
    Guose() : TriggerSkillV2("guose")
    {
        events << CardFinished;
        view_as_skill = new GuoseViewAsSkill;
        global = true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.from->isAlive() || !use.card || !use.card->getSkillNames().contains(objectName())
            || !use.sourceRef.isValid()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = use.from;
        ctx.sourceRef = use.sourceRef;
        ctx.instanceID = use.activationRef.isValid() ? use.activationRef.key.instanceID : use.sourceRef.key.instanceID;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.setModifiedAmount(use.skillExecutionID > 0
            ? getEffectiveAmount(room->getSkillExecutionContext(use.skillExecutionID)) : getBaseAmount());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.from == ctx.owner && use.sourceRef == ctx.sourceRef;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *card) const override { return card->isKindOf("Indulgence") ? 1 : 2; }
};

class LiuliViewAsSkill : public ViewAsSkillV2
{
public:
    LiuliViewAsSkill() : ViewAsSkillV2("liuli", 1) {  }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@liuli"
            && !standardPrompt(request.initiator, request.activationRef).isEmpty();
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LiuliCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator || !target->isAlive() || !selected.isEmpty()) return false;
        const QVariantMap prompt = standardPrompt(request.initiator, request.activationRef);
        if (target->objectName() == prompt.value("attacker").toString()
            || prompt.value("original_targets").toStringList().contains(target->objectName())) return false;
        const Player *attacker = nullptr;
        for (const Player *p : request.initiator->getAliveSiblings())
            if (p->objectName() == prompt.value("attacker").toString()) attacker = p;
        const Card *slash = Card::Parse(prompt.value("slash").toString());
        CardLifetimeManager &manager = globalCardLifetimeManager();
        CardLifetimeLease lease(manager, manager.observeCard(const_cast<Card *>(slash)));
        const auto retire = qScopeGuard([slash] {
            if (slash && slash->isVirtualCard()) const_cast<Card *>(slash)->deleteLater();
        });
        return attacker && slash && attacker->canSlash(target, slash, false)
            && request.initiator->inMyAttackRange(target, request.selectedCardIds);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator) return ContinueEffects;
        QVariantMap prompt = standardPrompt(ctx.initiator, ctx.activationRef);
        prompt.insert("redirected", target->objectName());
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "prompt", prompt);
        return ContinueEffects;
    }
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

    bool resolvePrompt(Room *room, SkillContext &ctx) const
    {
        ServerPlayer *daqiao = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!canRedirect(room, daqiao, use)) return false;
        QStringList names;
        for (ServerPlayer *target : use.to) names << target->objectName();
        const auto prompt = standardPromptScope(room, daqiao, ctx.activationRef,
            {{"slash", use.card->toString()}, {"attacker", use.from->objectName()}, {"original_targets", names}});
        const QVariant previousCard = daqiao->getTag("liuli-card");
        const QVariant previousProperty = daqiao->property("liuli");
        const bool oldFlag = use.from->hasFlag("LiuliSlashSource");
        room->setPlayerFlag(use.from, "LiuliSlashSource");
        daqiao->setTag("liuli-card", QVariant::fromValue(use.card));
        room->setPlayerProperty(daqiao, "liuli", use.card->toString());
        const auto restore = qScopeGuard([=] {
            if (previousCard.isValid()) daqiao->setTag("liuli-card", previousCard);
            else daqiao->removeTag("liuli-card");
            room->setPlayerProperty(daqiao, "liuli", previousProperty);
            if (!oldFlag) room->setPlayerFlag(use.from, "-LiuliSlashSource");
        });
        room->askForUseCard(daqiao, "@@liuli", "@liuli:" + use.from->objectName(), -1, Card::MethodDiscard);
        ctx.choice = standardPrompt(daqiao, ctx.activationRef).value("redirected").toString();
        return !ctx.choice.isEmpty();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The nested active declaration pays only after the parent's effect interception.
        if (!resolvePrompt(room, ctx)) return false;
        ctx.manual_effect = true;
        return skillEffect(event, room, ctx.owner, ctx, room->findPlayerByObjectName(ctx.choice));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!target || !target->isAlive() || !use.from || !use.card || !use.to.contains(ctx.owner)
            || !use.from->canSlash(target, use.card, false)) return false;
        use.to.removeOne(ctx.owner);
        if (!use.to.contains(target)) use.to << target;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        room->getThread()->trigger(TargetConfirming, room, target, *ctx.original_data);
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
        if (!use.card || !use.from || !use.card->isKindOf("Slash") || !use.to.contains(daqiao) || !daqiao->canDiscard(daqiao, "he"))
            return false;
        QList<ServerPlayer *> players = room->getOtherPlayers(daqiao);
        players.removeOne(use.from);
        foreach (ServerPlayer *p, players) {
            if (!use.to.contains(p) && use.from->canSlash(p, use.card, false) && daqiao->inMyAttackRange(p))
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
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LianyingCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target->isAlive()
            && selected.size() < standardPrompt(request.initiator, request.activationRef).value("limit").toInt();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }

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

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *luxun = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();

        int count = 0;
        for (int i = 0; i < move.from_places.length(); i++) {
            if (move.from_places[i] == Player::PlaceHand) count++;
        }
        const auto prompt = standardPromptScope(room, luxun, ctx.activationRef, {{"limit", count}});
        const QVariant previous = luxun->getTag("LianyingMoveData");
        luxun->setTag("LianyingMoveData", *ctx.original_data);
        const auto restore = qScopeGuard([=] {
            if (previous.isValid()) luxun->setTag("LianyingMoveData", previous);
            else luxun->removeTag("LianyingMoveData");
        });
        room->askForUseCard(luxun, "@@lianying", "@lianying-card:::" + QString::number(count));
        return false;
    }
};

class Jieyin : public ViewAsSkillV2
{
public:
    Jieyin() : ViewAsSkillV2("jieyin", 2) {  }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHandcardNum() >= 2;
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JieyinCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target != request.initiator && target->isMale() && target->isWounded();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        for (ServerPlayer *target : ctx.targets) skillEffect(ctx, target);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        RecoverStruct recover(objectName(), ctx.invoker);
        recover.recover = getEffectiveAmount(ctx);
        room->recover(target, recover, true);
        return ContinueEffects;
    }

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
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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
        events << TargetSpecified << CardResponded << CardFinished;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const QString key = receiptKey(room);
        if (event == CardFinished && !key.isEmpty()) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) use.card->removeTag(key);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardResponded) return false;
        const CardResponseStruct response = data.value<CardResponseStruct>();
        const Card *duel = response.m_toCard;
        const QString key = receiptKey(room);
        if (!player || !player->isAlive() || !duel || !duel->isKindOf("Duel") || !response.m_who || key.isEmpty())
            return true;
        const QVariantMap receipt = duel->getTag(key).toMap();
        if (receipt.value("responding").toStringList().contains(player->objectName())) return true;
        QVariantMap strongest;
        for (const QVariant &entry : receipt.value("sources").toList()) {
            const QVariantMap source = entry.toMap();
            if (source.value("responders").toStringList().contains(player->objectName())
                && source.value("amount").toInt() > strongest.value("amount").toInt()) strongest = source;
        }
        if (strongest.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = standardReceiptRef(strongest);
        ctx.instanceID = strongest.value("dispatch").toInt();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.extra_data = key;
        ctx.setModifiedAmount(strongest.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const Card *duel = ctx.original_data->value<CardResponseStruct>().m_toCard;
        if (!duel) return false;
        for (const QVariant &entry : duel->getTag(ctx.extra_data.toString()).toMap().value("sources").toList())
            if (standardReceiptRef(entry.toMap()) == ctx.sourceRef
                && entry.toMap().value("dispatch").toInt() == ctx.instanceID) return true;
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified || !player || !player->isAlive()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return result;
        if (use.card->isKindOf("Duel")) {
            if (player->hasSkill(objectName())) result[player] << objectName();
            for (ServerPlayer *target : use.to)
                if (target != player && target->hasSkill(objectName())) result[target] << objectName();
        } else if (use.card->isKindOf("Slash") && player->hasSkill(objectName())) result[player] << objectName();
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (event == CardResponded) return skillEffect(event, room, player, ctx, ctx.invoker);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        const QList<ServerPlayer *> responders = ctx.owner == use.from ? use.to : QList<ServerPlayer *>{use.from};
        for (ServerPlayer *responder : responders) skillEffect(event, room, player, ctx, responder);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardResponded) {
            const auto requestScope = standardRequestScope();
            CardResponseStruct response = ctx.original_data->value<CardResponseStruct>();
            const Card *duel = response.m_toCard;
            const QString key = ctx.extra_data.toString();
            if (!duel || !response.m_who) return false;
            QVariantMap receipt = duel->getTag(key).toMap();
            const QStringList previous = receipt.value("responding").toStringList();
            QStringList responding = previous;
            responding << target->objectName();
            receipt.insert("responding", responding);
            duel->setTag(key, receipt);
            const auto restore = qScopeGuard([=] {
                QVariantMap current = duel->getTag(key).toMap();
                current.insert("responding", previous);
                duel->setTag(key, current);
            });
            CardEffectStruct effect;
            effect.card = duel;
            effect.from = response.m_who;
            effect.to = target;
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
                if (!room->askForCard(target, "slash", "duel-slash:" + response.m_who->objectName(),
                    QVariant::fromValue(effect), Card::MethodResponse, response.m_who, false, "", false, duel)) {
                    response.nullified = true;
                    *ctx.original_data = QVariant::fromValue(response);
                    break;
                }
            }
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        const int amount = getEffectiveAmount(ctx);
        if (use.card->isKindOf("Duel")) {
            const QString key = receiptKey(room);
            if (key.isEmpty()) return false;
            QVariantMap receipt = use.card->getTag(key).toMap();
            QVariantList sources = receipt.value("sources").toList();
            sources << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"responders", QStringList{target->objectName()}}, {"amount", amount},
                {"dispatch", nextStandardReceiptId(room, target, objectName())}};
            receipt.insert("sources", sources);
            use.card->setTag(key, receipt);
        } else {
            QVariantList jinks = use.from->getTag("Jink_" + use.card->toString()).toList();
            const int index = use.to.indexOf(target);
            if (index >= 0 && index < jinks.size() && jinks.at(index).toInt() > 0)
                jinks[index] = qMax(jinks.at(index).toInt(), 1 + amount);
            use.from->setTag("Jink_" + use.card->toString(), jinks);
        }
        return false;
    }

private:
    static QString receiptKey(Room *room)
    {
        const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        return id > 0 ? "wushuang:" + QString::number(id) : QString();
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const QList<ServerPlayer *> duelTargets = ctx.targets;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        ctx.choice = "extract";
        if (damage.to && !damage.to->isNude()) skillEffect(event, room, callback, ctx, damage.to);
        ctx.choice = "duel";
        for (ServerPlayer *target : duelTargets) skillEffect(event, room, callback, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (ctx.choice == "extract") {
            if (target->isNude() || !ctx.owner->isAlive()) return false;
            ctx.extra_data = room->askForCardChosen(ctx.owner, target, "he", objectName());
            ctx.choice = "obtain";
            skillEffect(event, room, callback, ctx, ctx.owner);
        } else if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toInt();
            if (room->getCardOwner(id) == damage.to
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->obtainCard(target, id);
        } else if (ctx.owner->isAlive()) {
            Duel duel(Card::NoSuit, 0);
            duel.setSkillName("_liyu");
            if (ctx.owner->canUse(&duel, target)) room->useCardFromSkillEffect(CardUseStruct(&duel, ctx.owner, target), ctx);
        }
        return false;
    }
};

class Lijian : public ViewAsSkillV2
{
public:
    Lijian() : ViewAsSkillV2("lijian", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LijianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!target || !target->isMale() || selected.size() >= 2) return false;
        if (selected.isEmpty()) return true;
        Duel duel(Card::NoSuit, 0);
        return !target->isCardLimited(&duel, Card::MethodUse) && !target->isProhibited(selected.first(), &duel);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 2;
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.size() != 2 || !ctx.invoker) return ContinueEffects;
        // The ordered second target uses an ordinary Duel against the first.
        Duel duel(Card::NoSuit, 0);
        duel.setCancelable(true);
        duel.setSkillName("_" + objectName());
        if (targets.last()->canUse(&duel, targets.first()))
            ctx.invoker->getRoom()->useCardFromSkillEffect(CardUseStruct(&duel, targets.last(), targets.first()), ctx);
        return ContinueEffects;
    }

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
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->canDiscard(player, "he");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ChuliCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator
            || !request.initiator->canDiscard(target, "he")) return false;
        if (Config.EnableHegemony) {
            if (selected.size() >= 3) return false;
            if (!target->hasShownOneGeneral()) return true;
            for (const Player *p : selected) if (target->isFriendWith(p)) return false;
        } else {
            for (const Player *p : selected) if (target->getKingdom() == p->getKingdom()) return false;
        }
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (!ctx.invoker || !ctx.use_card) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> draws;
        if (ctx.use_card->subcardsLength() == 1
            && Sanguosha->getCard(ctx.use_card->getSubcards().first())->getSuit() == Card::Spade)
            draws << ctx.invoker;
        // All discards resolve before any of the spade rewards are drawn.
        for (ServerPlayer *target : targets) {
            if (!ctx.invoker->isAlive() || !ctx.invoker->canDiscard(target, "he")) continue;
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target || !ctx.invoker->canDiscard(target, id)) continue;
            const bool spade = Sanguosha->getCard(id)->getSuit() == Card::Spade;
            room->throwCard(id, target, ctx.invoker);
            if (spade) draws << target;
        }
        for (ServerPlayer *target : draws) skillEffect(ctx, target);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.owner;
        ServerPlayer *other = otherPlayer(event, ctx.original_data->value<DamageStruct>());
        if (!player || !other || other == player) return false;
        QList<ServerPlayer *> recipients{player, other};
        room->sortByActionOrder(recipients);
        for (int i = 0; i < ctx.extra_data.toInt(); ++i) {
            if (!player->isAlive() || !other->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (i > 0 && !room->askForSkillInvoke(player, objectName(), QVariant::fromValue(other), false)) break;
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName(), event == Damaged ? 1 : 2);
            // Each recipient can intercept its own draw without canceling the other's.
            for (ServerPlayer *recipient : recipients) skillEffect(event, room, callback, ctx, recipient);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        room->broadcastSkillInvoke(objectName());
        ctx.choice = "draw";
        skillEffect(event, room, player, ctx, ctx.owner);
        ctx.choice = "reduce";
        skillEffect(event, room, player, ctx, ctx.invoker);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else room->addMaxCards(target, -getEffectiveAmount(ctx));
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.original_data->value<DamageStruct>().from);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || damage.from->isDead()) return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(damage.to, objectName());

        if (damage.from->isWounded() && room->askForChoice(damage.from, objectName(), "recover+draw", *ctx.original_data) == "recover")
            room->recover(damage.from, RecoverStruct(objectName(), damage.to, getEffectiveAmount(ctx)));
        else
            damage.from->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Qiaomeng : public TriggerSkillV2
{
public:
    Qiaomeng() : TriggerSkillV2("qiaomeng")
    {
        events << Damage;
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.original_data->value<DamageStruct>().to);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (ctx.choice == "horse") {
            const int id = ctx.extra_data.toInt();
            if (room->getCardOwner(id) == victim && room->getCardPlace(id) == Player::PlaceEquip)
                room->obtainCard(target, id);
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->canDiscard(target, "e"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "e", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip
                || !ctx.owner->canDiscard(target, id)) continue;
            room->broadcastSkillInvoke(objectName());
            if (Sanguosha->getCard(id)->isKindOf("Horse")) {
                ctx.choice = "horse";
                ctx.extra_data = id;
                skillEffect(event, room, callback, ctx, ctx.owner);
                ctx.choice.clear();
            } else {
                CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, ctx.owner->objectName(), target->objectName(),
                    objectName(), QString());
                room->throwCard(Sanguosha->getCard(id), reason, target, ctx.owner);
            }
        }
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner->getNext());
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_xiaoxi");
        if (player->canSlash(target, &slash, false)) room->useCardFromSkillEffect(CardUseStruct(&slash, player, target), ctx);
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toInt();
            ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
            if (room->getCardOwner(id) == from
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->obtainCard(target, Sanguosha->getCard(id),
                    CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName()),
                    room->getCardPlace(id) != Player::PlaceHand);
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && !target->isNude(); ++i) {
            ctx.extra_data = room->askForCardChosen(ctx.owner, target, "he", objectName());
            ctx.choice = "obtain";
            skillEffect(event, room, callback, ctx, ctx.owner);
            ctx.choice.clear();
        }
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
        const auto requestScope = standardRequestScope();
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
        ctx.extra_data = card->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        return ctx.extra_data.isValid() && id >= 0 && !Sanguosha->getCard(id)->hasFlag("using")
            && room->getCardOwner(id) == player
            && room->getCardPlace(id) == Player::PlaceHand;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        return judge && skillEffect(event, room, player, ctx, judge->who);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.extra_data.isValid()) return false;
        const int id = ctx.extra_data.toInt();
        if (id < 0 || Sanguosha->getCard(id)->hasFlag("using") || room->getCardOwner(id) != player
            || room->getCardPlace(id) != Player::PlaceHand) return false;
        const Card *card = Sanguosha->getCard(id);
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
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
        if (judge.isGood()) skillEffect(event, room, callback, ctx, from);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = 2 * getEffectiveAmount(ctx);
        if (target->getHandcardNum() < count || !room->askForDiscard(target, objectName(), count, count, true))
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }

};

NosTuxiCard::NosTuxiCard()
{
    setSkillName("nostuxi");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosTuxiCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target != request.initiator && !target->isKongcheng()
            && selected.size() < 2;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "obtain") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("id").toInt();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
            if (!from || !from->handCards().contains(id)) return ContinueEffects;
            room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,
                target->objectName(), from->objectName(), objectName(), QString()), false);
            if (ctx.initiator) {
                QVariantMap prompt = standardPrompt(ctx.initiator, ctx.activationRef);
                prompt.insert("obtained", prompt.value("obtained").toInt() + 1);
                ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "prompt", prompt);
            }
            return ContinueEffects;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && !target->isKongcheng(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName());
            if (!target->handCards().contains(id)) continue;
            ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}};
            ctx.choice = "obtain";
            skillEffect(ctx, ctx.invoker);
            ctx.choice.clear();
        }
        return ContinueEffects;
    }
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const auto prompt = standardPromptScope(room, ctx.owner, ctx.activationRef, {});
        // Only an accepted nested activation replaces the normal draw phase.
        return room->askForUseCard(ctx.owner, "@@nostuxi", "@nostuxi-card");
    }
};

class NosLuoyiBuff : public LuoyiBuff
{
public:
    NosLuoyiBuff() : LuoyiBuff("#nosluoyi", "nosluoyi", true) {}
};

class NosLuoyi : public TriggerSkillV2
{
public:
    NosLuoyi() : TriggerSkillV2("nosluoyi")
    {
        events << DrawNCards << EventPhaseChanging;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *xuchu, QVariant &data) const override
    {
        return event == DrawNCards && xuchu && xuchu->isAlive() && xuchu->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{xuchu, {objectName()}}} : TriggerList();
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *player : room->getAllPlayers(true))
                room->setPlayerProperty(player, standardReceiptProperty(objectName()).toUtf8().constData(), QVariantList());
        return true;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        room->broadcastSkillInvoke(objectName());
        addStandardReceipt(room, ctx.owner, objectName(), ctx, getEffectiveAmount(ctx));
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = qMax(0, draw.num - getEffectiveAmount(ctx));
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

bool NosYiji::effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const
{
    ctx.manual_effect = true;
    ServerPlayer *guojia = ctx.owner;
    const int points = ctx.original_data->value<DamageStruct>().damage;
    for (int i = 0; i < points; ++i) {
        if (!guojia->isAlive() || !isSourceAvailable(room, ctx)) break;
        if (i > 0 && !room->askForSkillInvoke(guojia, objectName())) break;
        room->broadcastSkillInvoke("nosyiji");
        const QList<int> drawn = room->getNCards(n * getEffectiveAmount(ctx));
        QList<int> available = drawn;
        const QList<ServerPlayer *> viewers{guojia};
        QList<CardsMoveStruct> preview{CardsMoveStruct(drawn, nullptr, guojia, Player::PlaceTable, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_PREVIEW, guojia->objectName(), objectName(), QString()))};
        room->notifyMoveCards(true, preview, false, viewers);
        room->notifyMoveCards(false, preview, false, viewers);
        bool previewOpen = true;
        const auto closePreview = [&] {
            if (!previewOpen) return;
            QList<CardsMoveStruct> end{CardsMoveStruct(drawn, guojia, nullptr, Player::PlaceHand, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_PREVIEW, guojia->objectName(), objectName(), QString()))};
            room->notifyMoveCards(true, end, false, viewers);
            room->notifyMoveCards(false, end, false, viewers);
            previewOpen = false;
        };
        const auto cleanup = qScopeGuard([&] {
            closePreview();
            QList<int> unused;
            for (int id : drawn)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile
                    && !room->getDrawPile().contains(id)) unused << id;
            if (!unused.isEmpty()) room->returnToTopDrawPile(unused);
        });
        QList<CardsMoveStruct> assignments;
        while (guojia->isAlive() && !available.isEmpty()) {
            const CardsMoveStruct move = room->askForYijiStruct(guojia, available, objectName(),
                true, false, true, -1, room->getAlivePlayers(), CardMoveReason(), QString(), false, false);
            if (!move.to || move.card_ids.isEmpty()) break;
            assignments << move;
        }
        if (!available.isEmpty())
            assignments << CardsMoveStruct(available, nullptr, guojia, Player::DrawPile, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_PREVIEWGIVE, guojia->objectName(), objectName(), QString()));
        closePreview();
        // Keep all decisions ahead of movement, with a separate interception per recipient.
        for (const CardsMoveStruct &move : assignments) {
            ctx.extra_data = QVariant::fromValue(move);
            skillEffect(event, room, callback, ctx, room->findPlayerByObjectName(move.to->objectName()));
        }
    }
    return false;
}

bool NosYiji::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    CardsMoveStruct move = ctx.extra_data.value<CardsMoveStruct>();
    QList<int> ids;
    for (int id : move.card_ids)
        if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile
                    && !room->getDrawPile().contains(id)) ids << id;
    if (!ids.isEmpty()) {
        move.card_ids = ids;
        move.to = target;
        room->moveCardsAtomic(move, false);
    }
    return false;
}

NosRendeCard::NosRendeCard()
{
    setSkillName("nosrende");
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

class NosRendeViewAsSkill : public RendeViewAsSkill
{
public:
    NosRendeViewAsSkill() : RendeViewAsSkill("nosrende") {}
};

class NosRende : public TriggerSkillV2
{
public:
    NosRende() : TriggerSkillV2("nosrende")
    {
        events << EventPhaseChanging << EventSkillInvoking;
        global = true;
        view_as_skill = new NosRendeViewAsSkill;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging) return;
        if (player == ctx.owner && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive) {
            player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "given");
            player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "started");
        }
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext active = data.value<SkillContext>();
            if (active.activationRef.key.skillName == objectName() && active.use_card
                && active.use_card->getTypeId() == Card::TypeSkill) {
                static_cast<const RendeViewAsSkill *>(view_as_skill)->commitAccepted(active);
                data = QVariant::fromValue(active);
            }
            return true;
        }
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && player->getMark("nosrende") > 0) room->setPlayerMark(player, "nosrende", 0);
        // Continue per-instance recording so the exact given/started state also expires.
        return false;
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *callback, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const int first = ctx.extra_data.toInt();
        for (int index = first; index < use.to.size(); ++index) {
            ServerPlayer *target = use.to.at(index);
            if (!ctx.owner->isAlive() || !isSourceAvailable(room, ctx)) break;
            if (!target->isAlive()) continue;
            if (index > first && !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) continue;
            skillEffect(event, room, callback, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.to.contains(target)) return false;
        room->broadcastSkillInvoke(objectName());
        const bool flagged = target->hasFlag("NosTiejiTarget");
        target->setFlags("NosTiejiTarget");
        const auto restore = qScopeGuard([=] { if (!flagged) target->setFlags("-NosTiejiTarget"); });
        JudgeStruct judge;
        judge.pattern = ".|red";
        judge.good = true;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (judge.isGood()) {
            LogMessage log;
            log.type = "#NoJink";
            log.from = target;
            room->sendLog(log);
            QVariantList jinks = player->getTag("Jink_" + use.card->toString()).toList();
            const int index = ctx.original_data->value<CardUseStruct>().to.indexOf(target);
            if (index >= 0 && index < jinks.size()) jinks[index] = 0;
            player->setTag("Jink_" + use.card->toString(), jinks);
        }
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *yueying, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, yueying, ctx, yueying);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *yueying, SkillContext &ctx, ServerPlayer *) const override
    {
        room->broadcastSkillInvoke("jizhi");
        yueying->drawCards(getEffectiveAmount(ctx), objectName());
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
    setSkillName("noskurou");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosKurouCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive()) ctx.invoker->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return FinishSkill;
    }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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
    setSkillName("nosfanjian");
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosFanjianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || !from->isAlive() || from->isKongcheng()) return ContinueEffects;
        Room *room = from->getRoom();
        const Card::Suit suit = room->askForSuit(target, objectName());
        if (from->isKongcheng() || !target->isAlive()) return ContinueEffects;
        const int id = room->askForCardChosen(target, from, "h", objectName());
        if (!from->handCards().contains(id)) return ContinueEffects;
        const Card *card = Sanguosha->getCard(id);
        target->obtainCard(card);
        room->showCard(target, id);
        if (card->getSuit() != suit) room->damage(DamageStruct(objectName(), from, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

QingnangCard::QingnangCard()
{
    setSkillName("qingnang");
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

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->canDiscard(player, "h");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "QingnangCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || !selected.isEmpty() || !target->isWounded()) return false;
        const QString forced = request.initiator->hasAcquiredSkill(objectName())
            ? request.initiator->property("manweiwoFrom").toString() : QString();
        return forced.isEmpty() || target->objectName() == forced;
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.size() <= 1 && canSelectTarget(request, {}, targets.value(0, request.initiator));
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.targets.isEmpty() && ctx.invoker) {
            ctx.manual_effect = true;
            skillEffect(ctx, ctx.invoker);
            return FinishSkill;
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        RecoverStruct recover(objectName(), ctx.invoker);
        recover.recover = getEffectiveAmount(ctx);
        target->getRoom()->recover(target, recover);
        return ContinueEffects;
    }

};

NosLijianCard::NosLijianCard() : LijianCard(false)
{
    setSkillName("noslijian");
}

class NosLijian : public ViewAsSkillV2
{
public:
    NosLijian() : ViewAsSkillV2("noslijian", 1) {}

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getAliveSiblings().length() > 1
            && player->canDiscard(player, "he");
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosLijianCard"; }
    /*

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("Duel") ? 0 : -1;
    }*/
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!target || !target->isMale() || selected.size() >= 2) return false;
        if (selected.isEmpty()) return true;
        Duel duel(Card::NoSuit, 0);
        return !target->isCardLimited(&duel, Card::MethodUse) && !target->isProhibited(selected.first(), &duel);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 2;
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.size() != 2 || !ctx.invoker) return ContinueEffects;
        // The ordered second target uses an ordinary Duel against the first.
        Duel duel(Card::NoSuit, 0);
        duel.setCancelable(false);
        duel.setSkillName("_" + objectName());
        if (targets.last()->canUse(&duel, targets.first()))
            ctx.invoker->getRoom()->useCardFromSkillEffect(CardUseStruct(&duel, targets.last(), targets.first()), ctx);
        return ContinueEffects;
    }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (!ctx.invoker->isAlive() || ctx.invoker->getHp() <= ctx.owner->getHp()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const bool lord = ctx.invoker->isLord();
        ctx.choice = lord ? "draw_two" : "draw";
        skillEffect(event, room, player, ctx, ctx.owner);
        if (lord) {
            ctx.choice = "reduce";
            skillEffect(event, room, player, ctx, ctx.invoker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reduce") room->addMaxCards(target, -getEffectiveAmount(ctx));
        else target->drawCards((ctx.choice == "draw_two" ? 2 : 1) * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileTongjiCard::MobileTongjiCard()
{
    setSkillName("mobiletongji");
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
    CardLifetimeManager &manager = globalCardLifetimeManager();
    CardLifetimeLease lease(manager, manager.observeCard(const_cast<Card *>(slash)));
    const auto retire = qScopeGuard([slash] {
        if (slash && slash->isVirtualCard()) const_cast<Card *>(slash)->deleteLater();
    });
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

// Each borrowed entry is tied to the Yuan Shu instance offering this redirection.
class MobileTongjiVS : public ViewAsSkillV2
{
public:
    MobileTongjiVS() : ViewAsSkillV2("mobiletongji", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@mobiletongji"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getSkillInstanceStateValue(objectName(),
                request.activationRef.key.instanceID, "slash").toString().isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty()
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || !selected.isEmpty()) return false;
        const QVariantMap state = request.initiator->getSkillInstanceState(objectName(), request.activationRef.key.instanceID);
        if (target->objectName() != state.value("recipient").toString()) return false;
        const Player *attacker = nullptr;
        for (const Player *p : request.initiator->getAliveSiblings())
            if (p->objectName() == state.value("attacker").toString()) attacker = p;
        const Card *slash = Card::Parse(state.value("slash").toString());
        CardLifetimeManager &manager = globalCardLifetimeManager();
        CardLifetimeLease lease(manager, manager.observeCard(const_cast<Card *>(slash)));
        const auto retire = qScopeGuard([slash] {
            if (slash && slash->isVirtualCard()) const_cast<Card *>(slash)->deleteLater();
        });
        return attacker && slash && attacker->canSlash(target, slash, false)
            && request.initiator->inMyAttackRange(target, request.selectedCardIds);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.initiator && target && target->objectName() == ctx.sourceRef.ownerObjectName)
            ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID,
                                                     "redirected", target->objectName());
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileTongjiCard"; }
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
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (eligible(player, owner, use)) result[owner] << objectName();
        return result;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool resolvePrompt(Room *room, SkillContext &ctx) const
    {
        const auto requestScope = standardRequestScope();
        ServerPlayer *player = ctx.invoker;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!eligible(player, ctx.owner, use)) return false;
        Room::BorrowedSkillScope borrowed(room, player, objectName(), ctx.sourceRef);
        const SkillInstanceRef entry = borrowed.activationRef();
        if (!entry.isValid()) return false;
        const QVariantMap previous = player->getSkillInstanceState(objectName(), entry.key.instanceID);
        const QVariant oldCard = player->getTag("mobiletongji-card");
        const QVariant oldProperty = player->property("mobiletongji");
        const bool oldFlag = use.from->hasFlag("MobileTongjiSlashSource");
        const auto restore = qScopeGuard([=] {
            player->setSkillInstanceState(objectName(), entry.key.instanceID, previous);
            if (oldCard.isValid()) player->setTag("mobiletongji-card", oldCard);
            else player->removeTag("mobiletongji-card");
            room->setPlayerProperty(player, "mobiletongji", oldProperty);
            if (!oldFlag) room->setPlayerFlag(use.from, "-MobileTongjiSlashSource");
        });
        player->setSkillInstanceState(objectName(), entry.key.instanceID,
            {{"slash", use.card->toString()}, {"attacker", use.from->objectName()},
             {"recipient", ctx.owner->objectName()}});
        // Temporary legacy AI/UI hints never select the authoritative instance.
        player->setTag("mobiletongji-card", QVariant::fromValue(use.card));
        room->setPlayerProperty(player, "mobiletongji", use.card->toString());
        room->setPlayerFlag(use.from, "MobileTongjiSlashSource");
        room->askForUseCard(player, "@@mobiletongji", "@mobiletongji:" + use.from->objectName(), -1, Card::MethodDiscard);
        ctx.choice = player->getSkillInstanceStateValue(objectName(), entry.key.instanceID, "redirected").toString();
        return ctx.choice == ctx.owner->objectName();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The nested active declaration pays only after the parent's effect interception.
        if (!resolvePrompt(room, ctx)) return false;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        return skillEffect(event, room, ctx.owner, ctx, room->findPlayerByObjectName(ctx.choice));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!target || !target->isAlive() || !use.from || !use.card || !use.to.contains(player)
            || !use.from->canSlash(target, use.card, false)) return false;
        use.to.removeOne(player);
        if (!use.to.contains(target)) use.to.append(target);
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        room->getThread()->trigger(TargetConfirming, room, target, *ctx.original_data);
        return false;
    }

private:
    bool eligible(ServerPlayer *player, ServerPlayer *owner, const CardUseStruct &use) const
    {
        return player && player->isAlive() && owner && owner->isAlive() && owner != player
            && owner->hasSkill(objectName()) && use.card && use.card->isKindOf("Slash")
            && use.from && owner != use.from && !use.to.contains(owner) && use.to.contains(player)
            && player->canDiscard(player, "he") && player->inMyAttackRange(owner)
            && use.from->canSlash(owner, use.card, false);
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

public:
    int getMaxUsageLimit(const SkillContext &ctx) const override
    {
        return ctx.invoker ? ctx.invoker->getLostHp() + 1 : 1;
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    bool usesEventPriority() const override { return true; }

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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.owner);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->canDiscard(ctx.owner, "he")
            || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker))) return false;
        ctx.extra_data = room->askForCardChosen(ctx.invoker, ctx.owner, "he", objectName(), false, Card::MethodDiscard);
        return ctx.extra_data.toInt() >= 0;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner || !ctx.invoker->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, ctx.owner, ctx.invoker);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, player, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        ServerPlayer *p = ctx.owner;

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

        player->drawCards(3 * getEffectiveAmount(ctx), objectName());
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

    bool usesEventPriority() const override { return true; }

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
