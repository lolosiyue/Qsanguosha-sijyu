#include "exclusive-cards.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
#include "clientplayer.h"
#include "room.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "yjcm2013.h"
//#include "yingbian.h"

class HongduanqiangSkill : public WeaponSkill
{
public:
    HongduanqiangSkill() : WeaponSkill("_hongduanqiang")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMark("_hongduanqiang-Clear") > 0) return false;
        if (!player->hasTurn()) return false;

        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")
			&& damage.by_user && player->askForSkillInvoke(this, data)){
			room->setEmotion(player, "weapon/_hongduanqiang");
			room->addPlayerMark(player, "_hongduanqiang-Clear");
	
			JudgeStruct judge;
			judge.who = player;
			judge.reason = objectName();
			judge.pattern = ".";
			judge.play_animation = false;
			room->judge(judge);
	
			if (judge.card->getColor() == Card::Red)
				room->recover(player, RecoverStruct(objectName(), player));
			else if (judge.card->getColor() == Card::Black)
				player->drawCards(2, objectName());
		}
        return false;
    }
};

Hongduanqiang::Hongduanqiang(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("_hongduanqiang");
}

class LiecuidaoTargetMod : public TargetModSkill
{
public:
    LiecuidaoTargetMod() : TargetModSkill("#_liecuidao-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
		int n = from->getMark("_bintieshuangji-Clear");
		if(from->hasWeapon("_liecuidao")){
			if(card->hasFlag("Global_SlashAvailabilityChecker"))
				n++;
			else{
				QList<int> ids;
				if (card->isVirtualCard()) ids = card->getSubcards();
				else ids << card->getEffectiveId();
				const Card *c = from->getWeapon();
				if(c && ids.contains(c->getEffectiveId()))
					n--;
				n++;
			}
		}
		if(from->hasTreasure("_sanlve"))
			n++;
        return n;
    }
};

class LiecuidaoVS : public OneCardViewAsSkill
{
public:
    LiecuidaoVS() : OneCardViewAsSkill("_liecuidao")
    {
        response_pattern = "@@_liecuidao";
    }

    bool viewFilter(const Card *to_select) const
    {
        return !(to_select->isEquipped() && to_select->objectName() == "_liecuidao") && !Self->isJilei(to_select);
    }

    const Card *viewAs(const Card *c) const
    {
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcard(c);
        return card;
    }
};

class LiecuidaoSkill : public WeaponSkill
{
public:
    LiecuidaoSkill() : WeaponSkill("_liecuidao")
    {
        events << DamageCaused;
        view_as_skill = new LiecuidaoVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasTurn()) return false;
        if (player->getMark("_liecuidao-Clear") >= 2) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || damage.chain || damage.transfer) return false;
        if (!player->canDiscard(player, "he")) return false;

        const Card *card = room->askForCard(player, "@@_liecuidao", "@_liecuidao:" + damage.to->objectName(), data, objectName());

        if (card) {
            room->setEmotion(player, "weapon/_liecuidao");
            room->addPlayerMark(player, "_liecuidao-Clear");
            ++damage.damage;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

Liecuidao::Liecuidao(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("_liecuidao");
}

ShuibojianCard::ShuibojianCard()
{
    mute = true;
}

bool ShuibojianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return to_select->hasFlag("_shuibojian_canchoose") && targets.isEmpty();
}

void ShuibojianCard::onUse(Room *room, CardUseStruct &card_use) const
{
    room->setEmotion(card_use.from, "weapon/_shuibojian");
    room->addPlayerMark(card_use.from, "_shuibojian-Clear");
    foreach (ServerPlayer *p, card_use.to)
        room->setPlayerFlag(p, "_shuibojian_extratarget");
}

class ShuibojianVS : public ZeroCardViewAsSkill
{
public:
    ShuibojianVS() : ZeroCardViewAsSkill("_shuibojian")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.contains("@@_shuibojian");
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern=="@@_shuibojian1")
            return new ExtraCollateralCard;
        return new ShuibojianCard;
    }
};

