#include "fire.h"
//#include "general.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "engine.h"
#include "maneuvering.h"
#include "clientplayer.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

QuhuCard::QuhuCard()
{
    mute = true;
}

bool QuhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getHp() > Self->getHp() && Self->canPindian(to_select);
}

void QuhuCard::use(Room *room, ServerPlayer *xunyu, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *tiger = targets.first();

    int index = 1;
    if (xunyu->isJieGeneral())
        index = 3;
    room->broadcastSkillInvoke("quhu", index);

    bool success = xunyu->pindian(tiger, "quhu", nullptr);
    if (success) {
        index = 2;
        if (xunyu->isJieGeneral())
            index = 4;
        room->broadcastSkillInvoke("quhu", index);

        QList<ServerPlayer *> players = room->getOtherPlayers(tiger), wolves;
        foreach (ServerPlayer *player, players) {
            if (tiger->inMyAttackRange(player))
                wolves << player;
        }

        if (wolves.isEmpty()) {
            LogMessage log;
            log.type = "#QuhuNoWolf";
            log.from = xunyu;
            log.to << tiger;
            room->sendLog(log);

            return;
        }

        ServerPlayer *wolf = room->askForPlayerChosen(xunyu, wolves, "quhu", QString("@quhu-damage:%1").arg(tiger->objectName()));
        room->damage(DamageStruct("quhu", tiger, wolf));
    } else {
        room->damage(DamageStruct("quhu", tiger, xunyu));
    }
}

class Jieming : public MasochismSkill
{
public:
    Jieming() : MasochismSkill("jieming")
    {
    }

    void onDamaged(ServerPlayer *xunyu, const DamageStruct &damage) const
    {
        Room *room = xunyu->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            ServerPlayer *to = room->askForPlayerChosen(xunyu, room->getAlivePlayers(), objectName(), "jieming-invoke", true, true);
            if (!to) break;

            int upper = qMin(5, to->getMaxHp());
            int x = upper - to->getHandcardNum();
            if (x <= 0) continue;

            room->broadcastSkillInvoke(objectName());
            to->drawCards(x, objectName());
            if (!xunyu->isAlive())
                break;
        }
    }
};

class Quhu : public ZeroCardViewAsSkill
{
public:
    Quhu() : ZeroCardViewAsSkill("quhu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("QuhuCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new QuhuCard;
    }
};

QiangxiCard::QiangxiCard()
{
}

bool QiangxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return Self->inMyAttackRange(to_select, subcards) && targets.isEmpty() && to_select != Self;
}

void QiangxiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = to->getRoom();

    if (subcards.isEmpty())
        room->loseHp(HpLostStruct(from, 1, "qiangxi", from));

    room->damage(DamageStruct("qiangxi", from, to));
}

class Qiangxi : public ViewAsSkill
{
public:
    Qiangxi() : ViewAsSkill("qiangxi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("QiangxiCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.isEmpty() && to_select->isKindOf("Weapon") && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return new QiangxiCard;
        else if (cards.length() == 1) {
            QiangxiCard *card = new QiangxiCard;
            card->addSubcards(cards);
            return card;
        }
		return nullptr;
    }
};

class Luanji : public ViewAsSkill
{
public:
    Luanji() : ViewAsSkill("luanji")
    {
        response_or_use = true;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.isEmpty())
            return !to_select->isEquipped();
        else if (selected.length() == 1) {
            return !to_select->isEquipped() && to_select->getSuit() == selected.first()->getSuit();
        }
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

class Xueyi : public TriggerSkill
{
public:
    Xueyi() : TriggerSkill("xueyi$")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive() && target->hasLordSkill(objectName());
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::Discard)
				room->broadcastSkillInvoke(objectName());
        }
        return false;
    }
};

class XueyiMCS : public MaxCardsSkill
{
public:
    XueyiMCS() : MaxCardsSkill("#xueyi")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasLordSkill("xueyi")) {
            int extra = 0;
            foreach (const Player *p, target->getAliveSiblings()) {
                if (p->getKingdom() == "qun")
                    extra += 2;
            }
            return extra;
        }
		return 0;
    }
};

