#include "yjcm2014.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm2013.h"

DingpinCard::DingpinCard()
{
}

bool DingpinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->isWounded() && to_select->getMark("dingpin-Clear")<1;
}

void DingpinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    JudgeStruct judge;
    judge.who = effect.to;
    judge.good = true;
    judge.pattern = ".|black";
    judge.reason = "dingpin";

    room->judge(judge);

    if (judge.isGood()) {
        room->addPlayerMark(effect.to, "dingpin-Clear");
        effect.to->drawCards(effect.to->getLostHp(), "dingpin");
    } else {
        effect.from->turnOver();
    }
}

class Dingpin : public OneCardViewAsSkill
{
public:
    Dingpin() : OneCardViewAsSkill("dingpin")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (!player->canDiscard(player, "h")) return false;
        if (player->getMark("dingpin-Clear")<1 && player->isWounded()) return true;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->getMark("dingpin-Clear")<1 && p->isWounded()) return true;
        }
        return false;
    }

    bool viewFilter(const Card *to_select) const
    {
        return !to_select->isEquipped() && Self->getMark("dingpin_" + to_select->getType() + "-Clear") == 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        DingpinCard *card = new DingpinCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class DingpinBf : public TriggerSkill
{
public:
    DingpinBf() : TriggerSkill("#dingpinbf")
    {
        events << PreCardUsed << CardResponded << BeforeCardsMove;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->isAlive() || player->getPhase() == Player::NotActive) return false;
		if (triggerEvent == PreCardUsed || triggerEvent == CardResponded) {
			const Card *card = nullptr;
			if (triggerEvent == PreCardUsed) {
				card = data.value<CardUseStruct>().card;
			} else {
				CardResponseStruct resp = data.value<CardResponseStruct>();
				if (resp.m_isUse) card = resp.m_card;
			}
			if (!card || card->getTypeId() == Card::TypeSkill) return false;
			recordDingpinCardType(room, player, card, true);
		} else if (triggerEvent == BeforeCardsMove) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (player==move.from&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				foreach (int id, move.card_ids)
					recordDingpinCardType(room, player, Sanguosha->getCard(id), false);
            }
        }
        return false;
    }

private:
    void recordDingpinCardType(Room *room, ServerPlayer *player, const Card *card, bool isUse) const
    {
        room->addPlayerMark(player, "dingpin_" + card->getType() + "-Clear");
        if (isUse && player->getPhase() == Player::Play)
            room->addPlayerMark(player, "langmie_" + QString::number(card->getTypeId()) + "-PlayClear");
        if (isUse && player->getPhase() != Player::NotActive)
            room->addPlayerMark(player, "secondlangmie_" + QString::number(card->getTypeId()) + "-Clear");
    }
};

class Faen : public TriggerSkill
{
public:
    Faen() : TriggerSkill("faen")
    {
        events << TurnedOver << ChainStateChanged;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == ChainStateChanged && !player->isChained()) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!player->isAlive()) return false;
            if (TriggerSkill::triggerable(p)
                && room->askForSkillInvoke(p, objectName(), QVariant::fromValue(player))) {
                room->broadcastSkillInvoke(objectName());
                player->drawCards(1, objectName());
            }
        }
        return false;
    }
};

SidiCard::SidiCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SidiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "sidi", "");
    room->throwCard(this, reason, nullptr);
}

class SidiVS : public OneCardViewAsSkill
{
public:
    SidiVS() : OneCardViewAsSkill("sidi")
    {
        response_pattern = "@@sidi";
        filter_pattern = ".|.|.|sidi";
        expand_pile = "sidi";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        SidiCard *sd = new SidiCard;
        sd->addSubcard(originalCard);
        return sd;
    }
};

class Sidi : public TriggerSkill
{
public:
    Sidi() : TriggerSkill("sidi")
    {
        events << CardUsed << EventPhaseStart << EventPhaseChanging;
        //frequency = Frequent;
        view_as_skill = new SidiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from == Player::Play)
                room->setPlayerMark(player, "sidi", 0);
        } else if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Jink")) {
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (TriggerSkill::triggerable(p) && (p == player || p->hasFlag("CurrentPlayer"))
                        && room->askForSkillInvoke(p, objectName(), data)) {
                        room->broadcastSkillInvoke(objectName(), 1);
                        QList<int> ids = room->getNCards(1, false); // For UI
                        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, p->objectName(), "sidi", ""));
                        room->moveCardsAtomic(move, true);
                        p->addToPile("sidi", ids);
                    }
                }
            }
        } else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->getPhase() != Player::Play) return false;
                if (TriggerSkill::triggerable(p) && p->getPile("sidi").length() > 0 && room->askForUseCard(p, "@@sidi", "sidi_remove:remove", -1, Card::MethodNone))
                    room->addPlayerMark(player, "sidi");
            }
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 2;
    }
};

class SidiTargetMod : public TargetModSkill
{
public:
    SidiTargetMod() : TargetModSkill("#sidi-target")
    {
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        return card->isKindOf("Slash") ? -from->getMark("sidi") : 0;
    }
};

class ShenduanViewAsSkill : public OneCardViewAsSkill
{
public:
    ShenduanViewAsSkill() : OneCardViewAsSkill("shenduan")
    {
        response_pattern = "@@shenduan";
        expand_pile = "#shenduan";
    }

    bool viewFilter(const Card *to_select) const
    {
        return Self->getPile("#shenduan").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const Card *originalCard) const
    {
        SupplyShortage *ss = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        ss->addSubcard(originalCard);
        ss->setSkillName("shenduan");
        return ss;
    }
};

