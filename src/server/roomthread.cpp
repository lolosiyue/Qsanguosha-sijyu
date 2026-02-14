#include "roomthread.h"
#include "room.h"
#include "engine.h"
#include "gamerule.h"
//#include "scenario.h"
//#include "ai.h"
//#include "json.h"
#include "settings.h"
#include "standard.h"
#include "exppattern.h"

#ifdef QSAN_UI_LIBRARY_AVAILABLE
#pragma message WARN("UI elements detected in server side!!!")
#endif

using namespace QSanProtocol;
LogMessage::LogMessage()
	: from(nullptr)
{
}

QVariant LogMessage::toVariant() const
{
	QStringList tos,log;
	foreach(ServerPlayer*player, to)
		if (player != nullptr) tos << player->objectName();
	log << type << (from ? from->objectName() : "") << tos.join("+") << card_str << arg << arg2 << arg3 << arg4 << arg5;
	return JsonUtils::toJsonArray(log);
}

DamageStruct::DamageStruct()
	: from(nullptr), to(nullptr), card(nullptr), damage(1), nature(Normal), chain(false),
	transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const Card*card, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(card), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const QString &reason, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(nullptr), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), reason(reason), prevented(false), ignore_hujia(false)
{
}

QString DamageStruct::getReason() const
{
	if (reason.isEmpty()&&card)
		return card->objectName();
	return reason;
}

CardEffectStruct::CardEffectStruct()
	: card(nullptr), offset_card(nullptr), offset_num(1), from(nullptr), to(nullptr), multiple(false),
	nullified(false), no_respond(false), no_offset(false), extra_effect(0)
{
}

SlashEffectStruct::SlashEffectStruct()
	: jink_num(1), slash(nullptr), jink(nullptr), from(nullptr), to(nullptr), drank(0), nature(DamageStruct::Normal), multiple(false), nullified(false),
	no_respond(false), no_offset(false)
{
}

DyingStruct::DyingStruct()
	: who(nullptr), damage(nullptr)
{
}

DeathStruct::DeathStruct()
	: who(nullptr), damage(nullptr)
{
}

RecoverStruct::RecoverStruct(ServerPlayer*who, const Card*card, int recover, const QString &reason)
	: recover(recover), who(who), card(card), reason(reason)
{
}

RecoverStruct::RecoverStruct(const QString &reason, ServerPlayer*who, int recover)
	: recover(recover), who(who), card(nullptr), reason(reason)
{
}

MarkStruct::MarkStruct()
	: who(nullptr), count(1), gain(-1)
{
}

DrawStruct::DrawStruct()
	: who(nullptr), num(1), top(true), visible(false)
{
}

HpLostStruct::HpLostStruct()
	: from(nullptr), to(nullptr), lose(1), ignore_hujia(true)
{
}

HpLostStruct::HpLostStruct(ServerPlayer*to, int lose, const QString &reason, ServerPlayer*from, bool ignore_hujia)
	: from(from), to(to), reason(reason), lose(lose), ignore_hujia(ignore_hujia)
{
}

MaxHpStruct::MaxHpStruct()
	: who(nullptr), change(0)
{
}

MaxHpStruct::MaxHpStruct(ServerPlayer*who, int change, const QString &reason)
	: who(who), change(change), reason(reason)
{
}

PindianStruct::PindianStruct()
	: from(nullptr), to(nullptr), from_card(nullptr), to_card(nullptr), success(false)
{
}

bool PindianStruct::isSuccess() const
{
	return success;
}

JudgeStruct::JudgeStruct()
	: who(nullptr), card(nullptr), pattern("."), good(true), time_consuming(false),
	negative(false), play_animation(true), throw_card(true), retrial_by_response(nullptr),
	_m_result(TRIAL_RESULT_UNKNOWN)
{
}

bool JudgeStruct::isEffected() const
{
	return negative ? isBad() : isGood();
}

void JudgeStruct::updateResult()
{
	if (good == Sanguosha->matchExpPattern(pattern,who,card))
		_m_result = TRIAL_RESULT_GOOD;
	else
		_m_result = TRIAL_RESULT_BAD;
}