class ShuangxiongViewAsSkill : public OneCardViewAsSkill
{
public:
    ShuangxiongViewAsSkill() : OneCardViewAsSkill("shuangxiong")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("shuangxiong") != 0;
    }

    bool viewFilter(const Card *card) const
    {
        if (card->isEquipped())
            return false;

        int value = Self->getMark("shuangxiong");
        if (value == 1)
            return card->isBlack();
        else if (value == 2)
            return card->isRed();

        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Duel *duel = new Duel(originalCard->getSuit(), originalCard->getNumber());
        duel->addSubcard(originalCard);
        duel->setSkillName("_" + objectName());
        return duel;
    }
};

class Shuangxiong : public TriggerSkill
{
public:
    Shuangxiong() : TriggerSkill("shuangxiong")
    {
        events << EventPhaseStart << FinishJudge << EventPhaseChanging;
        view_as_skill = new ShuangxiongViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *shuangxiong, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (shuangxiong->getPhase() == Player::Start) {
                room->setPlayerMark(shuangxiong, "shuangxiong", 0);
            } else if (shuangxiong->getPhase() == Player::Draw && TriggerSkill::triggerable(shuangxiong)) {
                if (shuangxiong->askForSkillInvoke(objectName()+"$1")) {
                    room->setPlayerFlag(shuangxiong, "shuangxiong");

                    JudgeStruct judge;
                    judge.good = true;
                    judge.play_animation = false;
                    judge.reason = objectName();
                    judge.pattern = ".";
                    judge.who = shuangxiong;

                    room->judge(judge);
                    room->setPlayerMark(shuangxiong, "shuangxiong", judge.card->isRed() ? 1 : 2);
                    room->setPlayerMark(shuangxiong, "ViewAsSkill_shuangxiongEffect", 1);
                    return true;
                }
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == "shuangxiong") {
                if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                    shuangxiong->obtainCard(judge->card);
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                room->setPlayerMark(shuangxiong, "ViewAsSkill_shuangxiongEffect", 0);
        }
        return false;
    }
};

class Mengjin : public TriggerSkill
{
public:
    Mengjin() :TriggerSkill("mengjin")
    {
        events << CardOffset;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *pangde, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("Slash") && effect.to->isAlive() && pangde->canDiscard(effect.to, "he")) {
            if (pangde->askForSkillInvoke(objectName()+"$-1", data)) {
                int to_throw = room->askForCardChosen(pangde, effect.to, "he", objectName(), false, Card::MethodDiscard);
                room->throwCard(Sanguosha->getCard(to_throw), effect.to, pangde);
            }
        }

        return false;
    }
};

class Lianhuan : public OneCardViewAsSkill
{
public:
    Lianhuan() : OneCardViewAsSkill("lianhuan")
    {
        filter_pattern = ".|club|.|hand";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        IronChain *chain = new IronChain(originalCard->getSuit(), originalCard->getNumber());
        chain->setSkillName(objectName());
        chain->addSubcard(originalCard);
        return chain;
    }
};

class Niepan : public TriggerSkill
{
public:
    Niepan() : TriggerSkill("niepan")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@nirvana";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && target->getMark("@nirvana") > 0;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *pangtong, QVariant &data) const
    {
        DyingStruct dying_data = data.value<DyingStruct>();
        if (dying_data.who != pangtong)
            return false;

        if (pangtong->askForSkillInvoke(objectName()+"$-1", data)) {
            //room->doLightbox("$NiepanAnimate");
            room->doSuperLightbox(pangtong, "niepan");

            room->removePlayerMark(pangtong, "@nirvana");

            pangtong->throwAllCards(objectName());

            int n = qMin(3 - pangtong->getHp(), pangtong->getMaxHp() - pangtong->getHp());
            if (n > 0)
                room->recover(pangtong, RecoverStruct(pangtong, nullptr, n, objectName()));

            pangtong->drawCards(3, objectName());

            if (pangtong->isChained())
                room->setPlayerChained(pangtong);

            if (!pangtong->faceUp())
                pangtong->turnOver();
        }

        return false;
    }
};

class Huoji : public OneCardViewAsSkill
{
public:
    Huoji() : OneCardViewAsSkill("huoji")
    {
        filter_pattern = ".|red|.|hand";
        //response_pattern = "fire_attack";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FireAttack *fire_attack = new FireAttack(originalCard->getSuit(), originalCard->getNumber());
        fire_attack->addSubcard(originalCard->getId());
        fire_attack->setSkillName(objectName());
        return fire_attack;
    }
};