class ShuibojianSkill : public WeaponSkill
{
public:
    ShuibojianSkill() : WeaponSkill("_shuibojian")
    {
        events << PreCardUsed;
        view_as_skill = new ShuibojianVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreCardUsed && WeaponSkill::triggerable(player)) {
			if (!player->hasTurn()) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            int n = player->getMark("_shuibojian-Clear");
            if (n >= 2 || use.to.isEmpty()) return false;
            if (use.card->isKindOf("Slash") || use.card->isNDTrick()) {
				if (use.card->isKindOf("Collateral")) {
                    for (int i = 1; i <= n; i++) {
                        bool canextra = false;
                        foreach (ServerPlayer *p, room->getAlivePlayers()) {
                            if (use.to.contains(p)) continue;
                            int x = 0;
							if (use.card->targetFilter(QList<const Player *>(), p, player, x)||x>0) {
                                canextra = true;
                                break;
                            }
                        }
                        if (!canextra) break;
						QStringList tos;
						tos << use.card->toString();
						foreach (ServerPlayer *t, use.to)
							tos << t->objectName();
						tos << objectName();
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
                        room->askForUseCard(player, "@@_shuibojian1", "@_shuibojian:" + use.card->objectName());
						ServerPlayer *extra = player->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
						player->tag.remove("ExtraCollateralTarget");
						if (extra) {
							room->setEmotion(player, "weapon/_shuibojian");
							room->addPlayerMark(player, "_shuibojian-Clear");
							room->notifySkillInvoked(player, "_shuibojian");
							use.to.append(extra);
							room->sortByActionOrder(use.to);
							data = QVariant::fromValue(use);
						}
                    }
                }else{
                    bool canextra = false;
                    foreach (ServerPlayer *p, room->getAlivePlayers()) {
                        if (use.to.contains(p)) continue;
                        if (player->canUse(use.card,p)) {
                            room->setPlayerFlag(p, "_shuibojian_canchoose");
                            canextra = true;
                        }
                    }
                    if (!canextra) return false;
                    player->tag["_shuibojianData"] = data;
                    if (!room->askForUseCard(player, "@@_shuibojian", "@_shuibojian:" + use.card->objectName()))
                        return false;
                    LogMessage log;
                    foreach(ServerPlayer *p, room->getAlivePlayers()) {
                        room->setPlayerFlag(p, "-_shuibojian_canchoose");
                        if (p->hasFlag("_shuibojian_extratarget")) {
                            room->setPlayerFlag(p,"-_shuibojian_extratarget");
                            use.to.append(p);
                            log.to << p;
                        }
                    }
                    if (log.to.isEmpty()) return false;
                    log.type = "#QiaoshuiAdd";
                    log.from = player;
                    log.card_str = use.card->toString();
                    log.arg = "_shuibojian";
                    room->sendLog(log);
                    room->notifySkillInvoked(player, "_shuibojian");

                    room->sortByActionOrder(use.to);
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

Shuibojian::Shuibojian(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("_shuibojian");
}

void Shuibojian::onUninstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasWeapon(objectName(), false)){
		Room *room = player->getRoom();
		if (player->isWounded()) {
			LogMessage log;
			log.type = "#TriggerEquipSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->setEmotion(player, "weapon/_shuibojian");
			room->notifySkillInvoked(player, "_shuibojian");
		}
		room->recover(player, RecoverStruct(nullptr, this, 1, objectName()));
	}
}

class HunduwanbiSkill : public WeaponSkill
{
public:
    HunduwanbiSkill() : WeaponSkill("_hunduwanbi")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
        foreach (ServerPlayer *p, use.to) {
            if (player->isDead() || !WeaponSkill::triggerable(player)) return false;
            if (p->isDead()) continue;
            int mark = player->getMark("_hunduwanbi-Clear") + 1;
            if (!player->askForSkillInvoke(this, QString("_hunduwanbi:%1::%2").arg(p->objectName()).arg(mark))) continue;
            room->setEmotion(player, "weapon/_hunduwanbi");
            room->addPlayerMark(player, "_hunduwanbi-Clear");
            mark = qMin(mark, 5);
            if (mark > 0)
                room->loseHp(HpLostStruct(p, mark, objectName(), player));
        }
        return false;
    }
};

Hunduwanbi::Hunduwanbi(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("_hunduwanbi");
}

class TianleirenSkill : public WeaponSkill
{
public:
    TianleirenSkill() : WeaponSkill("_tianleiren")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
        foreach (ServerPlayer *p, use.to) {
            if (player->isDead() || !WeaponSkill::triggerable(player)) return false;
            if (p->isDead()) continue;
            if (!player->askForSkillInvoke(this, p)) continue;
            room->setEmotion(player, "weapon/_tianleiren");

            int weapon_id = -1;
            if (player->getWeapon() && player->getWeapon()->objectName() == objectName()) {
                weapon_id = player->getWeapon()->getEffectiveId();
                room->setCardFlag(weapon_id, "using");
            }

            JudgeStruct judge;
            judge.who = p;
            judge.reason = objectName();
            judge.pattern = ".|black";
            judge.good = false;
            room->judge(judge);

            if (weapon_id > 0)
                room->setCardFlag(weapon_id, "-using");

            if (judge.card->getSuit() == Card::Spade) {
                if (p->isAlive())
                    room->damage(DamageStruct(objectName(), nullptr, p, 3, DamageStruct::Thunder));
            } else if (judge.card->getSuit() == Card::Club) {
                if (p->isAlive())
                    room->damage(DamageStruct(objectName(), nullptr, p, 1, DamageStruct::Thunder));
                if (player->isAlive()) {
                    room->recover(player, RecoverStruct(objectName(), player));
                    player->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

Tianleiren::Tianleiren(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("_tianleiren");
}

Meirenji::Meirenji(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("__meirenji");
    damage_card = true;
}

bool Meirenji::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select->isMale() && !to_select->isKongcheng() && to_select != Self
	&& targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this) 
	&& !Self->isProhibited(to_select, this);
}

void Meirenji::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (effect.to->isDead() || effect.to->isKongcheng()) break;
        if (p->isDead() || !p->isFemale()) continue;
        int id = room->askForCardChosen(p, effect.to, "h", objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
        room->obtainCard(p, Sanguosha->getCard(id), reason, false);
        if (p->isAlive() && effect.from->isAlive() && !p->isKongcheng()) {
            const Card *card = room->askForCard(p, ".|.|.|hand!", "@__meirenji-give:" + effect.from->objectName(), QVariant::fromValue(effect.from), Card::MethodNone);
            if(!card) card = p->getHandcards().at(qrand()%p->getHandcardNum());
			if(card) room->giveCard(p, effect.from, card, objectName());
        }
    }
    if (effect.from->isDead() || effect.to->isDead()) return;
    if (effect.from->getHandcardNum() > effect.to->getHandcardNum())
        room->damage(DamageStruct(this, effect.to, effect.from));
    else if (effect.from->getHandcardNum() < effect.to->getHandcardNum())
        room->damage(DamageStruct(this, effect.from, effect.to));
}

Xiaolicangdao::Xiaolicangdao(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("__xiaolicangdao");
    damage_card = true;
}

bool Xiaolicangdao::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void Xiaolicangdao::onEffect(CardEffectStruct &effect) const
{
    effect.to->drawCards(qMin(5, effect.to->getLostHp()), objectName());
    if (effect.to->isDead()) return;
    effect.to->getRoom()->damage(DamageStruct(this, effect.from, effect.to));
}

class PilicheSkill : public WeaponSkill
{
public:
    PilicheSkill() : WeaponSkill("_piliche")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isDead() || !damage.card || damage.card->isKindOf("SkillCard") || damage.card->isKindOf("DelayedTrick")) return false;

