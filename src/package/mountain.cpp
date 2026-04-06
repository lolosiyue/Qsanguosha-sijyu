#include "mountain.h"
//#include "general.h"
//#include "settings.h"
#include "engine.h"
#include "standard.h"
#include "clientplayer.h"
//#include "client.h"
//#include "ai.h"
//#include "json.h"
//#include "util.h"
#include "room.h"
#include "roomthread.h"
#include "maneuvering.h"

QiaobianCard::QiaobianCard()
{
    mute = true;
    m_skillName = "qiaobian";
}

bool QiaobianCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Player::Phase phase = (Player::Phase)Self->getMark(m_skillName + "Phase");
    if (phase == Player::Draw)
        return targets.length() <= 2 && !targets.isEmpty();
    else if (phase == Player::Play)
        return targets.length() == 1;
    return false;
}

bool QiaobianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Player::Phase phase = (Player::Phase)Self->getMark(m_skillName + "Phase");
    if (phase == Player::Draw)
        return targets.length() < 2 && to_select != Self && !to_select->isKongcheng();
    else if (phase == Player::Play)
        return targets.isEmpty() && (!to_select->getJudgingArea().isEmpty() || !to_select->getEquips().isEmpty());
    return false;
}

void QiaobianCard::use(Room *room, ServerPlayer *zhanghe, QList<ServerPlayer *> &targets) const
{
    Player::Phase phase = (Player::Phase)zhanghe->getMark(m_skillName + "Phase");
    if (phase == Player::Draw) {
        foreach (ServerPlayer *target, targets) {
            if (zhanghe->isAlive() && target->isAlive())
                room->cardEffect(this, zhanghe, target);
        }
    } else if (phase == Player::Play) {
        if (targets.isEmpty()) return;
        ServerPlayer *from = targets.first();
        if (!from->hasEquip() && from->getJudgingArea().isEmpty()) return;
        int card_id = room->askForCardChosen(zhanghe, from, "ej", m_skillName);
        const Card *card = Sanguosha->getCard(card_id);
        Player::Place place = room->getCardPlace(card_id);

        int equip_index = -1;
        if (place == Player::PlaceEquip) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            equip_index = static_cast<int>(equip->location());
        }

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (equip_index > -1) {
                if (p->getEquip(equip_index) == nullptr)
                    tos << p;
            } else {
                if (!zhanghe->isProhibited(p, card) && !p->containsTrick(card->objectName()))
                    tos << p;
            }
        }

        QString tag = "QiaobianTarget";
        if (m_skillName == "olqiaobian")
            tag = "OLQiaobianTarget";

        room->setTag(tag, QVariant::fromValue(from));
        ServerPlayer *to = room->askForPlayerChosen(zhanghe, tos, m_skillName, "@qiaobian-to:::" + card->objectName());
        if (to){
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, from->objectName(), to->objectName());
            room->moveCardTo(card, from, to, place,
			CardMoveReason(CardMoveReason::S_REASON_TRANSFER, zhanghe->objectName(), m_skillName, ""), true);
		}
        room->removeTag(tag);
    }
}

void QiaobianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (!effect.to->isKongcheng()) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "h", m_skillName);
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, false);
    }
}

class QiaobianViewAsSkill : public ZeroCardViewAsSkill
{
public:
    QiaobianViewAsSkill() : ZeroCardViewAsSkill("qiaobian")
    {
        response_pattern = "@@qiaobian";
    }

    const Card *viewAs() const
    {
        return new QiaobianCard;
    }
};

class Qiaobian : public TriggerSkill
{
public:
    Qiaobian() : TriggerSkill("qiaobian")
    {
        events << EventPhaseChanging;
        view_as_skill = new QiaobianViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && target->canDiscard(target, "h");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhanghe, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        room->setPlayerMark(zhanghe, "qiaobianPhase", (int)change.to);
        int index = 0;
        switch (change.to) {
        case Player::RoundStart:
        case Player::Start:
        case Player::Finish:
        case Player::NotActive: return false;

        case Player::Judge: index = 1; break;
        case Player::Draw: index = 2; break;
        case Player::Play: index = 3; break;
        case Player::Discard: index = 4; break;
        case Player::PhaseNone: Q_ASSERT(false);
        }

        QString discard_prompt = QString("#qiaobian-%1").arg(index);
        QString use_prompt = QString("@qiaobian-%1").arg(index);
        if (index > 0 && room->askForDiscard(zhanghe, objectName(), 1, 1, true, false, discard_prompt)) {
            room->broadcastSkillInvoke("qiaobian", index);
            if (!zhanghe->isAlive()) return false;
            if (!zhanghe->isSkipped(change.to) && (index == 2 || index == 3))
                room->askForUseCard(zhanghe, "@@qiaobian", use_prompt, index);
            zhanghe->skip(change.to, true);
        }
        return false;
    }
};

class Beige : public TriggerSkill
{
public:
    Beige() : TriggerSkill("beige")
    {
        events << Damaged << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || damage.to->isDead())
                return false;

            foreach (ServerPlayer *caiwenji, room->getAllPlayers()) {
                if (!TriggerSkill::triggerable(caiwenji)) continue;
                if (caiwenji->canDiscard(caiwenji, "he") && room->askForCard(caiwenji, "..", "@beige", data, objectName())) {
                    JudgeStruct judge;
                    judge.good = true;
                    judge.play_animation = false;
                    judge.who = player;
                    judge.reason = objectName();

                    room->judge(judge);

                    Card::Suit suit = (Card::Suit)(judge.pattern.toInt());
                    switch (suit) {
                    case Card::Heart: {
                        room->broadcastSkillInvoke(objectName(), 4);
                        room->recover(player, RecoverStruct("beige", caiwenji));

                        break;
                    }
                    case Card::Diamond: {
                        room->broadcastSkillInvoke(objectName(), 3);
                        player->drawCards(2, objectName());
                        break;
                    }
                    case Card::Club: {
                        room->broadcastSkillInvoke(objectName(), 1);
                        if (damage.from && damage.from->isAlive())
                            room->askForDiscard(damage.from, "beige", 2, 2, false, true);

                        break;
                    }
                    case Card::Spade: {
                        room->broadcastSkillInvoke(objectName(), 2);
                        if (damage.from && damage.from->isAlive())
                            damage.from->turnOver();

                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        } else {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason != objectName()) return false;
            judge->pattern = QString::number(int(judge->card->getSuit()));
        }
        return false;
    }
};

class Duanchang : public TriggerSkill
{
public:
    Duanchang() : TriggerSkill("duanchang")
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

        if (death.damage && death.damage->from) {
            LogMessage log;
            log.type = "#DuanchangLoseSkills";
            log.from = player;
            log.to << death.damage->from;
            log.arg = objectName();
            room->sendLog(log);
            int index = qrand() % 2 + 2;
            if (player->isJieGeneral())
                index += 2;
            player->peiyin(this, index);
            room->notifySkillInvoked(player, objectName());

            QStringList detachList;
            foreach (const Skill *skill, death.damage->from->getVisibleSkillList()) {
                if (!skill->inherits("SPConvertSkill") && !skill->isAttachedLordSkill())
                    detachList.append("-" + skill->objectName());
            }
            room->handleAcquireDetachSkills(death.damage->from, detachList);
            if (death.damage->from->isAlive())
                death.damage->from->gainMark("@duanchang");
        }

        return false;
    }
};

class Tuntian : public TriggerSkill
{
public:
    Tuntian() : TriggerSkill("tuntian")
    {
        events << CardsMoveOneTime << FinishJudge;
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && !target->hasFlag("CurrentPlayer");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                && !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
                && player->askForSkillInvoke("tuntian", data)) {
                room->broadcastSkillInvoke("tuntian");
                JudgeStruct judge;
                judge.pattern = ".|heart";
                judge.good = false;
                judge.reason = "tuntian";
                judge.who = player;
                room->judge(judge);
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == "tuntian" && judge->isGood() && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                player->addToPile("field", judge->card->getEffectiveId());
        }

        return false;
    }
};

class TuntianDistance : public DistanceSkill
{
public:
    TuntianDistance() : DistanceSkill("#tuntian-dist")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        int n = from->getPile("field").length();
		if (n>0&&from->hasSkill("tuntian"))
            return -n;
        return 0;
    }
};