class Bazhen : public ViewAsEquipSkill
{
public:
    Bazhen() : ViewAsEquipSkill("bazhen")
    {
    }

    QString viewAsEquip(const Player *target) const
    {
        if (target->hasEquipArea(1) && !target->getArmor())
            return "eight_diagram";
        return "";
    }
};

class BazhenTrigger : public TriggerSkill
{
public:
    BazhenTrigger() : TriggerSkill("#bazhen")
    {
        events << InvokeSkill;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasSkill("bazhen")) return false;
        QString skill = data.toString();
        if (skill != "eight_diagram") return false;
        int index = qrand()%2+1;
        if (player->isJieGeneral("wolong") || player->isJieGeneral("zhugeliang"))
            index += 2;
        else if (player->isJieGeneral("pangtong"))
            index += 4;
        room->sendCompulsoryTriggerLog(player, "bazhen", true, true, index);
        return false;
    }
};

class Kanpo : public OneCardViewAsSkill
{
public:
    Kanpo() : OneCardViewAsSkill("kanpo")
    {
        filter_pattern = ".|black|.|hand";
        response_pattern = "nullification";
        response_or_use = true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *ncard = new Nullification(originalCard->getSuit(), originalCard->getNumber());
        ncard->addSubcard(originalCard);
        ncard->setSkillName(objectName());
        return ncard;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        return !player->isKongcheng() || !player->getHandPile().isEmpty();
    }
};

TianyiCard::TianyiCard()
{
}

bool TianyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void TianyiCard::use(Room *room, ServerPlayer *taishici, QList<ServerPlayer *> &targets) const
{
    bool success = taishici->pindian(targets.first(), "tianyi", nullptr);
    if (success)
        room->setPlayerFlag(taishici, "TianyiSuccess");
    else
        room->setPlayerCardLimitation(taishici, "use", "Slash", true);
}

class Tianyi : public ZeroCardViewAsSkill
{
public:
    Tianyi() : ZeroCardViewAsSkill("tianyi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TianyiCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new TianyiCard;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = 1;
        if (player->isJieGeneral())
            index += qrand() % 2 + 1;
        return index;
    }
};

class TianyiTargetMod : public TargetModSkill
{
public:
    TianyiTargetMod() : TargetModSkill("#tianyi-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("TianyiSuccess"))
            return 1;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("TianyiSuccess"))
            return 1000;
        return 0;
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        if (from->hasFlag("TianyiSuccess"))
            return 1;
        return 0;
    }
};

void YeyanCard::damage(ServerPlayer *shenzhouyu, ServerPlayer *target, int point) const
{
    shenzhouyu->getRoom()->damage(DamageStruct("yeyan", shenzhouyu, target, point, DamageStruct::Fire));
}

GreatYeyanCard::GreatYeyanCard()
{
    mute = true;
    m_skillName = "yeyan";
}

bool GreatYeyanCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    Q_ASSERT(false);
    return false;
}

bool GreatYeyanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length()==3;
}

bool GreatYeyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select,
    const Player *, int &maxVotes) const
{
	if(QSet<const Player*>(targets.begin(),targets.end()).size()==2&&!targets.contains(to_select))
		return false;
	int i = 0;
	foreach(const Player *player, targets)
		if (player == to_select) i++;
	maxVotes = qMax(3 - targets.size(), 0) + i;
	return maxVotes > 0;
}

void GreatYeyanCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<ServerPlayer *> targets;
	foreach(ServerPlayer *sp, card_use.to){
		sp->addMark("yeyan_damage");
		if(!targets.contains(sp))
			targets << sp;
	}
	card_use.to = targets;
    YeyanCard::onUse(room, card_use);
}

void GreatYeyanCard::use(Room *room, ServerPlayer *shenzhouyu, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(shenzhouyu, "@flame");
	room->broadcastSkillInvoke("yeyan", (targets.length() > 1) ? 2 : 3);
	room->doSuperLightbox(shenzhouyu, "yeyan");
	room->loseHp(HpLostStruct(shenzhouyu, 3, "yeyan", shenzhouyu));

	foreach(ServerPlayer *sp, targets){
		damage(shenzhouyu, sp, sp->getMark("yeyan_damage"));
		sp->setMark("yeyan_damage",0);
	}
}