        DummyCard *dummy = new DummyCard;
        dummy->deleteLater();

        if (damage.to->getArmor() && player->canDiscard(damage.to, damage.to->getArmor()->getEffectiveId()))
            dummy->addSubcard(damage.to->getArmor()->getEffectiveId());
        if (damage.to->getDefensiveHorse() && player->canDiscard(damage.to, damage.to->getDefensiveHorse()->getEffectiveId()))
            dummy->addSubcard(damage.to->getDefensiveHorse()->getEffectiveId());
        if (dummy->subcardsLength() == 0) return false;
        if (!player->askForSkillInvoke(this, damage.to)) return false;
        room->setEmotion(player, "weapon/_piliche");
        room->throwCard(dummy, damage.to, damage.from);
        return false;
    }
};

Piliche::Piliche(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_piliche");
}

class SecondPilicheSkill : public WeaponSkill
{
public:
    SecondPilicheSkill() : WeaponSkill("_secondpiliche")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!player->canDiscard(damage.to, "e")) return false;

        DummyCard *dummy = new DummyCard;
        dummy->deleteLater();

        foreach (int id, damage.to->getEquipsId()) {
            if (player->canDiscard(damage.to, id))
                dummy->addSubcard(id);
        }

        if (dummy->subcardsLength() == 0) return false;
        if (!player->askForSkillInvoke(this, damage.to)) return false;
        room->setEmotion(player, "weapon/_secondpiliche");
        room->throwCard(dummy, damage.to, damage.from);
        return false;
    }
};

SecondPiliche::SecondPiliche(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_secondpiliche");
}