class Shenduan : public TriggerSkill
{
public:
    Shenduan() : TriggerSkill("shenduan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new ShenduanViewAsSkill;
    }
    int getPriority(TriggerEvent) const
    {
        return 4;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && move.from == player
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
            int i = 0;
            QList<int> shenduan_card;
            foreach (int id, move.card_ids) {
                const Card *c = Sanguosha->getCard(id);
                if (room->getCardOwner(id)==nullptr && c->isBlack() && c->getTypeId() == Card::TypeBasic
                    && (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
                    shenduan_card << id;
                }
                i++;
            }
            while (!shenduan_card.isEmpty()&&player->isAlive()) {
				room->notifyMoveToPile(player, shenduan_card, objectName(), Player::DiscardPile, true);
                const Card *c = room->askForUseCard(player, "@@shenduan", "@shenduan-use");
				if (!c) break;
				shenduan_card.removeOne(c->getEffectiveId());
				foreach (int id, shenduan_card) {
					if (room->getCardOwner(id))
						shenduan_card.removeOne(id);
				}
            }
        }
        return false;
    }
};

class ShenduanTargetMod : public TargetModSkill
{
public:
    ShenduanTargetMod() : TargetModSkill("#shenduan-target")
    {
        pattern = "SupplyShortage";
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "shenduan")
            return 1000;
        return 0;
    }
};

class Yonglve : public PhaseChangeSkill
{
public:
    Yonglve() : PhaseChangeSkill("yonglve")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Judge) return false;
        foreach (ServerPlayer *hs, room->getOtherPlayers(target)) {
            if (target->isDead() || target->getJudgingArea().isEmpty()) break;
            if (!TriggerSkill::triggerable(hs) || !hs->inMyAttackRange(target)) continue;
            if (room->askForSkillInvoke(hs, objectName())) {
                room->broadcastSkillInvoke(objectName());
                int id = room->askForCardChosen(hs, target, "j", objectName(), false, Card::MethodDiscard);
                room->throwCard(id, nullptr, hs);
                if (hs->isAlive() && target->isAlive() && hs->canSlash(target, false)) {
                    room->setTag("YonglveUser", QVariant::fromValue(hs));
                    Slash *slash = new Slash(Card::NoSuit, 0);
                    slash->setSkillName("_yonglve");
					slash->deleteLater();
                    room->useCard(CardUseStruct(slash, hs, target));
                }
            }
        }
        return false;
    }
};

class YonglveSlash : public TriggerSkill
{
public:
    YonglveSlash() : TriggerSkill("#yonglve")
    {
        events << DamageDone << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && damage.card->getSkillNames().contains("yonglve"))
                damage.card->setFlags("YonglveDamage");
        } else if (!player->hasFlag("Global_ProcessBroken")) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && use.card->getSkillNames().contains("yonglve") && !use.card->hasFlag("YonglveDamage")) {
                ServerPlayer *hs = room->getTag("YonglveUser").value<ServerPlayer *>();
                if (hs)
                    hs->drawCards(1, "yonglve");
            }
        }
        return false;
    }
};

class Benxi : public TriggerSkill
{
public:
    Benxi() : TriggerSkill("benxi")
    {
        events << EventPhaseChanging << CardFinished << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to >= Player::NotActive) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->hasFlag("benxiAN"+player->objectName())){
						p->setFlags("-benxiAN"+player->objectName());
						p->removeEquipsNullified("Armor");
					}
				}
            }else{
				bool can = true;
				foreach (const Player *p, player->getAliveSiblings()) {
					if (player->distanceTo(p) > 1){
						can = false;
						break;
					}
				}
				if (can){
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())) continue;
						p->setFlags("benxiAN"+player->objectName());
						p->addEquipsNullified("Armor");
					}
				}else{
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())){
							p->setFlags("-benxiAN"+player->objectName());
							p->removeEquipsNullified("Armor");
						}
					}
				}
			}
        } else if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() != Card::TypeSkill && player->hasFlag("CurrentPlayer")) {
                room->addPlayerMark(player, "&benxi-Clear");
				bool can = true;
				foreach (const Player *p, player->getAliveSiblings()) {
					if (player->distanceTo(p) > 1){
						can = false;
						break;
					}
				}
				if (can){
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())) continue;
						p->setFlags("benxiAN"+player->objectName());
						p->addEquipsNullified("Armor");
					}
				}else{
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())){
							p->setFlags("-benxiAN"+player->objectName());
							p->removeEquipsNullified("Armor");
						}
					}
				}
            }
        } else if (triggerEvent == EventAcquireSkill || triggerEvent == EventLoseSkill) {
            if (data.toString() != objectName()) return false;
            int num = (triggerEvent == EventAcquireSkill) ? player->getMark("benxi-Clear") : 0;
            room->setPlayerMark(player, "&benxi-Clear", num);
        }
        return false;
    }
};

// the part of Armor ignorance is coupled in Player::hasArmorEffect

class BenxiTargetMod : public TargetModSkill
{
public:
    BenxiTargetMod() : TargetModSkill("#benxi-target")
    {
    }

    int getExtraTargetNum(const Player *from, const Card *card) const
    {
        if (isAllAdjacent(from, card)&&from->hasSkill("benxi"))
            return 1;
        return 0;
    }

private:
    bool isAllAdjacent(const Player *from, const Card *card) const
    {
        int rangefix = 0;
        if (card->isVirtualCard() && from->getOffensiveHorse()
            && card->getSubcards().contains(from->getOffensiveHorse()->getEffectiveId()))
            rangefix = 1;
        foreach (const Player *p, from->getAliveSiblings()) {
            if (from->distanceTo(p, rangefix) != 1)
                return false;
        }
        return true;
    }
};

class BenxiDistance : public DistanceSkill
{
public:
    BenxiDistance() : DistanceSkill("#benxi-dist")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasFlag("CurrentPlayer")&&from->hasSkill("benxi"))
            return -from->getMark("&benxi-Clear");
        return 0;
    }
};