SmallYeyanCard::SmallYeyanCard()
{
    mute = true;
    m_skillName = "yeyan";
}

bool SmallYeyanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < 3;
}

void SmallYeyanCard::use(Room *room, ServerPlayer *shenzhouyu, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(shenzhouyu, "@flame");
    room->broadcastSkillInvoke("yeyan", 1);
    room->doSuperLightbox(shenzhouyu, "yeyan");
    YeyanCard::use(room, shenzhouyu, targets);
}

void SmallYeyanCard::onEffect(CardEffectStruct &effect) const
{
    damage(effect.from, effect.to, 1);
}

class Yeyan : public ViewAsSkill
{
public:
    Yeyan() : ViewAsSkill("yeyan")
    {
        frequency = Limited;
        limit_mark = "@flame";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@flame") >= 1;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() >= 4)
            return false;

        if (to_select->isEquipped())
            return false;

        if (Self->isJilei(to_select))
            return false;

        foreach (const Card *item, selected) {
            if (to_select->getSuit() == item->getSuit())
                return false;
        }

        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 0)
            return new SmallYeyanCard;
        if (cards.length() != 4)
            return nullptr;

        GreatYeyanCard *card = new GreatYeyanCard;
        card->addSubcards(cards);

        return card;
    }
};

class Qinyin : public TriggerSkill
{
public:
    Qinyin() : TriggerSkill("qinyin")
    {
        events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    void perform(ServerPlayer *shenzhouyu) const
    {
        Room *room = shenzhouyu->getRoom();
        QStringList choices;
        choices << "down" << "cancel";
        QList<ServerPlayer *> all_players = room->getAllPlayers();
        foreach (ServerPlayer *player, all_players) {
            if (player->isWounded()) {
                choices.prepend("up");
                break;
            }
        }
        QString result = room->askForChoice(shenzhouyu, objectName(), choices.join("+"));
        if (result == "cancel") return;
        if (result == "up") {
            room->broadcastSkillInvoke(objectName(), 2);
			room->notifySkillInvoked(shenzhouyu, "qinyin");
            foreach(ServerPlayer *player, all_players)
                room->recover(player, RecoverStruct(objectName(), shenzhouyu));
        } else if (result == "down") {
            int index = 1;
            if (room->findPlayer("caocao+shencaocao+yt_shencaocao"))
                index = 3;
            room->broadcastSkillInvoke(objectName(), index);
			room->notifySkillInvoked(shenzhouyu, "qinyin");
            foreach(ServerPlayer *player, all_players)
                room->loseHp(HpLostStruct(player, 1, "qinyin", shenzhouyu));
        }
    }

    bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *shenzhouyu, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (shenzhouyu->getPhase() == Player::Discard && move.from == shenzhouyu
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                shenzhouyu->addMark("qinyin", move.card_ids.size());
            }
        } else if (triggerEvent == EventPhaseEnd && TriggerSkill::triggerable(shenzhouyu)
            && shenzhouyu->getPhase() == Player::Discard && shenzhouyu->getMark("qinyin") >= 2) {
            perform(shenzhouyu);
        } else if (triggerEvent == EventPhaseChanging) {
            shenzhouyu->setMark("qinyin", 0);
        }
        return false;
    }
};

QixingCard::QixingCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void QixingCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("stars");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard, to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    if (to_handcard.length() != to_pile.length())
        return;

    room->broadcastSkillInvoke("qixing");
    room->notifySkillInvoked(card_use.from, "qixing");

    card_use.from->addToPile("stars", to_pile, false);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, false);

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "qixing";
    room->sendLog(log);
}

class QixingVS : public ViewAsSkill
{
public:
    QixingVS() : ViewAsSkill("qixing")
    {
        response_pattern = "@@qixing";
        expand_pile = "stars";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length()<2*Self->getPile("stars").length()&&!to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hand = 0, pile = 0;
        foreach (const Card *card, cards) {
            if (Self->getPile("stars").contains(card->getId()))
				pile++;
            else
                hand++;
        }
        if (hand == pile) {
            QixingCard *c = new QixingCard;
            c->addSubcards(cards);
            return c;
        }
        return nullptr;
    }
};

