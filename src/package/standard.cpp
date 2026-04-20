#include "standard.h"
#include "serverplayer.h"
#include "room.h"
//#include "skill.h"
//#include "maneuvering.h"
#include "clientplayer.h"
#include "engine.h"
//#include "client.h"
#include "exppattern.h"
#include "roomthread.h"
#include "wrapped-card.h"

QString BasicCard::getType() const
{
    return "basic";
}

Card::CardType BasicCard::getTypeId() const
{
    return TypeBasic;
}

TrickCard::TrickCard(Suit suit, int number)
    : Card(suit, number), cancelable(true)
{
}

void TrickCard::setCancelable(bool cancelable)
{
    this->cancelable = cancelable;
}

QString TrickCard::getType() const
{
    return "trick";
}

Card::CardType TrickCard::getTypeId() const
{
    return TypeTrick;
}

bool TrickCard::isCancelable(const CardEffectStruct &effect) const
{
    Q_UNUSED(effect);
    return cancelable;
}

QString EquipCard::getType() const
{
    return "equip";
}

Card::CardType EquipCard::getTypeId() const
{
    return TypeEquip;
}

bool EquipCard::isAvailable(const Player *player) const
{
    if (targetFixed()) {
        // 检查所有需要的装备栏是否都存在（支持双栏装备）
        QList<int> occupy_slots = getOccupyLocations();
        foreach (int slot, occupy_slots) {
            if (!player->hasEquipArea(slot)) {
                return false;  // 有任何一个栏位不存在，就不能使用
            }
        }
        return Card::isAvailable(player) && !player->isProhibited(player, this);
    }
    return Card::isAvailable(player);
}

bool EquipCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty() || Self->isProhibited(to_select, this))
		return false;
	
	// 检查目标是否拥有所有需要的装备栏（支持双栏装备）
	QList<int> occupy_slots = getOccupyLocations();
	foreach (int slot, occupy_slots) {
		if (!to_select->hasEquipArea(slot)) {
			return false;  // 有任何一个栏位不存在，就不能选择该目标
		}
	}
	return true;
}
void EquipCard::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty())
        use.to << use.from;

    QVariant data = QVariant::fromValue(use);
    room->getThread()->trigger(PreCardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();

    LogMessage log;
    log.from = use.from;
    if (!use.card->targetFixed()||use.to.length()>1||!use.to.contains(use.from))
        log.to = use.to;
    log.type = "#UseCard";
    log.card_str = use.card->toString();
    room->sendLog(log);

    CardMoveReason reason(CardMoveReason::S_REASON_USE, use.from->objectName(), use.card->getSkillName(), "");
    if (use.to.size()==1&&use.to.first()!=use.from) reason.m_targetId = use.to.first()->objectName();
    reason.m_extraData = QVariant::fromValue(getRealCard());
    reason.m_useStruct = use;
	room->moveCardTo(use.card, nullptr, Player::PlaceTable, reason, true);

    if (room->getThread()->trigger(CardUsed, room, use.from, data) && room->CardInTable(use.card))
		room->moveCardTo(use.card, nullptr, Player::DiscardPile, reason, true);
    use = data.value<CardUseStruct>();
    room->getThread()->trigger(CardFinished, room, use.from, data);
}

void EquipCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!room->CardInTable(this)) return;
    CardUseStruct use = room->getTag("UseHistory"+toString()).value<CardUseStruct>();
    
    // 获取装备占用的所有栏位
    QList<int> occupy_slots = getOccupyLocations();
    
    foreach (ServerPlayer *to, targets) {
		if (to->isDead()) continue;
		
		// 检查是否所有需要的栏位都存在（没有被废除）
		bool all_slots_available = true;
		foreach (int slot, occupy_slots) {
			if (!to->hasEquipArea(slot)) {
				all_slots_available = false;
				break;
			}
		}
		
		if (!all_slots_available) {
			// 某个栏位被废除了，装备失败
			LogMessage log;
			log.type = "#EquipAreaAbandoned";
			log.from = to;
			log.card_str = toString();
			room->sendLog(log);
			continue;
		}
		
		if (use.nullified_list.contains("_ALL_TARGETS")||use.nullified_list.contains(to->objectName())){
			LogMessage log;
			log.type = "#CardNullified";
			log.from = to;
			log.card_str = toString();
			room->sendLog(log);
			room->setEmotion(to, "skill_nullify");
			continue;
		}
		
		QList<CardsMoveStruct> exchangeMove;
		QList<int> replaced_ids;
		
		foreach (int slot, occupy_slots) {
			QList<const Card*> slot_equips;
			foreach(const Card* c, to->getEquips()) {
				const EquipCard* e = qobject_cast<const EquipCard*>(c->getRealCard());
				if (e && e->getOccupyLocations().contains(slot))
					slot_equips << e;
			}
			
			if (slot_equips.length() >= to->getEquipArea(slot)) {
				const EquipCard *to_replace = NULL;
				
				if (slot_equips.length() > 1) {
					QList<int> equip_ids;
					foreach (const Card *equip, slot_equips) {
						int eid = equip->getEffectiveId();
						if (!replaced_ids.contains(eid)) {
							equip_ids << eid;
						}
					}
					if (!equip_ids.isEmpty()) {
						room->fillAG(equip_ids, to);
						int id = room->askForAG(to, equip_ids, false, objectName());
						room->clearAG(to);
						if (id > 0) {
							to_replace = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
						}
					}
				}
				
				if (!to_replace) {
					foreach (const Card *equip, slot_equips) {
						int eid = equip->getEffectiveId();
						if (!replaced_ids.contains(eid)) {
							to_replace = qobject_cast<const EquipCard *>(equip->getRealCard());
							break;
						}
					}
				}
				
				if (to_replace) {
					int replace_id = to_replace->getEffectiveId();
					if (!replaced_ids.contains(replace_id)) {
						CardsMoveStruct move2(replace_id, NULL, Player::DiscardPile,
							CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, to->objectName(), QString(), "change equip"));
						exchangeMove.append(move2);
						replaced_ids << replace_id;
					}
				}
			}
		}
		
        CardsMoveStruct move1(getEffectiveId(), to, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_USE, to->objectName(), getSkillName(), QString()));
        exchangeMove.append(move1);
		room->moveCardsAtomic(exchangeMove, true);
        return;
	}
	room->moveCardTo(this, NULL, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_USE, source->objectName(), getSkillName(), ""), true);
}

static bool isEquipSkillViewAsSkill(const Skill *skill)
{
    if (skill->inherits("TriggerSkill")) {
        const TriggerSkill *ts = qobject_cast<const TriggerSkill *>(skill);
        if (ts && ts->getViewAsSkill()) return true;
    }
    return skill->inherits("ViewAsSkill");
}

void EquipCard::onInstall(ServerPlayer *player) const
{
    const Skill *skill = Sanguosha->getSkill(this);
    if (skill) {
        Room *room = player->getRoom();
        if (isEquipSkillViewAsSkill(skill))
			room->attachSkillToPlayer(player, objectName());
		else
			room->acquireSkill(player,skill,true,true,false);
    }
}

void EquipCard::onUninstall(ServerPlayer *player) const
{
	player->getRoom()->detachSkillFromPlayer(player,objectName(),true,true,false);
}

// 双栏装备支持：获取装备实际占用的栏位列表
QList<int> EquipCard::getOccupyLocations() const
{
    if (!occupy_locations.isEmpty())
        return occupy_locations;
    
    // 默认返回单个location
    QList<int> result;
    result << location();
    return result;
}

// 设置装备占用的栏位（用于Lua创建的装备）
void EquipCard::setOccupyLocations(const QList<int> &locations)
{
    this->occupy_locations = locations;
}

QString GlobalEffect::getSubtype() const
{
    return "global_effect";
}

bool GlobalEffect::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return !Self->isProhibited(to_select, this, targets);
}