class Qiangzhi : public TriggerSkill
{
public:
    Qiangzhi() : TriggerSkill("qiangzhi")
    {
        events << EventPhaseStart << CardUsed << CardResponded;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::Play;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            player->setMark(objectName(), 0);
            if (TriggerSkill::triggerable(player)) {
                QList<ServerPlayer *> targets;
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    if (!p->isKongcheng())
                        targets << p;
                }
                if (targets.isEmpty()) return false;
                ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "qiangzhi-invoke", true, true);
                if (target) {
                    room->broadcastSkillInvoke(objectName(), 1);
                    int id = room->askForCardChosen(player, target, "h", objectName());
                    room->showCard(target, id);
                    player->setMark(objectName(), static_cast<int>(Sanguosha->getCard(id)->getTypeId()));
                }
            }
        } else if (player->getMark(objectName()) > 0) {
            const Card *card = nullptr;
            if (triggerEvent == CardUsed) {
                card = data.value<CardUseStruct>().card;
            } else {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                if (resp.m_isUse)
                    card = resp.m_card;
            }
            if (card && static_cast<int>(card->getTypeId()) == player->getMark(objectName())
                && room->askForSkillInvoke(player, objectName(), data)) {
                if (!player->hasSkill(objectName())) {
                    LogMessage log;
                    log.type = "#InvokeSkill";
                    log.from = player;
                    log.arg = objectName();
                    room->sendLog(log);
                }
                room->broadcastSkillInvoke(objectName(), 2);
                player->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class Xiantu : public TriggerSkill
{
public:
    Xiantu() : TriggerSkill("xiantu")
    {
        events << EventPhaseStart << EventPhaseEnd << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                p->setFlags("-XiantuInvoked");
                if (!player->isAlive()) return false;
                if (TriggerSkill::triggerable(p) && room->askForSkillInvoke(p, objectName())) {
                    room->broadcastSkillInvoke(objectName());
                    p->setFlags("XiantuInvoked");
                    p->drawCards(2, objectName());
                    if (p->isAlive() && player->isAlive()) {
                        if (!p->isNude()) {
                            int num = qMin(2, p->getCardCount(true));
                            const Card *to_give = room->askForExchange(p, objectName(), num, num, true,
                                QString("@xiantu-give::%1:%2").arg(player->objectName()).arg(num));
                            player->obtainCard(to_give, false);
                        }
                    }
                }
            }
        } else if (triggerEvent == EventPhaseEnd) {
            if (player->getPhase() == Player::Play) {
                QList<ServerPlayer *> zhangsongs;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->hasFlag("XiantuInvoked")) {
                        p->setFlags("-XiantuInvoked");
                        zhangsongs << p;
                    }
                }
                if (player->getMark("XiantuKill") > 0) {
                    player->setMark("XiantuKill", 0);
                    return false;
                }
                foreach (ServerPlayer *zs, zhangsongs) {
                    LogMessage log;
                    log.type = "#Xiantu";
                    log.from = player;
                    log.to << zs;
                    log.arg = objectName();
                    room->sendLog(log);

                    room->loseHp(HpLostStruct(zs, 1, objectName(), zs));
                }
            }
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.damage && death.damage->from && death.damage->from->getPhase() == Player::Play)
                death.damage->from->addMark("XiantuKill");
        }
        return false;
    }
};

class Zhongyong : public TriggerSkill
{
public:
    Zhongyong() : TriggerSkill("zhongyong")
    {
        events << CardOffset;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();

        const Card *jink = effect.offset_card;
        if (!jink||!effect.card->isKindOf("Slash")) return false;
        QList<int> ids;
        if (!jink->isVirtualCard()) {
            if (room->getCardPlace(jink->getEffectiveId()) == Player::DiscardPile)
                ids << jink->getEffectiveId();
        } else {
            foreach (int id, jink->getSubcards()) {
                if (room->getCardPlace(id) == Player::DiscardPile)
                    ids << id;
            }
        }
        if (ids.isEmpty()) return false;

        room->fillAG(ids, player);
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(effect.to), objectName(),
            "zhongyong-invoke:" + effect.to->objectName(), true, true);
        room->clearAG(player);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        DummyCard *dummy = new DummyCard(ids);
        room->obtainCard(target, dummy);
        delete dummy;

        if (player->isAlive() && effect.to->isAlive() && target != player) {
            if (player->canSlash(effect.to, nullptr, false))
                room->askForUseSlashTo(player, effect.to, QString("zhongyong-slash:%1").arg(effect.to->objectName()), false, true);
        }
        return false;
    }
};

ShenxingCard::ShenxingCard()
{
    target_fixed = true;
}

void ShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isAlive())
        room->drawCards(source, 1, "shenxing");
}

class Shenxing : public ViewAsSkill
{
public:
    Shenxing() : ViewAsSkill("shenxing")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        ShenxingCard *card = new ShenxingCard;
        card->addSubcards(cards);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getCardCount(true) >= 2 && player->canDiscard(player, "he");
    }
};

BingyiCard::BingyiCard()
{
}

bool BingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card::Color color = Card::Colorless;
    foreach (const Card *c, Self->getHandcards()) {
        if (color == Card::Colorless)
            color = c->getColor();
        else if (c->getColor() != color)
            return targets.isEmpty();
    }
    return targets.length() <= Self->getHandcardNum();
}

bool BingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    Card::Color color = Card::Colorless;
    foreach (const Card *c, Self->getHandcards()) {
        if (color == Card::Colorless)
            color = c->getColor();
        else if (c->getColor() != color)
            return false;
    }
    return targets.length() < Self->getHandcardNum();
}

void BingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source);
    foreach(ServerPlayer *p, targets)
        room->drawCards(p, 1, "bingyi");
}

class BingyiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    BingyiViewAsSkill() : ZeroCardViewAsSkill("bingyi")
    {
        response_pattern = "@@bingyi";
    }

    const Card *viewAs() const
    {
        return new BingyiCard;
    }
};