class Zaoxian : public PhaseChangeSkill
{
public:
    Zaoxian() : PhaseChangeSkill("zaoxian")
    {
        frequency = Wake;
        waked_skills = "jixi";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() &&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *dengai, Room *room) const
    {
        if (dengai->getPile("field").length() >= 3) {
            LogMessage log;
            log.type = "#ZaoxianWake";
            log.from = dengai;
            log.arg = QString::number(dengai->getPile("field").length());
            log.arg2 = objectName();
            log.arg3 = "field";
            room->sendLog(log);
        }else if(!dengai->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(dengai, objectName());

        //room->doLightbox("$ZaoxianAnimate", 4000);

        room->doSuperLightbox(dengai, "zaoxian");

        room->setPlayerMark(dengai, "zaoxian", 1);
        if (room->changeMaxHpForAwakenSkill(dengai, -1, objectName()))
            room->acquireSkill(dengai, "jixi");

        return false;
    }
};

class Jixi : public OneCardViewAsSkill
{
public:
    Jixi() : OneCardViewAsSkill("jixi")
    {
        filter_pattern = ".|.|.|field";
        expand_pile = "field";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->getPile("field").isEmpty();
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Snatch *snatch = new Snatch(originalCard->getSuit(), originalCard->getNumber());
        snatch->setSkillName(objectName());
        snatch->addSubcard(originalCard);
        return snatch;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->isJieGeneral())
            index += 2;
        return index;
    }
};

class Jiang : public TriggerSkill
{
public:
    Jiang() : TriggerSkill("jiang")
    {
        events << TargetSpecified << TargetConfirmed;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *sunce, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == TargetSpecified || (triggerEvent == TargetConfirmed && use.to.contains(sunce))) {
            if (use.card->isKindOf("Duel") || (use.card->isKindOf("Slash") && use.card->isRed())) {
                if (sunce->askForSkillInvoke(this, data)) {
                    int index = qrand()%2+1;
                    if (sunce->hasSkill("mouduan",true))
                        index += 2;
                    if (sunce->isJieGeneral())
                        index += 4;
                    room->broadcastSkillInvoke(objectName(), index,sunce);
                    sunce->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

class Hunzi : public PhaseChangeSkill
{
public:
    Hunzi() : PhaseChangeSkill("hunzi")
    {
        frequency = Wake;
		waked_skills = "nosyingzi,yinghun";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() &&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *sunce, Room *room) const
    {
        if (sunce->getHp() == 1) {
            LogMessage log;
            log.type = "#HunziWake";
            log.from = sunce;
            log.arg = QString::number(sunce->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!sunce->canWake(objectName()))
			return false;

        int index = qrand()%2+1;
        if (sunce->hasSkill("xiongyisy",true))
            index += 2;
        room->broadcastSkillInvoke(objectName(), index, sunce);
        room->notifySkillInvoked(sunce, objectName());
        //room->doLightbox("$HunziAnimate", 5000);

        room->doSuperLightbox(sunce, "hunzi");

        room->setPlayerMark(sunce, "hunzi", 1);
        if (room->changeMaxHpForAwakenSkill(sunce, -1, objectName()))
            room->handleAcquireDetachSkills(sunce, "nosyingzi|yinghun");
        return false;
    }
};

ZhibaCard::ZhibaCard()
{
	mute = true;
}

bool ZhibaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->hasLordSkill("zhiba") && Self->canPindian(to_select);
}

void ZhibaCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QVariant data = QVariant::fromValue(card_use);
	room->getThread()->trigger(PreCardUsed, room, card_use.from, data);
	room->getThread()->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
	foreach (ServerPlayer *sunce, card_use.to) {
		if (sunce->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,sunce);
			room->notifySkillInvoked(sunce, "weidi");
		}else
			room->broadcastSkillInvoke("zhiba",1,sunce);
		card_use.from->skillInvoked("zhiba",0,sunce);
		if ((sunce->getMark("hunzi") > 0 || sunce->getMark("mobilehunzi") > 0) && room->askForChoice(sunce, "zhiba_pindian", "accept+reject") == "reject") {
			LogMessage log;
			log.type = "#ZhibaReject";
			log.from = sunce;
			log.to << card_use.from;
			log.arg = "zhiba_pindian";
			room->sendLog(log);
			room->broadcastSkillInvoke("zhiba",4,sunce);
			continue;
		}
		PindianStruct *pindian = card_use.from->PinDian(sunce, "zhiba_pindian");
		if (!pindian) continue;
		if (pindian->from_number > pindian->to_number)
			room->broadcastSkillInvoke("zhiba",3,sunce);
		else {
			room->broadcastSkillInvoke("zhiba",2,sunce);
			if (pindian->to->isAlive()) {
				DummyCard *dummy = new DummyCard();
				int from_card_id = pindian->from_card->getEffectiveId();
				int to_card_id = pindian->to_card->getEffectiveId();
				if (room->getCardPlace(from_card_id) == Player::DiscardPile)
					dummy->addSubcard(from_card_id);
				if (room->getCardPlace(to_card_id) == Player::DiscardPile && from_card_id != to_card_id)
					dummy->addSubcard(to_card_id);
				if (dummy->subcardsLength()>0 && room->askForChoice(pindian->to, "zhiba_pindian_obtain", "obtainPindianCards+reject") != "reject")
					pindian->to->obtainCard(dummy);
				delete dummy;
			}
		}
	}
	room->getThread()->trigger(CardFinished, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
}

class ZhibaPindian : public ZeroCardViewAsSkill
{
public:
    ZhibaPindian() : ZeroCardViewAsSkill("zhiba_pindian")
    {
        attached_lord_skill = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return shouldBeVisible(player) && player->canPindian() && hasTarget(player);
    }

    bool shouldBeVisible(const Player *player) const
    {
        return player && player->getKingdom() == "wu";
    }

    bool hasTarget(const Player *player) const
    {
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->hasLordSkill("zhiba") && player->usedTimes("ZhibaCard")<1)
                return true;
        }
        return false;
    }

    const Card *viewAs() const
    {
        return new ZhibaCard;
    }
};

class Zhiba : public TriggerSkill
{
public:
    Zhiba() : TriggerSkill("zhiba$")
    {
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventAcquireSkill&&player->hasLordSkill(this,true)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("zhiba_pindian",true)){
					room->attachSkillToPlayer(p, "zhiba_pindian");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
        if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "zhiba_pindian");
					break;
				}
			}
        }else{
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasFlag("ZhibaInvoked"))
					room->setPlayerFlag(p, "-ZhibaInvoked");
			}
			if (player->hasSkill("zhiba_pindian",true))
				room->detachSkillFromPlayer(player, "zhiba_pindian", true);
		}
        return false;
    }
};

TiaoxinCard::TiaoxinCard()
{
}

bool TiaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->inMyAttackRange(Self);
}

void TiaoxinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool use_slash = false;
    if (effect.to->canSlash(effect.from, nullptr, false))
        use_slash = room->askForUseSlashTo(effect.to, effect.from, "@tiaoxin-slash:" + effect.from->objectName());
    if (!use_slash && effect.from->canDiscard(effect.to, "he"))
        room->throwCard(room->askForCardChosen(effect.from, effect.to, "he", "tiaoxin", false, Card::MethodDiscard), effect.to, effect.from);
}

class Tiaoxin : public ZeroCardViewAsSkill
{
public:
    Tiaoxin() : ZeroCardViewAsSkill("tiaoxin")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TiaoxinCard");
    }

    const Card *viewAs() const
    {
        return new TiaoxinCard;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (!player->hasInnateSkill(this) && player->hasSkill("baobian"))
            index += 3;
        else if (!player->hasInnateSkill(this) && player->getMark("fengliang") > 0)
            index += 5;
        else if (player->hasArmorEffect("eight_diagram"))
            index = 3;
        return index;
    }
};