void GlobalEffect::onUse(Room *room, CardUseStruct &card_use) const
{
    if(card_use.to.isEmpty()) card_use.to = room->getAllPlayers();
	QHash<const Skill *, QList<ServerPlayer *> > skillBans;
	foreach (ServerPlayer *p, card_use.to) {
		const Skill *skill = room->isProhibited(card_use.from, p, this);
		if (skill){
			if (skill->isVisible())
				skillBans[skill] << p;
			else{
				skill = Sanguosha->getMainSkill(skill->objectName());
				skillBans[skill] << p;
			}
			card_use.to.removeOne(p);
		}
	}
	foreach (const Skill *skill, skillBans.keys()) {
		LogMessage log;
		log.to = skillBans[skill];
		if (log.to.isEmpty()) continue;
		log.type = "#SkillAvoid";
		log.from = log.to.first();
		log.arg = skill->objectName();
		log.arg2 = objectName();
		room->broadcastSkillInvoke(log.arg);
		if (log.to.length()==1){
			if (log.from->hasSkill(log.arg)){
				room->sendLog(log);
				room->notifySkillInvoked(log.from, log.arg);
			}else{
				ServerPlayer *p = room->findPlayerBySkillName(log.arg);
				if (p){
					log.type = "#SkillAvoidFrom";
					log.from = p;
					room->sendLog(log);
					room->notifySkillInvoked(p, log.arg);
				}else
					room->sendLog(log);
			}
		}else{
			log.from = room->findPlayerBySkillName(log.arg);
			if (log.from){
				log.type = "#SkillAvoidFrom";
				room->sendLog(log);
				room->notifySkillInvoked(log.from, log.arg);
			}else{
				foreach (ServerPlayer *t, log.to) {
					log.from = t;
					room->sendLog(log);
				}
			}
		}
	}
    TrickCard::onUse(room, card_use);
}

bool GlobalEffect::isAvailable(const Player *player) const
{
    QList<const Player *> players = player->getAliveSiblings();
    players << player;
    foreach (const Player *p, players) {
        if (player->isProhibited(p, this)) continue;
		return TrickCard::isAvailable(player);
    }
	return false;
}

QString AOE::getSubtype() const
{
    return "aoe";
}

bool AOE::isAvailable(const Player *player) const
{
    foreach (const Player *p, player->getAliveSiblings()) {
        if (player->isProhibited(p, this)) continue;
		return TrickCard::isAvailable(player);
    }
    return false;
}

bool AOE::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return !Self->isProhibited(to_select, this, targets);
}

void AOE::onUse(Room *room, CardUseStruct &card_use) const
{
	if(card_use.to.isEmpty()) card_use.to = room->getOtherPlayers(card_use.from);
	QHash<const Skill *, QList<ServerPlayer*> > skillBans;
	foreach (ServerPlayer *p, card_use.to) {
		const Skill *skill = room->isProhibited(card_use.from, p, this);
		if (skill){
			if (skill->isVisible())
				skillBans[skill].append(p);
			else{
				skill = Sanguosha->getMainSkill(skill->objectName());
				skillBans[skill].append(p);
			}
			card_use.to.removeOne(p);
		}
	}
	foreach (const Skill *skill, skillBans.keys()) {
		LogMessage log;
		log.to = skillBans[skill];
		if (log.to.isEmpty()) continue;
		log.type = "#SkillAvoid";
		log.from = log.to.first();
		log.arg = skill->objectName();
		log.arg2 = objectName();
		room->broadcastSkillInvoke(log.arg);
		if (log.to.length()==1){
			if (log.from->hasSkill(log.arg)){
				room->sendLog(log);
				room->notifySkillInvoked(log.from, log.arg);
			}else{
				ServerPlayer *p = room->findPlayerBySkillName(log.arg);
				if (p){
					log.type = "#SkillAvoidFrom";
					log.from = p;
					room->sendLog(log);
					room->notifySkillInvoked(p, log.arg);
				}else
					room->sendLog(log);
			}
		}else{
			log.from = room->findPlayerBySkillName(log.arg);
			if (log.from){
				log.type = "#SkillAvoidFrom";
				room->sendLog(log);
				room->notifySkillInvoked(log.from, log.arg);
			}else{
				foreach (ServerPlayer *t, log.to) {
					log.from = t;
					room->sendLog(log);
				}
			}
		}
	}
    TrickCard::onUse(room, card_use);
}

QString SingleTargetTrick::getSubtype() const
{
    return "single_target_trick";
}

bool SingleTargetTrick::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return !Self->isProhibited(to_select,this,targets);
}

DelayedTrick::DelayedTrick(Suit suit, int number, bool movable)
    : TrickCard(suit, number), movable(movable)
{
    judge.negative = true;
}