class SichengliangyuSkill : public TreasureSkill
{
public:
    SichengliangyuSkill() : TreasureSkill("_sichengliangyu")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasTreasure(objectName())) continue;
            if (p->getHandcardNum() < p->getHp() && p->askForSkillInvoke(this)) {
                //room->setEmotion(p, "treasure/_sichengliangyu");
                p->drawCards(2, objectName());
                if (p->isAlive() && p->getTreasure() && p->getTreasure()->objectName() == objectName()
					&& p->canDiscard(p, p->getTreasure()->getEffectiveId()))
                    room->throwCard(p->getTreasure(), objectName(), p);
            }
        }
        return false;
    }
};

Sichengliangyu::Sichengliangyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_sichengliangyu");
}

class TiejixuanyuSkill : public TreasureSkill
{
public:
    TiejixuanyuSkill() : TreasureSkill("_tiejixuanyu")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("damage_point_round") <= 0 && target->canDiscard(target, "he");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->isDead() || !player->canDiscard(player, "he")) return false;
            if (p->isDead() || !p->hasTreasure(objectName())) continue;
            if (p->askForSkillInvoke(this, player)) {
                //room->setEmotion(p, "treasure/_tiejixuanyu");
                room->askForDiscard(player, objectName(), 2, 2, false, true);
                if (p->isAlive() && p->getTreasure() && p->getTreasure()->objectName() == objectName()
					&& p->canDiscard(p, p->getTreasure()->getEffectiveId()))
                    room->throwCard(p->getTreasure(), objectName(), p);
            }
        }
        return false;
    }
};

Tiejixuanyu::Tiejixuanyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_tiejixuanyu");
}

class FeilunzhanyuSkill : public TreasureSkill
{
public:
    FeilunzhanyuSkill() : TreasureSkill("_feilunzhanyu")
    {
        events << EventPhaseChanging << PreCardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || use.card->isKindOf("BasicCard")) return false;
			room->addPlayerMark(player, "_feilunzhanyu_used_notbasic-Clear");
		}else{
			if (data.value<PhaseChangeStruct>().to!=Player::NotActive
			||player->getMark("_feilunzhanyu_used_notbasic-Clear")<1) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead() || player->isNude()) return false;
				if (p->isDead() || !p->hasTreasure(objectName())) continue;
				if (p->askForSkillInvoke(this, player)) {
					//room->setEmotion(p, "treasure/_feilunzhanyu");

					const Card *card = room->askForExchange(player, objectName(), 1, 1, true, "@_feilunzhanyu-give:" + p->objectName());
					room->giveCard(player, p, card, objectName());

					if (p->isAlive() && p->getTreasure() && p->getTreasure()->objectName() == objectName()
						&& p->canDiscard(p, p->getTreasure()->getEffectiveId()))
						room->throwCard(p->getTreasure(), objectName(), p);
				}
			}
		}
        return false;
    }
};

Feilunzhanyu::Feilunzhanyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_feilunzhanyu");
}

class QiongshuSkill : public TreasureSkill
{
public:
    QiongshuSkill() : TreasureSkill("_qiongshu")
    {
        events << DamageInflicted;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        int num = damage.damage;
        if (!player->canDiscard(player, "he") || player->getCardCount() < num) return false;
        if (!room->askForDiscard(player, objectName(), num, num, true, true, "@_qiongshu-discard:" + QString::number(num), "^Qiongshu", objectName()))
            return false;
        room->setEmotion(player, "treasure/_qiongshu");
        room->broadcastSkillInvoke(this);
        return true;
    }
};

Qiongshu::Qiongshu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_qiongshu");
}

class XishuSkill : public TreasureSkill
{
public:
    XishuSkill() : TreasureSkill("_xishu")
    {
        events << EventPhaseChanging;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::Judge || player->isSkipped(Player::Judge)) return false;

        QStringList choices;
        if (!player->isSkipped(Player::Judge))
            choices << "judge";
        if (!player->isSkipped(Player::Discard))
            choices << "discard";
        if (choices.isEmpty()) return false;

        if (!player->askForSkillInvoke(objectName()+"$-1")) return false;
        room->setEmotion(player, "treasure/_xishu");
        QString choice = room->askForChoice(player, objectName(), choices.join("+"));
        if (choice == "judge")
            player->skip(Player::Judge);
        else if (choice == "discard")
            player->skip(Player::Discard);
        return false;
    }
};

Xishu::Xishu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_xishu");
}

class JinshuSkill : public TreasureSkill
{
public:
    JinshuSkill() : TreasureSkill("_jinshu")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        int hand = player->getHandcardNum(), max = qMin(5, player->getMaxCards());
        if (hand >= max) return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->setEmotion(player, "treasure/_jinshu");
        player->drawCards(max - hand, objectName());
        return false;
    }
};

Jinshu::Jinshu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_jinshu");
}