class Bingyi : public PhaseChangeSkill
{
public:
    Bingyi() : PhaseChangeSkill("bingyi")
    {
        view_as_skill = new BingyiViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish || target->isKongcheng()) return false;
        room->askForUseCard(target, "@@bingyi", "@bingyi-card");
        return false;
    }
};

class Zenhui : public TriggerSkill
{
public:
    Zenhui() : TriggerSkill("zenhui")
    {
        events << TargetSpecifying << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == CardFinished && (use.card->isKindOf("Slash") || (use.card->isNDTrick() && use.card->isBlack()))) {
            use.from->setFlags("-ZenhuiUser_" + use.card->toString());
            return false;
        }
        if (!TriggerSkill::triggerable(player) || player->getPhase() != Player::Play || player->hasFlag(objectName()))
            return false;

        if (use.to.length() == 1 && !use.card->targetFixed()
            && (use.card->isKindOf("Slash") || (use.card->isNDTrick() && use.card->isBlack()))) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p != player && p != use.to.first() && !room->isProhibited(player, p, use.card) && use.card->targetFilter(QList<const Player *>(), p, player))
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            use.from->setTag("zenhui", data);
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "zenhui-invoke:" + use.to.first()->objectName(), true, true);
            use.from->removeTag("zenhui");
            if (target) {
                player->setFlags(objectName());
                room->broadcastSkillInvoke(objectName());

                bool extra_target = true;
                if (!target->isNude()) {
                    const Card *card = room->askForCard(target, "..", "@zenhui-give:" + player->objectName(), data, Card::MethodNone);
                    if (card) {
                        extra_target = false;
                        player->obtainCard(card);

                        if (target->isAlive()) {
                            LogMessage log;
                            log.type = "#BecomeUser";
                            log.from = target;
                            log.card_str = use.card->toString();
                            room->sendLog(log);

                            target->setFlags("ZenhuiUser_" + use.card->toString()); // For AI
                            use.from = target;
                            data = QVariant::fromValue(use);
                        }
                    }
                }
                if (extra_target) {
                    LogMessage log;
                    log.type = "#BecomeTarget";
                    log.from = target;
                    log.card_str = use.card->toString();
                    room->sendLog(log);

                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    use.to.append(target);
                    room->sortByActionOrder(use.to);
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

class Jiaojin : public TriggerSkill
{
public:
    Jiaojin() : TriggerSkill("jiaojin")
    {
        events << DamageInflicted;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isMale() && player->canDiscard(player, "he")) {
            if (room->askForCard(player, ".Equip", "@jiaojin", data, objectName())) {
                room->broadcastSkillInvoke(objectName());

                LogMessage log;
                log.type = "#Jiaojin";
                log.from = player;
                log.arg = QString::number(damage.damage);
                log.arg2 = QString::number(--damage.damage);
                room->sendLog(log);

                if (damage.damage < 1)
                    return true;
                data = QVariant::fromValue(damage);
            }
        }
        return false;
    }
};

class Youdi : public PhaseChangeSkill
{
public:
    Youdi() : PhaseChangeSkill("youdi")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish || target->isNude()) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (p->canDiscard(target, "he")) players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *player = room->askForPlayerChosen(target, players, objectName(), "youdi-invoke", true, true);
        if (player) {
            room->broadcastSkillInvoke(objectName());
            int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, target, player);
            if (!Sanguosha->getCard(id)->isKindOf("Slash") && player->isAlive() && !player->isNude()) {
                int id2 = room->askForCardChosen(target, player, "he", "youdi_obtain");
                room->obtainCard(target, id2, false);
            }
        }
        return false;
    }
};

class Qieting : public TriggerSkill
{
public:
    Qieting() : TriggerSkill("qieting")
    {
        events << EventPhaseChanging << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive || player->getMark("qieting-Clear") > 0) return false;
			foreach (ServerPlayer *caifuren, room->getOtherPlayers(player)) {
				if (!TriggerSkill::triggerable(caifuren)) continue;
				QStringList choices;
				for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
					if (player->getEquip(i) && !caifuren->getEquip(i) && caifuren->hasEquipArea(i))
						choices << QString::number(i);
				}
				choices << "draw" << "cancel";
				QString choice = room->askForChoice(caifuren, objectName(), choices.join("+"), QVariant::fromValue(player));
				if (choice != "cancel") {
					LogMessage log;
					log.type = "#InvokeSkill";
					log.arg = objectName();
					log.from = caifuren;
					room->sendLog(log);
					if (choice == "draw") {
						room->broadcastSkillInvoke(objectName(), 2);
						room->notifySkillInvoked(caifuren, objectName());
						caifuren->drawCards(1, objectName());
					} else {
						int index = choice.toInt();
						room->broadcastSkillInvoke(objectName(), 1);
						room->notifySkillInvoked(caifuren, objectName());
						room->moveCardTo(player->getEquip(index), caifuren, Player::PlaceEquip);
					}
				}
			}
		}else{
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() != Card::TypeSkill) {
                foreach (ServerPlayer *p, use.to) {
                    if (p != player)
                        player->addMark("qieting-Clear");
                }
            }
		}
        return false;
    }
};

XianzhouDamageCard::XianzhouDamageCard()
{
    mute = true;
}

void XianzhouDamageCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    QVariant data = QVariant::fromValue(use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();
    thread->trigger(CardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, use.from, data);
}

bool XianzhouDamageCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    return targets.length() == Self->getMark("xianzhou");
}

bool XianzhouDamageCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < Self->getMark("xianzhou") && Self->inMyAttackRange(to_select);
}

void XianzhouDamageCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("xianzhou", effect.from, effect.to));
}

XianzhouCard::XianzhouCard()
{
}