bool JudgeStruct::isGood() const
{
	//Q_ASSERT(_m_result != TRIAL_RESULT_UNKNOWN);
	return _m_result == TRIAL_RESULT_GOOD;
}

bool JudgeStruct::isBad() const
{
	return !isGood();
}

bool JudgeStruct::isGood(const Card*card) const
{
	//Q_ASSERT(card);
	return good == Sanguosha->matchExpPattern(pattern,who,card);
}

PhaseChangeStruct::PhaseChangeStruct()
	: from(Player::NotActive), to(Player::NotActive)
{
}

CardUseStruct::CardUseStruct()
	: card(nullptr), from(nullptr), m_isOwnerUse(true), m_addHistory(true), m_isHandcard(false), whocard(nullptr), who(nullptr), extra_use(0)
{
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, QList<ServerPlayer*> to, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), to(to), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), whocard(whocard), who(who), extra_use(0)
{
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, ServerPlayer*target, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), whocard(whocard), who(who), extra_use(0)
{
	if (target) this->to << target;
}

bool CardUseStruct::isValid(const QString &pattern) const
{
	Q_UNUSED(pattern)
	return card != nullptr;
	/*if (card == nullptr) return false;
	if (!card->getSkillName().isEmpty()) {
	bool validSkill = false;
	QString skillName = card->getSkillName();
	QSet<const Skill*> skills = from->getVisibleSkills();
	for (int i = 0; i < 4; i++) {
	const EquipCard*equip = from->getEquip(i);
	if (equip == nullptr) continue;
	const Skill*skill = Sanguosha->getSkill(equip);
	if (skill)
	skills.insert(skill);
	}
	foreach (const Skill*skill, skills) {
	if (skill->objectName() != skillName) continue;
	const ViewAsSkill*vsSkill = ViewAsSkill::parseViewAsSkill(skill);
	if (vsSkill) {
	if (!vsSkill->isAvailable(from, m_reason, pattern))
	return false;
	else {
	validSkill = true;
	break;
	}
	} else if (skill->getFrequency() == Skill::Wake) {
	bool valid = (from->getMark(skill->objectName()) > 0);
	if (!valid)
	return false;
	else
	validSkill = true;
	} else
	return false;
	}
	if (!validSkill) return false;
	}
	if (card->targetFixed())
	return true;
	else {
	QList<const Player*> targets;
	foreach (const ServerPlayer*player, to)
	targets.push_back(player);
	return card->targetsFeasible(targets, from);
	}*/
}

bool CardUseStruct::tryParse(const QVariant &usage, Room*room)
{
	JsonArray use = usage.value<JsonArray>();
	if (use.length()>1&&use[1].canConvert<JsonArray>()){
		foreach(const QVariant &target, use[1].value<JsonArray>())
			to << room->findChild<ServerPlayer*>(target.toString());
		card = Card::Parse(use[0].toString());
		return true;
	}
	return false;
}

void CardUseStruct::parse(const QString &str, Room*room)
{
	card = Card::Parse(str);
	if (str.contains("->sgs")){
		foreach(QString target_name, str.split("->").last().split("+"))
			to << room->findChild<ServerPlayer*>(target_name);
	}
}

void CardUseStruct::clientReply()
{
	if (from){
		const QVariant &client = from->getRoom()->getTag("AiResult");
		if(client.canConvert<JsonArray>()) tryParse(client, from->getRoom());
		else parse(client.toString(), from->getRoom());
	}
}

void CardUseStruct::changeCard(Card*newcard)
{
	QVariantMap tag = newcard->tag;
	tag.unite(card->tag);
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+card->getFlags());
	newcard->change_cards << card;
	card = newcard;
}

void CardResponseStruct::changeCard(Card*newcard)
{
	QVariantMap tag = newcard->tag;
	tag.unite(m_card->tag);
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+m_card->getFlags());
	newcard->change_cards << m_card;
	m_card = newcard;
}

QString EventTriplet::toString() const
{
	return QString("event[%1], room[%2], target = %3[%4]\n")
		.arg(_m_event)
		.arg(_m_room->getId())
		.arg(_m_target ? _m_target->objectName() : "nullptr")
		.arg(_m_target ? _m_target->getGeneralName() : "");
}