class Qixing : public TriggerSkill
{
public:
    Qixing() : TriggerSkill("qixing")
    {
        view_as_skill = new QixingVS;
        events << EventPhaseEnd << DrawNCards << AfterDrawNCards;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *shenzhuge, QVariant &data) const
    {
        if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards") return false;
            room->sendCompulsoryTriggerLog(shenzhuge, this);
			shenzhuge->addMark(objectName());
            draw.num += 7;
			data = QVariant::fromValue(draw);
        }else if(event == AfterDrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards"||shenzhuge->getMark(objectName())<1) return false;
			shenzhuge->setMark(objectName(),0);
            const Card *exchange_card = room->askForExchange(shenzhuge, "qixing", 7, 7);
            shenzhuge->addToPile("stars", exchange_card->getSubcards(), false);
		}else if(shenzhuge->getPhase()==Player::Draw&&shenzhuge->getPile("stars").length()>0)
			room->askForUseCard(shenzhuge, "@@qixing", "@qixing-exchange", -1, Card::MethodNone);
        return false;
    }
};

KuangfengCard::KuangfengCard()
{
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool KuangfengCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void KuangfengCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, effect.from->objectName(), "kuangfeng", "");
    effect.to->getRoom()->throwCard(this, reason, nullptr);
    effect.to->gainMark("&kuangfeng");
	effect.from->tag["kuangfengUse"] = true;
}

class KuangfengViewAsSkill : public OneCardViewAsSkill
{
public:
    KuangfengViewAsSkill() : OneCardViewAsSkill("kuangfeng")
    {
        response_pattern = "@@kuangfeng";
        filter_pattern = ".|.|.|stars";
        expand_pile = "stars";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        KuangfengCard *kf = new KuangfengCard;
        kf->addSubcard(originalCard);
        return kf;
    }
};

class Kuangfeng : public TriggerSkill
{
public:
    Kuangfeng() : TriggerSkill("kuangfeng")
    {
        events << DamageForseen << EventPhaseStart << Death;
        view_as_skill = new KuangfengViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if(event==DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature == DamageStruct::Fire && player->getMark("&kuangfeng") > 0) {
				LogMessage log;
				log.type = "#GalePower";
				log.from = player;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(++damage.damage);
				room->sendLog(log);
				data = QVariant::fromValue(damage);
			}
		}else {
			if(event==EventPhaseStart){
				if(player->getPhase()==Player::Finish){
					if (player->getPile("stars").length()>0&&player->hasSkill(objectName()))
						room->askForUseCard(player, "@@kuangfeng", "@kuangfeng-card", -1, Card::MethodNone);
				}
				if(player->getPhase() != Player::RoundStart)
					return false;
			}else if(event==Death){
                DeathStruct death = data.value<DeathStruct>();
                if (death.who != player) return false;
			}
			if(player->tag["kuangfengUse"].toBool()){
				player->tag["kuangfengUse"] = false;
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					p->loseMark("&kuangfeng");
				}
			}
		}
        return false;
    }
};

DawuCard::DawuCard()
{
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool DawuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < subcards.length();
}

bool DawuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcards.length();
}

void DawuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "dawu", "");
    room->throwCard(this, reason, nullptr);

    source->tag["dawuUse"] = true;

    foreach(ServerPlayer *target, targets)
        target->gainMark("&dawu");
}

class DawuViewAsSkill : public ViewAsSkill
{
public:
    DawuViewAsSkill() : ViewAsSkill("dawu")
    {
        response_pattern = "@@dawu";
        expand_pile = "stars";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return Self->getPile("stars").contains(to_select->getId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (!cards.isEmpty()) {
            DawuCard *dw = new DawuCard;
            dw->addSubcards(cards);
            return dw;
        }

        return nullptr;
    }
};

class Dawu : public TriggerSkill
{
public:
    Dawu() : TriggerSkill("dawu")
    {
        events << DamageForseen << EventPhaseStart << Death;
        view_as_skill = new DawuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature!=DamageStruct::Thunder&&player->getMark("&dawu")>0) {
				LogMessage log;
				log.type = "#FogProtect";
				log.from = player;
				log.arg = QString::number(damage.damage);
				log.arg2 = "normal_nature";
				if (damage.nature == DamageStruct::Fire)
					log.arg2 = "fire_nature";
				room->sendLog(log);
				return true;
			}
		}else{
			if(event==EventPhaseStart){
				if(player->getPhase()==Player::Finish){
					if (player->getPile("stars").length()>0&&player->hasSkill(objectName()))
						room->askForUseCard(player, "@@dawu", "@dawu-card", -1, Card::MethodNone);
				}
				if(player->getPhase() != Player::RoundStart)
					return false;
			}else if(event==Death){
                DeathStruct death = data.value<DeathStruct>();
                if (death.who != player) return false;
			}
			if(player->tag["dawuUse"].toBool()){
				player->tag["dawuUse"] = false;
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					p->loseMark("&dawu");
				}
			}
		}
		return false;
    }
};