bool XianzhouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void XianzhouCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->removePlayerMark(effect.from, "@handover");
    //room->doLightbox("$XianzhouAnimate");
    room->doSuperLightbox(effect.from, "xianzhou");

    int len = 0;
    DummyCard *dummy = new DummyCard;
    foreach (const Card *c, effect.from->getEquips()) {
        dummy->addSubcard(c);
        len++;
    }
    room->setPlayerMark(effect.to, "xianzhou", len);
    effect.to->obtainCard(dummy);
    delete dummy;

    bool rec = true;
    int count = 0;
    foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
        if (effect.to->inMyAttackRange(p)) {
            count++;
            if (count >= len) {
                rec = false;
                break;
            }
        }
    }

    if ((rec || !room->askForUseCard(effect.to, "@xianzhou", "@xianzhou-damage:::" + QString::number(len))))
        room->recover(effect.from, RecoverStruct(effect.to, nullptr, qMin(len, effect.from->getMaxHp() - effect.from->getHp()), "xianzhou"));
}

class Xianzhou : public ZeroCardViewAsSkill
{
public:
    Xianzhou() : ZeroCardViewAsSkill("xianzhou")
    {
        frequency = Skill::Limited;
        limit_mark = "@handover";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@handover") > 0 && player->getEquips().length() > 0;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@xianzhou";
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@xianzhou") {
            return new XianzhouDamageCard;
        } else {
            return new XianzhouCard;
        }
    }
};

Jianying::Jianying() : TriggerSkill("jianying")
{
    frequency = Frequent;
    events << CardUsed << CardResponded << EventPhaseChanging << EventLoseSkill;
    jianying = "Jianying";
	global = true;
}

bool Jianying::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    if (triggerEvent == CardUsed || triggerEvent == CardResponded) {
        const Card *card = nullptr;
        if (triggerEvent == CardUsed)
            card = data.value<CardUseStruct>().card;
        else if (triggerEvent == CardResponded) {
            CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_isUse) card = resp.m_card;
        }
        if (!card || card->getTypeId() == Card::TypeSkill) return false;

        if (card->hasSuit()){
			if(player->getPhase() != Player::NotActive)
				room->setPlayerProperty(player, "MobileJianyingLastSuitString", card->getSuitString());
		}else if(card->getNumber() <= 0)
			return false;

        if (jianying == "TenyearJianying" || player->getPhase() == Player::Play) {
            int suit = player->getMark(jianying + "Suit"), number = player->getMark(jianying + "Number");
            player->setMark(jianying + "Suit", int(card->getSuit()) + 1);
            player->setMark(jianying + "Number", card->getNumber());

            if (player->isAlive() && player->hasSkill(objectName(), true)) {
                foreach (QString mark, player->getMarkNames()) {
                    if (!mark.startsWith("&" + objectName() + "+") && !mark.contains("+#record")) continue;
                    room->setPlayerMark(player, mark, 0);
                }
                QString jianyingmark = QString("&%1+%2+%3+#record").arg(objectName())
					.arg(card->getSuitString() + "_char").arg(card->getNumberString());
                room->setPlayerMark(player, jianyingmark, 1);
				
				if ((int(card->getSuit()) + 1 == suit || (number > 0 && card->getNumber() == number))
					&& player->hasSkill(objectName()) && player->askForSkillInvoke(this, data)) {
					room->broadcastSkillInvoke(objectName());
					room->drawCards(player, 1, objectName());
				}
            }
        }
    } else if (triggerEvent == EventPhaseChanging) {
        if (jianying == "TenyearJianying") return false;
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.from == Player::Play) {
            player->setMark(jianying + "Suit", 0);
            player->setMark(jianying + "Number", 0);
            room->setPlayerProperty(player, "MobileJianyingLastSuitString", "");

            foreach (QString mark, player->getMarkNames()) {
                if (mark.startsWith("&tenyearjianying")) continue;  //在jianying和mobilejianying里会移除这些标记，因为都是global，三个技能都触发
                if (!mark.startsWith("&" + objectName() + "+") && !mark.contains("+#record")) continue;
                room->setPlayerMark(player, mark, 0);
            }
        }
    } else if (triggerEvent == EventLoseSkill) {
        if (data.toString() != objectName()) return false;
        if (player->hasSkill(objectName(), true)) return false;
        foreach (QString mark, player->getMarkNames()) {
            if (!mark.startsWith("&" + objectName() + "+") && !mark.contains("+#record")) continue;
            room->setPlayerMark(player, mark, 0);
        }
    }
    return false;
}

class Shibei : public MasochismSkill
{
public:
    Shibei() : MasochismSkill("shibei")
    {
        frequency = Compulsory;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &) const
    {
        Room *room = player->getRoom();
        if (player->getMark("shibei") > 0) {
            room->sendCompulsoryTriggerLog(player, this, qrand()%2+1);

            if (player->getMark("shibei") == 1)
                room->recover(player, RecoverStruct("shibei", player));
            else
                room->loseHp(HpLostStruct(player, 1, objectName(), player));
        }
    }
};

class ShibeiRecord : public TriggerSkill
{
public:
    ShibeiRecord() : TriggerSkill("#shibei-record")
    {
        events << DamageDone << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                foreach(ServerPlayer *p, room->getAlivePlayers())
                    p->setMark("shibei", 0);
            }
        } else if (triggerEvent == DamageDone) {
            ServerPlayer *current = room->getCurrent();
            if (!current || current->isDead() || current->getPhase() == Player::NotActive)
                return false;
            player->addMark("shibei");
        }
        return false;
    }
};