class TenyearPilicheSkill : public TreasureSkill
{
public:
    TenyearPilicheSkill() : TreasureSkill("_tenyearpiliche")
    {
        events << ConfirmDamage << PreHpRecover << CardUsed;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreHpRecover) {
            RecoverStruct rec = data.value<RecoverStruct>();
            if (!rec.card || !rec.card->isKindOf("BasicCard") || !rec.who
			|| !rec.who->hasFlag("CurrentPlayer") || !TreasureSkill::triggerable(rec.who)) return false;
            if (rec.recover >= player->getMaxHp() - player->getHp()) return false;
            room->sendCompulsoryTriggerLog(player, this);
            //room->setEmotion(player, "treasure/_tenyearpiliche");
            rec.recover++;
            data = QVariant::fromValue(rec);
        }else if (player->hasFlag("CurrentPlayer")) {
            if (event == ConfirmDamage) {
                if (!TreasureSkill::triggerable(player)) return false;
                DamageStruct damage = data.value<DamageStruct>();
                if (!damage.from || !damage.by_user || !damage.card || !damage.card->isKindOf("BasicCard")) return false;
                room->sendCompulsoryTriggerLog(player, this);
                //room->setEmotion(player, "treasure/_tenyearpiliche");
                damage.damage++;
                data = QVariant::fromValue(damage);
            }
        } else {
            if (!TreasureSkill::triggerable(player)) return false;

            const Card *c = nullptr;
            if (event == CardUsed)
                c = data.value<CardUseStruct>().card;
            if (!c || !c->isKindOf("BasicCard")) return false;
            room->sendCompulsoryTriggerLog(player, this);
            //room->setEmotion(player, "treasure/_tenyearpiliche");
            player->drawCards(1, objectName());
        }
        return false;
    }
};

TenyearPiliche::TenyearPiliche(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_tenyearpiliche");
}

ZhizheBasic::ZhizheBasic(Suit suit, int number) : BasicCard(suit, number)
{
    setObjectName("_zhizhe_basic");
}

QString ZhizheBasic::getSubtype() const
{
    return "zhizhe_card";
}

bool ZhizheBasic::isAvailable(const Player *) const
{
    return false;
}

ZhizheTrick::ZhizheTrick(Card::Suit suit, int number) : TrickCard(suit, number)
{
    setObjectName("_zhizhe_trick");
}

QString ZhizheTrick::getSubtype() const
{
    return "zhizhe_card";
}

bool ZhizheTrick::isAvailable(const Player *) const
{
    return false;
}

ZhizheSuijiyingbian::ZhizheSuijiyingbian(Card::Suit suit, int number) : Suijiyingbian(suit, number)
{
    setObjectName("_zhizhe_suijiyingbian");
}

QString ZhizheSuijiyingbian::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheWeapon::ZhizheWeapon(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("_zhizhe_weapon");
}

QString ZhizheWeapon::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheArmor::ZhizheArmor(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("_zhizhe_armor");
}

QString ZhizheArmor::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheTreasure::ZhizheTreasure(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_zhizhe_treasure");
}

QString ZhizheTreasure::getSubtype() const
{
    return "zhizhe_card";
}

class BintieshuangjiSkill : public WeaponSkill
{
public:
    BintieshuangjiSkill() : WeaponSkill("_bintieshuangji")
    {
        events << CardOffset;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card->isKindOf("Slash")||!player->askForSkillInvoke(this,data)) return false;
        room->loseHp(player,1,true,player,objectName());
		if(player->isAlive()){
			if(!room->getCardOwner(effect.card->getEffectiveId()))
				player->obtainCard(effect.card);
			player->drawCards(1,objectName());
			room->addPlayerMark(player,"_bintieshuangji-Clear");
		}
        return false;
    }
};

Bintieshuangji::Bintieshuangji(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("_bintieshuangji");
}

class SanlveMax : public MaxCardsSkill
{
public:
    SanlveMax() : MaxCardsSkill("#_sanlve_max")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasTreasure("_sanlve"))
            return 1;
        return 0;
    }
};

class SanlveAttack : public AttackRangeSkill
{
public:
    SanlveAttack() : AttackRangeSkill("#_sanlve_attack")
    {
    }

    int getExtra(const Player *target, bool) const
    {
        if (target->hasTreasure("_sanlve"))
            return 1;
        return 0;
    }
};

Sanlve::Sanlve(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_sanlve");
}