bool DelayedTrick::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select->hasJudgeArea()&&!to_select->containsTrick(objectName())&&!Self->isProhibited(to_select,this,targets);
}

/*void DelayedTrick::onUse(Room *room, CardUseStruct &use) const
{
    use.card = Sanguosha->getWrappedCard(getEffectiveId());

    QVariant data = QVariant::fromValue(use);
    room->getThread()->trigger(PreCardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();

    LogMessage log;
    log.from = use.from;
    log.to = use.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    CardMoveReason reason(CardMoveReason::S_REASON_USE, use.from->objectName(), getSkillName(), "");
    if (use.to.size()==1&&use.to.first()!=use.from) reason.m_targetId = use.to.first()->objectName();
    reason.m_extraData = QVariant::fromValue(getRealCard());
    reason.m_useStruct = use;
	CardsMoveStruct move(getEffectiveId(), nullptr, Player::PlaceTable, reason);
    room->moveCardsAtomic(move, true);

    room->getThread()->trigger(CardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();
    room->getThread()->trigger(CardFinished, room, use.from, data);
}*/

void DelayedTrick::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!room->CardInTable(this)) return;
    CardUseStruct use = room->getTag("UseHistory"+toString()).value<CardUseStruct>();
	CardMoveReason reason(CardMoveReason::S_REASON_USE, source->objectName(), getSkillName(), "");
	reason.m_extraData = QVariant::fromValue(getRealCard());
	foreach (ServerPlayer *to, targets) {
		if (to->isDead()||!to->hasJudgeArea()||to->containsTrick(objectName())) continue;
		if (use.nullified_list.contains("_ALL_TARGETS")||use.nullified_list.contains(to->objectName())){
			LogMessage log;
			log.type = "#CardNullified";
			log.from = to;
			log.card_str = toString();
			room->sendLog(log);
			room->setEmotion(to, "skill_nullify");
			continue;
		}
		WrappedCard *wrapped = Sanguosha->getWrappedCard(getEffectiveId());
		if (isVirtualCard(true)){
			wrapped->takeOver(Sanguosha->cloneCard(this));
			room->broadcastUpdateCard(room->getPlayers(), wrapped->getId(), wrapped);
		}
        reason.m_targetId = to->objectName();
        reason.m_useStruct = CardUseStruct(wrapped,source,targets);
		room->moveCardTo(wrapped, to, Player::PlaceDelayedTrick, reason, true);
		return;
	}/*
	if (movable) {
		onNullified(source);
		if (!room->CardInTable(this)) return;
	}*/
	reason.m_useStruct = CardUseStruct(this,source,targets);
	room->moveCardTo(this, nullptr, Player::DiscardPile, reason, true);
}

QString DelayedTrick::getSubtype() const
{
    return "delayed_trick";
}

void DelayedTrick::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    LogMessage log;
    log.from = effect.to;
    log.type = "#DelayedTrick";
    log.arg = effect.card->objectName();
    room->sendLog(log);

    JudgeStruct judge_struct = judge;
    judge_struct.who = effect.to;
    room->judge(judge_struct);

    if (judge_struct.isBad()) {
        takeEffect(effect.to);
        if (room->getCardPlace(getEffectiveId()) == Player::PlaceTable)
            room->throwCard(this, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, effect.to->objectName()), nullptr);
    } else
        onNullified(effect.to);
}