class NewSidi : public TriggerSkill
{
public:
    NewSidi() : TriggerSkill("newsidi")
    {
        events << EventPhaseEnd << EventPhaseStart << PreCardUsed << EventPhaseChanging << Death;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName()) || p->getEquips().isEmpty() || !p->canDiscard(p, "he")) continue;
                QStringList pattern;
                foreach (const Card *c, p->getEquips()) {
                    if (c->isRed() && !pattern.contains("red"))
                        pattern << "red";
                    else if (c->isBlack() && !pattern.contains("black"))
                        pattern << "black";
                    if (pattern.contains("red") && pattern.contains("black")) break;
                }
                if (pattern.isEmpty()) continue;
                const Card *card = room->askForCard(p, "^BasicCard|" + pattern.join(","), "@newsidi-discard:" + player->objectName(), data, objectName());
                if (!card) continue;
                room->broadcastSkillInvoke(objectName());
                QString colour = "";
                if (card->isBlack())
                    colour = "black";
                else if (card->isRed())
                    colour = "red";
                if (colour == "") continue;
                QStringList colours = player->property("newsidi_colour").toStringList();
                if (!colours.contains(colour)) {
                    colours << colour;
                    room->setPlayerProperty(player, "newsidi_colour", colours);
                    room->setPlayerCardLimitation(player, "use,response", QString(".|%1").arg(colour), true);
                    room->addPlayerMark(player, "&newsidi+" + colour + "-Clear");
                }
                QStringList sidis = player->property("newsidi_from").toStringList();
                if (sidis.contains(p->objectName())) continue;
                sidis << p->objectName();
                room->setPlayerProperty(player, "newsidi_from", sidis);
            }
        } else if (event == EventPhaseEnd) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            if (player->getMark("newsidi_slash-PlayClear") > 0) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName())) continue;
                QStringList sidis = player->property("newsidi_from").toStringList();
                if (!sidis.contains(p->objectName())) continue;
                sidis.removeOne(p->objectName());
                room->setPlayerProperty(player, "newsidi_from", sidis);
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName("_newsidi");
                slash->deleteLater();
                if (!p->canSlash(player, slash, false)) continue;
                room->sendCompulsoryTriggerLog(p, objectName(), true);
                room->useCard(CardUseStruct(slash, p, player));
            }
        } else if (event == PreCardUsed) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            const Card *card = data.value<CardUseStruct>().card;
            if (!card->isKindOf("Slash")) return false;
            room->addPlayerMark(player, "newsidi_slash-PlayClear");
        } else {
            if (event == EventPhaseChanging) {
                if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            }
            room->setPlayerProperty(player, "newsidi_colour", QStringList());
            room->setPlayerProperty(player, "newsidi_from", QStringList());
        }
        return false;
    }
};

NewDingpinCard::NewDingpinCard()
{
}

bool NewDingpinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getMark("newdingpin_to-PlayClear") <=0 && to_select != Self;
}

void NewDingpinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "newdingpin_to-PlayClear");
    room->addPlayerMark(effect.from, "&newdingpin-PlayClear");

    int type = Sanguosha->getCard(getSubcards().first())->getTypeId();
    room->addPlayerMark(effect.from, "newdingpin_card" + QString::number(type) + "-PlayClear");

    if (effect.from->isDead() || effect.to->isDead()) return;
    QStringList choices;
    choices << "draw";
    if (!effect.to->isNude())
        choices << "discard";
    QString choice = room->askForChoice(effect.from, "newdingpin", choices.join("+"));
    int n = effect.from->getMark("&newdingpin-PlayClear");
    if (choice == "draw")
        effect.to->drawCards(n, "newdingpin");
    else
        room->askForDiscard(effect.to, "newdingpin", n, n, false, true);
    if (effect.from->isDead() || effect.to->isDead() || !effect.to->isWounded() || effect.from->isChained()) return;
    room->setPlayerChained(effect.from);
}

class NewDingpin : public OneCardViewAsSkill
{
public:
    NewDingpin() : OneCardViewAsSkill("newdingpin")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }

    bool viewFilter(const Card *to_select) const
    {
        int n = Self->getMark("newdingpin_card" + QString::number(to_select->getTypeId()) + "-PlayClear");
        return n <= 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        NewDingpinCard *card = new NewDingpinCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class NewFaen : public TriggerSkill
{
public:
    NewFaen() : TriggerSkill("newfaen")
    {
        events << TurnedOver << ChainStateChanged;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == ChainStateChanged && !player->isChained()) return false;
        if (triggerEvent == TurnedOver && !player->faceUp()) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!player->isAlive()) return false;
            if (TriggerSkill::triggerable(p)
                && room->askForSkillInvoke(p, objectName(), QVariant::fromValue(player))) {
                room->broadcastSkillInvoke(objectName());
                player->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class NewZhongyong : public TriggerSkill
{
public:
    NewZhongyong() : TriggerSkill("newzhongyong")
    {
        events << CardOffset << CardFinished;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card->isKindOf("Slash")) return false;
            if (!effect.offset_card||!effect.offset_card->isKindOf("Jink")) return false;
            QVariantList jink = effect.from->getTag("newzhongyong_jink" + effect.card->toString()).toList();
            if (effect.offset_card->isVirtualCard() && effect.offset_card->subcardsLength() > 0) {
                foreach (int id, effect.offset_card->getSubcards()) {
                    if (jink.contains(QVariant(id))) continue;
                    jink << id;
                }
            } else if (!effect.offset_card->isVirtualCard()) {
                if (!jink.contains(QVariant(effect.offset_card->getEffectiveId())))
                    jink << effect.offset_card->getEffectiveId();
            }
            effect.from->setTag("newzhongyong_jink" + effect.card->toString(), jink);
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (use.to.contains(p)) continue;
                targets << p;
            }
            if (targets.isEmpty()) return false;

            QVariantList jink = player->getTag("newzhongyong_jink" + use.card->toString()).toList();
            QList<int> slash_ids,jink_ids = ListV2I(jink);

            foreach (int id, use.card->getSubcards()) {
                if (room->getCardPlace(id) != Player::DiscardPile)
                    slash_ids.removeOne(id);
            }
            foreach (int id, jink_ids) {
                if (room->getCardPlace(id) != Player::DiscardPile)
                    jink_ids.removeOne(id);
            }

            QStringList choices;
            if (!slash_ids.isEmpty()) choices << "slash";
            if (!jink_ids.isEmpty()) choices << "jink";
            if (choices.isEmpty()) return false;

            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@newzhongyong-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());

            QList<int> give_list = jink_ids;
            if (room->askForChoice(player, objectName(), choices.join("+"), data) == "slash")
                give_list = slash_ids;
            room->giveCard(player, target, give_list, objectName(), true);

            if (target->isDead()) return false;
            bool red = false;
            foreach (int id, give_list) {
                if (Sanguosha->getCard(id)->isRed()) {
                    red = true;
                    break;
                }
            }
            if (!red) return false;

            QList<ServerPlayer *> tos;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (player->inMyAttackRange(p) && target->canSlash(p, true))
                    tos << p;
            }
            if (tos.isEmpty()) return false;
            room->askForUseSlashTo(target, tos, "@newzhongyong-slash");
        }
        return false;
    }
};