class Zhiji : public PhaseChangeSkill
{
public:
    Zhiji() : PhaseChangeSkill("zhiji")
    {
        frequency = Wake;
		waked_skills = "guanxing";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() &&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *jiangwei, Room *room) const
    {
        if (jiangwei->isKongcheng()) {
            LogMessage log;
            log.type = "#ZhijiWake";
            log.from = jiangwei;
            log.arg = objectName();
            room->sendLog(log);
        }else if(!jiangwei->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(jiangwei, objectName());

        //room->doLightbox("$ZhijiAnimate", 4000);

        room->doSuperLightbox(jiangwei, "zhiji");

        room->setPlayerMark(jiangwei, "zhiji", 1);
        if (room->changeMaxHpForAwakenSkill(jiangwei, -1, objectName())) {
            if (jiangwei->isWounded() && room->askForChoice(jiangwei, objectName(), "recover+draw") == "recover")
                room->recover(jiangwei, RecoverStruct("zhiji", jiangwei));
            else
                room->drawCards(jiangwei, 2, objectName());
            room->acquireSkill(jiangwei, "guanxing");
        }

        return false;
    }
};

ZhijianCard::ZhijianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->getEquip(equip_index) == nullptr && !Self->isProhibited(to_select, card);
}

void ZhijianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *erzhang = effect.from;
    erzhang->getRoom()->moveCardTo(this, erzhang, effect.to, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_PUT,
        erzhang->objectName(), "zhijian", ""));

    LogMessage log;
    log.type = "$ZhijianEquip";
    log.from = effect.to;
    log.card_str = QString::number(getEffectiveId());
    erzhang->getRoom()->sendLog(log);

    erzhang->drawCards(1, "zhijian");
}

class Zhijian : public OneCardViewAsSkill
{
public:
    Zhijian() :OneCardViewAsSkill("zhijian")
    {
        filter_pattern = "EquipCard|.|.|hand";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ZhijianCard *zhijian_card = new ZhijianCard();
        zhijian_card->addSubcard(originalCard);
        return zhijian_card;
    }
};

GuzhengCard::GuzhengCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void GuzhengCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->setTag("guzheng_card", subcards.first());
}

class GuzhengVS : public OneCardViewAsSkill
{
public:
    GuzhengVS() : OneCardViewAsSkill("guzheng")
    {
        response_pattern = "@@guzheng";
    }

    bool viewFilter(const Card *to_select) const
    {
        QStringList l = Self->property("guzheng_toget").toString().split("+");
        QList<int> li = ListS2I(l);
        return li.contains(to_select->getId());
    }

    const Card *viewAs(const Card *originalCard) const
    {
        GuzhengCard *gz = new GuzhengCard;
        gz->addSubcard(originalCard);
        return gz;
    }
};

class Guzheng : public TriggerSkill
{
public:
    Guzheng() : TriggerSkill("guzheng")
    {
        events << CardsMoveOneTime << EventPhaseEnd;
        view_as_skill = new GuzhengVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime&&player->getPhase()==Player::Discard) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				QVariantList guzhengToGet = room->getTag("GuzhengToGet").toList();
                int i = 0;
                foreach (int card_id, move.card_ids) {
                    if (move.from_places[i] == Player::PlaceHand)
                        guzhengToGet << card_id;
                    i++;
                }
				room->setTag("GuzhengToGet",guzhengToGet);
            }
        } else if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::Discard) {
            QVariantList lids = room->getTag("GuzhengToGet").toList();
			if(lids.isEmpty()) return false;
			room->removeTag("GuzhengToGet");
			QList<int>ids,ids2 = ListV2I(lids);
            foreach (int id, room->getDiscardPile()) {
				if(ids2.contains(id)) ids << id;
			}
			if(ids.isEmpty()||player->isDead()) return false;
			lids = ListI2V(ids);
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				p->setTag("GuzhengToGet", lids);
				if(p->isAlive()&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",player)){
					room->fillAG(ids,p);
					int id = room->askForAG(p,ids,false,objectName(),"@guzheng:" + player->objectName());
					room->clearAG(p);
					ids.removeOne(id);
					room->obtainCard(player,id);
					p->obtainCard(dummyCard(ids));
					break;
				}
			}
			/*ServerPlayer *erzhang = room->findPlayerBySkillName(objectName());
            if (erzhang == nullptr)
                return false;

            QVariantList guzheng_cardsToGet = erzhang->getTag("GuzhengToGet").toList();
            QVariantList guzheng_cardsOther = erzhang->getTag("GuzhengOther").toList();
            erzhang->removeTag("GuzhengToGet");
            erzhang->removeTag("GuzhengOther");

            if (player->isDead())
                return false;

            QList<int> cardsToGet;
            foreach (QVariant card_data, guzheng_cardsToGet) {
                int card_id = card_data.toInt();
                if (room->getCardPlace(card_id) == Player::DiscardPile)
                    cardsToGet << card_id;
            }
            QList<int> cardsOther;
            foreach (QVariant card_data, guzheng_cardsOther) {
                int card_id = card_data.toInt();
                if (room->getCardPlace(card_id) == Player::DiscardPile)
                    cardsOther << card_id;
            }


            if (cardsToGet.isEmpty())
                return false;

            QList<int> cards = cardsToGet + cardsOther;

            QString cardsList = ListI2S(cards).join("+");
            room->setPlayerProperty(erzhang, "guzheng_allCards", cardsList);
            QString toGetList = ListI2S(cardsToGet).join("+");
            room->setPlayerProperty(erzhang, "guzheng_toget", toGetList);

            erzhang->removeTag("guzheng_card");
            room->setPlayerFlag(erzhang, "guzheng_InTempMoving");
            CardMoveReason r(CardMoveReason::S_REASON_UNKNOWN, erzhang->objectName());
            CardsMoveStruct fake_move(cards, nullptr, erzhang, Player::DiscardPile, Player::PlaceHand, r);
            QList<CardsMoveStruct> moves;
            moves << fake_move;
            QList<ServerPlayer *> _erzhang;
            _erzhang << erzhang;
            room->notifyMoveCards(true, moves, true, _erzhang);
            room->notifyMoveCards(false, moves, true, _erzhang);
            bool invoke = room->askForUseCard(erzhang, "@@guzheng", "@guzheng:" + player->objectName(), -1, Card::MethodNone);
            CardsMoveStruct fake_move2(cards, erzhang, nullptr, Player::PlaceHand, Player::DiscardPile, r);
            QList<CardsMoveStruct> moves2;
            moves2 << fake_move2;
            room->notifyMoveCards(true, moves2, true, _erzhang);
            room->notifyMoveCards(false, moves2, true, _erzhang);
            room->setPlayerFlag(erzhang, "-guzheng_InTempMoving");

            if (invoke && erzhang->getTag("guzheng_card").isValid()) {
                bool ok = false;
                int to_back = erzhang->getTag("guzheng_card").toInt(&ok);
                if (ok) {
                    player->obtainCard(Sanguosha->getCard(to_back));
                    cards.removeOne(to_back);
                    DummyCard *dummy = new DummyCard(cards);
                    room->obtainCard(erzhang, dummy);
                    delete dummy;
                }
            }*/
        }
        return false;
    }
};

class Xiangle : public TriggerSkill
{
public:
    Xiangle() : TriggerSkill("xiangle")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liushan, QVariant &data) const
    {
        if (triggerEvent == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash")) {
                int index = qrand() % 2 + 1;
                if (liushan->isJieGeneral())
                    index += 2;
                room->broadcastSkillInvoke(objectName(), index);
                room->sendCompulsoryTriggerLog(liushan, objectName());

                QVariant dataforai = QVariant::fromValue(liushan);
                if (!room->askForCard(use.from, ".Basic", "@xiangle-discard", dataforai)) {
                    use.nullified_list << liushan->objectName();
                    data = QVariant::fromValue(use);
                }
            }
        }

        return false;
    }
};

FangquanCard::FangquanCard()
{
	mute = true;
}

bool FangquanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void FangquanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *liushan = effect.from, *player = effect.to;

    LogMessage log;
    log.type = "#Fangquan";
    log.from = liushan;
    log.to << player;
    room->sendLog(log);

    room->setTag("FangquanTarget", QVariant::fromValue(player));
}

class FangquanViewAsSkill : public OneCardViewAsSkill
{
public:
    FangquanViewAsSkill() : OneCardViewAsSkill("fangquan")
    {
        filter_pattern = ".|.|.|hand!";
        response_pattern = "@@fangquan";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FangquanCard *fangquan = new FangquanCard;
        fangquan->addSubcard(originalCard);
        return fangquan;
    }
};