class TenyearJianchu : public TriggerSkill
{
public:
    TenyearJianchu() : TriggerSkill("tenyearjianchu")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        foreach (ServerPlayer *p, use.to) {
            if (player->isDead() || !player->hasSkill(objectName())) break;
            if (p->isDead()) continue;
            if (!player->canDiscard(p, "he") || !player->askForSkillInvoke(objectName()+"$-1", QVariant::fromValue(p))) continue;
            int to_throw = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
            const Card *card = Sanguosha->getCard(to_throw);
            room->throwCard(card, p, player);
            if (card->isKindOf("EquipCard")) {
                LogMessage log;
                log.type = "#NoJink";
                log.from = p;
                room->sendLog(log);
                use.no_respond_list << p->objectName();
                data = QVariant::fromValue(use);
            } else {
                if (!room->CardInTable(use.card)) continue;
                p->obtainCard(use.card, true);
            }
        }
        return false;
    }
};





FirePackage::FirePackage()
    : Package("fire")
{
    General *dianwei = new General(this, "dianwei", "wei"); // WEI 012
    dianwei->addSkill(new Qiangxi);

    General *xunyu = new General(this, "xunyu", "wei", 3); // WEI 013
    xunyu->addSkill(new Quhu);
    xunyu->addSkill(new Jieming);

    General *pangtong = new General(this, "pangtong", "shu", 3); // SHU 010
    pangtong->addSkill(new Lianhuan);
    pangtong->addSkill(new Niepan);

    General *wolong = new General(this, "wolong", "shu", 3); // SHU 011
    wolong->addSkill(new Bazhen);
    wolong->addSkill(new BazhenTrigger);
    wolong->addSkill(new Huoji);
    wolong->addSkill(new Kanpo);
    related_skills.insertMulti("bazhen", "#bazhen");

    General *taishici = new General(this, "taishici", "wu"); // WU 012
    taishici->addSkill(new Tianyi);
    taishici->addSkill(new TianyiTargetMod);
    related_skills.insertMulti("tianyi", "#tianyi-target");

    General *yuanshao = new General(this, "yuanshao$", "qun"); // QUN 004
    yuanshao->addSkill(new Luanji);
    yuanshao->addSkill(new Xueyi);
    yuanshao->addSkill(new XueyiMCS);

    General *yanliangwenchou = new General(this, "yanliangwenchou", "qun"); // QUN 005
    yanliangwenchou->addSkill(new Shuangxiong);

    General *pangde = new General(this, "pangde", "qun"); // QUN 008
    pangde->addSkill("mashu");
    pangde->addSkill(new Mengjin);

    General *tenyear_pangde = new General(this, "tenyear_pangde", "qun", 4);
    tenyear_pangde->addSkill(new TenyearJianchu);
    tenyear_pangde->addSkill("mashu");

    General *shenzhouyu = new General(this, "shenzhouyu", "god"); // LE 003
    shenzhouyu->addSkill(new Qinyin);
    shenzhouyu->addSkill(new Yeyan);
    addMetaObject<YeyanCard>();
    addMetaObject<GreatYeyanCard>();
    addMetaObject<SmallYeyanCard>();

    General *shenzhugeliang = new General(this, "shenzhugeliang", "god", 3); // LE 004
    shenzhugeliang->addSkill(new Qixing);
    shenzhugeliang->addSkill(new Kuangfeng);
    shenzhugeliang->addSkill(new Dawu);
    addMetaObject<QixingCard>();
    addMetaObject<KuangfengCard>();
    addMetaObject<DawuCard>();


    addMetaObject<QuhuCard>();
    addMetaObject<QiangxiCard>();
    addMetaObject<TianyiCard>();
}
ADD_PACKAGE(Fire)