RoomThread::RoomThread(Room*room)
	: room(room)
{
}

void RoomThread::addPlayerSkills(ServerPlayer*player, bool invoke_game_start)
{
	bool invoke_verify = false;

	foreach (const TriggerSkill*skill, player->getTriggerSkills()) {
		addTriggerSkill(skill);

		if (invoke_game_start && skill->hasEvent(GameReady))
			invoke_verify = true;
	}

	//We should make someone trigger a whole GameReady event instead of trigger a skill only.
	if (invoke_verify)
		trigger(GameReady, room, player);
}

void RoomThread::constructTriggerTable()
{
	foreach(ServerPlayer*player, room->getPlayers())
		addPlayerSkills(player, true);
}

ServerPlayer*RoomThread::find3v3Next(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second)
{
	bool all_actioned = true;
	foreach (ServerPlayer*player, room->m_alivePlayers) {
		if (!player->hasFlag("actioned")) {
			all_actioned = false;
			break;
		}
	}

	if (all_actioned) {
		foreach (ServerPlayer*player, room->m_alivePlayers) {
			room->setPlayerFlag(player, "-actioned");
			trigger(ActionedReset, room, player);
		}

		qSwap(first, second);
		QList<ServerPlayer*> first_alive;
		foreach (ServerPlayer*p, first) {
			if (p->isAlive())
				first_alive << p;
		}
		return room->askForPlayerChosen(first.first(), first_alive, "3v3-action", "@3v3-action");
	}

	ServerPlayer*current = room->getCurrent();
	if (current != first.first()) {
		ServerPlayer*another = nullptr;
		if (current == first.last())
			another = first.at(1);
		else
			another = first.last();
		if (!another->hasFlag("actioned") && another->isAlive())
			return another;
	}

	QList<ServerPlayer*> targets;
	do {
		targets.clear();
		qSwap(first, second);
		foreach (ServerPlayer*player, first) {
			if (!player->hasFlag("actioned") && player->isAlive())
				targets << player;
		}
	} while (targets.isEmpty());

	return room->askForPlayerChosen(first.first(), targets, "3v3-action", "@3v3-action");
}