class Fangquan : public TriggerSkill
{
public:
    Fangquan() : TriggerSkill("fangquan")
    {
        events << EventPhaseChanging << EventPhaseStart;
        view_as_skill = new FangquanViewAsSkill;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseStart) return 1;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liushan, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            switch (change.to) {
            case Player::Play: {
                if (!TriggerSkill::triggerable(liushan) || liushan->isSkipped(Player::Play))
                    return false;
                if (liushan->askForSkillInvoke(this)) {
                    liushan->setFlags(objectName());
                    liushan->peiyin(this);
                    liushan->skip(Player::Play, true);
                }
                break;
            }
            case Player::NotActive: {
                if (liushan->hasFlag(objectName())) {
                    if (!liushan->canDiscard(liushan, "h"))
                        return false;
                    room->askForUseCard(liushan, "@@fangquan", "@fangquan-give", -1, Card::MethodDiscard);
                }
                break;
            }
            default:
                break;
            }
        } else if (triggerEvent == EventPhaseStart && liushan->getPhase() == Player::NotActive) {
            Room *room = liushan->getRoom();
            if (!room->getTag("FangquanTarget").isNull()) {
                ServerPlayer *target = room->getTag("FangquanTarget").value<ServerPlayer *>();
                room->removeTag("FangquanTarget");
                if (target->isAlive())
                    target->gainAnExtraTurn();
            }
        }
        return false;
    }
};

class Ruoyu : public PhaseChangeSkill
{
public:
    Ruoyu() : PhaseChangeSkill("ruoyu$")
    {
        frequency = Wake;
		waked_skills = "jijiang";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasLordSkill(this);
    }

    bool onPhaseChange(ServerPlayer *liushan, Room *room) const
    {
        if (liushan->isLowestHpPlayer()) {
            LogMessage log;
            log.type = "#RuoyuWake";
            log.from = liushan;
            log.arg = QString::number(liushan->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!liushan->canWake(objectName()))
			return false;
        if (liushan->isWeidi())
            room->broadcastSkillInvoke("weidi");
        else
            room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(liushan, objectName());
		room->doSuperLightbox(liushan, "ruoyu");

        room->setPlayerMark(liushan, "ruoyu", 1);
        if (room->changeMaxHpForAwakenSkill(liushan, 1, objectName())) {
            room->recover(liushan, RecoverStruct("ruoyu", liushan));
            room->acquireSkill(liushan, "jijiang");
        }
        return false;
    }
};

class Huashen : public GameStartSkill
{
public:
    Huashen() : GameStartSkill("huashen")
    {
    }

    static void playAudioEffect(ServerPlayer *zuoci, const QString &skill_name)
    {
        Room *room = zuoci->getRoom();
        int index = qrand() % 2 + 1;
        if (zuoci->isJieGeneral() && skill_name == "xinsheng")
            index = qrand() % 2 + 5;
        else {
            if (zuoci->isFemale())
                index += 2;
        }
        room->broadcastSkillInvoke(skill_name, index);
    }

    static void AcquireGenerals(ServerPlayer *zuoci, int n)
    {
        Room *room = zuoci->getRoom();
        QStringList list = GetAvailableGenerals(zuoci);
        if (list.isEmpty()) return;
        qShuffle(list);
        n = qMin(n, list.length());
        
        QStringList huashens = zuoci->property("Huashens").toString().split("+");
        if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();

        QStringList acquired = list.mid(0, n);
        foreach (QString name, acquired) {
            huashens << name;
            const General *general = Sanguosha->getGeneral(name);
            if (general) {
                foreach (const TriggerSkill *skill, general->getTriggerSkills())
                    room->getThread()->addTriggerSkill(skill);
            }
        }
        
        QString pile_str = huashens.join("+");
        
        room->setPlayerProperty(zuoci, "Huashens", pile_str);
        
        room->setPlayerProperty(zuoci, "huashen_general", pile_str);

        QStringList hidden;
        for (int i = 0; i < n; i++) hidden << "unknown";
        room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), hidden.join(":"), room->getOtherPlayers(zuoci));
        room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), acquired.join(":"), QList<ServerPlayer *>() << zuoci);

        LogMessage log;
        log.type = "#GetHuashen";
        log.from = zuoci;
        log.arg = QString::number(n);
        log.arg2 = QString::number(huashens.length());
        room->sendLog(log);

        log.type = "#GetHuashenDetail";
        log.arg = acquired.join("\\, \\");
        room->sendLog(log, zuoci);

        room->setPlayerMark(zuoci, "@huashen", huashens.length());
    }

    static QStringList GetAvailableGenerals(ServerPlayer *zuoci)
    {
        QStringList all = Sanguosha->getLimitedGeneralNames();
        Room *room = zuoci->getRoom();
        if (room->getMode() == "06_XMode") {
            foreach(ServerPlayer *p, room->getAlivePlayers())
                all << p->getTag("XModeBackup").toStringList();
        } else if (room->getMode() == "02_1v1") {
            foreach(ServerPlayer *p, room->getAlivePlayers())
                all << p->getTag("1v1Arrange").toStringList();
        }
        QSet<QString> huashen_set, room_set;
        foreach(QString huashen, zuoci->property("Huashens").toString().split("+")){
			if(huashen.isEmpty()) continue;
            huashen_set << huashen;
		}
        foreach (ServerPlayer *player, room->getAlivePlayers()) {
            QString name = player->getGeneralName();/*
            if (Sanguosha->isGeneralHidden(name)) {
                QString fname = Sanguosha->findConvertFrom(name);
                if (!fname.isEmpty()) name = fname;
            }*/
            room_set << name;

            if (!player->getGeneral2()) continue;

            name = player->getGeneral2Name();/*
            if (Sanguosha->isGeneralHidden(name)) {
                QString fname = Sanguosha->findConvertFrom(name);
                if (!fname.isEmpty()) name = fname;
            }*/
            room_set << name;
        }

        static QSet<QString> banned;
        if (banned.isEmpty()) {
            banned << "zuoci" << "guzhielai" << "dengshizai" << "yt_caochong" << "jiangboyue" << "ol_zuoci";
        }
        return (QSet<QString>(all.begin(), all.end()) - banned - huashen_set - room_set).values();
    }

    static void SelectSkill(ServerPlayer *zuoci)
    {
        Room *room = zuoci->getRoom();

        QStringList huashens = zuoci->property("Huashens").toString().split("+");
		if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();
        if (huashens.isEmpty()) return;
        QStringList ac_dt_list;
        QString huashen_skill = zuoci->getTag("HuashenSkill").toString();
        if (!huashen_skill.isEmpty())
            ac_dt_list.append("-" + huashen_skill);

        QString skill_name;
        QStringList skill_names;
        const General *general = nullptr;
        AI* ai = zuoci->getAI();
        if (ai) {
            QHash<QString, const General *> hash;
            foreach (QString general_name, huashens) {
                const General *general = Sanguosha->getGeneral(general_name);
                foreach (const Skill *skill, general->getVisibleSkillList()) {
                    if (skill->isLordSkill()
                        || skill->isLimitedSkill()
                        || skill->getFrequency() == Skill::Wake)
                        continue;
					hash[skill->objectName()] = general;
					skill_names << skill->objectName();
                }
            }
            if (skill_names.isEmpty()) return;
            skill_name = ai->askForChoice("huashen", skill_names.join("+"), QVariant());
            general = hash[skill_name];
        } else {
            QString general_name = room->askForGeneral(zuoci, huashens);
            general = Sanguosha->getGeneral(general_name);

            foreach (const Skill *skill, general->getVisibleSkillList()) {
                if (skill->isLordSkill()
                    || skill->isLimitedSkill()
                    || skill->getFrequency() == Skill::Wake)
                    continue;
                skill_names << skill->objectName();
            }
            if (skill_names.isEmpty()) return;
            skill_name = room->askForChoice(zuoci, "huashen", skill_names.join("+"));
        }

        QString kingdom = general->getKingdom();
        if (general->getKingdoms().contains("+")) {
            room->setPlayerProperty(zuoci, "kingdom", room->askForKingdom(zuoci, general->objectName() + "_ChooseKingdom"));
        } else if (zuoci->getKingdom() != kingdom) {
            if (kingdom == "god")
                kingdom = room->askForKingdom(zuoci);
            room->setPlayerProperty(zuoci, "kingdom", kingdom);
        }

        if (zuoci->getGender() != general->getGender())
            zuoci->setGender(general->getGender());

        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_HUASHEN << zuoci->objectName() << general->objectName() << skill_name;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

        ac_dt_list.append(skill_name);
        zuoci->setTag("HuashenSkill", skill_name);
        room->handleAcquireDetachSkills(zuoci, ac_dt_list, true);
    }

    void onGameStart(ServerPlayer *zuoci) const
    {
        playAudioEffect(zuoci, objectName());
        zuoci->getRoom()->sendCompulsoryTriggerLog(zuoci, objectName());
        AcquireGenerals(zuoci, 2);
        SelectSkill(zuoci);
    }

    QDialog *getDialog() const
    {
        static HuashenDialog *dialog;

        if (dialog == nullptr)
            dialog = new HuashenDialog(objectName());

        return dialog;
    }
};