void DelayedTrick::onNullified(ServerPlayer *source) const
{
    if(getEffectiveId()<0) return;
	Room *room = source->getRoom();
    if (movable) {
        QList<ServerPlayer *> players = room->getOtherPlayers(source);
        players << source;
        foreach (ServerPlayer *target, players) {
            if (target->containsTrick(objectName()))
                continue;

			LogMessage log;
			log.from = target;
            if (!target->hasJudgeArea()) {
                log.type = "#NoJudgeAreaAvoid";
                log.arg = objectName();
                room->sendLog(log);
                continue;
            }

            const Skill *skill = Sanguosha->isProhibited(source, target, this);
            if (skill) {
				log.arg = skill->objectName();
                if (skill->isVisible() && target->hasSkill(skill)) {
                    log.type = "#SkillAvoid";
                    log.arg2 = objectName();
                    room->sendLog(log);
                    room->broadcastSkillInvoke(log.arg);
                    room->notifySkillInvoked(target, log.arg);
                } else {
                    skill = Sanguosha->getMainSkill(log.arg);
                    if (skill&&skill->isVisible()) {
						log.arg = skill->objectName();
						log.from = room->findPlayerBySkillName(log.arg);
						if (!log.from||log.from==target) {
                            log.type = "#SkillAvoid";
                            log.arg2 = objectName();
                            room->sendLog(log);
                            room->broadcastSkillInvoke(log.arg);
                            room->notifySkillInvoked(target, log.arg);
                        } else {
                            log.type = "#SkillAvoidFrom";
                            log.to << target;
                            log.arg2 = objectName();
                            room->sendLog(log);
                            room->broadcastSkillInvoke(log.arg);
                            room->notifySkillInvoked(source, log.arg);
                        }
                    }
                }
                continue;
            }
            CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), getSkillName(), "");
			reason.m_extraData = QVariant::fromValue(getRealCard());
			reason.m_useStruct = CardUseStruct(this,nullptr,target);
            if (source != target){
				QVariant data = QVariant::fromValue(reason.m_useStruct);
				room->getThread()->trigger(TargetConfirming, room, target, data);
				reason.m_useStruct = data.value<CardUseStruct>();
				if (reason.m_useStruct.to.isEmpty())
					continue;//onNullified(target);
				foreach(ServerPlayer *p, room->getAllPlayers())
					room->getThread()->trigger(TargetConfirmed, room, p, data);
			}
			foreach (ServerPlayer *p, reason.m_useStruct.to) {
				reason.m_targetId = p->objectName();
				room->moveCardTo(this, source, p, Player::PlaceDelayedTrick, reason, true);
				return;
			}
        }
    }
	CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName());
	reason.m_extraData = QVariant::fromValue(getRealCard());
	room->throwCard(this, reason, nullptr);
}

Weapon::Weapon(Suit suit, int number, int range)
    : EquipCard(suit, number), range(range)
{
    can_recast = false;
}

bool Weapon::isAvailable(const Player *player) const
{
    return (player->getGameMode() == "04_1v3" && !player->isCardLimited(this, Card::MethodRecast))
		|| EquipCard::isAvailable(player);
}

int Weapon::getRange(const Player *) const
{
    return range;
}

void Weapon::setRange(int n)
{
    range = n;
}

QString Weapon::getSubtype() const
{
    return "weapon";
}

void Weapon::onUse(Room *room, CardUseStruct &use) const
{
    if (room->getMode()=="04_1v3"&&use.card->isKindOf("Weapon")&&(use.from->isCardLimited(use.card,Card::MethodUse)
        ||(!use.from->getHandPile().contains(getEffectiveId())&&use.from->askForSkillInvoke("weapon_recast",QVariant::fromValue(use))))) {
        room->moveCardTo(use.card, use.from, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, use.from->objectName(),"","weapon_recast"));
        use.from->broadcastSkillInvoke("@recast");

        LogMessage log;
        log.type = "#UseCard_Recast";
        log.from = use.from;
        log.card_str = use.card->toString();
        room->sendLog(log);

        use.from->drawCards(1, "weapon_recast");
        return;
    }
    EquipCard::onUse(room, use);
}

EquipCard::Location Weapon::location() const
{
    return WeaponLocation;
}

QString Weapon::getCommonEffectName() const
{
    return "weapon";
}

QString Armor::getSubtype() const
{
    return "armor";
}

EquipCard::Location Armor::location() const
{
    return ArmorLocation;
}

QString Armor::getCommonEffectName() const
{
    return "armor";
}

Horse::Horse(Suit suit, int number, int correct)
    : EquipCard(suit, number), correct(correct)
{
}

int Horse::getCorrect(const Player *) const
{
    return correct;
}

class horseSkill : public DistanceSkill
{
public:
    horseSkill(const Horse *horse)
	: DistanceSkill(horse->objectName()), horse(horse)
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        if (horse->inherits("OffensiveHorse")){
			if (from->hasOffensiveHorse(horse->objectName()))
				return horse->getCorrect(from);
		}else if(horse->inherits("DefensiveHorse")){
			if (to->hasDefensiveHorse(horse->objectName()))
				return horse->getCorrect(to);
		}
		return 0;
    }