class Fenli : public TriggerSkill
{
public:
    Fenli() : TriggerSkill("fenli")
    {
        events << EventPhaseChanging;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        Player::Phase phase = data.value<PhaseChangeStruct>().to;
        if (player->isSkipped(phase)) return false;
        if (phase == Player::Draw) {
            int hand = player->getHandcardNum();
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() > hand)
                    return false;
            }
            if (!player->askForSkillInvoke(this, "draw")) return false;
        } else if (phase == Player::Play) {
            int hp = player->getHp();
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHp() > hp)
                    return false;
            }
            if (!player->askForSkillInvoke(this, "play")) return false;
        } else if (phase == Player::Discard) {
            int equip = player->getEquips().length();
            if (equip <= 0) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getEquips().length() > equip)
                    return false;
            }
            if (!player->askForSkillInvoke(this, "discard")) return false;
        } else
            return false;

        room->broadcastSkillInvoke(objectName());
        player->skip(phase);
        return false;
    }
};

PingkouCard::PingkouCard()
{
}

bool PingkouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < Self->getMark("pingkou_phase_skipped-Clear") && to_select != Self;
}

void PingkouCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("pingkou", effect.from, effect.to));
}

class PingkouVS : public ZeroCardViewAsSkill
{
public:
    PingkouVS() : ZeroCardViewAsSkill("pingkou")
    {
        response_pattern = "@@pingkou";
    }

    const Card *viewAs() const
    {
        return new PingkouCard;
    }
};

class Pingkou : public TriggerSkill
{
public:
    Pingkou() : TriggerSkill("pingkou")
    {
        events << EventPhaseChanging << EventPhaseSkipped;
        view_as_skill = new PingkouVS;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            int max = player->getMark("pingkou_phase_skipped-Clear");
            if (max <= 0||!player->hasSkill(objectName())) return false;
            max = qMin(max, room->alivePlayerCount() - 1);
            room->askForUseCard(player, "@@pingkou", "@pingkou:" + QString::number(max));
        } else
            room->addPlayerMark(player, "pingkou_phase_skipped-Clear");
        return false;
    }
};

class OLBenxiVS : public ZeroCardViewAsSkill
{
public:
    OLBenxiVS() : ZeroCardViewAsSkill("olbenxi")
    {
        response_pattern = "@@olbenxi!";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new ExtraCollateralCard;
    }
};

class OLBenxi : public TriggerSkill
{
public:
    OLBenxi() : TriggerSkill("olbenxi")
    {
        events << CardUsed << Damage << PreCardUsed << CardResponded;
        view_as_skill =new OLBenxiVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!player->hasFlag("CurrentPlayer") || use.card->isKindOf("SkillCard")) return false;
            //room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            room->notifySkillInvoked(player, objectName());
            room->addDistance(player, -1);
            room->addPlayerMark(player, "&olbenxi-Clear");
        } else if (event == CardResponded) {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (!player->hasFlag("CurrentPlayer") || res.m_card->isKindOf("SkillCard")) return false;
            if (!res.m_isUse) return false;
            room->notifySkillInvoked(player, objectName());
            room->addDistance(player, -1);
            room->addPlayerMark(player, "&olbenxi-Clear");
        } else if (event == PreCardUsed) {
            if (!player->hasFlag("CurrentPlayer")) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
            if (use.to.length() != 1) return false;
            bool allone = true;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->distanceTo(p) != 1) {
                    allone = false;
                    break;
                }
            }
            if (!allone) return false;
            QStringList choices, excepts;
            QList<ServerPlayer *> available_targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (use.to.contains(p) || player->isProhibited(p, use.card)) continue;
                if (use.from == p && use.card->isKindOf("AOE")) continue;
                if (use.card->targetFixed()) {
                    if (!use.card->isKindOf("Peach") || p->isWounded())
                        available_targets << p;
                } else {
                    if (use.card->isKindOf("Collateral")) {
						int x = 0;
						if (use.card->targetFilter(QList<const Player *>(), p, player, x)||x>0)
							available_targets << p;
					}else if (use.card->targetFilter(QList<const Player *>(), p, player))
                        available_targets << p;
                }
            }
            if (!available_targets.isEmpty()) choices << "extra";
            choices << "ignore" << "noresponse" << "draw" <<"cancel";
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);

            for (int i = 1; i <= 2; i++) {
                if (choices.isEmpty()) break;
                QString choice = room->askForChoice(player, objectName(), choices.join("+"), data, excepts.join("+"));
                if (choice == "cancel") break;
                choices.removeOne(choice);
                excepts << choice;
                LogMessage log;
                log.type = "#FumianFirstChoice";
                log.from = player;
                log.arg = "olbenxi:" + choice;
                room->sendLog(log);
                if (choice == "extra") {
                    ServerPlayer *target;
                    if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos.append(use.card->toString());
						foreach (ServerPlayer *t, use.to)
							tos.append(t->objectName());
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
                        room->askForUseCard(player, "@@olbenxi!", "@olbenxi-extra:" + use.card->objectName());
                        target = player->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
						player->removeTag("ExtraCollateralTarget");
                        if (!target) {
                            target = available_targets.at(qrand() % available_targets.length() - 1);
                            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                                if (target->canSlash(p)){
									target->setTag("attachTarget", QVariant::fromValue(p));
									break;
								}
                            }
                        }
					}else
                        target = room->askForPlayerChosen(player, available_targets, objectName(), "@olbenxi-extra:" + use.card->objectName());
                    use.to.append(target);
                    room->sortByActionOrder(use.to);

                    if (use.card->hasFlag("olbenxi_ignore"))
                        target->addQinggangTag(use.card);

                    LogMessage log;
                    log.type = "#QiaoshuiAdd";
                    log.from = player;
                    log.to << target;
                    log.card_str = use.card->toString();
                    log.arg = "olbenxi";
                    room->sendLog(log);
                    data = QVariant::fromValue(use);
                } else if (choice == "ignore") {
                    room->setCardFlag(use.card, "olbenxi_ignore");
                    foreach (ServerPlayer *p, use.to)
                        p->addQinggangTag(use.card);
                } else if (choice == "noresponse") {
                    use.no_offset_list << "_ALL_TARGETS";
                    data = QVariant::fromValue(use);
                } else
                    room->setCardFlag(use.card, "olbenxi_damage");
            }
        } else if (event == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->hasFlag("olbenxi_damage")) return false;
            player->drawCards(1, objectName());
        }
        return false;
    }
};