// 1. 建構子實作：接收參數並賦值給 m_propertyName
HuashenDialog::HuashenDialog(const QString &propertyName)
    : GeneralOverview(), m_propertyName(propertyName)
{
    setPreviewMode(true);
}

// 2. 彈出視窗與載入資料實作
void HuashenDialog::popup()
{
    if (Self == nullptr || m_propertyName.isEmpty())
        return;

    QString skill_name = m_propertyName;
    if (skill_name.endsWith("_general", Qt::CaseInsensitive))
        skill_name.chop(8);

    QByteArray key = m_propertyName.toLatin1();
    QVariant pile_value = Self->property(key.constData());

    QStringList general_names;
    if (pile_value.type() == QVariant::String) {
        QString pile_str = pile_value.toString();
        if (!pile_str.isEmpty())
            general_names = pile_str.split("+", QString::SkipEmptyParts);
    } else if (pile_value.type() == QVariant::List) {
        foreach (const QVariant &v, pile_value.toList()) {
            QString name = v.toString();
            if (!name.isEmpty())
                general_names << name;
        }
    } else if (pile_value.canConvert<QStringList>()) {
        general_names = pile_value.toStringList();
    } else {
        QString pile_str = pile_value.toString();
        if (!pile_str.isEmpty())
            general_names = pile_str.split("+", QString::SkipEmptyParts);
    }

    QList<const General *> generals;
    foreach (const QString &general_name, general_names) {
        const General *general = Sanguosha->getGeneral(general_name);
        if (general != nullptr)
            generals << general;
    }

    fillGenerals(generals);
    setWindowTitle(Sanguosha->translate(skill_name));
    show();
}

class HuashenSelect : public PhaseChangeSkill
{
public:
    HuashenSelect() : PhaseChangeSkill("#huashen-select")
    {
    }

    int getPriority(TriggerEvent) const
    {
        return 4;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && (target->getPhase() == Player::RoundStart || target->getPhase() == Player::NotActive);
    }

    bool onPhaseChange(ServerPlayer *zuoci, Room *) const
    {
        if (zuoci->hasSkill("huashen") && zuoci->askForSkillInvoke("huashen"))
            Huashen::SelectSkill(zuoci);
        return false;
    }
};

class HuashenClear : public DetachEffectSkill
{
public:
    HuashenClear() : DetachEffectSkill("huashen")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if (player->getKingdom() != player->getGeneral()->getKingdom() && player->getGeneral()->getKingdom() != "god")
            room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
        if (player->getGender() != player->getGeneral()->getGender())
            player->setGender(player->getGeneral()->getGender());
        QString huashen_skill = player->getTag("HuashenSkill").toString();
        if (!huashen_skill.isEmpty())
            room->detachSkillFromPlayer(player, huashen_skill, false, true);
		room->setPlayerProperty(player,"Huashens","");
        room->setPlayerMark(player, "@huashen", 0);
    }
};

class Xinsheng : public MasochismSkill
{
public:
    Xinsheng() : MasochismSkill("xinsheng")
    {
        frequency = Frequent;
    }

    void onDamaged(ServerPlayer *zuoci, const DamageStruct &damage) const
    {
        if (zuoci->askForSkillInvoke(this)) {
            Huashen::playAudioEffect(zuoci, objectName());
            Huashen::AcquireGenerals(zuoci, damage.damage);
        }
    }
};

class Renjie : public TriggerSkill
{
public:
    Renjie() : TriggerSkill("renjie")
    {
        events << Damaged << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            if (player->getPhase() == Player::Discard) {
                CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                    int n = move.card_ids.length();
                    if (n > 0) {
                        room->broadcastSkillInvoke(objectName());
                        room->notifySkillInvoked(player, objectName());
                        player->gainMark("&bear", n);
                    }
                }
            }
        } else if (triggerEvent == Damaged) {
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());
            DamageStruct damage = data.value<DamageStruct>();
            player->gainMark("&bear", damage.damage);
        }

        return false;
    }
};

class Baiyin : public PhaseChangeSkill
{
public:
    Baiyin() : PhaseChangeSkill("baiyin")
    {
        frequency = Wake;
        waked_skills = "jilve";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() &&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *shensimayi, Room *room) const
    {
        if (shensimayi->getMark("&bear") >= 4) {
            LogMessage log;
            log.type = "#BaiyinWake";
            log.from = shensimayi;
            log.arg = QString::number(shensimayi->getMark("&bear"));
            room->sendLog(log);
        }else if(!shensimayi->canWake("baiyin"))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(shensimayi, objectName());
        //room->doLightbox("$BaiyinAnimate");
        room->doSuperLightbox(shensimayi, "baiyin");

        room->setPlayerMark(shensimayi, "baiyin", 1);
        if (room->changeMaxHpForAwakenSkill(shensimayi, -1, objectName()))
            room->acquireSkill(shensimayi, "jilve");

        return false;
    }
};

JilveCard::JilveCard()
{
    target_fixed = true;
    mute = true;
}

void JilveCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *shensimayi = card_use.from;

    QStringList choices;/*
    if (!shensimayi->hasFlag("JilveZhiheng") &&shensimayi->canDiscard(shensimayi, "he")) {
        if (Sanguosha->getSkill("zhiheng"))
            choices << "zhiheng";
        if (Sanguosha->getSkill("tenyearzhiheng"))
            choices << "jilve_tenyearzhiheng";
        if (Sanguosha->getSkill("mobilemouzhiheng"))
            choices << "jilve_mobilemouzhiheng";
    }
    if (!shensimayi->hasFlag("JilveWansha")) {
        if (Sanguosha->getSkill("wansha"))
            choices << "wansha";
        if (Sanguosha->getSkill("olwansha"))
            choices << "jilve_olwansha";
    }*/
	foreach(QString s, Sanguosha->getSkillNames()){
		if(s.endsWith("zhiheng")&&!s.contains("#")&&!shensimayi->hasFlag("JilveZhiheng")&&shensimayi->canDiscard(shensimayi, "he")){
			const ViewAsSkill* zhiheng = Sanguosha->getViewAsSkill(s);
			if(zhiheng&&zhiheng->isEnabledAtResponse(shensimayi,"@"+s))
				choices << s;
		}
		if(s.endsWith("wansha")&&!s.contains("#")&&!shensimayi->hasFlag("JilveWansha")){
			const TriggerSkill* wansha = Sanguosha->getTriggerSkill(s);
			if(wansha&&wansha->hasEvent(Dying))
				choices << s;
		}
	}
    if (choices.isEmpty())
        return;
    choices << "cancel";

    QString choice = room->askForChoice(shensimayi, "jilve", choices.join("+"));
    if (choice == "cancel") {
        room->addPlayerHistory(shensimayi, "JilveCard", -1);
        return;
    }
    room->notifySkillInvoked(shensimayi, "jilve");
    shensimayi->loseMark("&bear");

    if (choice.contains("wansha")) {
        shensimayi->setTag("JilveWansha", choice);
        room->setPlayerFlag(shensimayi, "JilveWansha");
        room->acquireSkill(shensimayi, choice);
    } else {
        room->setPlayerFlag(shensimayi, "JilveZhiheng");
        room->askForUseCard(shensimayi, "@@"+choice, "@jilve-zhiheng", -1, Card::MethodDiscard);
    }
}

class JilveViewAsSkill : public ZeroCardViewAsSkill
{
public: // wansha & zhiheng & tenyearzhiheng
    JilveViewAsSkill() : ZeroCardViewAsSkill("jilve")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("JilveCard") < 2 && player->getMark("&bear") > 0;
    }

    const Card *viewAs() const
    {
        return new JilveCard;
    }
};