class ZhaogujingVS : public ZeroCardViewAsSkill
{
public:
    ZhaogujingVS() : ZeroCardViewAsSkill("_zhaogujing")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.startsWith("@@_zhaogujing");
    }

    const Card *viewAs() const
    {
		Card*dc = Sanguosha->cloneCard(Self->property("_zhaogujingUse").toString());
		dc->setSkillName("_zhaogujing");
        return dc;
    }
};

class ZhaogujingSkill : public TreasureSkill
{
public:
    ZhaogujingSkill() : TreasureSkill("_zhaogujing")
    {
        events << EventPhaseEnd;
        view_as_skill = new ZhaogujingVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play || player->isKongcheng()) return false;
        const Card*c = room->askForCard(player,"BasicCard,TrickCard+^DelayedTrick|.|.|hand","_zhaogujing0:",data,Card::MethodNone);
        if (c){
			player->skillInvoked(this,0);
			room->setEmotion(player, "treasure/_zhaogujing");
			room->showCard(player,c->getEffectiveId());
			Card*dc = Sanguosha->cloneCard(c->objectName());
			dc->setSkillName("_zhaogujing");
			if(dc->isAvailable(player)){
				room->setPlayerProperty(player,"_zhaogujingUse",dc->objectName());
				room->askForUseCard(player,"@@_zhaogujing","_zhaogujing0:"+dc->objectName());
			}
			dc->deleteLater();
		}
        return false;
    }
};

Zhaogujing::Zhaogujing(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_zhaogujing");
}

class DagongcheJinjiSkill : public WeaponSkill
{
public:
    DagongcheJinjiSkill() : WeaponSkill("_dagongche_jinji")
    {
        events << BeforeCardsMove << DamageCaused;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.to_place==Player::PlaceEquip){
				room->sendCompulsoryTriggerLog(player,this);
				move.to_place = Player::DiscardPile;
				data.setValue(move);
			}else if(move.from==player&&move.from_places.contains(Player::PlaceEquip)){
				QList<int>ids;
				int n = 0;
				foreach (int id, move.card_ids) {
					if(move.from_places[n]==Player::PlaceEquip&&Sanguosha->getCard(id)->objectName().contains("_dagongche_"))
						ids << id;
					n++;
				}
				if(ids.length()>0&&move.reason.m_skillName!="quchong"&&move.reason.m_skillName!="BreakCard"){
					QString tt = "durable"+QString::number(ids[0]);
					n = room->getTag(tt).toInt();
					move.removeCardIds(ids);
					data.setValue(move);
					n--;
					room->setTag(tt,n);
					room->sendCompulsoryTriggerLog(player,this);
					if(n<1) room->breakCard(ids,player);
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(player->askForSkillInvoke(this,damage.to)){
				if(player->getWeapon()){
					QString tt = "durable"+player->getWeapon()->toString();
					int n = room->getTag(tt).toInt();
					n--;
					room->setTag(tt,n);
					if(n<1){
						room->breakCard(player->getWeapon(),player);
					}
				}
				int n = qMin(3,room->getTag("TurnLengthCount").toInt());
				player->damageRevises(data,n);
			}
		}
        return false;
    }
};

DagongcheJinji::DagongcheJinji(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_dagongche_jinji");
}

void DagongcheJinji::onInstall(ServerPlayer *player) const
{
	Room *room = player->getRoom();
	room->setTag("durable"+toString(),2);
	Weapon::onInstall(player);
	QList<int>ids = player->getEquipsId();
	ids.removeAll(getEffectiveId());
	room->throwCard(ids,objectName(),nullptr);
}

void DagongcheJinji::onUninstall(ServerPlayer *player) const
{
	Room *room = player->getRoom();
	room->setTag("dagongche"+toString(),"");
	Weapon::onUninstall(player);
}

class DagongcheShouyuSkill : public WeaponSkill
{
public:
    DagongcheShouyuSkill() : WeaponSkill("_dagongche_shouyu")
    {
        events << BeforeCardsMove << DamageInflicted;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.to_place==Player::PlaceEquip){
				room->sendCompulsoryTriggerLog(player,this);
				move.to_place = Player::DiscardPile;
				data.setValue(move);
			}else if(move.from==player&&move.from_places.contains(Player::PlaceEquip)){
				QList<int>ids;
				int n = 0;
				foreach (int id, move.card_ids) {
					if(move.from_places[n]==Player::PlaceEquip&&Sanguosha->getCard(id)->objectName().contains("_dagongche_"))
						ids << id;
					n++;
				}
				if(ids.length()>0&&move.reason.m_skillName!="quchong"&&move.reason.m_skillName!="BreakCard"){
					QString tt = "durable"+QString::number(ids[0]);
					n = room->getTag(tt).toInt();
					move.removeCardIds(ids);
					data.setValue(move);
					n--;
					room->setTag(tt,n);
					room->sendCompulsoryTriggerLog(player,this);
					if(n<1) room->breakCard(ids,player);
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			int x = 0;
			if(player->getWeapon()){
				room->sendCompulsoryTriggerLog(player,this);
				QString tt = "durable"+player->getWeapon()->toString();
				int n = room->getTag(tt).toInt();
				x = qMin(damage.damage,n);
				n -= x;
				room->setTag(tt,n);
				if(n<1) room->breakCard(player->getWeapon(),player);
			}
			if(x>0)
				return player->damageRevises(data,-x);
		}
        return false;
    }
};