private:
	const Horse *horse;
};

void Horse::onInstall(ServerPlayer *player) const
{
	EquipCard::onInstall(player);
}

void Horse::onUninstall(ServerPlayer *player) const
{
	EquipCard::onUninstall(player);
}

QString Horse::getSubtype() const
{
    return "horse";
}

EquipCard::Location Horse::location() const
{
    /*if (correct < 0) return OffensiveHorseLocation;
    else return DefensiveHorseLocation;*/
	return HorseLocation;
}

QString Horse::getCommonEffectName() const
{
    return "horse";
}

OffensiveHorse::OffensiveHorse(Card::Suit suit, int number, int correct)
    : Horse(suit, number, correct)
{
}

QString OffensiveHorse::getSubtype() const
{
    return "offensive_horse";
}

EquipCard::Location OffensiveHorse::location() const
{
    return OffensiveHorseLocation;
}

DefensiveHorse::DefensiveHorse(Card::Suit suit, int number, int correct)
    : Horse(suit, number, correct)
{
}

QString DefensiveHorse::getSubtype() const
{
    return "defensive_horse";
}

EquipCard::Location DefensiveHorse::location() const
{
    return DefensiveHorseLocation;
}

QString Treasure::getSubtype() const
{
    return "treasure";
}

EquipCard::Location Treasure::location() const
{
    return TreasureLocation;
}

QString Treasure::getCommonEffectName() const
{
    return "treasure";
}

class GameRuleProhibit : public ProhibitSkill
{
public:
    GameRuleProhibit() : ProhibitSkill("gameruleprohibit")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (card->isKindOf("DelayedTrick"))
            return !to->hasJudgeArea()||to->containsTrick(card->objectName());
        else if (card->isKindOf("EquipCard"))
            return !to->hasEquipArea(qobject_cast<const EquipCard*>(card->getRealCard())->location());
        return false;
    }
};

class GameRuleMaxCards : public MaxCardsSkill
{
public:
    GameRuleMaxCards() : MaxCardsSkill("gamerulemaxcards")
    {
    }

    int getExtra(const Player *target) const
    {
        return target->getMark("ExtraBfMaxCards")+target->getMark("ExtraBfMaxCards-Clear");
    }
};

class GameRuleAttackRange : public AttackRangeSkill
{
public:
    GameRuleAttackRange() : AttackRangeSkill("gameruleattackrange")
    {
    }

    int getExtra(const Player *target, bool) const
    {
        return target->getMark("ExtraBfAttackRange")+target->getMark("ExtraBfAttackRange-Clear");
    }
};

class GameRuleSlashBuff : public TargetModSkill
{
public:
    GameRuleSlashBuff() : TargetModSkill("gameruleslashbuff")
    {
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        return from->getMark("ExtraBfSlashCishu")+from->getMark("ExtraBfSlashCishu-Clear");
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        return from->getMark("ExtraBfSlashJuli")+from->getMark("ExtraBfSlashJuli-Clear");
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        return from->getMark("ExtraBfSlashMubiao")+from->getMark("ExtraBfSlashMubiao-Clear");
    }
};

class GameRuleDistanceFrom : public DistanceSkill
{
public:
    GameRuleDistanceFrom() : DistanceSkill("gameruledistancefrom")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        return from->getMark("ExtraBfDistanceFrom")+from->getMark("ExtraBfDistanceFrom-Clear");
    }
};

class GameRuleDistanceTo : public DistanceSkill
{
public:
    GameRuleDistanceTo() : DistanceSkill("gameruledistanceto")
    {
    }

    int getCorrect(const Player *, const Player *to) const
    {
        return to->getMark("ExtraBfDistanceTo")+to->getMark("ExtraBfDistanceTo-Clear");
    }
};