class Jilve : public TriggerSkill
{
public:
    Jilve() : TriggerSkill("jilve")
    {
        events << CardUsed // JiZhi TenyearJizhi
            << AskForRetrial // GuiCai
            << Damaged; // FangZhu
        view_as_skill = new JilveViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return TriggerSkill::triggerable(target) && target->getMark("&bear") > 0;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        player->setMark("JilveEvent", (int)triggerEvent);
        try {
            if (triggerEvent == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                if (use.card && use.card->getTypeId() == Card::TypeTrick) {
					QStringList skills;
					const TriggerSkill *jizhi;
					foreach(QString s, Sanguosha->getSkillNames()){
						if(s.endsWith("jizhi")&&!s.contains("#")){
							jizhi = Sanguosha->getTriggerSkill(s);
							if(jizhi&&jizhi->hasEvent(triggerEvent))
								skills << s;
						}
					}
					if (!skills.isEmpty() && player->askForSkillInvoke("jilve_jizhi", data)) {
						room->notifySkillInvoked(player, objectName());
						player->loseMark("&bear");
						jizhi = Sanguosha->getTriggerSkill(room->askForChoice(player, "_jilve", skills.join("+")));
						jizhi->trigger(triggerEvent, room, player, data);
					}
                }
            } else if (triggerEvent == AskForRetrial) {
                QStringList skills;
				const TriggerSkill *guicai;
				foreach(QString s, Sanguosha->getSkillNames()){
					if(s.endsWith("guicai")&&!s.contains("#")){
						guicai = Sanguosha->getTriggerSkill(s);
						if(guicai&&guicai->hasEvent(triggerEvent))
							skills << s;
					}
				}
                if (!skills.isEmpty() && player->askForSkillInvoke("jilve_guicai", data)) {
                    room->notifySkillInvoked(player, objectName());
                    player->loseMark("&bear");
					guicai = Sanguosha->getTriggerSkill(room->askForChoice(player, "_jilve", skills.join("+")));
                    guicai->trigger(triggerEvent, room, player, data);
                }
            } else if (triggerEvent == Damaged) {
                QStringList skills;
				const TriggerSkill *fangzhu;
				foreach(QString s, Sanguosha->getSkillNames()){
					if(s.endsWith("fangzhu")&&!s.contains("#")){
						fangzhu = Sanguosha->getTriggerSkill(s);
						if(fangzhu&&fangzhu->hasEvent(triggerEvent))
							skills << s;
					}
				}
                if (!skills.isEmpty() && player->askForSkillInvoke("jilve_fangzhu", data)) {
                    room->notifySkillInvoked(player, objectName());
                    player->loseMark("&bear");
					fangzhu = Sanguosha->getTriggerSkill(room->askForChoice(player, "_jilve", skills.join("+")));
                    fangzhu->trigger(triggerEvent, room, player, data);
                }
            }
            player->setMark("JilveEvent", 0);
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == StageChange || triggerEvent == TurnBroken)
                player->setMark("JilveEvent", 0);
            throw triggerEvent;
        }

        return false;
    }
};

class JilveClear : public TriggerSkill
{
public:
    JilveClear() : TriggerSkill("#jilve-clear")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->hasFlag("JilveWansha");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive) return false;
        QString wansha = target->getTag("JilveWansha").toString();
        if (wansha.isEmpty()) return false;
        room->detachSkillFromPlayer(target, wansha, false, true);
        return false;
    }
};

class LianpoCount : public TriggerSkill
{
public:
    LianpoCount() : TriggerSkill("#lianpo-count")
    {
        events << Death << EventPhaseStart;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.damage&&death.damage->from==player) {
                player->addMark("lianpoBf");
            }
        }else if (player->getPhase() == Player::RoundStart){
            foreach(ServerPlayer *p, room->getAlivePlayers())
				p->setMark("lianpoBf",0);
		}
        return false;
    }
};

class Lianpo : public PhaseChangeSkill
{
public:
    Lianpo() : PhaseChangeSkill("lianpo")
    {
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() == Player::NotActive) {
            foreach(ServerPlayer *p, room->getAllPlayers()){
				if(p->getMark("lianpoBf")>0&&p->hasSkill(objectName())&&p->askForSkillInvoke(this)){
					room->broadcastSkillInvoke(objectName());/*

					LogMessage log;
					log.type = "#LianpoCanInvoke";
					log.from = p;
					log.arg = QString::number(p->getMark("lianpo"));
					log.arg2 = objectName();
					room->sendLog(log);*/

					p->gainAnExtraTurn();
				}
			}
        }
        return false;
    }
};

class Juejing : public DrawCardsSkill
{
public:
    Juejing() : DrawCardsSkill("#juejing-draw")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        if (player->isWounded() && player->hasSkill("juejing")) {
            Room *room = player->getRoom();
            room->broadcastSkillInvoke("juejing");

            LogMessage log;
            log.type = "#YongsiGood";
            log.from = player;
            log.arg = QString::number(player->getLostHp());
            log.arg2 = "juejing";
            room->sendLog(log);
            room->notifySkillInvoked(player, "juejing");
        }
        return n + player->getLostHp();
    }
};

class JuejingKeep : public MaxCardsSkill
{
public:
    JuejingKeep() : MaxCardsSkill("juejing")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill(objectName()))
            return 2;
        return 0;
    }
};

Longhun::Longhun() : ViewAsSkill("longhun")
{
    response_or_use = true;
}

bool Longhun::isEnabledAtResponse(const Player *player, const QString &pattern) const
{
    return (pattern.contains("slash") || pattern.contains("Slash"))
        || pattern == "jink"
        || (pattern.contains("peach") && player->getMark("Global_PreventPeach") == 0)
        || pattern == "nullification";
}

bool Longhun::isEnabledAtPlay(const Player *player) const
{
    return player->isWounded() || Slash::IsAvailable(player);
}

bool Longhun::viewFilter(const QList<const Card *> &selected, const Card *card) const
{
    int n = getEffHp(Self);

    if (selected.length() >= n || card->hasFlag("using"))
        return false;

    if (n > 1 && !selected.isEmpty()) {
        Card::Suit suit = selected.first()->getSuit();
        return card->getSuit() == suit;
    }

    switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
    case CardUseStruct::CARD_USE_REASON_PLAY: {
        if (Self->isWounded() && card->getSuit() == Card::Heart)
            return true;
        else if (card->getSuit() == Card::Diamond) {
            FireSlash *slash = new FireSlash(Card::SuitToBeDecided, -1);
            slash->addSubcards(selected);
            slash->addSubcard(card->getEffectiveId());
            slash->deleteLater();
            return slash->isAvailable(Self);
        } else
            return false;
    }
    case CardUseStruct::CARD_USE_REASON_RESPONSE:
    case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "jink")
            return card->getSuit() == Card::Club;
        else if (pattern == "nullification")
            return card->getSuit() == Card::Spade;
        else if (pattern == "peach" || pattern == "peach+analeptic")
            return card->getSuit() == Card::Heart;
        else if (pattern.contains("slash") || pattern.contains("Slash"))
            return card->getSuit() == Card::Diamond;
    }
    default:
        break;
    }

    return false;
}

const Card *Longhun::viewAs(const QList<const Card *> &cards) const
{
    int n = getEffHp(Self);

    if (cards.length() != n)
        return nullptr;

    const Card *card = cards.first();
    Card *new_card = nullptr;

    switch (card->getSuit()) {
    case Card::Spade: {
        new_card = new Nullification(Card::SuitToBeDecided, 0);
        break;
    }
    case Card::Heart: {
        new_card = new Peach(Card::SuitToBeDecided, 0);
        break;
    }
    case Card::Club: {
        new_card = new Jink(Card::SuitToBeDecided, 0);
        break;
    }
    case Card::Diamond: {
        new_card = new FireSlash(Card::SuitToBeDecided, 0);
        break;
    }
    default:
        break;
    }

    if (new_card) {
        new_card->setSkillName(objectName());
        new_card->addSubcards(cards);
    }

    return new_card;
}

int Longhun::getEffectIndex(const ServerPlayer *, const Card *card) const
{
    if (!QFile::exists("audio/skill/"+objectName()+"1.ogg")) return -2;
	return Sanguosha->getCard(card->getSubcards().first())->getSuit() + 1;
}