DagongcheShouyu::DagongcheShouyu(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_dagongche_shouyu");
}

void DagongcheShouyu::onInstall(ServerPlayer *player) const
{
	Room *room = player->getRoom();
	room->setTag("durable"+toString(),4);
	Weapon::onInstall(player);
	QList<int>ids = player->getEquipsId();
	ids.removeAll(getEffectiveId());
	room->throwCard(ids,objectName(),nullptr);
}

void DagongcheShouyu::onUninstall(ServerPlayer *player) const
{
	Room *room = player->getRoom();
	room->setTag("dagongche"+toString(),"");
	Weapon::onUninstall(player);
}

class ZhuobangSkillvs : public ViewAsSkill
{
public:
	ZhuobangSkillvs() : ViewAsSkill("__zhuobang")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *c) const
	{
		const ViewAsSkill*vs = Sanguosha->getViewAsSkill(Self->property("__zhuobangSkill").toString());
		return vs&&vs->viewFilter(cards,c);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		const ViewAsSkill*vs = Sanguosha->getViewAsSkill(Self->property("__zhuobangSkill").toString());
		if(vs) return vs->viewAs(cards);
		return nullptr;
	}

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
		const ViewAsSkill*vs = Sanguosha->getViewAsSkill(player->property("__zhuobangSkill").toString());
		return vs&&vs->isEnabledAtResponse(player,pattern);
    }

	bool isEnabledAtPlay(const Player *player) const
	{
		const ViewAsSkill*vs = Sanguosha->getViewAsSkill(player->property("__zhuobangSkill").toString());
		return vs&&vs->isEnabledAtPlay(player);
	}
};

class ZhuobangSkill : public WeaponSkill
{
public:
    ZhuobangSkill() : WeaponSkill("__zhuobang")
    {
		for (int i = (int)GameStart; i < (int)NumOfEvents; i++) {
			events << (TriggerEvent)i;
		}
		view_as_skill = new ZhuobangSkillvs;
    }

    bool triggerable(const ServerPlayer *) const
    {
        return true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
		foreach(ServerPlayer *p, room->getAllPlayers()){
			QString ws = p->property("__zhuobangSkill").toString();
			if(ws.isEmpty()) continue;
			const TriggerSkill*ts = Sanguosha->getTriggerSkill(ws);
			if(ts&&ts->hasEvent(event)&&ts->triggerable(player,room,event,p,data)){
				if(p->hasWeapon(objectName())&&ts->trigger(event, room, player, data))
					return true;
			}
		}
		return false;
    }
};

Zhuobang::Zhuobang(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("__zhuobang");
}

int Zhuobang::getRange(const Player *player) const
{
    if(player) return qMax(range,player->property("__zhuobangRange").toInt());
	return range;
}

Youbi::Youbi(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("__youbi");
}

int Youbi::getRange(const Player *player) const
{
    if(player) return qMax(range,player->property("__youbiRange").toInt());
    return range;
}