void RoomThread::run3v3(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second, GameRule*game_rule, ServerPlayer*current)
{
	try {
		forever{
			room->setCurrent(current);
			trigger(TurnStart, room, current);
			room->setPlayerFlag(current, "actioned");
			current = find3v3Next(first, second);
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBroken3v3(first, second, game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBroken3v3(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second, GameRule*game_rule)
{
	try {
		ServerPlayer*player = room->getCurrent();
		trigger(TurnBroken, room, player);
		if (player->getPhase() != Player::NotActive) {
			game_rule->trigger(EventPhaseEnd, room, player);
			player->changePhase(player->getPhase(), Player::NotActive);
		}
		if (!player->hasFlag("actioned"))
			room->setPlayerFlag(player, "actioned");

		ServerPlayer*next = find3v3Next(first, second);
		run3v3(first, second, game_rule, next);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBroken3v3(first, second, game_rule);
		else
			throw triggerEvent;
	}
}

ServerPlayer*RoomThread::findHulaoPassNext(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, int stage)
{
	ServerPlayer*current = room->getCurrent();
	if (stage == 1) {
		if (current == shenlvbu) {
			foreach (ServerPlayer*p, league) {
				if (p->isAlive() && !p->hasFlag("actioned"))
					return p;
			}
			foreach (ServerPlayer*p, league) {
				if (p->isAlive())
					return p;
			}
			Q_ASSERT(false);
			return league.first();
		} else {
			return shenlvbu;
		}
	} else {
		Q_ASSERT(stage == 2);
		return current->getNextAlive();
	}
}

void RoomThread::actionHulaoPass(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, GameRule*game_rule, int stage)
{
	try {
		if (stage == 1) {
			forever{
				ServerPlayer*current = room->getCurrent();
				trigger(TurnStart, room, current);

				ServerPlayer*next = findHulaoPassNext(shenlvbu, league, 1);
				if (current != shenlvbu) {
					if (current->isAlive() && !current->hasFlag("actioned"))
						room->setPlayerFlag(current, "actioned");
				} else {
					bool all_actioned = true;
					foreach (ServerPlayer*player, league) {
						if (player->isAlive() && !player->hasFlag("actioned")) {
							all_actioned = false;
							break;
						}
					}
					if (all_actioned) {
						foreach (ServerPlayer*player, league) {
							if (player->hasFlag("actioned"))
								room->setPlayerFlag(player, "-actioned");
						}
						foreach (ServerPlayer*player, league) {
							if (player->isDead())
								trigger(TurnStart, room, player);
						}
					}
				}

				room->setCurrent(next);
			}
		} else {
			Q_ASSERT(stage == 2);
			forever{
				ServerPlayer*current = room->getCurrent();
				trigger(TurnStart, room, current);

				ServerPlayer*next = findHulaoPassNext(shenlvbu, league, 2);

				if (current == shenlvbu) {
					foreach (ServerPlayer*player, league) {
						if (player->isDead())
							trigger(TurnStart, room, player);
					}
				}
				room->setCurrent(next);
			}
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == StageChange) {
			stage = 2;
			trigger(triggerEvent, room, nullptr);
			foreach (ServerPlayer*player, room->getPlayers()) {
				if (player != shenlvbu) {
					if (player->hasFlag("actioned"))
						room->setPlayerFlag(player, "-actioned");

					if (player->getPhase() != Player::NotActive) {
						game_rule->trigger(EventPhaseEnd, room, player);
						player->changePhase(player->getPhase(), Player::NotActive);
					}
				}
			}

			room->setCurrent(shenlvbu);
			actionHulaoPass(shenlvbu, league, game_rule, 2);
		} else if (triggerEvent == TurnBroken) {
			_handleTurnBrokenHulaoPass(shenlvbu, league, game_rule, stage);
		} else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBrokenHulaoPass(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, GameRule*game_rule, int stage)
{
	try {
		ServerPlayer*player = room->getCurrent();
		trigger(TurnBroken, room, player);
		ServerPlayer*next = findHulaoPassNext(shenlvbu, league, stage);
		if (player->getPhase() != Player::NotActive) {
			game_rule->trigger(EventPhaseEnd, room, player);
			player->changePhase(player->getPhase(), Player::NotActive);
			if (player != shenlvbu && stage == 1)
				room->setPlayerFlag(player, "actioned");
		}
		room->setCurrent(next);
		actionHulaoPass(shenlvbu, league, game_rule, stage);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenHulaoPass(shenlvbu, league, game_rule, stage);
		else
			throw triggerEvent;
	}
}

void RoomThread::actionNormal(GameRule*game_rule)
{
	try {
		forever{
			trigger(TurnStart, room, room->getCurrent());
			if (room->isFinished()) break;
			room->setCurrent(room->getCurrent()->getNextAlive());
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenNormal(game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBrokenNormal(GameRule*game_rule)
{
	try {
		ServerPlayer*player = room->getCurrent();
		trigger(TurnBroken, room, player);
		ServerPlayer*next = player->getNextAlive();
		if (player->getPhase() != Player::NotActive) {
			game_rule->trigger(EventPhaseEnd, room, player);
			player->changePhase(player->getPhase(), Player::NotActive);
		}
		room->setCurrent(next);
		actionNormal(game_rule);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenNormal(game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::run()
{
	qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));
	Sanguosha->registerRoom(room);

	foreach(const TriggerSkill*triggerSkill, Sanguosha->getGlobalTriggerSkills())
		addTriggerSkill(triggerSkill);

	static QList<const EquipCard*> equips = Sanguosha->findChildren<const EquipCard*>();
	foreach (const EquipCard*e, equips)
		addTriggerSkill(Sanguosha->getTriggerSkill(e->objectName()));

	if (Config.EnableBasara)
		addTriggerSkill(new BasaraMode(this));

	GameRule*game_rule = room->getMode() == "04_1v3" ? new HulaoPassMode(this) : new GameRule(this);
	addTriggerSkill(game_rule);

	// start game
	try {
		QList<ServerPlayer*> warm, cool, first, second;
		if (room->getMode() == "06_3v3") {
			foreach (ServerPlayer*player, room->m_players) {
				switch (player->getRoleEnum()) {
				case Player::Lord: warm.prepend(player); break;
				case Player::Loyalist: warm.append(player); break;
				case Player::Renegade: cool.prepend(player); break;
				case Player::Rebel: cool.append(player); break;
				}
			}
			if (room->askForOrder(cool.first(), "cool") == "warm") {
				second = cool;
				first = warm;
			} else {
				first = cool;
				second = warm;
			}
		}
		room->removeDerivativeCards();
		constructTriggerTable();
		trigger(GameReady, room, nullptr);
		if (room->getMode() == "06_3v3") {
			run3v3(first, second, game_rule, first.first());
		} else if (room->getMode() == "04_1v3") {
			ServerPlayer*shenlvbu = room->getLord();
			QList<ServerPlayer*> league = room->getAlivePlayers();
			league.removeOne(shenlvbu);
			room->setCurrent(league.first());
			actionHulaoPass(shenlvbu, league, game_rule, 1);
		} else {
			if (room->getMode() == "02_1v1") {
				ServerPlayer*first = room->getAlivePlayers().first();
				trigger(Debut, room, first);
				trigger(Debut, room, first->getNext());
				room->setCurrent(first);
			}
			actionNormal(game_rule);
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == GameFinished) {
			Sanguosha->unregisterRoom();
			return;
		} else if (triggerEvent == TurnBroken || triggerEvent == StageChange) { // caused in Debut trigger
			ServerPlayer*first = room->getAlivePlayers().first();
			if (first->getRole() != "renegade")
				first = room->getAlivePlayers().at(1);
			room->setCurrent(first);
			actionNormal(game_rule);
		} else
			Q_ASSERT(false);
	}
}

const QList<EventTriplet>*RoomThread::getEventStack() const
{
	return &event_stack;
}

static bool CompareByPriority(TriggerSkill*a, TriggerSkill*b)
{
	if (a->getDynamicPriority()!=b->getDynamicPriority()) return a->getDynamicPriority()>b->getDynamicPriority();
	return b->inherits("WeaponSkill")||b->inherits("ArmorSkill")||b->inherits("TreasureSkill")||b->inherits("GameRule");
}

bool RoomThread::trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target, QVariant &data)
{
	// push it to event stack
	EventTriplet triplet(triggerEvent, room, target);
	event_stack.push_back(triplet);
	bool broken = false;/*
	QList<ServerPlayer*>players = room->getAllPlayers(true);
	foreach(ServerPlayer*p,players){
		QList<TriggerSkill*>triggered;
		while(broken==false){
			if(triggerEvent==EnterDying||triggerEvent==Dying||triggerEvent==AskForPeaches){
				if(!data.value<DyingStruct>().who->hasFlag("Global_Dying"))
					break;
			}
			int x = -99;
			QHash<int,QList<TriggerSkill*> >i2ss;
			foreach(TriggerSkill*ts,skill_table[triggerEvent]){
				if(triggered.contains(ts)) continue;
				if(ts->triggerable(target,room,triggerEvent,p,data)){
					int n = ts->getPriority(triggerEvent);
					i2ss[n] << ts;
					x = qMax(x,n);
				}
			}
			if(i2ss.isEmpty()) break;
			QStringList choices;
			foreach(TriggerSkill*ts,i2ss[x]){
				if(p->hasSkill(ts->objectName(),true)){
					if(ts->isVisible()){
						choices << ts->objectName();
						continue;
					}
				}if(p!=players.first())
					continue;
				triggered << ts;
				room->tryPause();
				broken = ts->trigger(triggerEvent,room,target,data,p);
				i2ss.clear();
				break;
			}
			if(i2ss.isEmpty()) continue;
			if(choices.isEmpty()) break;
			QString choice = choices.first();
			if(choices.length()>1) choice = room->askForChoice(p,"triggered",choices.join("+"),data);
			foreach(TriggerSkill*ts,i2ss[x]){
				if(ts->objectName()==choice){
					triggered << ts;
					room->tryPause();
					broken = ts->trigger(triggerEvent,room,target,data,p);
					break;
				}
			}
		}
	}*/
	if(skill_table[triggerEvent].length()>1){
		QList<ServerPlayer*> players = room->getAllPlayers(true);
		foreach (TriggerSkill*skill, skill_table[triggerEvent]) {
			double len = players.length(), priority = skill->getPriority(triggerEvent);
			foreach (ServerPlayer*p, players) {
				if (p->hasSkill(skill->objectName())) {
					priority += len/100.0;
					break;
				}
				len--;
			}
			skill->setDynamicPriority(priority);
		}
		std::stable_sort(skill_table[triggerEvent].begin(), skill_table[triggerEvent].end(), CompareByPriority);
	}
		/*foreach(ServerPlayer*p, room->getAlivePlayers()){
			p->setProperty("handMax",p->getMaxCards());
			foreach (ServerPlayer*q, room->getPlayers()){
				if (q->isOffline()) continue;
				room->notifyProperty(q,p,"handMax");
			}
		}*/
	try {
		QList<TriggerSkill*>triggered;
		for (int i = 0; i < skill_table[triggerEvent].length(); i++) {
			TriggerSkill*ts = skill_table[triggerEvent][i];
			if (triggered.contains(ts)) continue;
			triggered << ts;
			if(triggerEvent==EnterDying||triggerEvent==Dying||triggerEvent==AskForPeaches){
				if(!data.value<DyingStruct>().who->hasFlag("Global_Dying")) break;
			}
			if (ts->triggerable(target,room,triggerEvent,target,data)) {
				if(ts->getFrequency(target)==Skill::Wake&&!ts->canWake(triggerEvent,target,data,room)) continue;
				room->tryPause();
				broken = ts->trigger(triggerEvent,room,target,data);
				if(triggerEvent!=SkillTriggered&&room->tag["notifyInvoked:"+ts->objectName()].toBool()){
					room->tag.remove("notifyInvoked:"+ts->objectName());
					QVariant skillName = ts->objectName();
					trigger(SkillTriggered,room,target,skillName);
				}
				if(broken) break;
				i = 0;
			}
		}
		if (target) target->getSmartAI()->filterEvent(triggerEvent, target, data);
		event_stack.pop_back();// pop event stack
	}catch (TriggerEvent throwed_event) {
		if (target) target->getSmartAI()->filterEvent(triggerEvent, target, data);
		event_stack.pop_back();// pop event stack
		throw throwed_event;
	}
	//room->tryPause();
	return broken;
}

bool RoomThread::trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target)
{
	QVariant data;
	//QVariant data = QVariant::fromValue(target);
	return trigger(triggerEvent, room, target, data);
}

void RoomThread::addTriggerSkill(const TriggerSkill*skill)
{
	if (!skill || skillSet.contains(skill)) return;
	skillSet << skill;
	foreach (TriggerEvent event, skill->getTriggerEvents()) {
		skill_table[event] << const_cast<TriggerSkill*>(skill);
		if(skill_table[event].length()<2) continue;
		if(skill->inherits("GameRule")||room->tag["TurnLengthCount"].toInt()>0){
			QList<ServerPlayer*> players = room->getAllPlayers(true);
			foreach (TriggerSkill*ts, skill_table[event]) {
				double len = players.length(), priority = ts->getPriority(event);
				foreach (ServerPlayer*p, players) {
					if (p->hasSkill(ts->objectName(),true)) {
						priority += len/100.0;
						break;
					}
					len--;
				}
				ts->setDynamicPriority(priority);
			}
			std::stable_sort(skill_table[event].begin(), skill_table[event].end(), CompareByPriority);
		}
	}
	if (skill->isVisible()) {
		foreach (const Skill*rs, Sanguosha->getRelatedSkills(skill->objectName()))
			addTriggerSkill(qobject_cast<const TriggerSkill*>(rs));
	}
}

void RoomThread::delay(long secs)
{
	//Q_ASSERT(secs >= 0);
	if (secs<0) secs = Config.AIDelay;
	if (Config.AIDelay>0&&room->property("to_test").isNull())
		msleep(secs);
}