bool Longhun::isEnabledAtNullification(const ServerPlayer *player) const
{
    int n = getEffHp(player), count = 0;
    foreach (const Card *card, player->getHandcards() + player->getEquips()) {
        if (card->getSuit() == Card::Spade)
            count++;
    }

    foreach (int id, player->getHandPile()) {
        if (Sanguosha->getCard(id)->getSuit() == Card::Spade)
            count++;
    }

    if (count >= n) return true;

    return false;
}

int Longhun::getEffHp(const Player *zhaoyun) const
{
    return qMax(1, zhaoyun->getHp());
}

class NewJuejing : public MaxCardsSkill
{
public:
    NewJuejing() : MaxCardsSkill("newjuejing")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill(objectName()))
            return 2;
        return 0;
    }
};

class NewJuejingDraw : public TriggerSkill
{
public:
    NewJuejingDraw() : TriggerSkill("#newjuejing-draw")
    {
        events << EnterDying << QuitDying;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->sendCompulsoryTriggerLog(player, "newjuejing", true, true);
        player->drawCards(1, "newjuejing");
        return false;
    }
};

class NewLonghunVS : public ViewAsSkill
{
public:
    NewLonghunVS() : ViewAsSkill("newlonghun")
    {
        response_or_use = true;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern.contains("slash") || pattern.contains("Slash"))
            || pattern == "jink"
            || (pattern.contains("peach") && player->getMark("Global_PreventPeach") == 0)
            || pattern == "nullification";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->isWounded() || Slash::IsAvailable(player);
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *card) const
    {
        if (selected.length() >= 2 || card->hasFlag("using"))
            return false;

        if (!selected.isEmpty()) {
            Card::Suit suit = selected.first()->getSuit();
            return card->getSuit() == suit;
        }

        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            if (Self->isWounded() && card->getSuit() == Card::Heart)
                return true;
            else if (card->getSuit() == Card::Diamond) {
                FireSlash *slash = new FireSlash(Card::SuitToBeDecided, -1);
                slash->addSubcards(selected);
                slash->addSubcard(card->getEffectiveId());
                slash->deleteLater();
                return slash->isAvailable(Self);
            } else
                return false;
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "jink")
                return card->getSuit() == Card::Club;
            else if (pattern == "nullification")
                return card->getSuit() == Card::Spade;
            else if (pattern == "peach" || pattern == "peach+analeptic")
                return card->getSuit() == Card::Heart;
            else if (pattern.contains("slash") || pattern.contains("Slash"))
                return card->getSuit() == Card::Diamond;
        }
        default:
            break;
        }
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 0 || cards.length() > 2)
            return nullptr;

        const Card *card = cards.first();
        Card *new_card = nullptr;

        switch (card->getSuit()) {
        case Card::Spade: {
            new_card = new Nullification(Card::SuitToBeDecided, 0);
            break;
        }
        case Card::Heart: {
            new_card = new Peach(Card::SuitToBeDecided, 0);
            break;
        }
        case Card::Club: {
            new_card = new Jink(Card::SuitToBeDecided, 0);
            break;
        }
        case Card::Diamond: {
            new_card = new FireSlash(Card::SuitToBeDecided, 0);
            break;
        }
        default:
            break;
        }

        if (new_card) {
            new_card->setSkillName(objectName());
            new_card->addSubcards(cards);
        }

        return new_card;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        foreach (const Card *card, player->getHandcards() + player->getEquips()) {
            if (card->getSuit() == Card::Spade)
                return true;
        }

        foreach (int id, player->getHandPile()) {
            if (Sanguosha->getCard(id)->getSuit() == Card::Spade)
                return true;
        }
        return false;
    }
};

class NewLonghun : public TriggerSkill
{
public:
    NewLonghun() : TriggerSkill("newlonghun")
    {
        events << PreHpRecover << ConfirmDamage << CardUsed << CardResponded;
        view_as_skill = new NewLonghunVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreHpRecover) {
            RecoverStruct recover = data.value<RecoverStruct>();
            if (!recover.card || !recover.card->getSkillNames().contains(objectName())) return false;
            if (recover.card->subcardsLength() != 2) return false;
            foreach (int id, recover.card->getSubcards()) {
                if (!Sanguosha->getCard(id)->isRed()) return false;
            }
            int old = recover.recover;
            ++recover.recover;
            int now = qMin(recover.recover, player->getMaxHp() - player->getHp());
            if (now <= 0)
                return true;
            if (recover.who && now > old) {
                LogMessage log;
                log.type = "#NewlonghunRecover";
                log.from = recover.who;
                log.to << player;
                log.arg = objectName();
                log.arg2 = QString::number(now);
                room->sendLog(log);
            }

            recover.recover = now;
            data = QVariant::fromValue(recover);
        } else if (event == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->getSkillNames().contains(objectName()) || damage.to->isDead()) return false;
            if (damage.card->subcardsLength() != 2) return false;
            foreach (int id, damage.card->getSubcards()) {
                if (!Sanguosha->getCard(id)->isRed()) return false;
            }

            LogMessage log;
            log.type = "#NewlonghunDamage";
            log.from = player;
            log.to << damage.to;
            log.arg = objectName();
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);
        } else {
            if (!room->hasCurrent() || !player->canDiscard(room->getCurrent(), "he")) return false;
            const Card *card = nullptr;
            if (event == CardUsed)
                card = data.value<CardUseStruct>().card;
            else
                card = data.value<CardResponseStruct>().m_card;

            if (!card || card->isKindOf("SkillCard") || !card->getSkillNames().contains(objectName())) return false;
            if (card->subcardsLength() != 2) return false;
            foreach (int id, card->getSubcards()) {
                if (!Sanguosha->getCard(id)->isBlack()) return false;
            }
            int id = room->askForCardChosen(player, room->getCurrent(), "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, room->getCurrent(), player);
        }
        return false;
    }
};

class MobileRenjie : public TriggerSkill
{
public:
    MobileRenjie() : TriggerSkill("mobilerenjie")
    {
        events << ChoiceMade << CardAsked << TrickCardCanceling << CardOnEffect;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == ChoiceMade) {
            QStringList promptlist = data.toString().split(":");
            if(promptlist.first()=="cardResponded"&&promptlist.last()==""){
				QStringList asked = player->getTag("mobilerenjieAsked").toStringList();
				if(asked.length()<4||player->getMark("bear_lun")>3||!player->hasSkill(objectName())) return false;
				player->removeTag("mobilerenjieAsked");
				if(promptlist[1]==asked[0]&&asked[1].contains(promptlist[2])){
					room->sendCompulsoryTriggerLog(player, this);
					player->gainMark("&bear");
					player->addMark("bear_lun");
				}
			}else if(promptlist.first()=="cardUsed"){
				QStringList asked = player->getTag("mobilerenjieAsked").toStringList();
				if(asked.length()<4||player->getMark("bear_lun")>3||!player->hasSkill(objectName())) return false;
				player->removeTag("mobilerenjieAsked");
				if(promptlist[1]==asked[0]&&asked[1].contains(promptlist[2])){
					room->sendCompulsoryTriggerLog(player, this);
					player->gainMark("&bear");
					player->addMark("bear_lun");
				}
			}else if(promptlist.first()=="Nullification"){
				foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
					CardEffectStruct effect = p->getTag("mobilerenjieEffect").value<CardEffectStruct>();
					p->removeTag("mobilerenjieEffect");
					if(p!=player&&effect.card&&effect.card->getClassName()==promptlist[1]&&effect.from!=p&&p->getMark("bear_lun")<4){
						room->sendCompulsoryTriggerLog(p, this);
						p->gainMark("&bear");
						p->addMark("bear_lun");
					}
				}
			}
        } else if (event == CardAsked) {
            QStringList asked = data.toStringList();
            if(asked.length()<4) return false;
			player->setTag("mobilerenjieAsked", data);
        } else if (event == CardOnEffect) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getTypeId()==2){
				foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
					CardEffectStruct asked = p->getTag("mobilerenjieEffect").value<CardEffectStruct>();
					p->removeTag("mobilerenjieEffect");
					if(effect.from!=p&&effect.card==asked.card&&p->getMark("bear_lun")<4){
						room->sendCompulsoryTriggerLog(p, this);
						p->gainMark("&bear");
						p->addMark("bear_lun");
					}
				}
			}
        } else {
			player->setTag("mobilerenjieEffect", data);
        }
        return false;
    }
};