class ExclusiveEquipSkill : public TriggerSkill
{
public:
    ExclusiveEquipSkill() : TriggerSkill("exclusiveequipskill")
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
        global = true;
    }

    int getPriority(TriggerEvent) const
    {
        return 9;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || move.reason.m_eventName == "break_equip") return false;
        QList<int> destroy;
		if(move.to_place != Player::PlaceEquip){
			static QStringList to_places;
			if(to_places.isEmpty()) to_places << "_qiongshu" << "_xishu" << "_jinshu";
			foreach (int id, move.card_ids) {
				const Card *card = Sanguosha->getEngineCard(id);
				if (to_places.contains(card->objectName())||to_places.contains(room->ZhizheCardViewAsEquip(card)))
					destroy << id;
			}
		}
		if(move.from_places.contains(Player::PlaceEquip)){
			static QStringList equiparea;
			if(equiparea.isEmpty()) equiparea << "_piliche" << "_sichengliangyu" << "_tiejixuanyu" << "_feilunzhanyu"
			<< "_tenyearpiliche" << "god_ship" << "__zhuobang" << "__youbi";
			for (int i = 0; i < move.card_ids.length(); i++) {
				if (move.from_places.at(i)!=Player::PlaceEquip||destroy.contains(move.card_ids.at(i))) continue;
				const Card *card = Sanguosha->getEngineCard(move.card_ids.at(i));
				if (equiparea.contains(card->objectName())||equiparea.contains(room->ZhizheCardViewAsEquip(card)))
					destroy << move.card_ids.at(i);
			}
		}
		if (move.to_place == Player::DiscardPile) {
			static QStringList discardpile;
			if(discardpile.isEmpty()) discardpile << "_hongduanqiang" << "_liecuidao" << "_shuibojian" << "_hunduwanbi" << "_tianleiren";
			foreach (int id, move.card_ids) {
				if (destroy.contains(id)) continue;
				const Card *card = Sanguosha->getEngineCard(id);
				if (discardpile.contains(card->objectName())||discardpile.contains(room->ZhizheCardViewAsEquip(card)))
					destroy << id;
			}
		}
        if (destroy.length()>0) {
            move.removeCardIds(destroy);
            data = QVariant::fromValue(move);
			move.reason.m_eventName = "break_equip";
            room->moveCardsAtomic(CardsMoveStruct(destroy,nullptr,Player::PlaceTable,move.reason),true);
        }
        return false;
    }
};

ExclusiveCardsPackage::ExclusiveCardsPackage()
    : Package("exclusive_cards", Package::CardPack)
{
    QList<Card *> cards;

    cards << new Hunduwanbi(Card::Spade, 1)
		<< new Tianleiren(Card::Spade, 1)
		<< new Shuibojian(Card::Club, 1)
		<< new Hongduanqiang(Card::Heart, 1)
		<< new Liecuidao(Card::Diamond, 1)
		<< new Feilunzhanyu(Card::Spade, 5)
		<< new Tiejixuanyu(Card::Club, 5)
		<< new Sichengliangyu(Card::Heart, 5)
        << new Qiongshu(Card::Spade, 12)
		<< new Xishu(Card::Club, 12)
		<< new Jinshu(Card::Heart, 12)
		<< new Piliche(Card::Diamond, 9)
		<< new SecondPiliche(Card::Diamond, 9)
        << new TenyearPiliche(Card::Diamond, 9)
        << new Bintieshuangji(Card::Diamond, 13)
        << new Sanlve(Card::Spade, 5)
        << new Zhaogujing(Card::Diamond, 4)
        << new DagongcheJinji(Card::NoSuit, 0)
        << new DagongcheShouyu(Card::NoSuit, 0);

    cards << new Meirenji(Card::NoSuit, 0) << new Xiaolicangdao(Card::NoSuit, 0);

    for (int i = 0; i < 5; i++) {
        cards << new ZhizheBasic(Card::NoSuit, 0);
        cards << new ZhizheTrick(Card::NoSuit, 0);
        cards << new ZhizheSuijiyingbian(Card::NoSuit, 0);
        cards << new ZhizheWeapon(Card::NoSuit, 0);
        cards << new ZhizheArmor(Card::NoSuit, 0);
        Horse *horse = new DefensiveHorse(Card::NoSuit, 0, 0);
        horse->setObjectName("_zhizhe_defensivehorse");
        cards << horse;
        horse = new OffensiveHorse(Card::NoSuit, 0, 0);
        horse->setObjectName("_zhizhe_offensivehorse");
        cards << horse;
        cards << new ZhizheTreasure(Card::NoSuit, 0);
    }

    foreach(Card *card, cards)
        card->setParent(this);

    addMetaObject<ShuibojianCard>();

    skills << new HongduanqiangSkill << new LiecuidaoTargetMod << new LiecuidaoSkill << new ShuibojianSkill
           << new HunduwanbiSkill << new TianleirenSkill << new PilicheSkill << new SecondPilicheSkill
           << new SichengliangyuSkill << new TiejixuanyuSkill << new FeilunzhanyuSkill << new QiongshuSkill
           << new XishuSkill << new JinshuSkill << new TenyearPilicheSkill << new BintieshuangjiSkill
		   << new SanlveMax << new SanlveAttack << new ZhaogujingSkill << new DagongcheJinjiSkill
		   << new DagongcheShouyuSkill;

    skills << new ExclusiveEquipSkill;

    related_skills.insertMulti("_liecuidao", "#_liecuidao-target");
}
ADD_PACKAGE(ExclusiveCards)