YJCM2014Package::YJCM2014Package()
    : Package("YJCM2014")
{
    General *caifuren = new General(this, "caifuren", "qun", 3, false); // YJ 301
    caifuren->addSkill(new Qieting);
    caifuren->addSkill(new Xianzhou);

    General *caozhen = new General(this, "caozhen", "wei"); // YJ 302
    caozhen->addSkill(new Sidi);
    caozhen->addSkill(new SidiTargetMod);
    related_skills.insertMulti("sidi", "#sidi-target");

    General *new_caozhen = new General(this, "new_caozhen", "wei");
    new_caozhen->addSkill(new NewSidi);

    General *chenqun = new General(this, "chenqun", "wei", 3); // YJ 303
    chenqun->addSkill(new Dingpin);
    chenqun->addSkill(new Faen);
    chenqun->addSkill(new DingpinBf);
    related_skills.insertMulti("dingpin", "#dingpinbf");

    General *new_chenqun = new General(this, "new_chenqun", "wei", 3);
    new_chenqun->addSkill(new NewDingpin);
    new_chenqun->addSkill(new NewFaen);

    General *guyong = new General(this, "guyong", "wu", 3); // YJ 304
    guyong->addSkill(new Shenxing);
    guyong->addSkill(new Bingyi);

    General *hanhaoshihuan = new General(this, "hanhaoshihuan", "wei"); // YJ 305
    hanhaoshihuan->addSkill(new Shenduan);
    hanhaoshihuan->addSkill(new ShenduanTargetMod);
    hanhaoshihuan->addSkill(new Yonglve);
    hanhaoshihuan->addSkill(new YonglveSlash);
    related_skills.insertMulti("shenduan", "#shenduan-target");
    related_skills.insertMulti("yonglve", "#yonglve");

    General *jvshou = new General(this, "jvshou", "qun", 3); // YJ 306
    jvshou->addSkill(new Jianying);
    jvshou->addSkill(new Shibei);
    jvshou->addSkill(new ShibeiRecord);
    related_skills.insertMulti("shibei", "#shibei-record");

    General *sunluban = new General(this, "sunluban", "wu", 3, false); // YJ 307
    sunluban->addSkill(new Zenhui);
    sunluban->addSkill(new Jiaojin);

    General *wuyi = new General(this, "wuyi", "shu"); // YJ 308
    wuyi->addSkill(new Benxi);
    wuyi->addSkill(new BenxiTargetMod);
    wuyi->addSkill(new BenxiDistance);
    related_skills.insertMulti("benxi", "#benxi-target");
    related_skills.insertMulti("benxi", "#benxi-dist");

    General *ol_wuyi = new General(this, "ol_wuyi", "shu");
    ol_wuyi->addSkill(new OLBenxi);

    General *zhangsong = new General(this, "zhangsong", "shu", 3); // YJ 309
    zhangsong->addSkill(new Qiangzhi);
    zhangsong->addSkill(new Xiantu);

    General *zhoucang = new General(this, "zhoucang", "shu"); // YJ 310
    zhoucang->addSkill(new Zhongyong);

    General *new_zhoucang = new General(this, "new_zhoucang", "shu");
    new_zhoucang->addSkill(new NewZhongyong);

    General *zhuhuan = new General(this, "zhuhuan", "wu"); // YJ 311
    zhuhuan->addSkill(new Youdi);

    General *new_zhuhuan = new General(this, "new_zhuhuan", "wu");
    new_zhuhuan->addSkill(new Fenli);
    new_zhuhuan->addSkill(new Pingkou);

    addMetaObject<DingpinCard>();
    addMetaObject<ShenxingCard>();
    addMetaObject<BingyiCard>();
    addMetaObject<XianzhouCard>();
    addMetaObject<XianzhouDamageCard>();
    addMetaObject<SidiCard>();
    addMetaObject<NewDingpinCard>();
    addMetaObject<PingkouCard>();
}

ADD_PACKAGE(YJCM2014)