class MobileLianpo : public TriggerSkill
{
public:
    MobileLianpo() : TriggerSkill("mobilelianpo")
    {
        events << Death << EventPhaseStart;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target!=nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.damage&&death.damage->from==player&&player->isAlive()&&player->hasSkill(objectName())){
				QStringList choices;
				if(player->getMark("mobilelianpo1")<1)
					choices << "mobilelianpo1";
				if(player->hasSkill("mobilejilve",true)){
					if(!player->hasSkill("guicai",true))
						choices << "mobilelianpo2=guicai";
					if(!player->hasSkill("fangzhu",true))
						choices << "mobilelianpo2=fangzhu";
					if(!player->hasSkill("jizhi",true))
						choices << "mobilelianpo2=jizhi";
					if(!player->hasSkill("zhiheng",true))
						choices << "mobilelianpo2=zhiheng";
					if(!player->hasSkill("wansha",true))
						choices << "mobilelianpo2=wansha";
				}
				if(choices.length()>0&&player->askForSkillInvoke(objectName()+"$-1",data)){
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					if(choice=="mobilelianpo1"){
						player->addMark("mobilelianpo1");
					}else{
						choices = choice.split("=");
						room->acquireSkill(player,choices.last());
					}
				}
			}
        } else if(player->getPhase()==Player::NotActive){
            foreach(ServerPlayer *p, room->getAllPlayers()){
				if(p->getMark("mobilelianpo1")>0){
					p->setMark("mobilelianpo1", 0);
					p->gainAnExtraTurn();
				}
			}
        }
        return false;
    }
};

class MobileBaiyin : public PhaseChangeSkill
{
public:
    MobileBaiyin() : PhaseChangeSkill("mobilebaiyin")
    {
        frequency = Wake;
        waked_skills = "mobilejilve";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() &&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *shensimayi, Room *room) const
    {
        if (shensimayi->getMark("&bear") >= 4) {
            LogMessage log;
            log.type = "#BaiyinWake";
            log.from = shensimayi;
            log.arg = QString::number(shensimayi->getMark("&bear"));
            room->sendLog(log);
        }else if(!shensimayi->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(shensimayi, objectName());
        //room->doLightbox("$BaiyinAnimate");
        room->doSuperLightbox(shensimayi, objectName());

        room->setPlayerMark(shensimayi, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(shensimayi, -1, objectName()))
            room->acquireSkill(shensimayi, "mobilejilve");

        return false;
    }
};

class MobileJilve : public TriggerSkill
{
public:
    MobileJilve() : TriggerSkill("mobilejilve")
    {
        events << EventAcquireSkill << EventPhaseStart;
		waked_skills = "guicai,fangzhu,jizhi,zhiheng,wansha";
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventAcquireSkill) {
            if (data.toString()==objectName()){
				room->sendCompulsoryTriggerLog(player,this);
				room->acquireSkill(player,"guicai");
				if(player->getKingdom()=="wei")
					room->acquireSkill(player,"fangzhu");
				if(player->getKingdom()=="shu")
					room->acquireSkill(player,"jizhi");
				if(player->getKingdom()=="wu")
					room->acquireSkill(player,"zhiheng");
				if(player->getKingdom()=="qun")
					room->acquireSkill(player,"wansha");
			}
        } else if(player->getPhase()==Player::Play&&player->getMark("&bear")>0){
				QStringList choices;
				for (int i = 1; i <= qMin(3,player->getMark("&bear")); i++){
					choices << "mobilejilve1="+QString::number(i);
				}
				int n = player->getMark("mobilejilveUse")+1;
				if(n<=player->getMark("&bear")){
					if(!player->hasSkill("guicai",true))
						choices << "mobilejilve2=guicai="+QString::number(n);
					if(!player->hasSkill("fangzhu",true))
						choices << "mobilejilve2=fangzhu="+QString::number(n);
					if(!player->hasSkill("jizhi",true))
						choices << "mobilejilve2=jizhi="+QString::number(n);
					if(!player->hasSkill("zhiheng",true))
						choices << "mobilejilve2=zhiheng="+QString::number(n);
					if(!player->hasSkill("wansha",true))
						choices << "mobilejilve2=wansha="+QString::number(n);
				}
				if(choices.length()>0&&player->askForSkillInvoke(objectName()+"$-1",data)){
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					if(choice.contains("mobilejilve1")){
						choices = choice.split("=");
						n = choices.last().toInt();
						player->loseMark("&bear",n);
						player->drawCards(n,objectName());
					}else{
						choices = choice.split("=");
						player->loseMark("&bear",n);
						room->acquireSkill(player,choices[1]);
						player->addMark("mobilejilveUse");
					}
				}
        }
        return false;
    }
};






MountainPackage::MountainPackage()
    : Package("mountain")
{
    General *zhanghe = new General(this, "zhanghe", "wei"); // WEI 009
    zhanghe->addSkill(new Qiaobian);

    General *dengai = new General(this, "dengai", "wei", 4); // WEI 015
    dengai->addSkill(new Tuntian);
    dengai->addSkill(new TuntianDistance);
    dengai->addSkill(new Zaoxian);
    related_skills.insertMulti("tuntian", "#tuntian-dist");

    General *jiangwei = new General(this, "jiangwei", "shu"); // SHU 012
    jiangwei->addSkill(new Tiaoxin);
    jiangwei->addSkill(new Zhiji);

    General *liushan = new General(this, "liushan$", "shu", 3); // SHU 013
    liushan->addSkill(new Xiangle);
    liushan->addSkill(new Fangquan);
    liushan->addSkill(new Ruoyu);

    General *sunce = new General(this, "sunce$", "wu"); // WU 010
    sunce->addSkill(new Jiang);
    sunce->addSkill(new Hunzi);
    sunce->addSkill(new Zhiba);

    General *erzhang = new General(this, "erzhang", "wu", 3); // WU 015
    erzhang->addSkill(new Zhijian);
    erzhang->addSkill(new Guzheng);

    General *zuoci = new General(this, "zuoci", "qun", 3); // QUN 009
    zuoci->addSkill(new Huashen);
    zuoci->addSkill(new HuashenSelect);
    zuoci->addSkill(new HuashenClear);
    zuoci->addSkill(new Xinsheng);
    related_skills.insertMulti("huashen", "#huashen-select");
    related_skills.insertMulti("huashen", "#huashen-clear");

    General *caiwenji = new General(this, "caiwenji", "qun", 3, false); // QUN 012
    caiwenji->addSkill(new Beige);
    caiwenji->addSkill(new Duanchang);

    General *shenzhaoyun = new General(this, "shenzhaoyun", "god", 2); // LE 007
    shenzhaoyun->addSkill(new JuejingKeep);
    shenzhaoyun->addSkill(new Juejing);
    shenzhaoyun->addSkill(new Longhun);
    related_skills.insertMulti("juejing", "#juejing-draw");

    General *new_shenzhaoyun = new General(this, "new_shenzhaoyun", "god", 2);
    new_shenzhaoyun->addSkill(new NewJuejing);
    new_shenzhaoyun->addSkill(new NewJuejingDraw);
    new_shenzhaoyun->addSkill(new NewLonghun);
    related_skills.insertMulti("newjuejing", "#newjuejing-draw");

    General *shensimayi = new General(this, "shensimayi", "god", 4); // LE 008
    shensimayi->addSkill(new Renjie);
    shensimayi->addSkill(new Baiyin);
    related_skills.insertMulti("jilve", "#jilve-clear");
    shensimayi->addSkill(new Lianpo);
    shensimayi->addSkill(new LianpoCount);
    related_skills.insertMulti("lianpo", "#lianpo-count");
    addMetaObject<JilveCard>();

    addMetaObject<QiaobianCard>();
    addMetaObject<TiaoxinCard>();
    addMetaObject<ZhijianCard>();
    addMetaObject<ZhibaCard>();
    addMetaObject<FangquanCard>();
    addMetaObject<GuzhengCard>();

    skills << new ZhibaPindian << new Jixi << new Jilve << new JilveClear;

    General *mobile_shensimayi = new General(this, "mobile_shensimayi", "god", 4); // LE 008
    mobile_shensimayi->addSkill(new MobileRenjie);
    mobile_shensimayi->addSkill(new MobileLianpo);
    mobile_shensimayi->addSkill(new MobileBaiyin);
    skills << new MobileJilve;

}
ADD_PACKAGE(Mountain)