class GameRuleState : public TriggerSkill
{
public:
    GameRuleState() : TriggerSkill("gamerulestate")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << BeforeCardsMove;
        frequency = Compulsory;
        global = true;
    }

    int getPriority(TriggerEvent) const
    {
        return 9;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == GameStart) {
            foreach (const Skill *sk, player->getSkillList()) {
                if (sk->isChangeSkill()) {
                    int n = player->getChangeSkillState(sk->objectName());
                    room->setPlayerMark(player, QString("&%1+%2_num").arg(sk->objectName()).arg(n), 1);
                }
				QString cn = sk->property("ChargeNum").toString();
				if(cn.contains("/"))
					player->gainMark("&charge_num",cn.split("/").first().toInt());
            }
        } else if (event == EventAcquireSkill) {
            const Skill *sk = Sanguosha->getSkill(data.toString());
            if (sk && sk->isChangeSkill() && player->hasSkill(sk,true)) {
                int n = player->getChangeSkillState(sk->objectName());
                room->setPlayerMark(player, QString("&%1+%2_num").arg(sk->objectName()).arg(n), 1);
            }
        } else if (event == EventLoseSkill) {
            const Skill *sk = Sanguosha->getSkill(data.toString());
            if (sk && sk->isChangeSkill() && !player->hasSkill(sk,true)) {
                int n = player->getChangeSkillState(sk->objectName());
                room->setPlayerMark(player, QString("&%1+%2_num").arg(sk->objectName()).arg(n), 0);
            }
        }else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place == Player::PlaceEquip) {
				QList<int>ids;
				foreach (int id, move.card_ids) {
					const Card *card = Sanguosha->getCard(id);
					if(card->isKindOf("EquipCard")){
						if (move.to->hasEquipArea(((const EquipCard*)card->getRealCard())->location())) continue;
					}
					ids << id;
				}
				if (ids.isEmpty()) return false;
				move.removeCardIds(ids);
				data = QVariant::fromValue(move);
				room->throwCard(ids, move.reason.m_skillName, nullptr);
			} else if (move.to_place == Player::PlaceDelayedTrick){
				QList<int>ids;
				foreach (int id, move.card_ids) {
					if(move.to->hasJudgeArea()){
						const Card *card = Sanguosha->getCard(id);
						if(!card->isKindOf("DelayedTrick")||!move.to->containsTrick(card->objectName()))
							continue;
					}
					ids << id;
				}
				if (ids.isEmpty()) return false;
				move.removeCardIds(ids);
				data = QVariant::fromValue(move);
				room->throwCard(ids, move.reason.m_skillName, nullptr);
			}
		}
        return false;
    }
};

StandardPackage::StandardPackage()
    : Package("standard")
{
    addGenerals();

    patterns["."] = new ExpPattern(".|.|.|hand");
    patterns[".S"] = new ExpPattern(".|spade|.|hand");
    patterns[".C"] = new ExpPattern(".|club|.|hand");
    patterns[".H"] = new ExpPattern(".|heart|.|hand");
    patterns[".D"] = new ExpPattern(".|diamond|.|hand");
    patterns[".N"] = new ExpPattern(".|no_suit|.|hand");

    patterns[".black"] = new ExpPattern(".|black|.|hand");
    patterns[".red"] = new ExpPattern(".|red|.|hand");

    patterns[".."] = new ExpPattern(".");
    patterns["..S"] = new ExpPattern(".|spade");
    patterns["..C"] = new ExpPattern(".|club");
    patterns["..H"] = new ExpPattern(".|heart");
    patterns["..D"] = new ExpPattern(".|diamond");
    patterns["..N"] = new ExpPattern(".|no_suit");

    patterns[".Basic"] = new ExpPattern("BasicCard");
    patterns[".Trick"] = new ExpPattern("TrickCard");
    patterns[".Equip"] = new ExpPattern("EquipCard");

    patterns[".Weapon"] = new ExpPattern("Weapon");
    patterns["slash"] = new ExpPattern("Slash");
    patterns["jink"] = new ExpPattern("Jink");
    patterns["peach"] = new ExpPattern("Peach");
    patterns["nullification"] = new ExpPattern("Nullification");
    patterns["peach+analeptic"] = new ExpPattern("Peach,Analeptic");

    skills << new GameRuleProhibit << new GameRuleMaxCards << new GameRuleAttackRange << new GameRuleSlashBuff
           << new GameRuleDistanceFrom <<  new GameRuleDistanceTo << new GameRuleState;
}
ADD_PACKAGE(Standard)