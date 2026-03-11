#include "room.h"
#include "engine.h"
#include "settings.h"
#include "standard.h"
#include "ai.h"
#include "scenario.h"
#include "gamerule.h"
#include "banpair.h"
#include "roomthread3v3.h"
#include "roomthreadxmode.h"
#include "roomthread1v1.h"
#include "server.h"
#include "generalselector.h"
//#include "structs.h"
#include "miniscenarios.h"
#include "lua.hpp"
#include "exppattern.h"
//#include "util.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "clientstruct.h"
#include <ctime>

#ifdef QSAN_UI_LIBRARY_AVAILABLE
#pragma message WARN("UI elements detected in server side!!!")
#endif

using namespace QSanProtocol;

Room::Room(QObject*parent, const QString&mode)
	: QThread(parent), mode(mode), player_count(Sanguosha->getPlayerCount(mode)), current(nullptr),
	pile1(Sanguosha->getRandomCards(true)), m_drawPile(&pile1), m_discardPile(&pile2),
	game_state(0), game_paused(false), m_lua(Sanguosha->getLuaState()),
	thread(nullptr),//game_started(false), game_finished(false),
	thread_3v3(nullptr), thread_xmode(nullptr), thread_1v1(nullptr), _m_semRaceRequest(0), _m_semRoomMutex(1),
	_m_raceStarted(false), scenario(Sanguosha->getScenario(mode)), m_surrenderRequestReceived(false), _virtual(false), _m_roomState(false)
{
	static int s_global_room_id = 0;
	_m_Id = s_global_room_id++;
	_m_lastMovementId = 0;

	initCallbacks();

	if (_m_Id<1&&!DoLuaScript(m_lua, "lua/ai/smart-ai.lua")){
		QMessageBox::warning(nullptr, "", "LuaAI加载失败，程序将无法进行AI操作");
		m_lua = nullptr;
	}

	//connect(this,SIGNAL(signalSetProperty(ServerPlayer*,const char*,QVariant)),this, SLOT(slotSetProperty(ServerPlayer*,const char*,QVariant)),Qt::QueuedConnection);
	connect(this, SIGNAL(signalSetProperty(ServerPlayer*, const char*, QVariant)), this, SLOT(slotSetProperty(ServerPlayer*, const char*, QVariant)), Qt::BlockingQueuedConnection);
}

Room::~Room()
{
	foreach(ServerPlayer*player, m_players)
		delete player;
	if (thread != nullptr)
		delete thread;
	//lua_close(m_lua);
}

void Room::initCallbacks()
{
	// init request response pair
	m_requestResponsePair[S_COMMAND_PLAY_CARD] = S_COMMAND_RESPONSE_CARD;
	m_requestResponsePair[S_COMMAND_NULLIFICATION] = S_COMMAND_RESPONSE_CARD;
	m_requestResponsePair[S_COMMAND_SHOW_CARD] = S_COMMAND_RESPONSE_CARD;
	m_requestResponsePair[S_COMMAND_ASK_PEACH] = S_COMMAND_RESPONSE_CARD;
	m_requestResponsePair[S_COMMAND_PINDIAN] = S_COMMAND_RESPONSE_CARD;
	m_requestResponsePair[S_COMMAND_EXCHANGE_CARD] = S_COMMAND_DISCARD_CARD;
	m_requestResponsePair[S_COMMAND_CHOOSE_DIRECTION] = S_COMMAND_MULTIPLE_CHOICE;
	m_requestResponsePair[S_COMMAND_LUCK_CARD] = S_COMMAND_INVOKE_SKILL;

	// client request handlers
	m_callbacks[S_COMMAND_SURRENDER] = &Room::processRequestSurrender;
	m_callbacks[S_COMMAND_CHEAT] = &Room::processRequestCheat;

	// Client notifications
	m_callbacks[S_COMMAND_TOGGLE_READY] = &Room::toggleReadyCommand;
	m_callbacks[S_COMMAND_ADD_ROBOT] = &Room::addRobotCommand;

	m_callbacks[S_COMMAND_SPEAK] = &Room::speakCommand;
	m_callbacks[S_COMMAND_TRUST] = &Room::trustCommand;
	m_callbacks[S_COMMAND_PAUSE] = &Room::pauseCommand;

	//Client request
	m_callbacks[S_COMMAND_NETWORK_DELAY_TEST] = &Room::networkDelayTestCommand;
}

ServerPlayer*Room::getCurrent() const
{
	return current;
}

void Room::setCurrent(ServerPlayer*current)
{
	this->current = current;
}

int Room::alivePlayerCount() const
{
	return m_alivePlayers.count();
}

bool Room::notifyUpdateCard(ServerPlayer*player, int cardId, const Card*newCard)
{
	JsonArray val;
	//Q_ASSERT(newCard);
	val << cardId << newCard->getSuit() << newCard->getNumber() << newCard->getClassName()
		<< newCard->getSkillName() << newCard->objectName() << JsonUtils::toJsonArray(newCard->getFlags());
	return doNotify(player, S_COMMAND_UPDATE_CARD, val);
}

bool Room::broadcastUpdateCard(const QList<ServerPlayer*>&players, int cardId, const Card*newCard)
{
	foreach(ServerPlayer*player, players)
		notifyUpdateCard(player, cardId, newCard);
	return true;
}

bool Room::notifyResetCard(ServerPlayer*player, int cardId)
{
	return doNotify(player, S_COMMAND_UPDATE_CARD, cardId);
}

bool Room::broadcastResetCard(const QList<ServerPlayer*>&players, int cardId)
{
	resetCard(cardId);
	foreach(ServerPlayer*player, players)
		notifyResetCard(player, cardId);
	return true;
}

QList<ServerPlayer*> Room::getPlayers() const
{
	return m_players;
}

QList<ServerPlayer*> Room::getAllPlayers(bool include_dead) const
{
	//QList<ServerPlayer*> count_players = m_players;
	if (!current) return m_players;

	int index = m_players.indexOf(current);
	if (index < 0) return m_players;

	QList<ServerPlayer*> all_players;

	for (int i = index; i < m_players.length(); i++){
		if (include_dead || m_players[i]->isAlive())
			all_players << m_players[i];
	}
	for (int i = 0; i < index; i++){
		if (include_dead || m_players[i]->isAlive())
			all_players << m_players[i];
	}/*

	if (current->getPhase() == Player::NotActive){
		all_players.removeOne(current);
		all_players.append(current);
	}*/

	return all_players;
}

QList<ServerPlayer*> Room::getOtherPlayers(ServerPlayer*except, bool include_dead) const
{
	QList<ServerPlayer*> other_players = getAllPlayers(include_dead);	
	if (except) other_players.removeOne(except);
	return other_players;
}

QList<ServerPlayer*> Room::getAlivePlayers() const
{
	return m_alivePlayers;
}

void Room::output(const QString&message)
{
	emit room_message(message);
}

void Room::outputEventStack()
{
	QString msg = "End of Event Stack.";
	foreach(EventTriplet triplet,*thread->getEventStack())
		msg.prepend(triplet.toString());
	msg.prepend("Event Stack:\n");
	output(msg);
}

void Room::enterDying(ServerPlayer*player, DamageStruct*reason, HpLostStruct*hplost)
{
	setPlayerFlag(player, "Global_Dying");
	QStringList currentdying = getTag("CurrentDying").toStringList();
	currentdying << player->objectName();
	setTag("CurrentDying", currentdying);

	JsonArray arg;
	arg << QSanProtocol::S_GAME_EVENT_PLAYER_DYING << player->objectName();
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

	DyingStruct dying;
	dying.who = player;
	dying.damage = reason;
	dying.hplost = hplost;
	QVariant dying_data = QVariant::fromValue(dying);

	if (!(thread->trigger(EnterDying, this, player, dying_data) || !player->hasFlag("Global_Dying"))){
		LogMessage log;
		log.type = "#enterDying";
		log.from = player;
		//sendLog(log);
		foreach(ServerPlayer*p, getAllPlayers()){
			if (thread->trigger(Dying, this, p, dying_data) || !player->hasFlag("Global_Dying"))
				break;
		}
		//thread->trigger(Dying, this, player, dying_data);
		if (player->hasFlag("Global_Dying")){
			log.type = "#AskForPeaches";
			log.to = getAllPlayers();
			log.arg = QString::number(1-player->getHp());
			sendLog(log);
			foreach(ServerPlayer*saver, log.to){
				QString cd = saver->property("currentdying").toString();
				setPlayerProperty(saver, "currentdying", player->objectName());
				thread->trigger(AskForPeaches, this, saver, dying_data);
				setPlayerProperty(saver, "currentdying", cd);
				if (!player->hasFlag("Global_Dying")) break;
			}
			notifyMoveFocus(player, S_COMMAND_ASK_PEACH);
			thread->trigger(AskForPeachesDone, this, player, dying_data);
		}
	}
	setPlayerFlag(player, "-Global_Dying");

	currentdying = getTag("CurrentDying").toStringList();
	currentdying.removeOne(player->objectName());
	setTag("CurrentDying", currentdying);

	if (player->isAlive()){
		JsonArray arg;
		arg << QSanProtocol::S_GAME_EVENT_PLAYER_QUITDYING << player->objectName();
		doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);
	}
	thread->trigger(QuitDying, this, player, dying_data);
	player->tag.remove("MyDyingSaver");
}

ServerPlayer*Room::getCurrentDyingPlayer() const
{
	QStringList currentdying = getTag("CurrentDying").toStringList();
	if (currentdying.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, m_alivePlayers){
		if (p->objectName() == currentdying.last())
			return p;
	}
	return nullptr;
}

ServerPlayer*Room::getCardUser(const Card*card) const
{
	CardUseStruct card_use = getTag("UseHistory"+card->toString()).value<CardUseStruct>();
	return card_use.from;
}

void Room::revivePlayer(ServerPlayer*player, bool sendlog, bool throw_mark, bool visible_only)
{
	if (player->isAlive()) return;

	QVariant rev = player->property("Revived_Times").toInt();
	if (thread->trigger(Revive, this, player, rev)) return;

	setEmotion(player, "revive");

	int turn = player->getMark("Global_TurnCount"), turn2 = player->getMark("Global_TurnCount2");
	if (throw_mark){
		player->throwAllMarks(visible_only);
		if(!visible_only){
			setPlayerMark(player, "Global_TurnCount", turn);
			setPlayerMark(player, "Global_TurnCount2", turn2);
		}
	}
	player->setAlive(true);
	broadcastProperty(player, "alive");

	if(current==player)
		setPlayerFlag(player,"CurrentPlayer");

	m_alivePlayers.clear();
	foreach(ServerPlayer*p, m_players){
		if (p->isAlive()) m_alivePlayers << p;
	}

	for (int i = 0; i < m_alivePlayers.length(); i++){
		m_alivePlayers[i]->setSeat(i + 1);
		broadcastProperty(m_alivePlayers[i], "seat");
	}

	doBroadcastNotify(S_COMMAND_REVIVE_PLAYER, player->objectName());
	updateStateItem();

	if (sendlog){
		LogMessage log;
		log.type = "#Revive";
		log.from = player;
		sendLog(log);
	}
	turn = rev.toInt();
	rev = turn+1;
	thread->trigger(Revived, this, player, rev);
	safeSetPlayerProperty(player, "Revived_Times", rev);
}

static bool CompareByRole(ServerPlayer*player1, ServerPlayer*player2)
{
	int role1 = player1->getRoleEnum();
	int role2 = player2->getRoleEnum();

	if (role1 != role2)
		return role1 < role2;
	return player1->isAlive() && !player2->isAlive();
}

void Room::updateStateItem()
{
	QString roles;
	QList<ServerPlayer*> players = m_players;
	std::sort(players.begin(), players.end(), CompareByRole);
	foreach(ServerPlayer*p, players){
		QChar c = "ZCFN"[p->getRoleEnum()];
		if (p->isDead()&&!p->property("RestPlayer").toBool()) c = c.toLower();
		roles.append(c);
	}

	doBroadcastNotify(S_COMMAND_UPDATE_STATE_ITEM, roles);
}

void Room::killPlayer(ServerPlayer*victim, DamageStruct*reason, HpLostStruct*hplost)
{
	victim->setAlive(false);

	int n = m_alivePlayers.indexOf(victim)+1;
	for (int i = n; i < m_alivePlayers.length(); i++){
		m_alivePlayers[i]->setSeat(m_alivePlayers[i]->getSeat() - 1);
		broadcastProperty(m_alivePlayers[i], "seat");
	}

	m_alivePlayers.removeOne(victim);

	DeathStruct death;
	death.who = victim;
	death.damage = reason;
	death.hplost = hplost;
	QVariant data = QVariant::fromValue(death);
	if(thread->trigger(BeforeGameOverJudge, this, victim, data))
		return;

	updateStateItem();

	LogMessage log;
	log.to << victim;
	log.type = "#Contingency";
	log.arg = Config.EnableHegemony ? victim->getKingdom() : victim->getRole();
	if(reason&&reason->from){
		log.from = reason->from;
		log.type = reason->from == victim ? "#Suicide" : "#Murder";
	}
	sendLog(log);

	broadcastProperty(victim, "alive");
	broadcastProperty(victim, "role");

	doBroadcastNotify(S_COMMAND_KILL_PLAYER, victim->objectName());

	thread->trigger(GameOverJudge, this, victim, data);
	if (victim->isAlive()) return;

	setEmotion(victim, "death");
	foreach(ServerPlayer*p, getAllPlayers(true)){
		if (p->isAlive() || p == victim)
			thread->trigger(Death, this, p, data);
	}
	//thread->trigger(Death, this, victim, data);
	if (victim->isAlive()) return;

	try {
		thread->trigger(BuryVictim, this, victim, data);
	} catch (TriggerEvent triggerEvent){
		if (triggerEvent == TurnBroken || triggerEvent == StageChange)
			victim->setMark("wujieNoRewardAndPunish-Keep", 0);
	}
	victim->setMark("wujieNoRewardAndPunish-Keep", 0);
	victim->detachAllSkills();

	death = data.value<DeathStruct>();
	if (death.damage){
		QString death_reason = death.damage->reason;
		if (death.damage->card){
			if (death.damage->card->isKindOf("SkillCard"))
				death_reason = death.damage->card->getSkillName();
			else
				death_reason = death.damage->card->objectName();
		}
		setPlayerProperty(victim, "My_Death_Reason", death_reason);
	}

	if (!victim->isAlive()&&Config.EnableAI){
		bool expose_roles = true;
		foreach(ServerPlayer*p, m_alivePlayers){
			if (!p->isOffline()) expose_roles = false;
			if (victim->getState() != "robot")
				notifyProperty(victim, p, "role");
		}

		if (expose_roles){
			foreach(ServerPlayer*p, m_alivePlayers){
				if (Config.EnableHegemony){
					QString role = p->getKingdom();
					if (role == "god")
						role = Sanguosha->getGeneral(p->property("basara_generals").toString().split("+").first())->getKingdom();
					role = BasaraMode::getMappedRole(role);
					broadcastProperty(p, "role", role);
				}
			}

			static QStringList continue_list;
			if (continue_list.isEmpty())
				continue_list << "02_1v1" << "04_1v3" << "06_XMode";
			if (continue_list.contains(Config.GameMode))
				return;

			if (Config.AlterAIDelayAD)
				Config.AIDelay = Config.AIDelayAD;
			if (victim->isOnline()&&Config.SurrenderAtDeath&&mode != "02_1v1"
				&&mode != "06_XMode"&&askForSkillInvoke(victim, "surrender", "yes", false))
				makeSurrender(victim);
		}
	}
}

void Room::judge(JudgeStruct&judge_struct)
{
	//Q_ASSERT(judge_struct.who != nullptr);
	QVariant data = QVariant::fromValue(&judge_struct);
	thread->trigger(StartJudge, this, judge_struct.who, data);
	foreach(ServerPlayer*player, getAllPlayers()){
		if (thread->trigger(AskForRetrial, this, player, data))
			break;
	}
	//thread->trigger(AskForRetrial, this, judge_struct.who, data);
	if(thread->trigger(FinishRetrial, this, judge_struct.who, data)){
		if(getCardPlace(judge_struct.card->getEffectiveId())==Player::PlaceJudge)
			moveCardTo(judge_struct.card,nullptr,Player::DiscardPile,CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, judge_struct.who->objectName(),"judge",""),true);
		judge_struct.card = nullptr;
		judge(judge_struct);//终止判定后的再判
	}else
		thread->trigger(FinishJudge, this, judge_struct.who, data);
}

void Room::sendJudgeResult(const JudgeStruct*judge)
{
	JsonArray arg;
	arg << QSanProtocol::S_GAME_EVENT_JUDGE_RESULT << judge->card->getEffectiveId()
		<< judge->isEffected() << judge->who->objectName() << judge->reason;
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);
}

/*static QList<int> intReverse(QList<int>&ids)
{
	QList<int> ids2 = ids;
	ids.clear();
	while(ids2.length()>0)
		ids << ids2.takeLast();
	return ids;
}*/

QList<int> Room::getNCards(int n, bool update_pile_number, bool isTop)
{
	QList<int> card_ids;
	for (int i = 0; i < n; i++)
		card_ids << drawCard(isTop);
	//if(!isTop&&n>1) intReverse(card_ids);

	if (update_pile_number)
		doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());

	return card_ids;
}

QStringList Room::aliveRoles(ServerPlayer*except) const
{
	QStringList roles;
	foreach(ServerPlayer*p, m_players){
		if (p != except&&(p->isAlive() || p->property("RestPlayer").toBool()))
			roles << p->getRole();
	}
	return roles;
}

void Room::gameOver(const QString&winner)
{
	QVariant data = winner;
	thread->trigger(GameOver, this, m_alivePlayers.first(), data);

	game_state = -1;

	emit game_over(winner);

	if (mode.contains("_mini_")){
		QStringList winners = winner.split("+");
		foreach(ServerPlayer*sp, m_players){
			if (sp->getState() != "robot"&&(winners.contains(sp->getRole())
				|| winners.contains(sp->objectName()))){
				QString id = Config.GameMode;
				id.replace("_mini_", "");
				int current = id.toInt();
				if (current < Sanguosha->getMiniSceneCounts()){
					int stage = Config.value("MiniSceneStage", 1).toInt();
					if (current + 1 > stage) Config.setValue("MiniSceneStage", current + 1);
					id = QString(MiniScene::S_KEY_MINISCENE).arg(current + 1);
					Config.setValue("GameMode", id);
					Config.GameMode = id;
				}
				break;
			}
		}
	}
	Config.AIDelay = Config.OriginAIDelay;

	QString name = getTag("NextGameMode").toString();
	if (!name.isEmpty()){
		Config.GameMode = name;
		Config.setValue("GameMode", name);
		removeTag("NextGameMode");
	}
	data = getTag("NextGameSecondGeneral");
	if (data.canConvert(QVariant::Bool)){
		Config.Enable2ndGeneral = data.toBool();
		Config.setValue("Enable2ndGeneral", data);
		removeTag("NextGameSecondGeneral");
	}

	QStringList all_roles;
	foreach(ServerPlayer*player, m_players)
		all_roles << player->getRole();
	JsonArray arg;
	arg << winner << JsonUtils::toJsonArray(all_roles);
	doBroadcastNotify(S_COMMAND_GAME_OVER, arg);
	throw GameFinished;
}

void Room::slashEffect(const SlashEffectStruct&effect)
{
	QVariant data = QVariant::fromValue(effect);
	if (thread->trigger(SlashEffected, this, effect.to, data)){
		if (effect.to->hasFlag("Global_NonSkillNullify"))
			effect.to->setFlags("-Global_NonSkillNullify");
		else
			setEmotion(effect.to, "skill_nullify");
		if (effect.slash)
			effect.to->removeQinggangTag(effect.slash);
	}
}

void Room::slashResult(const SlashEffectStruct&effect, const Card*jink)
{
	SlashEffectStruct result_effect = effect;
	result_effect.jink = jink;
	QVariant data = QVariant::fromValue(result_effect);

	if (effect.slash&&effect.from&&effect.to)
		setCardFlag(effect.slash, QString("-NonSkillNullify_%1").arg(effect.to->objectName()));

	if (jink){
		if (effect.slash){
			effect.to->removeQinggangTag(effect.slash);
			if (effect.from&&effect.to)
				setCardFlag(effect.slash, QString("NonSkillNullify_%1").arg(effect.to->objectName()));
		}
		thread->trigger(SlashMissed, this, effect.from, data);
	} else {
		if (effect.to->isAlive())
			thread->trigger(SlashHit, this, effect.from, data);
	}
}

void Room::attachSkillToPlayer(ServerPlayer*player, const QString&skill_name)
{
	player->acquireSkill(skill_name);
	doNotify(player, S_COMMAND_ATTACH_SKILL, skill_name);
	thread->addTriggerSkill(Sanguosha->getTriggerSkill(skill_name));
}

void Room::detachSkillFromPlayer(ServerPlayer*player, const QString&skill_name, bool is_equip, bool acquire_only, bool event_and_log)
{
	if (player->hasSkill(skill_name, true)){
		if (player->getAcquiredSkills().contains(skill_name)) player->detachSkill(skill_name);
		else if (!acquire_only) player->loseSkill(skill_name);
		else return;

		const Skill*skill = Sanguosha->getSkill(skill_name);
		if(skill){
			if (skill->isVisible()){
				JsonArray args;
				args << QSanProtocol::S_GAME_EVENT_DETACH_SKILL << player->objectName() << skill_name;
				doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
				if (!is_equip&&event_and_log){
					LogMessage log;
					log.type = "#LoseSkill";
					log.from = player;
					log.arg = skill_name;
					sendLog(log);
					QVariant data = skill_name;
					thread->trigger(EventLoseSkill, this, player, data);
				}
				foreach(const Skill*rs, Sanguosha->getRelatedSkills(skill_name))
					if (rs->isVisible()) detachSkillFromPlayer(player, rs->objectName());
			}
			if (skill->inherits("ViewAsEquipSkill")){
				const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill_name);
				QString view = vaes->viewAsEquip(player);
				if(view!=""){
					foreach(QString equip_name, view.split(",")){
						if (skill_name!=equip_name&&Sanguosha->getViewAsSkill(equip_name))
							detachSkillFromPlayer(player,equip_name,true);
					}
				}
			}
		}
	}
}

void Room::handleAcquireDetachSkills(ServerPlayer*player, const QStringList&skill_names, bool acquire_only, bool getmark, bool event_and_log)
{
	QStringList triggerList;
	QList<TriggerEvent> events;
	foreach(QString skill_name, skill_names){
		if (skill_name.startsWith("-")){
			skill_name = skill_name.mid(1);
			if (player->hasSkill(skill_name, true)){
				if (player->getAcquiredSkills().contains(skill_name)) player->detachSkill(skill_name);
				else if(!acquire_only) player->loseSkill(skill_name);
				else continue;
				const Skill*skill = Sanguosha->getSkill(skill_name);
				if(skill){
					if (skill->isVisible()){
						JsonArray args;
						args << QSanProtocol::S_GAME_EVENT_DETACH_SKILL << player->objectName() << skill_name;
						doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
						if (event_and_log){
							LogMessage log;
							log.type = "#LoseSkill";
							log.from = player;
							log.arg = skill_name;
							sendLog(log);
							triggerList << skill_name;
							events << EventLoseSkill;
						}
						foreach(const Skill*rs, Sanguosha->getRelatedSkills(skill_name))
							if (rs->isVisible()) detachSkillFromPlayer(player, rs->objectName());
					}
					if (skill->inherits("ViewAsEquipSkill")){
						const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill->objectName());
						QString view = vaes->viewAsEquip(player);
						if(view!=""){
							foreach(QString equip_name, view.split(",")){
								if (Sanguosha->getViewAsSkill(equip_name))
									detachSkillFromPlayer(player,equip_name,true);
							}
						}
					}
				}
			}
		} else {
			if (player->hasSkill(skill_name, true)) continue;
			player->acquireSkill(skill_name);
			const Skill*skill = Sanguosha->getSkill(skill_name);
			if(skill){
				if (skill->inherits("TriggerSkill"))
					thread->addTriggerSkill(qobject_cast<const TriggerSkill*>(skill));
				else if (skill->inherits("ViewAsEquipSkill")){
					const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill_name);
					QString view = vaes->viewAsEquip(player);
					if(view!=""){
						foreach(QString equip_name, view.split(",")){
							if (Sanguosha->getViewAsSkill(equip_name))
								attachSkillToPlayer(player,equip_name);
						}
					}
				}
				if (getmark&&!skill->getLimitMark().isEmpty())
					setPlayerMark(player, skill->getLimitMark(), 1);
				if (skill->isVisible()){
					JsonArray args;
					args << QSanProtocol::S_GAME_EVENT_ACQUIRE_SKILL << player->objectName() << skill_name;
					doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
					if (event_and_log){
						LogMessage log;
						log.type = "#AcquireSkill";
						log.from = player;
						log.arg = skill_name;
						sendLog(log);
						triggerList << skill_name;
						events << EventAcquireSkill;
					}
					foreach(const Skill*rs, Sanguosha->getRelatedSkills(skill_name))
						acquireSkill(player, rs);
				}
			}
		}
	}
	for (int i = 0; i < events.length(); i++){
		QVariant data = triggerList.at(i);
		thread->trigger(events.at(i), this, player, data);
	}
}

void Room::handleAcquireDetachSkills(ServerPlayer*player, const QString&skill_names, bool acquire_only, bool getmark, bool event_and_log)
{
	handleAcquireDetachSkills(player, skill_names.split("|"), acquire_only, getmark, event_and_log);
}

void Room::acquireOneTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names)
{
	QString st = "OneTurnSkill_" + skill_name;
	QStringList skilllist = player->tag["OneTurnSkill"].toStringList();
	QStringList list = player->tag[st].toStringList();

	if (!skilllist.contains(st)){
		skilllist << st;
		player->tag["OneTurnSkill"] = skilllist;
	}
	foreach(QString str, skill_names){
		if (list.contains(str) || player->hasSkill(str, true)) continue;
		list << str;
	}
	player->tag[st] = list;
	handleAcquireDetachSkills(player, list);
}

void Room::acquireOneTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names)
{
	acquireOneTurnSkills(player, skill_name, skill_names.split("|"));
}

void Room::acquireNextTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names)
{
	QString st = "NextTurnSkill_" + skill_name;
	QStringList skilllist = player->tag["NextTurnSkill"].toStringList();
	QStringList list = player->tag[st].toStringList();

	if (!skilllist.contains(st)){
		skilllist << st;
		player->tag["NextTurnSkill"] = skilllist;
	}
	foreach(QString str, skill_names){
		if (list.contains(str) || player->hasSkill(str, true)) continue;
		list << str;
	}
	player->tag[st] = list;
	handleAcquireDetachSkills(player, list);
}

void Room::acquireNextTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names)
{
	acquireNextTurnSkills(player, skill_name, skill_names.split("|"));
}

bool Room::doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, bool wait)
{
	return doRequest(player, command, arg, ServerInfo.getCommandTimeout(command, S_SERVER_INSTANCE), wait);
}

bool Room::doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, time_t timeOut, bool wait)
{
	ServerPlayer*onsole = player->getOnsoleOwner();
	if(onsole!=player){
		setPlayerProperty(onsole,"onsole_target",player->objectName());
		bool has = doRequest(onsole, command, arg, timeOut, wait);
		setPlayerProperty(onsole,"onsole_target","");
		player->setClientReply(onsole->getClientReply());
		return has;
	}
	Packet packet(S_SRC_ROOM | S_TYPE_REQUEST | S_DEST_CLIENT, command);
	packet.setMessageBody(arg);
	player->acquireLock(ServerPlayer::SEMA_MUTEX);
	player->m_isClientResponseReady = false;
	player->drainLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	player->setClientReply(QVariant());
	player->setClientReplyString("");
	player->m_isWaitingReply = true;
	player->m_expectedReplySerial = packet.globalSerial;
	if (m_requestResponsePair.contains(command))
		player->m_expectedReplyCommand = m_requestResponsePair[command];
	else
		player->m_expectedReplyCommand = command;

	player->invoke(&packet);
	player->releaseLock(ServerPlayer::SEMA_MUTEX);

	return !wait || getResult(player, timeOut);
}

bool Room::doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command)
{
	return doBroadcastRequest(players, command, ServerInfo.getCommandTimeout(command, S_SERVER_INSTANCE));
}

bool Room::doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut)
{
	foreach(ServerPlayer*player, players)
		doRequest(player, command, player->m_commandArgs, timeOut, false);
	QElapsedTimer timer;
	timer.start();
	foreach(ServerPlayer*player, players){
		time_t remainTime = timeOut - timer.elapsed();
		if (remainTime < 0) remainTime = 0;
		getResult(player, remainTime);
	}
	return true;
}

ServerPlayer*Room::doBroadcastRaceRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command,
	time_t timeOut, ResponseVerifyFunction validateFunc, void*funcArg)
{
	_m_semRoomMutex.acquire();
	_m_raceStarted = true;
	_m_raceWinner = nullptr;
	while (_m_semRaceRequest.tryAcquire(1)){
	} //drain lock
	_m_semRoomMutex.release();
	Countdown countdown;
	countdown.max = timeOut;
	countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
	if (command == S_COMMAND_NULLIFICATION)
		notifyMoveFocus(m_alivePlayers, command, countdown);
	else
		notifyMoveFocus(players, command, countdown);
	foreach(ServerPlayer*player, players)
		doRequest(player, command, player->m_commandArgs, timeOut, false);

	return getRaceResult(players, command, timeOut, validateFunc, funcArg);
}

ServerPlayer*Room::getRaceResult(QList<ServerPlayer*> players, QSanProtocol::CommandType, time_t timeOut,
	ResponseVerifyFunction validateFunc, void*funcArg)
{
	QElapsedTimer timer;
	timer.start();
	bool validResult = false;
	for (int i = 0; i < players.size(); i++){
		bool tryAcquireResult = true;
		if (Config.OperationNoLimit)
			_m_semRaceRequest.acquire();
		else{
			time_t timeRemain = timeOut - timer.elapsed();
			if (timeRemain < 0) timeRemain = 0;
			tryAcquireResult = _m_semRaceRequest.tryAcquire(1, timeRemain);
		}
		bool roomMutexHeld = false;
		if (!tryAcquireResult)
			roomMutexHeld = _m_semRoomMutex.tryAcquire(1);
		else
			roomMutexHeld = true; // _m_semRaceRequest.acquire() guarantees the mutex
		// So that processResponse cannot update raceWinner when we are reading it.

		if (_m_raceWinner == nullptr){
			if (roomMutexHeld) _m_semRoomMutex.release();
			continue;
		}

		if (validateFunc == nullptr || (_m_raceWinner->m_isClientResponseReady&&(this->*validateFunc)(_m_raceWinner, _m_raceWinner->getClientReply(), funcArg))){
			validResult = true;
			break;
		} else {
			// Don't give this player any more chance for this race
			_m_raceWinner->m_isWaitingReply = false;
			_m_raceWinner = nullptr;
			if (roomMutexHeld) _m_semRoomMutex.release();
		}
	}

	if (!validResult) _m_semRoomMutex.acquire();
	_m_raceStarted = false;
	foreach(ServerPlayer*player, players){
		player->acquireLock(ServerPlayer::SEMA_MUTEX);
		player->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
		player->m_isWaitingReply = false;
		player->m_expectedReplySerial = -1;
		player->releaseLock(ServerPlayer::SEMA_MUTEX);
	}
	_m_semRoomMutex.release();
	return _m_raceWinner;
}

bool Room::doNotify(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg)
{
	Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, command);
	packet.setMessageBody(arg);
	player->invoke(&packet);
	return true;
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, QSanProtocol::CommandType command, const QVariant&arg)
{
	foreach(ServerPlayer*player, players)
		doNotify(player, command, arg);
	return true;
}

bool Room::doBroadcastNotify(QSanProtocol::CommandType command, const QVariant&arg)
{
	return doBroadcastNotify(m_players, command, arg);
}

// the following functions for Lua
bool Room::doNotify(ServerPlayer*player, int command, const char*arg)
{
	Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, (QSanProtocol::CommandType)command);
	JsonDocument doc = JsonDocument::fromJson(arg);
	if (doc.isValid()){
		packet.setMessageBody(doc.toVariant());
		player->invoke(&packet);
	} else
		output(QString("Fail to parse the Json Value %1").arg(arg));
	return true;
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const char*arg)
{
	foreach(ServerPlayer*player, players)
		doNotify(player, command, arg);
	return true;
}

bool Room::doBroadcastNotify(int command, const char*arg)
{
	return doBroadcastNotify(m_players, command, arg);
}

bool Room::doNotify(ServerPlayer*player, int command, const QVariant&arg)
{
	Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, (QSanProtocol::CommandType)command);
	packet.setMessageBody(arg);
	player->invoke(&packet);
	return true;
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const QVariant&arg)
{
	foreach(ServerPlayer*player, players)
		doNotify(player, command, arg);
	return true;
}

bool Room::doBroadcastNotify(int command, const QVariant&arg)
{
	return doBroadcastNotify(m_players, command, arg);
}

// end for Lua

void Room::broadcastInvoke(const char*method, const QString&arg, ServerPlayer*except)
{
	broadcast(QString("%1 %2").arg(method).arg(arg), except);
}

void Room::broadcastInvoke(const QSanProtocol::AbstractPacket*packet, ServerPlayer*except)
{
	broadcast(packet->toString(), except);
}

bool Room::getResult(ServerPlayer*player, time_t timeOut)
{
	//Q_ASSERT(player->m_isWaitingReply);
	bool validResult = false;
	player->acquireLock(ServerPlayer::SEMA_MUTEX);

	if (player->isOnline()){
		player->releaseLock(ServerPlayer::SEMA_MUTEX);

		if (Config.OperationNoLimit){
			// Avoid blocking forever: fall back to a large but finite
			// timeout (10 min) so a silent disconnect cannot freeze the
			// room thread permanently at 0% CPU.
			const time_t kMaxWaitMs = 600000;
			player->tryAcquireLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE, kMaxWaitMs);
		} else
			player->tryAcquireLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE, timeOut);

		// Note that we rely on processResponse to filter out all unrelevant packet.
		// By the time the lock is released, m_clientResponse must be the right message
		// assuming the client side is not tampered.

		// Also note that lock can be released when a player switch to trust or offline status.
		// It is ensured by trustCommand and reportDisconnection that the player reports these status
		// is the player waiting the lock. In these cases, the serial number and command type doesn't matter.
		player->acquireLock(ServerPlayer::SEMA_MUTEX);
		validResult = player->m_isClientResponseReady;
	}
	player->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
	player->m_isWaitingReply = false;
	player->m_expectedReplySerial = -1;
	player->releaseLock(ServerPlayer::SEMA_MUTEX);
	return validResult&&!player->getClientReply().isNull();
}

bool Room::notifyMoveFocus(ServerPlayer*player)
{
	Countdown countdown;
	countdown.type = Countdown::S_COUNTDOWN_NO_LIMIT;
	return notifyMoveFocus(QList<ServerPlayer*>() << player, S_COMMAND_MOVE_FOCUS, countdown);
}

bool Room::notifyMoveFocus(ServerPlayer*player, CommandType command)
{
	Countdown countdown;
	countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
	countdown.max = ServerInfo.getCommandTimeout(command, S_CLIENT_INSTANCE);
	return notifyMoveFocus(QList<ServerPlayer*>() << player, S_COMMAND_MOVE_FOCUS, countdown);
}

bool Room::notifyMoveFocus(const QList<ServerPlayer*>&players, CommandType command, Countdown countdown)
{
	JsonArray arg, arg1;
	foreach(ServerPlayer*p, players){
		if (p->hasFlag("ignoreFocus"))
			p->setFlags("-ignoreFocus");
		else arg1 << p->objectName();
	}
	arg << QVariant(arg1) << command << countdown.toVariant();
	return doBroadcastNotify(S_COMMAND_MOVE_FOCUS, arg);
}

bool Room::askForSkillInvoke(ServerPlayer*player, const QString&skill_name, const QVariant&data, bool notify)
{
	QString skillName = skill_name;
	if(skill_name.contains("$"))
		skillName = skill_name.split("$").first();
	QVariant skill_data = skillName;
	thread->trigger(InvokeSkill, this, player, skill_data);

	tryPause();
	notifyMoveFocus(player, S_COMMAND_INVOKE_SKILL);

	bool invoked = false;
	ServerPlayer*tp = data.value<ServerPlayer*>();
	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		invoked = ai->askForSkillInvoke(skillName, data);
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	} else {
		JsonArray skillCommand;
		skillCommand << skillName;
		if (data.type() == QVariant::String)
			skillCommand << data.toString();
		else {
			if (tp) skillCommand << "playerdata:"+tp->objectName();
			else skillCommand << "";
		}
		if (doRequest(player, S_COMMAND_INVOKE_SKILL, skillCommand, true)){
			skill_data = player->getClientReply();
			if (skill_data.canConvert(QVariant::Bool))
				invoked = skill_data.toBool();
		}else{
			ai = player->getAI();
			if(ai) invoked = ai->askForSkillInvoke(skillName, data);
		}
	}

	if (invoked&&notify){
		JsonArray msg;
		msg << skillName << player->objectName();
		doBroadcastNotify(S_COMMAND_INVOKE_SKILL, msg);
		if(skill_name.contains("$"))
			broadcastSkillInvoke(skillName,skill_name.split("$").last().toInt(),player);
		notifySkillInvoked(player, skillName);
	}
	skillName = "skillInvoke:"+skillName;
	if (tp) skillName.append(":"+tp->objectName());
	skill_data = skillName + ":" + (invoked ? "yes" : "no");
	thread->trigger(ChoiceMade, this, player, skill_data);
	return invoked;
}

QString Room::askForChoice(ServerPlayer*player, const QString&skill_name, const QString&choices, const QVariant&data,
						const QString&except_choices, const QString&tip)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_MULTIPLE_CHOICE);

	QString answer = choices;
	if (choices.contains("+")){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			answer = ai->askForChoice(skill_name, choices, data);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			answer = "cancel";
			if (doRequest(player, S_COMMAND_MULTIPLE_CHOICE, JsonArray() << skill_name << choices << except_choices << tip, true)){
				QVariant clientReply = player->getClientReply();
				if (clientReply.canConvert(QVariant::String))
					answer = clientReply.toString();
			}else{
				ai = player->getAI();
				if(ai) answer = ai->askForChoice(skill_name, choices, data);
			}
		}
		if (!choices.contains(answer)){
			QStringList _choices = choices.split("+");
			answer = _choices.at(qrand() % _choices.length());
		}
	}
	QVariant decisionData = "skillChoice:" + skill_name + ":" + answer;
	thread->trigger(ChoiceMade, this, player, decisionData);
	return answer;
}

void Room::obtainCard(ServerPlayer*target, const Card*card, const CardMoveReason&reason, bool visible)
{
	moveCardTo(card, target, Player::PlaceHand, reason, visible);
}

void Room::obtainCard(ServerPlayer*target, const Card*card, bool visible)
{
	obtainCard(target, card, "",visible);
}

void Room::obtainCard(ServerPlayer*target, int card_id, bool visible)
{
	obtainCard(target, Sanguosha->getCard(card_id), visible);
}

void Room::obtainCard(ServerPlayer*target, const Card*card, const QString&skill_name, bool visible)
{
	CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK, target->objectName(),skill_name,"");
	ServerPlayer*from = getCardOwner(card->getEffectiveId());
	if(from){
		reason.m_reason = CardMoveReason::S_REASON_EXTRACTION;
		reason.m_targetId = from->objectName();
	}
	reason.m_extraData = QVariant::fromValue(card);
	obtainCard(target, card, reason, visible);
}

void Room::obtainCard(ServerPlayer*target, int card_id, const QString&skill_name, bool visible)
{
	obtainCard(target, Sanguosha->getCard(card_id), skill_name, visible);
}

bool Room::useNullified(const Card*use_card)
{
	return tag["UseHistory"+use_card->toString()].value<CardUseStruct>().nullified_list.contains("_ALL_TARGETS");
}

const Card*Room::isCanceled(const CardEffectStruct&effect)
{
	if (effect.offset_num<1||effect.no_offset||effect.no_respond) return nullptr;
	if (effect.card->isKindOf("TrickCard")&&effect.card->isCancelable(effect)){
		effect.to->tag["TrickEffectData"] = QVariant::fromValue(effect);
		return askForNullification(effect.card, effect.from, effect.to, true);
	}else if (effect.card->isKindOf("Slash")){
		tag["SlashData"] = QVariant::fromValue(effect);
		if (effect.offset_num==1){
			const Card*jink = askForUseCard(effect.to,"jink","slash-jink:"+effect.from->objectName(),-1,Card::MethodUse,true,effect.from,effect.card);
			if (jink&&!useNullified(jink))
				return jink;
		} else {
			//Card*jinks = Sanguosha->cloneCard("jink");
			//jinks->deleteLater();
			for (int i=effect.offset_num;i>0;i--){
				QString prompt = QString("@multi-jink%1:%2::%3").arg(i==effect.offset_num?"-start":"").arg(effect.from->objectName()).arg(i);
				const Card*jink = askForUseCard(effect.to,"jink",prompt,-1,Card::MethodUse,true,effect.from,effect.card);
				if (jink&&!useNullified(jink)){
					//jinks->addSubcard(jink);
					if (i==1) return jink;
				}else break;
			}
		}
	}
	return nullptr;
}

bool Room::verifyNullificationResponse(ServerPlayer*, const QVariant&response, void*)
{
	if (response.canConvert<JsonArray>()){
		JsonArray responseArray = response.value<JsonArray>();
		if (JsonUtils::isString(responseArray[0]))
			return Card::Parse(responseArray[0].toString())!=nullptr;
	}
	return false;
}

const Card*Room::askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	/*_NullificationAiHelper aiHelper;
	aiHelper.m_from = from;
	aiHelper.m_to = to;
	aiHelper.m_trick = trick;*/
	return _askForNullification(trick, from, to, positive);
}

const Card*Room::_askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	tryPause();
	CardUseStruct card_use = getTag("UseHistory"+trick->toString()).value<CardUseStruct>();
	if (card_use.no_respond_list.contains("_ALL_TARGETS")||card_use.no_offset_list.contains("_ALL_TARGETS")) return nullptr;
	
	_m_roomState.setCurrentCardUsePattern("nullification");
	_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	
	CardEffectStruct trickEffect,TrickEffect = to->tag["TrickEffectData"].value<CardEffectStruct>();
	if(TrickEffect.card==trick)
		trickEffect = TrickEffect;
	else{
		trickEffect.card = trick;
		trickEffect.from = from;
		trickEffect.to = to;
	}
	
	QList<ServerPlayer*> validPlayers,validHumanPlayers;
	QVariant data = QVariant::fromValue(trickEffect);
	foreach(ServerPlayer*player, getAllPlayers()){
		if (card_use.no_respond_list.contains(player->objectName())||card_use.no_offset_list.contains(player->objectName())) continue;
		if (player->hasNullification()){
			if(thread->trigger(TrickCardCanceling,this,player,data)) continue;
			validPlayers << player;
		}
	}
	if (validPlayers.isEmpty())
		return nullptr;
	
	JsonArray args;
	args << trick->objectName();
	args << (from?from->objectName():"");
	args << (to?to->objectName():"");
	foreach(ServerPlayer*player, validPlayers){
		if (player->isOnline()){
			validHumanPlayers << player;
			player->m_commandArgs = args;
			if (card_use.to.length()>1)
				doNotify(player, S_COMMAND_NULLIFICATION_ASKED, trick->objectName());
		}
	}
	
	CardUseStruct use(nullptr,nullptr);
	if (validHumanPlayers.length()>0){
		time_t timeOut = ServerInfo.getCommandTimeout(S_COMMAND_NULLIFICATION, S_SERVER_INSTANCE);
		use.from = doBroadcastRaceRequest(validHumanPlayers, S_COMMAND_NULLIFICATION, timeOut,&Room::verifyNullificationResponse);
		if (use.from){
			args = use.from->getClientReply().value<JsonArray>();
			if (args.size()>0&&JsonUtils::isString(args[0]))
				use.card = Card::Parse(args[0].toString());
		}
	}
	if (!use.card){
		QElapsedTimer timer;
		timer.start();
		qShuffle(validPlayers);
		foreach(ServerPlayer*player, validPlayers){
			AI*ai = player->getAI();
			if (ai){
				use.card = ai->askForNullification(TrickEffect.card, TrickEffect.from, TrickEffect.to, positive);
				if (use.card){
					use.from = player;
					if (Config.AIDelay>timer.elapsed())
						thread->delay(Config.AIDelay-timer.elapsed());
					break;
				}
			}
		}
	}
	if (!use.card) return nullptr;
	use.whocard = trick;
	use.who = from;
	trickEffect.nullified = positive;
	use.from->tag["NullifyingEffect"] = QVariant::fromValue(trickEffect);
	if (!useCard(use)) return _askForNullification(trick, from, to, positive);/*
	QString tn = use.from->objectName();
	if (to) tn = to->objectName();
	thread->delay(Config.AIDelay/2);
	data = "Nullification:"+trick->getClassName()+":"+tn+":"+(positive?"true":"false");
	thread->trigger(ChoiceMade, this, use.from, data);*/
	card_use = getTag("UseHistory"+use.card->toString()).value<CardUseStruct>();
	if(card_use.no_offset_list.contains("_HAS_EFFECT")) return use.card;
	return nullptr;/*
	if (useNullified(use.card))
		return _askForNullification(trick, from, to, positive);
	if (_askForNullification(use.card, use.from, to, !positive))
		return nullptr;
	return use.card;*/
}

int Room::askForCardChosen(ServerPlayer*player, ServerPlayer*who, const QString&flags, const QString&reason,
	bool handcard_visible, Card::HandlingMethod method, const QList<int>&disabled_ids, bool can_cancel)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_CARD);

	// process dongcha
	if (!handcard_visible&&player->canSeeHandcard(who))
		handcard_visible = true;

	if (handcard_visible&&!who->isKongcheng()){
		JsonArray arg;
		arg << who->objectName() << JsonUtils::toJsonArray(who->handCards());
		doNotify(player, S_COMMAND_SET_KNOWN_CARDS, arg);
	}
	int card_id = -1;

	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		player->tag["cardChosenForAI"] = ListI2V(disabled_ids);
		card_id = ai->askForCardChosen(who, flags, reason, method);
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	} else {
		JsonArray arg;
		arg << who->objectName() << flags << reason << handcard_visible;
		arg << (int)method << JsonUtils::toJsonArray(disabled_ids) << can_cancel;
		if (doRequest(player, S_COMMAND_CHOOSE_CARD, arg, true)){
			const QVariant&clientReply = player->getClientReply();
			if (JsonUtils::isNumber(clientReply))
				card_id = clientReply.toInt();
		}else{
			ai = player->getAI();
			if(ai){
				player->tag["cardChosenForAI"] = ListI2V(disabled_ids);
				card_id = ai->askForCardChosen(who, flags, reason, method);
			}
		}
	}
	if (card_id==-1&&!can_cancel){
		foreach(const Card*c, who->getCards(flags)){
			if (disabled_ids.contains(c->getId())) continue;
			if (method!=Card::MethodDiscard||player->canDiscard(who,c->getId())){
				card_id = c->getId();
				break;
			}
		}
	}
	//Q_ASSERT(card_id != Card::S_UNKNOWN_CARD_ID);
	QVariant madeData = QString("cardChosen:%1:%2:%3:%4").arg(reason).arg(card_id).arg(who->objectName()).arg(handcard_visible?"visible":"");
	thread->trigger(ChoiceMade, this, player, madeData);
	return card_id;
}

const Card*Room::askForCard(ServerPlayer*player, const QString&pattern, const QString&prompt,
	const QVariant&data, const QString&skill_name)
{
	return askForCard(player, pattern, prompt, data, Card::MethodDiscard, nullptr, false, skill_name, false);
}

const Card*Room::askForCard(ServerPlayer*player, const QString&pattern, const QString&prompt,
	const QVariant&data, Card::HandlingMethod method, ServerPlayer*m_who, bool isRetrial, const QString&skill_name,
	bool isProvision, const Card*m_toCard)
{
	//Q_ASSERT(pattern != "slash" || method != Card::MethodUse); // use askForUseSlashTo instead
	if (!player->isAlive()) return nullptr;
	tryPause();
	notifyMoveFocus(player, S_COMMAND_RESPONSE_CARD);
	_m_roomState.setCurrentCardUsePattern(pattern);
	CardUseStruct::CardUseReason u_reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
	if (method == Card::MethodResponse) u_reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
	else if (method == Card::MethodUse) u_reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
	_m_roomState.setCurrentCardUseReason(u_reason);
	QString _pattern = pattern;
	if ((method==Card::MethodUse||method==Card::MethodResponse)&&!isRetrial){
		QStringList asked;
		asked << pattern << prompt << (method==Card::MethodUse?"use":"response");
		if (m_toCard){
			asked << m_toCard->toString();
			CardUseStruct use = getTag("UseHistory"+asked.last()).value<CardUseStruct>();
			if (use.no_respond_list.contains("_ALL_TARGETS")||use.no_respond_list.contains(player->objectName()))
				return nullptr;
		}
		QVariant askedData = asked;
		thread->trigger(CardAsked, this, player, askedData);
		_pattern = askedData.toStringList().first();
	}
	CardResponseStruct resp(nullptr, m_who, method == Card::MethodUse);
	for (int i = 0; i < 9; i++){
		if(!player->isAlive()) return nullptr;
		if(resp.m_card) break;
		CardUseStruct use = getTag("provided").value<CardUseStruct>();
		if (use.card){
			resp.m_card = use.card;
			tag.remove("provided");
		} else {
			tag.remove("AiResult");
			AI*ai = player->getAI();
			if (ai){
				QElapsedTimer timer;
				timer.start();
				resp.m_card = ai->askForCard(_pattern, prompt, data, method);
				if (Config.AIDelay>timer.elapsed())
					thread->delay(Config.AIDelay-timer.elapsed());
			} else {
				JsonArray arg;
				arg << _pattern << prompt << int(method);
				if (doRequest(player, S_COMMAND_RESPONSE_CARD, arg, true)){
					arg = player->getClientReply().value<JsonArray>();
					if (arg.size()>0){
						tag["AiResult"] = player->getClientReply();
						resp.m_card = Card::Parse(arg[0].toString());
					}
				}else{
					ai = player->getAI();
					if (ai) resp.m_card = ai->askForCard(_pattern, prompt, data, method);
				}
			}
		}
		if(resp.m_card){
			if(resp.m_card->isKindOf("DummyCard")&&resp.m_card->subcardsLength()==1)
				resp.m_card = Sanguosha->getCard(resp.m_card->getEffectiveId());
		}else{
			QVariant askedData = QString("cardResponded:%1:%2:").arg(_pattern).arg(prompt);
			thread->trigger(ChoiceMade, this, player, askedData);
			return nullptr;
		}
		if(method == Card::MethodUse || method == Card::MethodResponse){
			resp.m_card = resp.m_card->validateInResponse(player);
			if(resp.m_card==nullptr) continue;
		}
		if(player->isCardLimited(resp.m_card, method)){
			resp.m_card = nullptr;
		}else if(isRetrial)
			return resp.m_card;
	}
	QList<int> ids;
	if (resp.m_card->isVirtualCard())
		ids = resp.m_card->getSubcards();
	else{
		ids << resp.m_card->getId();
		WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
		if (wrapped->isModified()) broadcastUpdateCard(m_players, ids.first(), wrapped);
		//else broadcastResetCard(m_players, ids.first());
	}
	QVariant askedData = QString("cardResponded:%1:%2:%3").arg(_pattern).arg(prompt).arg(resp.m_card->toString());
	thread->trigger(ChoiceMade, this, player, askedData);
	LogMessage log;
	log.from = player;
	CardMoveReason reason(CardMoveReason::S_REASON_LETUSE, player->objectName(), resp.m_card->getSkillName(), "");
	reason.m_extraData = QVariant::fromValue(resp.m_card);
	if (method == Card::MethodDiscard){
		log.type = "$DiscardCardWithSkill";
		log.card_str = ListI2S(ids).join("+");
		if (skill_name.isEmpty()){
			log.type = "$DiscardCard";
			sendLog(log);
		}else{
			log.arg = skill_name;
			if(skill_name.contains("$")){
				log.arg = skill_name.split("$").first();
				broadcastSkillInvoke(log.arg,skill_name.split("$").last().toInt(),player);
			}
			sendLog(log);
			reason.m_skillName = log.arg;
			notifySkillInvoked(player, log.arg);
		}
		reason.m_reason = CardMoveReason::S_REASON_THROW;
		moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::DiscardPile, reason), true);
	} else if (method == Card::MethodUse || method == Card::MethodResponse){
		foreach(int id, ids){
			resp.m_isHandcard = player->handCards().contains(id);
			if(!resp.m_isHandcard) break;
		}
		resp.m_toCard = m_toCard;
		askedData.setValue(resp);
		thread->trigger(PreCardResponded, this, player, askedData);
		log.type = "#UseCard";
		log.card_str = resp.m_card->toString();
		if(method == Card::MethodResponse){
			reason.m_reason = CardMoveReason::S_REASON_RESPONSE;
			log.type += "_Resp";
		}
		sendLog(log);
		moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, reason), true);
		thread->trigger(CardResponded, this, player, askedData);
		if (!isProvision){
			foreach(int id, ids){
				if (getCardPlace(id) != Player::PlaceTable)
					ids.removeOne(id);
			}
			moveCardsAtomic(CardsMoveStruct(ids, player, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
		}
		thread->trigger(PostCardResponded, this, player, askedData);
		clearCardFlag(resp.m_card);
		resp = askedData.value<CardResponseStruct>();
		if (resp.nullified) resp.m_card = nullptr;
	}
	return resp.m_card;
}

const Card*Room::askForUseCard(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index,
	Card::HandlingMethod method, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseCardStruct(player, pattern, prompt, notice_index, method, addHistory, who, whocard, flag).card;
}

CardUseStruct Room::askForUseCardStruct(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index,
	Card::HandlingMethod method, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	//Q_ASSERT(method != Card::MethodResponse);
	if (!player->isAlive()) return CardUseStruct();
	tryPause();
	notifyMoveFocus(player, S_COMMAND_RESPONSE_CARD);
	_m_roomState.setCurrentCardUsePattern(pattern);
	if(method==Card::MethodPlay){
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
		method = Card::MethodUse;
	}else
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	
	QStringList asked;
	asked << pattern << prompt << "use";
	if (whocard){
		asked << whocard->toString();
		CardUseStruct use = getTag("UseHistory"+whocard->toString()).value<CardUseStruct>();
		if (use.no_respond_list.contains("_ALL_TARGETS")||use.no_respond_list.contains(player->objectName()))
			return CardUseStruct();
	}
	QVariant asked_data = asked;
	thread->trigger(CardAsked, this, player, asked_data);
	for (int i = 0; i < 9; i++){
		if(!player->isAlive()) break;
		CardUseStruct card_use = getTag("provided").value<CardUseStruct>();
		card_use.from = player;
		card_use.whocard = whocard;
		card_use.who = who;
		if (card_use.card){
			tag.remove("provided");
		} else {
			AI*ai = player->getAI();
			if (ai){
				QElapsedTimer timer;
				timer.start();
				QString answer = ai->askForUseCard(pattern, prompt, method);
				if (answer != ".") card_use.parse(answer, this);
				if (Config.AIDelay>timer.elapsed())
					thread->delay(Config.AIDelay-timer.elapsed());
			} else {
				JsonArray ask_str;
				ask_str << pattern << prompt << int(method) << notice_index;
				if (doRequest(player, S_COMMAND_RESPONSE_CARD, ask_str, true)){
					card_use.tryParse(player->getClientReply(), this);
				}else{
					ai = player->getAI();
					if(ai){
						QString answer = ai->askForUseCard(pattern, prompt, method);
						if (answer != ".") card_use.parse(answer, this);
					}
				}
			}
		}
		if (card_use.card!=nullptr){/*&&card_use.isValid(pattern)
			asked_data.setValue(card_use);
			thread->trigger(ChoiceMade, this, player, asked_data);*/
			if (!flag.isEmpty()) setCardFlag(card_use.card, flag);
			if (useCard(card_use, addHistory)) return card_use;
		} else {
			asked_data = "cardUsed:"+pattern+":"+prompt+":";
			thread->trigger(ChoiceMade, this, player, asked_data);
			break;
		}
	}
	return CardUseStruct();
}

const Card*Room::askForUseSlashTo(ServerPlayer*slasher, QList<ServerPlayer*> victims, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseSlashToStruct(slasher, victims, prompt, distance_limit, disable_extra, addHistory, who, whocard, flag).card;
}

const Card*Room::askForUseSlashTo(ServerPlayer*slasher, ServerPlayer*victim, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseSlashTo(slasher, QList<ServerPlayer*>()<<victim, prompt, distance_limit, disable_extra, addHistory, who, whocard, flag);
}

CardUseStruct Room::askForUseSlashToStruct(ServerPlayer*slasher, QList<ServerPlayer*> victims, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	// The realization of this function in the Slash::onUse and Slash::targetFilter.
	setPlayerFlag(slasher, "slashTargetFix");
	if (!distance_limit) setPlayerFlag(slasher, "slashNoDistanceLimit");
	if (disable_extra) setPlayerFlag(slasher, "slashDisableExtraTarget");
	if (victims.length() == 1) setPlayerFlag(slasher, "slashTargetFixToOne");
	foreach(ServerPlayer*victim, victims)
		setPlayerFlag(victim, "SlashAssignee");

	CardUseStruct use = askForUseCardStruct(slasher, "slash", prompt, -1, Card::MethodUse, addHistory, who, whocard, flag);
	if (use.card==nullptr){
		setPlayerFlag(slasher, "-slashTargetFix");
		setPlayerFlag(slasher, "-slashTargetFixToOne");
		foreach(ServerPlayer*victim, victims)
			setPlayerFlag(victim, "-SlashAssignee");
		setPlayerFlag(slasher, "-slashNoDistanceLimit");
		setPlayerFlag(slasher, "-slashDisableExtraTarget");
	}
	return use;
}

CardUseStruct Room::askForUseSlashToStruct(ServerPlayer*slasher, ServerPlayer*victim, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseSlashToStruct(slasher, QList<ServerPlayer*>()<<victim, prompt, distance_limit, disable_extra, addHistory, who, whocard, flag);
}

int Room::askForAG(ServerPlayer*player, const QList<int>&card_ids, bool refusable, const QString&reason, const QString&prompt)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_AMAZING_GRACE);
	//Q_ASSERT(card_ids.length() > 0);

	int card_id = -1;
	if(refusable||card_ids.length()>1){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			card_id = ai->askForAG(card_ids, refusable, reason);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			if (doRequest(player, S_COMMAND_AMAZING_GRACE, JsonArray() << refusable << reason << prompt, true)){
				const QVariant&clientReply = player->getClientReply();
				if (JsonUtils::isNumber(clientReply))
					card_id = clientReply.toInt();
			}else{
				ai = player->getAI();
				if(ai) card_id = ai->askForAG(card_ids, refusable, reason);
			}
		}
	}
	if (!card_ids.contains(card_id)) card_id = (refusable||card_ids.isEmpty()) ? -1 : card_ids.first();
	QVariant decisionData = QString("AGChosen:%1:%2").arg(reason).arg(card_id);
	thread->trigger(ChoiceMade, this, player, decisionData);
	return card_id;
}

const Card*Room::askForCardShow(ServerPlayer*player, ServerPlayer*requestor, const QString&reason)
{
	//Q_ASSERT(!player->isKongcheng());
	tryPause();
	notifyMoveFocus(player, S_COMMAND_SHOW_CARD);
	const Card*card = nullptr;

	if (player->getHandcardNum()>1){
		AI*ai = player->getAI();
		if (ai) card = ai->askForCardShow(requestor, reason);
		else if (doRequest(player, S_COMMAND_SHOW_CARD, requestor->objectName(), true)){
			JsonArray clientReply = player->getClientReply().value<JsonArray>();
			if (clientReply.size()>0) card = Card::Parse(clientReply[0].toString());
		}else{
			ai = player->getAI();
			if(ai) card = ai->askForCardShow(requestor, reason);
		}
	}
	if (!card) card = player->getRandomHandCard();
	QVariant decisionData = "cardShow:" + reason + ":";
	if(card) decisionData = "cardShow:" + reason + ":" + card->toString();
	thread->trigger(ChoiceMade, this, player, decisionData);
	return card;
}

const Card*Room::askForSinglePeach(ServerPlayer*player, ServerPlayer*dying)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_ASK_PEACH);
	_m_roomState.setCurrentCardUsePattern(player==dying?"peach+analeptic":"peach");
	_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);

	const Card*card = nullptr;

	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		card = ai->askForSinglePeach(dying);
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	}else{
		JsonArray arg;
		arg << dying->objectName() << 1-dying->getHp();
		if (doRequest(player, S_COMMAND_ASK_PEACH, arg, true)){
			arg = player->getClientReply().value<JsonArray>();
			if (arg.size()>0&&JsonUtils::isString(arg[0]))
				card = Card::Parse(arg[0].toString());
		}else{
			ai = player->getAI();
			if(ai) card = ai->askForSinglePeach(dying);
		}
	}
	if (card){
		card = card->validateInResponse(player);
		if (!card||player->isCardLimited(card, Card::MethodUse))
			return askForSinglePeach(player, dying);
		else{
			QVariant decisionData = QString("peach:%1:%2:%3").arg(dying->objectName()).arg(1-dying->getHp()).arg(card->toString());
			thread->trigger(ChoiceMade, this, player, decisionData);
		}
	}
	return card;
}

void Room::addPlayerHistory(ServerPlayer*player, const QString&key, int times)
{
	if (player){
		if (key == ".") player->clearHistory();
		else if (times == 0) player->clearHistory(key);
		else player->addHistory(key, times);
	}

	JsonArray arg;
	arg << key << times;

	if (player) doNotify(player, S_COMMAND_ADD_HISTORY, arg);
	else doBroadcastNotify(S_COMMAND_ADD_HISTORY, arg);
}

void Room::playAudioEffect(const QString&filename, bool superpose)
{
	JsonArray arg;
	arg << filename << superpose;

	doBroadcastNotify(S_COMMAND_PLAY_AUDIO, arg);
}

void Room::setPlayerFlag(ServerPlayer*player, const QString&flag)
{
	if (flag.startsWith("-")&&!player->hasFlag(flag.mid(1)))
		return;
	player->setFlags(flag);
	broadcastProperty(player, "flags", flag);
}

void Room::_setAreaMark(ServerPlayer*player, int i, bool flag)
{
	if (flag == true){/*
		setPlayerMark(player, "@Equip5lose", 0);
		for (int m = 0; m < 5; m++){
			if (!player->hasEquipArea(m))
				setPlayerMark(player, "@Equip" + QString::number(m) +"lose", 1);
		}*/
	} else {
		if (player->getEquip(i))
			throwCard(player->getEquip(i), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
		//setPlayerMark(player, "@Equip" + QString::number(i) +"lose", 1);
	}
}

void Room::setPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value)
{
	if (!player) return; // 防禦性檢查

	int old = player->getMaxHp();
	bool same = player->property(property_name).toString() == value.toString();

	// 核心修正：統一處理跨執行緒寫入，移除 #ifdef QT_DEBUG 遮蔽
	if (QThread::currentThread() == player->thread()) {
		// 同一執行緒，直接安全寫入
		player->setProperty(property_name, value);
	}
	else {
		// 跨執行緒：發送信號交由主執行緒處理
		// 由於已改為 Qt::BlockingQueuedConnection，emit 會在此阻塞，直到 slot 執行完畢才返回
		emit signalSetProperty(player, property_name, value);
	}

	broadcastProperty(player, property_name);

	if (same) return;

	QString propertyName = QString(property_name);
	if (propertyName == "hp"){
		QVariant data = getTag("HpChangedData");
		thread->trigger(HpChanged, this, player, data);
	}else if (propertyName == "maxhp"){
		MaxHpStruct maxhp(player, player->getMaxHp() - old);
		QVariant data = QVariant::fromValue(maxhp);
		thread->trigger(MaxHpChanged, this, player, data);
	}else if (propertyName == "chained"){
		thread->trigger(ChainStateChanged, this, player);
	}else if (propertyName == "kingdom"){
		QVariant data = value;
		thread->trigger(KingdomChanged, this, player, data);
	}else if (propertyName == "weapon_area"){
		QVariantList list;
		list << 0;
		QVariant data = list;
		if (value.toBool()){
			thread->trigger(ObtainEquipArea, this, player, data);
		} else {
			if (player->getEquip(0))
				throwCard(player->getEquip(0), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowEquipArea, this, player, data);
		}
	}else if (propertyName == "armor_area"){
		QVariantList list;
		list << 1;
		QVariant data = list;
		if (value.toBool()){
			thread->trigger(ObtainEquipArea, this, player, data);
		} else {
			if (player->getEquip(1))
				throwCard(player->getEquip(1), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowEquipArea, this, player, data);
		}
	}else if (propertyName == "defensive_horse_area"){
		QVariantList list;
		list << 2;
		QVariant data = list;
		if (value.toBool()){
			thread->trigger(ObtainEquipArea, this, player, data);
		} else {
			if (player->getEquip(1))
				throwCard(player->getEquip(1), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowEquipArea, this, player, data);
		}
	}else if (propertyName == "offensive_horse_area"){
		QVariantList list;
		list << 3;
		QVariant data = list;
		if (value.toBool()){
			thread->trigger(ObtainEquipArea, this, player, data);
		} else {
			if (player->getEquip(3))
				throwCard(player->getEquip(3), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowEquipArea, this, player, data);
		}
	}else if (propertyName == "treasure_area"){
		QVariantList list;
		list << 4;
		QVariant data = list;
		if (value.toBool()){
			thread->trigger(ObtainEquipArea, this, player, data);
		} else {
			if (player->getEquip(4))
				throwCard(player->getEquip(4), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowEquipArea, this, player, data);
		}
	}else if (propertyName == "hasjudgearea"){
		if (player->hasJudgeArea()){
			thread->trigger(ObtainJudgeArea, this, player);
		} else {
			throwCard(player->getJudgingAreaID(), CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()), nullptr);
			thread->trigger(ThrowJudgeArea, this, player);
		}
	}
}

void Room::slotSetProperty(ServerPlayer*player, const char*property_name, const QVariant&value)
{
	player->setProperty(property_name, value);
	playerPropertySet = true;
}

void Room::safeSetPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value)
{
	if (!player) return;
	if (QThread::currentThread() == player->thread())
		player->setProperty(property_name, value);
	else
		emit signalSetProperty(player, property_name, value);
}

void Room::setPlayerMark(ServerPlayer*player, const QString&mark, int value, QList<ServerPlayer*> only_viewers)
{
	if (value==player->getMark(mark)) return;

	if (mark.endsWith("Clear")&&value != 0&&!current) return;

	bool trigger = game_state>0&&!(mark.endsWith("Clear")||mark.endsWith("_lun")||mark.endsWith("-Keep")||mark=="@HuJia"||mark.contains("Global_")
			||mark.contains("ExtraBf")||mark.contains("damage_point_")||(mark.startsWith("&")&&mark.endsWith("_num")));

	MarkStruct mark_struct;
	mark_struct.who = player;
	mark_struct.name = mark;
	mark_struct.count = value;
	mark_struct.gain = value - player->getMark(mark);
	QVariant data = QVariant::fromValue(mark_struct);
	if(trigger){
		if(thread->trigger(MarkChange, this, player, data))
			return;
		mark_struct = data.value<MarkStruct>();
		if (mark_struct.count == player->getMark(mark)) return;
	}
	player->setMark(mark_struct.name, mark_struct.count);

	JsonArray arg;
	arg << player->objectName() << mark_struct.name << mark_struct.count;
	if (only_viewers.isEmpty()) doBroadcastNotify(S_COMMAND_SET_MARK, arg);
	else doBroadcastNotify(only_viewers, S_COMMAND_SET_MARK, arg);

	if (trigger) thread->trigger(MarkChanged, this, player, data);
}

void Room::addPlayerMark(ServerPlayer*player, const QString&mark, int add_num, QList<ServerPlayer*> only_viewers)
{
	setPlayerMark(player, mark, player->getMark(mark)+add_num, only_viewers);
}

void Room::removePlayerMark(ServerPlayer*player, const QString&mark, int remove_num)
{
	setPlayerMark(player, mark, qMax(0, player->getMark(mark)-remove_num));
}

void Room::setPlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
	const QString&pattern, bool single_turn)
{
	player->setCardLimitation(limit_list, pattern, single_turn);

	JsonArray arg;
	arg << true << limit_list << pattern << single_turn;
	doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void Room::removePlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
	const QString&pattern)
{
	player->removeCardLimitation(limit_list, pattern);

	JsonArray arg;
	arg << false << limit_list << pattern << false;
	doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void Room::clearPlayerCardLimitation(ServerPlayer*player, bool single_turn)
{
	player->clearCardLimitation(single_turn);

	JsonArray arg;
	arg << true << QVariant() << QVariant() << single_turn;
	doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void Room::addCardMark(int card_id, const QString&mark, int add_num, ServerPlayer*who)
{
	addCardMark(Sanguosha->getCard(card_id), mark, add_num, who);
}

void Room::addCardMark(const Card*card, const QString&mark, int add_num, ServerPlayer*who)
{
	if (!card) return;
	setCardMark(card, mark, card->getMark(mark)+add_num, who);
}

void Room::removeCardMark(int card_id, const QString&mark, int remove_num)
{
	removeCardMark(Sanguosha->getCard(card_id), mark, remove_num);
}

void Room::removeCardMark(const Card*card, const QString&mark, int remove_num)
{
	if (!card) return;
	setCardMark(card, mark, qMax(0, card->getMark(mark)-remove_num));
}

void Room::setCardMark(const Card*card, const QString&mark, int value, ServerPlayer*who)
{
	card->setMark(mark, value);
	if (card->isVirtualCard()) return;
	setCardMark(card->getId(), mark, value, who);
}

void Room::setCardMark(int card_id, const QString&mark, int value, ServerPlayer*who)
{
	Sanguosha->getCard(card_id)->setMark(mark, value);
	JsonArray arg;
	arg << card_id << mark << value;
	if (who) doNotify(who, S_COMMAND_CARD_MARK, arg);
	else doBroadcastNotify(S_COMMAND_CARD_MARK, arg);
}

void Room::setCardFlag(const Card*card, const QString&flag, ServerPlayer*who)
{
	card->setFlags(flag);
	if (card->isVirtualCard()) return;
	setCardFlag(card->getId(), flag, who);
}

void Room::setCardFlag(int card_id, const QString&flag, ServerPlayer*who)
{
    Sanguosha->getCard(card_id)->setFlags(flag);
	JsonArray arg;
	arg << card_id << flag;
	if (who) doNotify(who, S_COMMAND_CARD_FLAG, arg);
	else doBroadcastNotify(S_COMMAND_CARD_FLAG, arg);
}

void Room::clearCardFlag(const Card*card, ServerPlayer*who)
{
	setCardFlag(card,".",who);
}

void Room::clearCardFlag(int card_id, ServerPlayer*who)
{
	clearCardFlag(Sanguosha->getCard(card_id),who);
}

void Room::setCardTip(int card_id, const QString&tip)
{
	if (tip.isEmpty()) return;
	if (tip.startsWith("-")) setCardFlag(card_id, "-cardTip:"+tip.mid(1));
	else setCardFlag(card_id, "cardTip:"+tip);
}

void Room::clearCardTip(int card_id)
{
	Card*card = Sanguosha->getCard(card_id);
	if (!card) return;
	foreach(const QString&flag, card->getFlags()){
		if (flag.startsWith("cardTip:"))
			setCardFlag(card_id, "-"+flag);
	}
}

ServerPlayer*Room::addSocket(ClientSocket*socket)
{
	ServerPlayer*player = new ServerPlayer(this);
	player->setSocket(socket);
	m_players << player;

	connect(player, SIGNAL(disconnected()), this, SLOT(reportDisconnection()));
	connect(player, SIGNAL(request_got(QString)), this, SLOT(processClientPacket(QString)));

	return player;
}

bool Room::isFull() const
{
	return m_players.length() >= player_count;
}

bool Room::isFinished() const
{
	return game_state<0;
}

bool Room::canPause(ServerPlayer*player) const
{
	if (player->isOwner()&&isFull()){
		foreach(ServerPlayer*p, m_alivePlayers){
			if (p==player||p->getState()=="robot") continue;
			return false;
		}
		return true;
	}
	return false;
}

void Room::tryPause()
{
	//tag["callback"] = true;
	if (canPause(getOwner())){
		QMutexLocker locker(&m_mutex);
		// Use a timed wait instead of unconditional wait: if the owner
		// disconnects while game_paused == true no one will ever call
		// wakeAll(), which would leave the room thread frozen at 0% CPU.
		const unsigned long kPauseCheckIntervalMs = 500;
		while (game_paused){
			bool woken = m_waitCond.wait(locker.mutex(), kPauseCheckIntervalMs);
			// If we timed out (not woken by wakeAll), re-evaluate canPause:
			// if the owner has gone offline, canPause() returns false and we
			// break out of the deadlock automatically.
			if (!woken && !canPause(getOwner())){
				game_paused = false;
				break;
			}
		}
	}
}

int Room::getLack() const
{
	return player_count - m_players.length();
}

QString Room::getMode() const
{
	return mode;
}

const Scenario*Room::getScenario() const
{
	return scenario;
}

void Room::broadcast(const QString&message, ServerPlayer*except)
{
	foreach(ServerPlayer*player, m_players){
		if (player != except)
			player->unicast(message);
	}
}

void Room::swapPile()
{
	if (m_discardPile->isEmpty())// the standoff
		gameOver(".");

	int times = tag.value("SwapPile", 0).toInt() + 1;
	tag.insert("SwapPile", times);

	QVariant data = times;
	foreach(ServerPlayer*p, getAllPlayers())
		thread->trigger(SwapPile, this, p, data);
	//thread->trigger(SwapPile, this, current, data);

	int limit = Config.value("PileSwappingLimitation", 5).toInt() + 1;
	if (mode == "04_1v3")
		limit = qMin(limit, Config.BanPackages.contains("maneuvering") ? 3 : 2);
	else if (mode == "08_defense")
		limit = qMin(limit, Config.BanPackages.contains("maneuvering") ? 9 : 6);
	if (limit > 0&&times >= limit)
		gameOver(".");

	qSwap(m_drawPile, m_discardPile);

	doBroadcastNotify(S_COMMAND_RESET_PILE, data);
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());

	qShuffle(*m_drawPile);
	foreach(int card_id,*m_drawPile){
		setCardMapping(card_id, nullptr, Player::DrawPile);
		clearCardFlag(card_id);
	}

	foreach(ServerPlayer*p, getAllPlayers())
		thread->trigger(SwappedPile, this, p, data);
	//thread->trigger(SwappedPile, this, current, data);
}

ServerPlayer*Room::findPlayer(const QString&general_name, bool include_dead) const
{
	if (general_name.contains("+")){
		QStringList names = general_name.split("+");
		foreach(ServerPlayer*player, include_dead ? m_players : m_alivePlayers){
			if (names.contains(player->getGeneralName()))
				return player;
		}
	}else{
		foreach(ServerPlayer*player, include_dead ? m_players : m_alivePlayers){
			if (player->getGeneralName() == general_name)
				return player;
		}
	}
	return nullptr;
}

QList<ServerPlayer*>Room::findPlayersBySkillName(const QString&skill_name) const
{
	QList<ServerPlayer*> list;
	foreach(ServerPlayer*player, getAllPlayers()){
		if (player->hasSkill(skill_name))
			list << player;
	}
	return list;
}

ServerPlayer*Room::findPlayerBySkillName(const QString&skill_name, bool include_lose) const
{
	foreach(ServerPlayer*player, getAllPlayers()){
		if (player->hasSkill(skill_name, include_lose))
			return player;
	}
	return nullptr;
}

ServerPlayer*Room::findPlayerByObjectName(const QString&objectName, bool include_dead) const
{
	foreach(ServerPlayer*p, getAllPlayers(include_dead)){
		if (p->objectName() == objectName)
			return p;
	}
	return nullptr;
}

void Room::installEquip(ServerPlayer*player, const QString&equip_name)
{
	int card_id = getCardFromPile(equip_name);
	if (card_id>-1){
		CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, player->objectName());
		QList<CardsMoveStruct> moves;
		const Card*card = Sanguosha->getCard(card_id);
		const EquipCard*equip = (const EquipCard*)card->getRealCard();
		if(!player->hasEquipArea(equip->location())) return;
		card = player->getEquip(equip->location());
		if(card){
			reason.m_reason = CardMoveReason::S_REASON_PUT;
			moves << CardsMoveStruct(card->getEffectiveId(), nullptr, Player::DiscardPile, reason);
		}
		moves << CardsMoveStruct(card_id, player, Player::PlaceEquip, reason);
		moveCardsAtomic(moves, true);
	}
}

void Room::resetAI(ServerPlayer*player)
{
	AI*smart_ai = player->getSmartAI();
	int index = -1;
	if (smart_ai){
		index = ais.indexOf(smart_ai);
		ais.removeOne(smart_ai);
		//delete smart_ai;  changeHero后返回主菜单会闪退
		smart_ai->deleteLater();
	}
	AI*new_ai = cloneAI(player);
	player->setAI(new_ai);
	if (index<0) ais.append(new_ai);
	else ais.insert(index, new_ai);
}

void Room::changeHero(ServerPlayer*player, const QString&new_general, bool full_state, bool invokeStart,
	bool isSecondaryHero, bool sendLog, int start_hp)
{
	JsonArray arg;
	arg << (int)S_GAME_EVENT_CHANGE_HERO << player->objectName();
	arg << new_general << isSecondaryHero << sendLog;
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

	QString old_kingdom = player->getKingdom();
	if (isSecondaryHero) changePlayerGeneral2(player, new_general);
	else changePlayerGeneral(player, new_general);

	int maxhp = player->getGeneralMaxHp();
	int max_hp = player->property("ChangeHeroMaxHp").toInt();
	if (max_hp>0){
		setPlayerProperty(player, "ChangeHeroMaxHp", 0);
		maxhp = max_hp-1;
	}

	if (full_state)
		start_hp = player->getGeneralStartHp();

	player->setMaxHp(maxhp);

	if (start_hp > 0)
		player->setHp(qMin(start_hp, maxhp));

	broadcastProperty(player, "maxhp");
	broadcastProperty(player, "hp");

	const General*gen = isSecondaryHero ? player->getGeneral2() : player->getGeneral();

	QString kingdom = player->property("yinni_general_kingdom").toString();
	if(kingdom.isEmpty()){
		kingdom = old_kingdom;
		if(gen&&!isSecondaryHero){
			kingdom = gen->getKingdom();
			if(gen->getKingdoms().contains("+"))
				kingdom = askForKingdom(player, new_general+"_ChooseKingdom");
			else if(kingdom=="demon")
				kingdom = askForKingdom(player,"gamerule_demon");
			else if(kingdom=="god"&&!scenario&&new_general!="anjiang"&&!new_general.startsWith("boss_"))
				kingdom = askForKingdom(player,"gamerule_god");
		}
	}else
		setPlayerProperty(player, "yinni_general_kingdom", "");
	setPlayerProperty(player, "kingdom", kingdom);

	if (gen){
		foreach(const Skill*skill, gen->getSkillList()){
			kingdom = skill->getLimitMark();
			if (kingdom!=""&&!player->tag["DontGiveLimitMark_"+skill->objectName()].toBool()){
				player->tag["DontGiveLimitMark_"+skill->objectName()] = true;
				setPlayerMark(player, kingdom, 1);
			}
			if (skill->inherits("ViewAsEquipSkill")){
				const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill->objectName());
				QString view = vaes->viewAsEquip(player);
				if(view!=""){
					foreach(QString equip_name, view.split(",")){
						if (Sanguosha->getViewAsSkill(equip_name))
							attachSkillToPlayer(player,equip_name);
					}
				}
			}else if (skill->inherits("TriggerSkill")){
				const TriggerSkill*ts = qobject_cast<const TriggerSkill*>(skill);
				thread->addTriggerSkill(ts);
				if (invokeStart&&ts->hasEvent(GameStart)&&ts->triggerable(player, this, GameStart)){
					QVariant data;
					ts->trigger(GameStart, this, player, data);
				}
			}
			/*data = skill->objectName();
			thread->trigger(EventAcquireSkill, this, player, data);*/
		}
	}
	resetAI(player);
}

lua_State*Room::getLuaState() const
{
	return m_lua;
}

void Room::setFixedDistance(Player*from, const Player*to, int distance)
{
	from->setFixedDistance(to, distance);

	JsonArray arg;
	arg << from->objectName() << to->objectName() << distance << true;
	doBroadcastNotify(S_COMMAND_FIXED_DISTANCE, arg);
}

void Room::removeFixedDistance(Player*from, const Player*to, int distance)
{
	from->removeFixedDistance(to, distance);

	JsonArray arg;
	arg << from->objectName() << to->objectName() << distance << false;
	doBroadcastNotify(S_COMMAND_FIXED_DISTANCE, arg);
}

void Room::insertAttackRangePair(Player*from, const Player*to)
{
	from->insertAttackRangePair(to);

	JsonArray arg;
	arg << from->objectName() << to->objectName() << true;
	doBroadcastNotify(S_COMMAND_ATTACK_RANGE, arg);
}

void Room::removeAttackRangePair(Player*from, const Player*to)
{
	from->removeAttackRangePair(to);

	JsonArray arg;
	arg << from->objectName() << to->objectName() << false;
	doBroadcastNotify(S_COMMAND_ATTACK_RANGE, arg);
}

void Room::reverseFor3v3(const Card*card, ServerPlayer*player, QList<ServerPlayer*>&list)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_DIRECTION);

	QString isClockwise = "ccw";
	if (player->isOnline()){
		if (doRequest(player, S_COMMAND_CHOOSE_DIRECTION, QVariant(), true)){
			QVariant clientReply = player->getClientReply();
			if (JsonUtils::isString(clientReply))
				isClockwise = clientReply.toString();
		}
	} else
		isClockwise = askForChoice(player, "3v3_direction", "cw+ccw", QVariant::fromValue(card));

	LogMessage log;
	log.type = "#TrickDirection";
	log.from = player;
	log.arg = isClockwise;
	log.arg2 = card->objectName();
	sendLog(log);

	if (isClockwise == "cw"){
		QList<ServerPlayer*> new_list;

		while (list.length()>0)
			new_list << list.takeLast();

		if (new_list.contains(current)){
			new_list.removeLast();
			new_list.prepend(current);
		}
		list = new_list;
	}
}

const ProhibitSkill*Room::isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*>&others) const
{
	return Sanguosha->isProhibited(from, to, card, others);
}

const ProhibitPindianSkill*Room::isPindianProhibited(const Player*from, const Player*to) const
{
	return Sanguosha->isPindianProhibited(from, to);
}

int Room::drawCard(bool isTop)
{
	thread->trigger(FetchDrawPileCard, this, nullptr);
	if (m_drawPile->isEmpty()) swapPile();
	if (isTop) return m_drawPile->takeFirst();
	else return m_drawPile->takeLast();
}

void Room::prepareForStart()
{
	if (scenario){
		bool already = false;
		if (scenario->objectName() == "challengedeveloper"){
			Config.EnableCheat = false;
			Config.setValue("EnableCheat", false);

			QList<ServerPlayer*> humans;
			foreach(ServerPlayer*p, m_players){
				if (p->getState() != "robot")
					humans << p;
			}
			if (!humans.isEmpty()){
				already = true;
				ServerPlayer*human = humans.at(qrand() % humans.length());
				human->setGeneralName("sujiang");
				broadcastProperty(human, "general");
				human->setRole("lord");
				broadcastProperty(human, "role");

				foreach(ServerPlayer*p, m_players){
					if (p == human) continue;
					p->setGeneralName("sujiang");
					broadcastProperty(p, "general");
					p->setRole("rebel");
					broadcastProperty(p, "role");
				}
			}
		}
		if (!already){
			QStringList generals, roles;
			scenario->assign(generals, roles);
			for (int i = 0; i < m_players.length(); i++){
				ServerPlayer*player = m_players[i];
				if (generals.length() > i){
					player->setGeneralName(generals[i]);
					broadcastProperty(player, "general");
				}
				player->setRole(roles[i]);
				if (scenario->exposeRoles()||roles[i]=="lord")
					broadcastProperty(player, "role");
				else
					notifyProperty(player, player, "role");
			}
		}
		updateStateItem();
	} else if (mode == "06_3v3" || mode == "06_XMode" || mode == "02_1v1"){
		return;
	} else {
		if (Config.RandomSeat||mode=="08_defense")
			qShuffle(m_players);
		if (mode!="04_2v2"&&!Config.EnableHegemony&&Config.value("FreeAssign").toBool()){
			ServerPlayer*owner = getOwner();
			if (owner&&owner->isOnline()){
				notifyMoveFocus(owner, S_COMMAND_CHOOSE_ROLE);
				if(doRequest(owner, S_COMMAND_CHOOSE_ROLE, QVariant(), true)){
					QVariant clientReply = owner->getClientReply();
					if(clientReply.canConvert<JsonArray>()){
						JsonArray replyArray = clientReply.value<JsonArray>();/*
						if(Config.FreeAssignSelf){
							QString name = replyArray.value(0).value<JsonArray>().value(0).toString();
							QString role = replyArray.value(1).value<JsonArray>().value(0).toString();
							owner = findChild<ServerPlayer*>(name);
							owner->setRole(role);
							QList<ServerPlayer*> all_players = m_players;
							all_players.removeOne(owner);
							QStringList roles = Sanguosha->getRoleList(mode);
							roles.removeOne(role);
							qShuffle(roles);
							for (int i = 0; i < all_players.count(); i++){
								all_players[i]->setRole(roles[i]);
								if (mode.contains("_")||roles[i] == "lord")
									broadcastProperty(all_players[i], "role", roles[i]);
								else
									notifyProperty(all_players[i], all_players[i], "role");
							}
						}else{*/
							QList<ServerPlayer*> all_players = m_players;
							QStringList roles = Sanguosha->getRoleList(mode);
							for (int i = 0; i < replyArray.value(0).value<JsonArray>().size(); i++){
								QString name = replyArray.value(0).value<JsonArray>().value(i).toString();
								QString role = replyArray.value(1).value<JsonArray>().value(i).toString();
								owner = findChild<ServerPlayer*>(name);
								m_players.swapItemsAt(i, m_players.indexOf(owner));
								all_players.removeOne(owner);
								roles.removeOne(role);
								owner->setRole(role);
							}
							qShuffle(roles);
							for (int i = 0; i < all_players.count(); i++)
								all_players[i]->setRole(roles[i]);
							for (int i = 0; i < m_players.count(); i++){
								if (mode.contains("_")||m_players[i]->getRole() == "lord")
									broadcastProperty(m_players[i], "role");
								else
									notifyProperty(m_players[i], m_players[i], "role");
							}
						//}
						adjustSeats();
						return;
					}
				}
			}
		}
		if (mode == "04_1v3" || mode == "04_boss"){
			ServerPlayer*lord = m_players[qrand()%4];
			for (int i = 0; i < 4; i++){
				if (m_players[i] == lord) m_players[i]->setRole("lord");
				else m_players[i]->setRole("rebel");
				broadcastProperty(m_players[i], "role");
			}
			adjustSeats();
			return;
		}
		assignRoles();
	}
	adjustSeats();
}

void Room::reportDisconnection()
{
	ServerPlayer*player = qobject_cast<ServerPlayer*>(sender());
	if (player == nullptr) return;

	// send disconnection message to server log
	emit room_message(player->reportHeader() + tr("disconnected"));

	// the 4 kinds of circumstances
	// 1. Just connected, with no object name : just remove it from player list
	// 2. Connected, with an object name : remove it, tell other clients and decrease signup_count
	// 3. Game is not started, but role is assigned, give it the default general(general2) and others same with fourth case
	// 4. Game is started, do not remove it just set its state as offline
	// all above should set its socket to nullptr

	player->setSocket(nullptr);

	if (player->objectName().isEmpty()){
		// first case
		player->setParent(nullptr);
		m_players.removeOne(player);
	} else if (player->getRole().isEmpty()){
		// second case
		if (m_players.length() < player_count){
			player->setParent(nullptr);
			m_players.removeOne(player);

			if (player->getState() != "robot"){
				QString leaveStr = "<font color=#000000>已离开游戏</font>";//tr("<font color=#000000>Player <b>%1</b> left the game</font>").arg(player->screenName());
				speakCommand(player, leaveStr.toUtf8().toBase64());
			}

			doBroadcastNotify(S_COMMAND_REMOVE_PLAYER, player->objectName());
		}
	} else {
		// fourth case
		if (player->m_isWaitingReply)
			player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
		setPlayerProperty(player, "state", "offline");

		bool someone_is_online = false;
		foreach(ServerPlayer*p, m_players){
			if (p->isOffline()) continue;
			someone_is_online = true;
			break;
		}

		if (!someone_is_online){
			game_state = -1;
			emit game_over("");
			return;
		}
	}

	if (player->isOwner()){
		player->setOwner(false);
		broadcastProperty(player, "owner");
		foreach(ServerPlayer*p, m_players){
			if (p->isOffline()) continue;
			p->setOwner(true);
			broadcastProperty(p, "owner");
			break;
		}
	}
}

void Room::trustCommand(ServerPlayer*player, const QVariant&)
{
	player->acquireLock(ServerPlayer::SEMA_MUTEX);
	if (player->isOnline()){
		player->setState("trust");
		if (player->m_isWaitingReply){
			player->releaseLock(ServerPlayer::SEMA_MUTEX);
			player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
		}
	} else
		player->setState("online");

	player->releaseLock(ServerPlayer::SEMA_MUTEX);
	broadcastProperty(player, "state");
	return;
}

void Room::pauseCommand(ServerPlayer*player, const QVariant&arg)
{
	if (canPause(player)){
		QMutexLocker locker(&m_mutex);
		if (game_paused != arg.toBool()){
			game_paused = arg.toBool();
			JsonArray json;
			json << S_GAME_EVENT_PAUSE << arg;
			doNotify(player, S_COMMAND_LOG_EVENT, json);
			if (!game_paused) m_waitCond.wakeAll();
		}
	}
}

void Room::processRequestCheat(ServerPlayer*player, const QVariant&arg)
{
	player->m_cheatArgs = QVariant();
	if (!Config.EnableCheat) return;
	if (!arg.canConvert<JsonArray>() || !arg.value<JsonArray>().value(0).canConvert(QVariant::Int))
		return;
	//@todo: synchronize this
	player->m_cheatArgs = arg;
	player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	return;
}

bool Room::makeSurrender(ServerPlayer*initiator)
{
	bool loyalGiveup = true;
	int loyalAlive = 0;
	bool renegadeGiveup = true;
	int renegadeAlive = 0;
	bool rebelGiveup = true;
	int rebelAlive = 0;

	// broadcast polling request
	QList<ServerPlayer*> playersAlive;
	foreach(ServerPlayer*player, m_players){
		QString playerRole = player->getRole();
		if ((playerRole == "loyalist" || playerRole == "lord")&&player->isAlive()) loyalAlive++;
		else if (playerRole == "rebel"&&player->isAlive()) rebelAlive++;
		else if (playerRole == "renegade"&&player->isAlive()) renegadeAlive++;

		if (player != initiator&&player->isAlive()&&player->isOnline()){
			player->m_commandArgs = initiator->getGeneral()->objectName();
			playersAlive << player;
		}
	}
	doBroadcastRequest(playersAlive, S_COMMAND_SURRENDER);

	// collect polls
	foreach(ServerPlayer*player, playersAlive){
		bool result = false;
		if (!player->m_isClientResponseReady || !player->getClientReply().canConvert(QVariant::Bool))
			result = !player->isOnline();
		else
			result = player->getClientReply().toBool();

		QString playerRole = player->getRole();
		if (playerRole == "loyalist" || playerRole == "lord"){
			loyalGiveup&= result;
			if (player->isAlive()) loyalAlive++;
		} else if (playerRole == "rebel"){
			rebelGiveup&= result;
			if (player->isAlive()) rebelAlive++;
		} else if (playerRole == "renegade"){
			renegadeGiveup&= result;
			if (player->isAlive()) renegadeAlive++;
		}
	}

	// vote counting
	if (loyalGiveup&&renegadeGiveup&&!rebelGiveup)
		gameOver("rebel");
	else if (loyalGiveup&&!renegadeGiveup&&rebelGiveup)
		gameOver("renegade");
	else if (!loyalGiveup&&renegadeGiveup&&rebelGiveup)
		gameOver("lord+loyalist");
	else if (loyalGiveup&&renegadeGiveup&&rebelGiveup){
		// if everyone give up, then ensure that the initiator doesn't win.
		QString playerRole = initiator->getRole();
		if (playerRole == "lord" || playerRole == "loyalist")
			gameOver(renegadeAlive >= rebelAlive ? "renegade" : "rebel");
		else if (playerRole == "renegade")
			gameOver(loyalAlive >= rebelAlive ? "loyalist+lord" : "rebel");
		else if (playerRole == "rebel")
			gameOver(renegadeAlive >= loyalAlive ? "renegade" : "loyalist+lord");
	}

	m_surrenderRequestReceived = false;

	initiator->setFlags("Global_ForbidSurrender");
	doNotify(initiator, S_COMMAND_ENABLE_SURRENDER, QVariant(false));
	return true;
}

void Room::processRequestSurrender(ServerPlayer*player, const QVariant&)
{
	//@todo: Strictly speaking, the client must be in the PLAY phase
	//@todo: return false for 3v3 and 1v1!!!
	if (!player->m_isWaitingReply)
		return;
	if (!_m_isFirstSurrenderRequest
		&&_m_timeSinceLastSurrenderRequest.elapsed() <= Config.S_SURRENDER_REQUEST_MIN_INTERVAL)
		return; //@todo: warn client here after new protocol has been enacted on the warn request

	_m_isFirstSurrenderRequest = false;
	_m_timeSinceLastSurrenderRequest.restart();
	m_surrenderRequestReceived = true;
	player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	return;
}

void Room::processClientPacket(const QString&request)
{
	Packet packet;
	if (packet.parse(request.toLatin1())){
		ServerPlayer*player = qobject_cast<ServerPlayer*>(sender());
#ifdef LOGNETWORK
		emit Sanguosha->logNetworkMessage("recv "+player->objectName()+":"+request);
#endif // LOGNETWORK
		if (game_state<0){
			if (player&&player->isOnline())
				doNotify(player, S_COMMAND_WARN, QString("GAME_OVER"));
			return;
		}
		if (packet.getPacketType() == S_TYPE_REPLY){
			if (player == nullptr) return;
			player->setClientReplyString(request);
			processResponse(player,&packet);
		} else if (packet.getPacketType() == S_TYPE_REQUEST || packet.getPacketType() == S_TYPE_NOTIFICATION){
			Callback callback = m_callbacks[packet.getCommandType()];
			if (!callback) return;
			(this->*callback)(player, packet.getMessageBody());
		}
	}
}

void Room::addRobotCommand(ServerPlayer*player, const QVariant&arg)
{
	if (player&&!player->isOwner())
		return;

	int r = 0, add_num = arg.toInt();
	if (add_num == -1) add_num = player_count - m_players.length();
	foreach(ServerPlayer*p, m_players){
		if (p->getState() == "robot") r++;
	}

	static bool seeded = false;
	if (!seeded) {
		qsrand(time(NULL));
		seeded = true;
	}

	static QStringList devs;
	static QStringList all_generals;
	if (all_generals.isEmpty()) {
		foreach(const General* general, Sanguosha->findChildren<const General*>()) {
			all_generals << general->objectName();
		}
	}
	if(devs.length()<add_num){
		foreach(const General*general, Sanguosha->findChildren<const General*>()){
			if(general->objectName().contains("dev_"))
				devs << general->objectName();
		}
		qShuffle(devs);
	}

	for (int i = 0; i < add_num; i++){
		if (isFull()) break;
		ServerPlayer*robot = new ServerPlayer(this);
		robot->setState("robot");

		m_players << robot;

		//QString robot_name = tr("Computer %1").arg(QChar('A' + r));
		r++;
		QString avatar;
		if (!devs.isEmpty()) {
			avatar = devs.takeFirst();
		} else {
			if (!all_generals.isEmpty()) {
				avatar = all_generals.at(qrand() % all_generals.size());
			} else {
				avatar = "";
			}
		}
		signup(robot, QString("神小杀0%1号").arg(r), avatar, true);

		//robot_name = tr("Hello, I'm a robot").toUtf8().toBase64();
		//speakCommand(robot, robot_name);

		broadcastProperty(robot, "state");
	}
}

ServerPlayer*Room::getOwner() const
{
	foreach(ServerPlayer*player, m_players)
		if (player->isOwner()) return player;
	return nullptr;
}

void Room::toggleReadyCommand(ServerPlayer*, const QVariant&)
{
	if (game_state<1&&isFull())
		start();
}

void Room::signup(ServerPlayer*player, const QString&screen_name, const QString&avatar, bool is_robot)
{
	player->setObjectName(generatePlayerName());
	safeSetPlayerProperty(player, "avatar", avatar);
	player->setScreenName(screen_name);

	if (!is_robot){
		notifyProperty(player, player, "objectName");

		if (!getOwner()){
			player->setOwner(true);
			notifyProperty(player, player, "owner");
		}
	}

	// introduce the new joined player to existing players except himself
	player->introduceTo(nullptr);

	if (is_robot)
		toggleReadyCommand(player, QVariant());
	else{
		QString greetingStr = "<font color=#EEB422>成功加入游戏</font>";//tr("<font color=#EEB422>Player <b>%1</b> joined the game</font>").arg(screen_name);
		speakCommand(player, greetingStr.toUtf8().toBase64());
		player->startNetworkDelayTest();

		// introduce all existing player to the new joined
		foreach(ServerPlayer*p, m_players){
			if (p != player) p->introduceTo(player);
		}
	}
}

void Room::assignGeneralsForPlayers(const QList<ServerPlayer*>&to_assign)
{
	QSet<QString> existed;
	foreach(ServerPlayer*player, m_players){
		QString gn = player->getGeneralName();
		if(gn.isEmpty()) continue;
		existed << gn;
		if(gn=="yinni_hide")
			existed << player->property("yinni_general").toString();
		gn = player->getGeneral2Name();
		if(gn.isEmpty()) continue;
		existed << gn;
		if(gn=="yinni_hide")
			existed << player->property("yinni_general2").toString();
	}
	if (Config.Enable2ndGeneral){
		foreach(QString name, BanPair::getAllBanSet())
			existed << name;
		if (to_assign.first()->getGeneral()){
			foreach(QString name, BanPair::getSecondBanSet())
				existed << name;
		}
	}

	const int max_choice = (Config.EnableHegemony&&Config.Enable2ndGeneral) ?
		Config.value("HegemonyMaxChoice", 7).toInt() : Config.value("MaxChoice", 5).toInt();
	const int total = Sanguosha->getGeneralCount();
	const int max_available = (total - existed.size()) / to_assign.length();
	const int choice_count = qMin(max_choice, max_available);
	QStringList choices = Sanguosha->getRandomGenerals(total - existed.size(), existed);

	if (Config.EnableHegemony){
		if (to_assign.first()->getGeneral()){
			foreach(ServerPlayer*sp, m_players){
				QStringList old_list = sp->getSelected();
				sp->clearSelected();

				//keep legal generals
				foreach(QString name, old_list){
					if (Sanguosha->getGeneral(name)->getKingdom() != sp->getGeneral()->getKingdom()
						|| sp->findReasonable(old_list, true) == name){
						sp->addToSelected(name);
						old_list.removeOne(name);
					}
				}

				//drop the rest and add new generals
				while (old_list.length()){
					QString choice = sp->findReasonable(choices);
					sp->addToSelected(choice);
					old_list.pop_front();
					choices.removeOne(choice);
				}
			}
			return;
		}
	}

	foreach(ServerPlayer*player, to_assign){
		player->clearSelected();
		QStringList hidden;
		for (int i = 0; i < choice_count; i++){
			hidden << "unknown";
			QString choice = player->findReasonable(choices, true);
			if (choice.isEmpty()) break;
			player->addToSelected(choice);
			choices.removeOne(choice);
		}
		doAnimate(S_ANIMATE_HUASHEN, player->objectName(), hidden.join(":"));
	}
	if(thread) thread->delay();
}

void Room::assignGeneralsForPlayersOfJianGeDefenseMode(const QList<ServerPlayer*>&to_assign)
{
	QMap<QString, QSet<QString> > existed;
	foreach(ServerPlayer*player, m_players){
		if (player->property("jiange_defense_type").toString() != "general")
			continue;
		if (player->getGeneral())
			existed[player->getGeneral()->getKingdom()] << player->getGeneralName();
		if (player->getGeneral2())
			existed[player->getGeneral2()->getKingdom()] << player->getGeneral2Name();
	}
	if (Config.Enable2ndGeneral){
		foreach(QString name, BanPair::getAllBanSet()){
			const General*gen = Sanguosha->getGeneral(name);
			if (gen) existed[gen->getKingdom()] << name;
		}
		if (to_assign.first()->getGeneral()){
			foreach(QString name, BanPair::getSecondBanSet()){
				const General*gen = Sanguosha->getGeneral(name);
				if (gen) existed[gen->getKingdom()] << name;
			}
		}
	}

	QMap<QString, QStringList> general_choices;
	foreach(QString key, Config.JianGeDefenseKingdoms.keys()){
		QString kingdom = Config.JianGeDefenseKingdoms[key];
		int total = Sanguosha->getGeneralCount(false, kingdom);
		general_choices[kingdom] = Sanguosha->getRandomGenerals(total - existed[kingdom].size(), existed[kingdom], kingdom);
	}

	const int max_choice = Config.value("MaxChoice", 5).toInt();
	foreach(ServerPlayer*player, to_assign){
		QStringList choices;
		int choice_count = 0;
		QString kingdom = Config.JianGeDefenseKingdoms[player->getRole()];
		QString jiange_defense_type = player->property("jiange_defense_type").toString();
		if (jiange_defense_type == "machine"){
			choices = Config.JianGeDefenseMachine[kingdom];
			choice_count = choices.length();
		} else if (jiange_defense_type == "soul"){
			choices = Config.JianGeDefenseSoul[kingdom];
			choice_count = choices.length();
		} else{
			int total = Sanguosha->getGeneralCount(false, kingdom);
			int max_available = (total - existed[kingdom].size()) / 2;
			choice_count = qMin(max_choice, max_available);
			choices = general_choices[kingdom];
		}

		player->clearSelected();

		for (int i = 0; i < choice_count; i++){
			QString choice = player->findReasonable(choices, true);
			if (choice.isEmpty()) break;
			player->addToSelected(choice);
			choices.removeOne(choice);
			if (jiange_defense_type == "general")
				general_choices[kingdom].removeOne(choice);
		}
	}
}

void Room::chooseGenerals(QList<ServerPlayer*> players)
{
	if (Config.Enable2ndGeneral)
		Config.Enable2ndGeneral = mode!="02_1v1"&&mode!="06_3v3"&&mode!="06_XMode"&&mode!="04_1v3";
	if (players.isEmpty()) players = m_players;
	// for lord.
	QString general = "sujiang";
	ServerPlayer*the_lord = getLord();
	if (players.contains(the_lord)){
		QStringList lord_list;
		if (Config.EnableSame || mode == "03_1v2"){
			lord_list = Sanguosha->getRandomGenerals(Config.value("MaxChoice", 5).toInt());
			if(mode == "03_1v2"){
				QStringList all_generals = Sanguosha->getLimitedGeneralNames();
				qShuffle(all_generals);
				foreach(QString general_name, all_generals){
					if(general_name.contains("ddz_")&&!lord_list.contains(general_name)){
						lord_list.prepend(general_name);
						break;
					}
				}
			}
		}else
			lord_list = Sanguosha->getRandomLords();
		general = askForGeneral(the_lord, lord_list);
		the_lord->setGeneralName(general);
		notifyProperty(the_lord, the_lord, "general");
		if (!Config.EnableBasara){
			if (the_lord->hasHideSkill()){
				setPlayerProperty(the_lord, "yinni_general", general);
				general = "yinni_hide";
				the_lord->setGeneralName(general);
			}
			if (mode != "03_1v2")
				broadcastProperty(the_lord, "general", general);
		}
		players.removeOne(the_lord);
		if (Config.EnableSame){
			foreach(ServerPlayer*p, players){
				p->setGeneralName(general);
				if(general=="yinni_hide")
					setPlayerProperty(p, "yinni_general", the_lord->property("yinni_general"));
			}
			Config.Enable2ndGeneral = false;
			return;
		}else if(Config.Enable2ndGeneral){
			if(general=="yinni_hide") general = the_lord->property("yinni_general").toString();
			lord_list = Sanguosha->getRandomGenerals(Config.value("MaxChoice", 5).toInt(),QSet<QString>()<<general);
			general = askForGeneral(the_lord, lord_list);
			the_lord->setGeneral2Name(general);
			notifyProperty(the_lord, the_lord, "general2");
			if (!Config.EnableBasara){
				if (the_lord->hasHideSkill()){
					setPlayerProperty(the_lord, "yinni_general2", general);
					general = "yinni_hide";
					the_lord->setGeneral2Name(general);
				}
				if (mode != "03_1v2")
					broadcastProperty(the_lord, "general2", general);
			}
		}
	}

	assignGeneralsForPlayers(players);
	foreach(ServerPlayer*player, players)
		_setupChooseGeneralRequestArgs(player);

	doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);
	foreach(ServerPlayer*player, players){
		if ((player->m_isClientResponseReady&&_setPlayerGeneral(player, player->getClientReply().toString(), true))
			||_setPlayerGeneral(player, _chooseDefaultGeneral(player), true)){
			if (player->hasHideSkill()){
				setPlayerProperty(player, "yinni_general", player->getGeneralName());
				player->setGeneralName("yinni_hide");
			}
			notifyProperty(player, player, "general");
		}
	}

	if (Config.Enable2ndGeneral){
		assignGeneralsForPlayers(players);
		foreach(ServerPlayer*player, players)
			_setupChooseGeneralRequestArgs(player);

		doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);
		foreach(ServerPlayer*player, players){
			if ((player->m_isClientResponseReady&&_setPlayerGeneral(player, player->getClientReply().toString(), false))
				||_setPlayerGeneral(player, _chooseDefaultGeneral(player), false)){
				if(player->hasHideSkill(2)){
					setPlayerProperty(player, "yinni_general2", player->getGeneral2Name());
					player->setGeneral2Name("yinni_hide");
				}
				notifyProperty(player, player, "general2");
			}
		}
	}

	if (Config.EnableBasara){
		foreach(ServerPlayer*player, m_players){
			QStringList names;
			if (player->getGeneral()){
				names.append(player->getGeneralName());
				if(names.last()=="yinni_hide"){
					names.takeLast();
					names.append(player->property("yinni_general").toString());
				}
				player->setGeneralName("anjiang");
				notifyProperty(player, player, "general");
			}
			if (player->getGeneral2()){
				names.append(player->getGeneral2Name());
				if(names.last()=="yinni_hide"){
					names.takeLast();
					names.append(player->property("yinni_genera2").toString());
				}
				player->setGeneral2Name("anjiang");
				notifyProperty(player, player, "general2");
			}
			safeSetPlayerProperty(player, "basara_generals", names.join("+"));
			notifyProperty(player, player, "basara_generals");
		}
	}else{
		if (mode == "03_1v2"&&the_lord){
			broadcastProperty(the_lord, "general");
			if(Config.Enable2ndGeneral)
				broadcastProperty(the_lord, "general2");
		}
	}
	/*if (Config.value("EnableSUPERConvert", true).toBool()&&mode != "05_ol"){
		foreach(ServerPlayer*p, m_players){
			QStringList choicelist;
			foreach(QString gen, Sanguosha->getLimitedGeneralNames()){
				if (p->getGeneralName().endsWith(gen.split("_").last()))
					choicelist << gen;
			}
			QString to_cv;
			if (choicelist.length() > 1){
				AI*ai = p->getAI();
				if (ai) to_cv = askForChoice(p, "gamerule", choicelist.join("+"));
				else to_cv = askForGeneral(p, choicelist);
				p->setGeneralName(to_cv);
				if (Config.EnableBasara)
					notifyProperty(p, p, "general", to_cv);
				else
					broadcastProperty(p, "general", to_cv);
				if (Config.EnableSame){
					foreach(ServerPlayer*p, players){
						if (!p->isLord())
						p->setGeneralName(to_cv);
					}
					Config.Enable2ndGeneral = false;
					return;
				}
				to_cv = Sanguosha->getGeneral(to_cv)->getKingdom();
				if (to_cv != p->getKingdom())
					setPlayerProperty(p, "kingdom", to_cv);
			}
			if (p->getGeneral2()){
				QStringList choicelis;
				foreach(QString gen, Sanguosha->getLimitedGeneralNames()){
					if (p->getGeneral2Name().endsWith(gen.split("_").last()))
						choicelis << gen;
				}
				if (choicelis.length() > 1){
					AI*ai = p->getAI();
					if (ai) to_cv = askForChoice(p, "gamerule", choicelis.join("+"));
					else to_cv = askForGeneral(p, choicelis);
					p->setGeneral2Name(to_cv);
					if (Config.EnableBasara)
						notifyProperty(p, p, "general2", to_cv);
					else
						broadcastProperty(p, "general2", to_cv);
					if (Config.EnableSame){
						foreach(ServerPlayer*p, players){
							if (!p->isLord())
							p->setGeneralName(to_cv);
						}
					}
				}
			}
		}
	}*/
}

void Room::chooseGeneralsOfJianGeDefenseMode()
{
	QList<ServerPlayer*> to_assign = m_players;

	assignGeneralsForPlayersOfJianGeDefenseMode(to_assign);
	foreach(ServerPlayer*player, to_assign)
		_setupChooseGeneralRequestArgs(player);

	doBroadcastRequest(to_assign, S_COMMAND_CHOOSE_GENERAL);
	foreach(ServerPlayer*player, to_assign){
		if(player->m_isClientResponseReady&&_setPlayerGeneral(player, player->getClientReply().toString(), true)) continue;
		QString result = _chooseDefaultGeneral(player);
		if (player->property("jiange_defense_type").toString() != "general"){ // randomly chosen
			QStringList selected = player->getSelected();
			result = selected.at(qrand() % selected.length());
		}
		_setPlayerGeneral(player, result, true);
	}

	if (Config.Enable2ndGeneral){
		QList<ServerPlayer*> to_assign;
		foreach(ServerPlayer*p, m_players){
			if (p->property("jiange_defense_type").toString() == "general")
				to_assign << p;
		}
		assignGeneralsForPlayersOfJianGeDefenseMode(to_assign);
		foreach(ServerPlayer*player, to_assign)
			_setupChooseGeneralRequestArgs(player);

		doBroadcastRequest(to_assign, S_COMMAND_CHOOSE_GENERAL);
		foreach(ServerPlayer*player, to_assign){
			if (player->m_isClientResponseReady&&_setPlayerGeneral(player, player->getClientReply().toString(), false)) continue;
			_setPlayerGeneral(player, _chooseDefaultGeneral(player), false);
		}
	}
}

bool Room::changeBGM(const QString&bgm_name, bool reset, QList<ServerPlayer*> to_assign)
{
	QString bgm = QString("audio/system/BGM/%1.ogg").arg(bgm_name);
	if (!QFile::exists(bgm)) return false;
	if(to_assign.isEmpty()) to_assign = m_players;
	JsonArray arg;
	arg << QSanProtocol::S_GAME_EVENT_CHANGE_BGM;
	arg << bgm;
	arg << reset;
	foreach(ServerPlayer*player, to_assign)
		doNotify(player, QSanProtocol::S_COMMAND_LOG_EVENT, arg);
	return true;
}

void Room::run()
{
	// initialize random seed for later use
	qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));
	AIHumanized = Config.value("AIHumanized", true).toBool();
	Config.AIDelay = Config.OriginAIDelay;

	foreach(ServerPlayer*player, m_players){
		//Ensure that the game starts with all player's mutex locked
		player->drainAllLocks();
		player->releaseLock(ServerPlayer::SEMA_MUTEX);
	}
#ifdef AUDIO_SUPPORT
	Audio::stopBGM();
#endif

	prepareForStart();

	bool using_countdown = !_virtual&&property("to_test").toString().isEmpty();

#ifndef QT_NO_DEBUG
	using_countdown = false;
#endif

	if (using_countdown){
		for (int i = Config.CountDownSeconds; i >= 0; i--){
			doBroadcastNotify(S_COMMAND_START_IN_X_SECONDS, i);
			sleep(1);
		}
	} else
		doBroadcastNotify(S_COMMAND_START_IN_X_SECONDS, QVariant(0));

	if (scenario&&!scenario->generalSelection()){
	} else if (mode == "06_3v3"){
		thread_3v3 = new RoomThread3v3(this);
		thread_3v3->start();

		connect(thread_3v3, SIGNAL(finished()), this, SLOT(startGame()));
		connect(thread_3v3, SIGNAL(finished()), thread_3v3, SLOT(deleteLater()));
		return;
	} else if (mode == "06_XMode"){
		thread_xmode = new RoomThreadXMode(this);
		thread_xmode->start();

		connect(thread_xmode, SIGNAL(finished()), this, SLOT(startGame()));
		connect(thread_xmode, SIGNAL(finished()), thread_xmode, SLOT(deleteLater()));
		return;
	} else if (mode == "02_1v1"){
		thread_1v1 = new RoomThread1v1(this);
		thread_1v1->start();

		connect(thread_1v1, SIGNAL(finished()), this, SLOT(startGame()));
		connect(thread_1v1, SIGNAL(finished()), thread_1v1, SLOT(deleteLater()));
		return;
	} else if (mode == "04_1v3"){
		ServerPlayer*lord = m_players.first();
		QStringList lords = GetConfigFromLuaState(m_lua, "extra_boss").toStringList();
		if(lords.isEmpty()){
			setPlayerProperty(lord, "general", "shenlvbu1");
		}else
			setPlayerProperty(lord, "general", askForGeneral(lord, lords));

		QStringList names;
		foreach(QString gen_name, GetConfigFromLuaState(m_lua, "hulao_generals").toStringList()){
			if (gen_name.startsWith("-")){ // means banned generals
				names.removeOne(gen_name.mid(1));
			} else if (gen_name.startsWith("package:")){
				const Package*pack = Sanguosha->findChild<const Package*>(gen_name.mid(8));
				if (pack){
					foreach(const General*general, pack->findChildren<const General*>()){
						if(general->isTotallyHidden()||names.contains(general->objectName())) continue;
						if(!Config.AddGodGeneral&&general->getKingdoms().contains("god")) continue;
						names << general->objectName();
					}
				}
			} else if (!names.contains(gen_name))
				names << gen_name;
		}
		qShuffle(names);
		foreach(ServerPlayer*player, m_players){
			if (player == lord) continue;
			lords.clear();
			for (int i = 0; i < 3; i++){
				lords << names.takeFirst();
				if (names.isEmpty()) break;
			}
			setPlayerProperty(player, "general", askForGeneral(player, lords));
		}
	} else if (mode == "04_boss"){
		QStringList boss_lv_1 = Config.BossGenerals.first().split("+");
		if (Config.value("BossYanluo").toBool()){
			boss_lv_1.clear();
			boss_lv_1 << "yl_qinguang";
		}
		QStringList lords = GetConfigFromLuaState(m_lua, "extra_boss").toStringList();
		if(lords.length()>0) boss_lv_1 = lords;
		ServerPlayer*lord = m_players.first();

		if (Config.value("OptionalBoss").toBool()){
			setPlayerProperty(lord, "general", askForGeneral(lord, boss_lv_1));
		} else
			setPlayerProperty(lord, "general", boss_lv_1.at(qrand() % boss_lv_1.length()));
		setPlayerMark(lord, "BossMode_Boss", 1);

		QList<ServerPlayer*> players = m_players;
		players.removeOne(lord);
		chooseGenerals(players);
	} else if (mode == "05_ol"){
		QStringList jiang_list, bing_list;
		jiang_list << "godlai_zhangji" << "godlai_fanchou" << "godlai_niufudongxie" << "godlai_dongyue" << "godlai_lijue" << "godlai_guosi";
		bing_list << "godlai_longxiang" << "godlai_huben" << "godlai_fengyao" << "godlai_baolve" << "godlai_feixiong_right" << "godlai_feixiong_right";
		QStringList lords = GetConfigFromLuaState(m_lua, "extra_boss").toStringList();
		if(lords.length()>0) jiang_list = lords;
		foreach(ServerPlayer*player, m_players){
			if (player->isLord()){
				QString jiang = askForGeneral(player, jiang_list);
				setPlayerProperty(player, "general", jiang);
				QString bing = bing_list[jiang_list.indexOf(jiang)];
				foreach(ServerPlayer*p, m_players){
					if (p->getRole() == "loyalist"){
						setPlayerProperty(p, "general", bing);
						if (bing == "godlai_feixiong_right")
							bing = "godlai_feixiong_left";
					}
				}
			}
		}
		bing_list << jiang_list;
		jiang_list = Sanguosha->getRandomGenerals(m_players.length()*4,QSet<QString>(bing_list.begin(),bing_list.end()));
		foreach(ServerPlayer*player, m_players){
			if (player->getRole() == "rebel"){
				lords.clear();
				for (int i = 0; i < 5; i++){
					lords << jiang_list.takeFirst();
					if (jiang_list.isEmpty()) break;
				}
				setPlayerProperty(player, "general", askForGeneral(player, lords));
			}
		}
	} else if (mode == "06_ol"){
		QStringList gui_list, list, god_list;
		gui_list << "hundun" << "qiongqi" << "taowu" << "taotie" << "yingzhao" << "xiangliu" << "zhuyan" << "bifang";
		QStringList lords = GetConfigFromLuaState(m_lua, "extra_boss").toStringList();
		if(lords.length()>0) gui_list = lords;
		foreach(ServerPlayer*player, m_players){
			if (player->getRole() == "loyalist")
				setPlayerProperty(player, "general", "zhuyin");
			else if (player->isLord())
				setPlayerProperty(player, "general", askForGeneral(player, gui_list));
		}
		foreach(QString god, Sanguosha->getLimitedGeneralNames("god"))
			if (god.contains("shen")) list << god;
		qShuffle(list);
		for (int i = 0; i < Config.value("fuck_god_spinbox", 3).toInt(); ++i){
			if (list.isEmpty()) break;
			god_list << list.takeFirst();
		}
		gui_list << god_list;
		list = Sanguosha->getRandomGenerals(m_players.length()*4,QSet<QString>(gui_list.begin(),gui_list.end()));
		foreach(ServerPlayer*player, m_players){
			if (player->getRole() == "rebel"){
				lords.clear();
				for (int i = 0; i < 5; ++i)
					lords << list.takeFirst();
				QString general = askForGeneral(player, lords+god_list);
				setPlayerProperty(player, "general", general);
				god_list.removeOne(general);
			}
		}
	} else if (mode == "08_defense"){
		QStringList type_list;
		type_list << "machine" << "general" << "soul" << "general"
			<< "general" << "soul" << "general" << "machine";
		for (int i = 0; i < 8; i++)
			setPlayerProperty(m_players[i], "jiange_defense_type", type_list[i]);
		chooseGeneralsOfJianGeDefenseMode();
	} else
		chooseGenerals();
	startGame();

	if (_m_Id<1&&QFile::exists("lua/ai/cstring")){
		QStringList pns,all_generals;
		foreach(const General*general, Sanguosha->findChildren<const General*>()){
			all_generals << general->objectName();
			if(general->isTotallyHidden()) continue;
			QString pn = general->objectName();
			if(!QFile::exists("image/fullskin/generals/full/"+pn+".jpg"))
				output(pn+"-full_jpg");
			if(!QFile::exists("image/generals/card/"+pn+".jpg"))
				output(pn+"-card_jpg");
			if(!QFile::exists("audio/death/"+pn+".ogg"))
				output(pn+"-death_ogg");
			foreach(const Skill*vs, general->getVisibleSkillList()){
				pn = vs->objectName();
				if(pns.contains(pn)) continue;
				if(vs->getSources().isEmpty())
					output(pn+"-ogg");
				if(Sanguosha->translate("$"+pn+"1").contains(pn))
					output(pn+"-translate");
				pns << pn;
			}
			foreach(QString pn, general->getRelatedSkillNames()){
				if(pn.contains("#")||pns.contains(pn)) continue;
				if(!QFile::exists("audio/skill/"+pn+".ogg")&&!QFile::exists("audio/skill/"+pn+"1.ogg"))
					output(pn+"-ogg");
				if(Sanguosha->translate("$"+pn+"1").contains(pn))
					output(pn+"-translate");
				pns << pn;
			}
		}
		tag["AllGenerals"] = all_generals;
	}
}

void Room::assignRoles()
{
	QStringList roles = Sanguosha->getRoleList(mode);
	if (mode == "04_2v2"){/*
		roles.clear();
		if (qrand()%2<1) roles << "loyalist" << "rebel" << "rebel" << "loyalist";
		else roles << "rebel" << "loyalist" << "loyalist" << "rebel";*/
		qShuffle(m_players);
	} else if (mode == "02_1v1"){
		roles.prepend(roles.takeLast());
	}else if (mode != "08_defense"&&mode != "05_ol"&&mode != "06_ol")
		qShuffle(roles);

	for (int i = 0; i < m_players.count(); i++){
		if (i >= roles.count()) break;
		m_players[i]->setRole(roles[i]);
		if (mode.contains("_") || (roles[i] == "lord"&&!ServerInfo.EnableHegemony))
			//|| mode == "06_ol"|| mode == "05_ol" || mode == "04_1v3" || mode == "04_boss" || mode == "08_defense" || mode == "03_1v2" || mode == "04_2v2")
			broadcastProperty(m_players[i], "role", roles[i]);
		else
			notifyProperty(m_players[i], m_players[i], "role");
	}
}

void Room::swapSeat(ServerPlayer*a, ServerPlayer*b)
{
	int seat1 = m_players.indexOf(a);
	int seat2 = m_players.indexOf(b);
	m_players.swapItemsAt(seat1, seat2);
	QStringList player_circle;
	foreach(ServerPlayer*player, m_players)
		player_circle << player->objectName();
	doBroadcastNotify(S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(player_circle));

	m_alivePlayers.clear();
	for (int i = 0; i < m_players.length(); i++){
		ServerPlayer*player = m_players[i];
		if (player->isAlive()){
			m_alivePlayers << player;
			player->setSeat(m_alivePlayers.length());
		} else
			player->setSeat(0);

		broadcastProperty(player, "seat");

		player->setPlayerSeat(i+1);
		broadcastProperty(player, "player_seat");

		player->setNext(m_players[(i+1)%m_players.length()]);
	}
}

void Room::adjustSeats()
{
	int n = 0;
	foreach(ServerPlayer*p, m_players){
		if (p->getRoleEnum() == Player::Lord)
			break;
		n++;
	}
	if(mode == "02_1v1") n = 0;
	QList<ServerPlayer*> players;
	for (int j = n; j < m_players.length(); j++)
		players << m_players.at(j);
	for (int j = 0; j < n; j++)
		players << m_players.at(j);

	m_players = players;

	QStringList player_circle;
	for (int i = 0; i < players.length(); i++){
		players[i]->setSeat(i + 1);
		players[i]->setPlayerSeat(i + 1);
		broadcastProperty(players[i], "player_seat");
		player_circle << players[i]->objectName();
	}

	// tell the players about the seat, and the first is always the lord
	doBroadcastNotify(S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(player_circle));
}

int Room::getCardFromPile(const QString&card_pattern)
{
	if (m_drawPile->isEmpty())
		swapPile();

	if (card_pattern.startsWith("@")){
		if (card_pattern == "@duanliang"){
			foreach(int card_id,*m_drawPile){
				const Card*card = Sanguosha->getCard(card_id);
				if (card->isBlack()&&(card->isKindOf("BasicCard") || card->isKindOf("EquipCard")))
					return card_id;
			}
		}
	} else {
		foreach(int card_id,*m_drawPile){
			if (Sanguosha->getCard(card_id)->objectName() == card_pattern)
				return card_id;
		}
	}

	return -1;
}

QString Room::_chooseDefaultGeneral(ServerPlayer*player) const
{
	if (Config.EnableHegemony&&Config.Enable2ndGeneral){
		foreach(QString name, player->getSelected()){
			const General*gen = player->getGeneral();
			if (gen != nullptr){ // choosing first general
				if (name == gen->objectName()) continue;
				if (Sanguosha->getGeneral(name)->getKingdom() == gen->getKingdom())
					return name;
			} else {
				gen = Sanguosha->getGeneral(name);
				foreach(QString other, player->getSelected()){ // choosing second general
					if (name == other) continue;
					if (gen->getKingdom() == Sanguosha->getGeneral(other)->getKingdom())
						return name;
				}
			}
		}
		//Q_ASSERT(false);
		return "";
	} else {
		GeneralSelector*selector = GeneralSelector::getInstance();
		return selector->selectFirst(player, player->getSelected());
	}
}

bool Room::_setPlayerGeneral(ServerPlayer*player, const QString&generalName, bool isFirst)
{
	if(Sanguosha->getGeneral(generalName)&&(Config.FreeChoose||player->getSelected().contains(generalName))){
		if (isFirst){
			player->setGeneralName(generalName);
			notifyProperty(player, player, "general", generalName);
		} else {
			player->setGeneral2Name(generalName);
			notifyProperty(player, player, "general2", generalName);
		}
		return true;
	}
	return false;
}

void Room::speakCommand(ServerPlayer*player, const QVariant&arg)
{
#define _NO_BROADCAST_SPEAKING {\
	broadcast = false;\
	JsonArray nbbody;\
	nbbody << player->objectName();\
	nbbody << arg;\
	doNotify(player, S_COMMAND_SPEAK, nbbody);\
}
	bool broadcast = true;
	if (player){
		QString sentence = QString::fromUtf8(QByteArray::fromBase64(arg.toString().toLatin1()));
		if (sentence.startsWith("$")){
			QString new_sentence = sentence;
			new_sentence = new_sentence.mid(1);
			QStringList _new_sentence = new_sentence.split(":");

			if (_new_sentence.length() == 1){
				QString audio = QString("audio/skill/%1.ogg").arg(new_sentence);
				if (QFile::exists(audio)){
					if (Sanguosha->translate(sentence) != sentence){
						JsonArray body;
						body << player->objectName() << Sanguosha->translate(sentence).toUtf8().toBase64();
						doBroadcastNotify(S_COMMAND_SPEAK, body);
					}
					broadcastSkillInvoke(new_sentence);
				}
			} else if (_new_sentence.length() == 2){
				QString audio = QString("audio/skill/%1%2.ogg").arg(_new_sentence.first()).arg(_new_sentence.last());
				if (QFile::exists(audio)){
					QString _sentence = "$" + _new_sentence.first() + _new_sentence.last();
					if (Sanguosha->translate(_sentence) != _sentence){
						JsonArray body;
						body << player->objectName() << Sanguosha->translate(_sentence).toUtf8().toBase64();
						doBroadcastNotify(S_COMMAND_SPEAK, body);
					}
					broadcastSkillInvoke(_new_sentence.first(), _new_sentence.last().toInt());
				}
			}
			return;
		} else if (sentence.startsWith("~")){
			QString new_sentence = sentence;
			new_sentence = new_sentence.mid(1);
			QString filename = QString("audio/death/%1.ogg").arg(new_sentence);
			if (Sanguosha->translate(sentence) != sentence){
				JsonArray body;
				body << player->objectName() << Sanguosha->translate(sentence).toUtf8().toBase64();
				doBroadcastNotify(S_COMMAND_SPEAK, body);
			}
			Sanguosha->playAudioEffect(filename);
			return;
		}
		if(Config.EnableCheat){
			if (sentence == ".BroadcastRoles"){
				_NO_BROADCAST_SPEAKING
				foreach(ServerPlayer*p, m_alivePlayers)
					broadcastProperty(p, "role", p->getRole());
			} else if (sentence.startsWith(".BroadcastRoles=")){
				_NO_BROADCAST_SPEAKING
				QString name = sentence.mid(12);
				foreach(ServerPlayer*p, m_alivePlayers){
					if (p->objectName() == name || p->getGeneralName() == name){
						broadcastProperty(p, "role", p->getRole());
						break;
					}
				}
			} else if (sentence == ".ShowHandCards"){
				_NO_BROADCAST_SPEAKING
	
				JsonArray body;
				body << player->objectName() << QString("----------").toUtf8().toBase64();
				doNotify(player, S_COMMAND_SPEAK, body);
	
				foreach(ServerPlayer*p, m_alivePlayers){
					if (!p->isKongcheng()){
						QStringList handcards;
						foreach(int id, p->handCards())
							handcards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
						JsonArray body;
						body << p->objectName() << handcards.join("，").toUtf8().toBase64();
						doNotify(player, S_COMMAND_SPEAK, body);
					}
				}
				doNotify(player, S_COMMAND_SPEAK, body);
			} else if (sentence.startsWith(".ShowHandCards=")){
				_NO_BROADCAST_SPEAKING
				QString name = sentence.mid(15);
				foreach(ServerPlayer*p, m_alivePlayers){
					if (p->objectName() == name || p->getGeneralName() == name){
						if (!p->isKongcheng()){
							QStringList handcards;
							foreach(int id, p->handCards())
								handcards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
							JsonArray body;
							body << p->objectName() << handcards.join("，").toUtf8().toBase64();
							doNotify(player, S_COMMAND_SPEAK, body);
						}
						break;
					}
				}
			} else if (sentence.startsWith(".ShowPrivatePile=")){
				_NO_BROADCAST_SPEAKING
				QStringList arg = sentence.mid(17).split(":");
				if (arg.length() == 2){
					QString name = arg.first();
					QString pile_name = arg.last();
					foreach(ServerPlayer*p, m_alivePlayers){
						if (p->objectName() == name || p->getGeneralName() == name){
							if (!p->getPile(pile_name).isEmpty()){
								QStringList pile_cards;
								foreach(int id, p->getPile(pile_name))
									pile_cards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
								JsonArray body;
								body << p->objectName() << pile_cards.join("，").toUtf8().toBase64();
								doNotify(player, S_COMMAND_SPEAK, body);
							}
							break;
						}
					}
				}
			} else if (sentence == ".ShowHuashen"){
				_NO_BROADCAST_SPEAKING
				foreach(ServerPlayer*zuoci, m_alivePlayers){
					if(zuoci->property("Huashens").toString().isEmpty()) continue;
					QStringList huashen_name;
					foreach(QString name, zuoci->property("Huashens").toString().split("+"))
						huashen_name << QString("<b>%1</b>").arg(Sanguosha->translate(name));
	
					JsonArray body;
					body << zuoci->objectName() << huashen_name.join("，").toUtf8().toBase64();
					doNotify(player, S_COMMAND_SPEAK, body);
				}
			} else if (sentence.startsWith(".SetAIDelay=")){
				_NO_BROADCAST_SPEAKING
				bool ok = false;
				int delay = sentence.mid(12).toInt(&ok);
				if (ok){
					Config.AIDelay = Config.OriginAIDelay = delay;
					Config.setValue("OriginAIDelay", delay);
				}
			} else if (sentence.startsWith(".SetGameMode=")){
				_NO_BROADCAST_SPEAKING
				setTag("NextGameMode", sentence.mid(13));
			} else if (sentence.startsWith(".SecondGeneral=")){
				_NO_BROADCAST_SPEAKING
				QString prop = sentence.mid(15);
				setTag("NextGameSecondGeneral", !prop.isEmpty()&&prop != "0"&&prop != "false");
			} else if (sentence == ".Pause"){
				_NO_BROADCAST_SPEAKING
				pauseCommand(player, true);
			} else if (sentence == ".Resume"){
				_NO_BROADCAST_SPEAKING
				pauseCommand(player, false);
			}
		}
		if (broadcast){
			JsonArray body;
			body << player->objectName() << arg;
			doBroadcastNotify(S_COMMAND_SPEAK, body);
		}
	}
	return;
#undef _NO_BROADCAST_SPEAKING
}

void Room::processResponse(ServerPlayer*player, const Packet*packet)
{
	player->acquireLock(ServerPlayer::SEMA_MUTEX);
	bool success = false;
	if (player == nullptr)
		emit room_message(tr("Unable to parse player"));
	else if (!player->m_isWaitingReply || player->m_isClientResponseReady)
		emit room_message(tr("Server is not waiting for reply from %1").arg(player->objectName()));
	else if (packet->getCommandType() != player->m_expectedReplyCommand)
		emit room_message(tr("Reply command should be %1 instead of %2")
		.arg(player->m_expectedReplyCommand).arg(packet->getCommandType()));
	else if (packet->localSerial != player->m_expectedReplySerial)
		emit room_message(tr("Reply serial should be %1 instead of %2")
		.arg(player->m_expectedReplySerial).arg(packet->localSerial));
	else
		success = true;

	if (success){
		_m_semRoomMutex.acquire();
		if (_m_raceStarted){
			player->setClientReply(packet->getMessageBody());
			player->m_isClientResponseReady = true;
			// Warning: the statement below must be the last one before releasing the lock!!!
			// Any statement after this statement will totally compromise the synchronization
			// because getRaceResult will then be able to acquire the lock, reading a non-null
			// raceWinner and proceed with partial data. The current implementation is based on
			// the assumption that the following line is ATOMIC!!!
			// @todo: Find a Qt atomic semantic or use _asm to ensure the following line is atomic
			// on a multi-core machine. This is the core to the whole synchornization mechanism for
			// broadcastRaceRequest.
			_m_raceWinner = player;
			// the _m_semRoomMutex.release() signal is in getRaceResult();
			_m_semRaceRequest.release();
		} else {
			_m_semRoomMutex.release();
			player->setClientReply(packet->getMessageBody());
			player->m_isClientResponseReady = true;
			player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
		}
	}
	player->releaseLock(ServerPlayer::SEMA_MUTEX);
}

CardUseStruct Room::getUseStruct(const Card*card)
{
	return tag["UseHistory"+card->toString()].value<CardUseStruct>();
}

bool Room::useCard(const CardUseStruct&use, bool add_history)
{
	CardUseStruct new_use = use;
	return useCard(new_use, add_history);
}

bool Room::useCard(CardUseStruct&use, bool add_history)
{
	if ((!use.card->canRecast()||use.from->isCardLimited(use.card,Card::MethodRecast))&&use.from->isCardLimited(use.card,use.card->getHandlingMethod()))
		return false;
	use.m_addHistory = add_history;
	const Card*card = use.card->validate(use);
	if(card==nullptr) return false;
	QList<int> ids;
	if (use.card->isVirtualCard()) ids = use.card->getSubcards();
	else ids << use.card->getId();
	foreach(int id, ids){
		use.m_isHandcard = use.from->handCards().contains(id);
		if (!use.m_isHandcard) break;
	}
	QString key = use.card->getClassName();
	tag.remove("UseHistory"+use.card->toString());
	if (use.card->inherits("LuaSkillCard")) key = "#"+use.card->objectName();
	//use.m_isOwnerUse = (ids.isEmpty()&&use.m_isOwnerUse)||getCardOwner(ids.first())==use.from;
	if(_m_roomState.getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY)
		addPlayerHistory(nullptr, "pushPile");
	if (use.m_addHistory){
		add_history = true;
		addPlayerHistory(use.from, key);
	}
	try {
		if (use.card->getRealCard() == card){
			if (!use.card->isVirtualCard()){
				WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
				if (wrapped->isModified()) broadcastUpdateCard(m_players, ids.first(), wrapped);
				//else broadcastResetCard(m_players, ids.first());
			}
			use.card->onUse(this, use);
		} else {
			use.card = card;
			return useCard(use, add_history);
		}
	} catch (TriggerEvent triggerEvent){
		if (triggerEvent == StageChange || triggerEvent == TurnBroken){
			CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(), use.card->getSkillName(), "");
			if (use.to.size() == 1) reason.m_targetId = use.to.first()->objectName();
			foreach(int id, ids){
				if (getCardPlace(id) != Player::PlaceTable)
					ids.removeOne(id);
			}
			moveCardsAtomic(CardsMoveStruct(ids, use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
			QVariant data = QVariant::fromValue(use);
			use.from->setFlags("Global_ProcessBroken");
			thread->trigger(CardFinished, this, use.from, data);
			use.from->setFlags("-Global_ProcessBroken");
			foreach(ServerPlayer*p, m_alivePlayers)
				p->removeQinggangTag(use.card);
			foreach(int id, pile1){
				if (getCardPlace(id) == Player::PlaceJudge)
					moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile, true);
				setCardFlag(id, "-using");
			}
		}
		throw triggerEvent;
	}
	if(add_history&&!use.m_addHistory)
		addPlayerHistory(use.from, key, -1);
	return true;
}

void Room::loseHp(ServerPlayer*victim, int lose, bool ignore_hujia, ServerPlayer*from, const QString&reason)
{
	HpLostStruct lost;
	lost.from = from;
	lost.to = victim;
	lost.lose = lose;
	lost.reason = reason;
	lost.ignore_hujia = ignore_hujia;
	loseHp(lost);
}

void Room::loseHp(const HpLostStruct&lost_data)
{
	QVariant data = QVariant::fromValue(lost_data);

	if (lost_data.lose<1||lost_data.to->isDead()||thread->trigger(PreHpLost, this, lost_data.to, data)) return;

	HpLostStruct lost = data.value<HpLostStruct>();
	if (lost.lose<1 || lost.to->isDead()) return;

	int hujia = lost.ignore_hujia?0:lost.to->getHujia();
	if (hujia > 0){
		hujia = qMin(hujia,lost.lose);
		lost.to->loseHujia(hujia);
	}
	JsonArray arg;
	arg << lost.to->objectName() << -lost.lose << -1 << hujia;
	doBroadcastNotify(S_COMMAND_CHANGE_HP, arg);

	if (lost.lose > hujia){
		setTag("HpChangedData", data);
		setPlayerProperty(lost.to, "hp", lost.to->getHp()+hujia-lost.lose);
	}

	thread->trigger(HpLost, this, lost.to, data);
}

void Room::changePlayerMaxHp(ServerPlayer*player, int change, const QString&reason)
{
	if (change == 0 || player->isDead() || player->inYinniState())
		return;

	MaxHpStruct maxhp(player, change, reason);
	QVariant data = QVariant::fromValue(maxhp);
	if (thread->trigger(MaxHpChange, this, player, data)) return;

	maxhp = data.value<MaxHpStruct>();
	if (maxhp.change==0||player->isDead()) return;

	player->setMaxHp(qMax(player->getMaxHp() + maxhp.change, 0));
	broadcastProperty(player, "maxhp");
	broadcastProperty(player, "hp");

	LogMessage log;
	log.type = "#GainMaxHp";
	log.arg = QString::number(maxhp.change);
	if(maxhp.change<0){
		log.type = "#LoseMaxHp";
		log.arg = QString::number(-maxhp.change);
	}
	log.from = player;
	sendLog(log);

	JsonArray arg;
	arg << player->objectName() << maxhp.change;
	doBroadcastNotify(S_COMMAND_CHANGE_MAXHP, arg);

	if (player->getMaxHp() <= 0) killPlayer(player);
	else thread->trigger(MaxHpChanged, this, player, data);
}

void Room::loseMaxHp(ServerPlayer*victim, int lose, const QString&reason)
{
	changePlayerMaxHp(victim, -lose, reason);
}

void Room::gainMaxHp(ServerPlayer*player, int gain, const QString&reason)
{
	changePlayerMaxHp(player, gain, reason);
}

bool Room::changeMaxHpForAwakenSkill(ServerPlayer*player, int magnitude, const QString&reason)
{
	int n = player->getMark("@waked");
	addPlayerMark(player, "@waked");
	if (magnitude < 0){
		if (Config.Enable2ndGeneral&&player->getGeneral2()
			&&Config.MaxHpScheme > 0&&Config.PreventAwakenBelow3&&player->getMaxHp() <= 3){
			setPlayerMark(player, "AwakenLostMaxHp", 1);
		} else
			loseMaxHp(player, -magnitude, reason);
	} else if (magnitude > 0)
		gainMaxHp(player, magnitude, reason);
	foreach(QString skill_name, player->tag[reason + "_SKILLCANWAKE"].toStringList())
		setPlayerMark(player, "&" + skill_name + "+:+" + reason, 0);
	player->tag.remove(reason + "_SKILLCANWAKE");
	return player->getMark("@waked") > n;
}

void Room::recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion)
{
	if (player->isDead() || recover.recover <= 0) return;

	QVariant data = QVariant::fromValue(recover);
	if (thread->trigger(StartHpRecover,this,player,data)||player->getLostHp()<=0||thread->trigger(PreHpRecover,this,player,data)) return;

	if (player->hasFlag("Global_Dying")&&recover.who){
		QStringList list = player->tag["MyDyingSaver"].toStringList();
		list << recover.who->objectName();
		player->tag["MyDyingSaver"] = list;
	}

	RecoverStruct recover_struct = data.value<RecoverStruct>();
	recover_struct.recover = qMin(player->getMaxHp() - player->getHp(), recover_struct.recover);

	setEmotion(player, "recover_hp");

	JsonArray arg;
	arg << player->objectName() << recover_struct.recover << (recover.card&&recover.card->isKindOf("Peach")?0:1) << 0;
	doBroadcastNotify(S_COMMAND_CHANGE_HP, arg);

	setTag("HpChangedData", data);
	setPlayerProperty(player, "hp", qMin(player->getHp() + recover_struct.recover, player->getMaxHp()));

	if (set_emotion)
		setEmotion(player, "recover");/*

	if (player->getHp() > 0&&player->hasFlag("Global_Dying")){
		setPlayerFlag(player, "-Global_Dying");
		QStringList currentdying = getTag("CurrentDying").toStringList();
		currentdying.removeOne(player->objectName());
		setTag("CurrentDying", currentdying);
	}*/

	thread->trigger(HpRecover, this, player, data);
}

void Room::changeKingdom(ServerPlayer*player, const QString&kingdom)
{
	QVariant data = kingdom;
	if (kingdom.isEmpty()||player->getKingdom()==kingdom||thread->trigger(KingdomChange,this,player,data)) return;
	if (player->getKingdom()==data.toString()) return;
	LogMessage log;
	log.type = "#ChangeKingdom2";
	log.from = player;
	log.arg = player->getKingdom();
	log.arg2 = data.toString();
	static QMap<QString, QString> colorQString;
	if (colorQString.isEmpty()){
		QVariantMap map = GetValueFromLuaState(m_lua, "config", "kingdom_colors").toMap();
		QMapIterator<QString, QVariant> itor(map);
		while (itor.hasNext()){
			itor.next();
			colorQString[itor.key()] = itor.value().toString();
		}
	}
	log.arg3 = colorQString[player->getKingdom()];
	log.arg4 = colorQString[data.toString()];
	sendLog(log);
	setPlayerProperty(player,"kingdom",data);
}

ServerPlayer*Room::getSaver(ServerPlayer*player) const
{
	QStringList list = player->tag["MyDyingSaver"].toStringList();
	if (list.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, m_alivePlayers){
		if (p->objectName() == list.last())
			return p;
	}
	return nullptr;
}

bool Room::cardEffect(const Card*card, ServerPlayer*from, ServerPlayer*to, bool multiple)
{
	CardEffectStruct effect;
	effect.card = card;
	effect.from = from;
	effect.to = to;
	effect.multiple = multiple;
	return cardEffect(effect);
}

bool Room::cardEffect(CardEffectStruct&effect)
{
	bool cancel = false;
	QVariant data = QVariant::fromValue(effect);
	if (effect.to->isAlive()){ // Be care!!!
		// No skills should be triggered here!
		thread->trigger(CardEffect, this, effect.to, data);
		effect = data.value<CardEffectStruct>();
		// Make sure that effectiveness of Slash isn't judged here!
		if (thread->trigger(CardEffected, this, effect.to, data)){
			if (effect.to->hasFlag("Global_NonSkillNullify"))
				effect.to->setFlags("-Global_NonSkillNullify");
			else
				setEmotion(effect.to, "skill_nullify");
		} else
			cancel = true;
		effect = data.value<CardEffectStruct>();
		effect.to->removeQinggangTag(effect.card);
	}
	thread->trigger(PostCardEffected, this, effect.to, data);
	return cancel;
}

bool Room::isJinkEffected(ServerPlayer*user, const Card*jink)
{
	//Q_ASSERT(jink->isKindOf("Jink"));
	QVariant jink_data = QVariant::fromValue(jink);
	return !thread->trigger(JinkEffect, this, user, jink_data);
}

void Room::damage(DamageStruct damage)
{
	if (damage.damage<1 || !damage.to->isAlive()) return;

	try {
		bool prevented = true;
		QVariant data = QVariant::fromValue(damage);
		do {
			if (!damage.tips.contains("ConfirmDamage")){//(!damage.chain&&!damage.transfer){
				if(damage.reason.isEmpty()&&damage.card) damage.reason = damage.card->getSkillName();
				damage.tips << "ConfirmDamage" << QString("STARTDAMAGE:%1").arg(damage.damage);
				data.setValue(damage);
				if (thread->trigger(ConfirmDamage, this, damage.from, data))
					break;
				damage = data.value<DamageStruct>();
			}

			if (thread->trigger(Predamage, this, damage.from, data))
				break;
			damage = data.value<DamageStruct>();

			if (thread->trigger(DamageForseen, this, damage.to, data))
				break;
			damage = data.value<DamageStruct>();

			if (thread->trigger(DamageCaused, this, damage.from, data))
				break;
			damage = data.value<DamageStruct>();

			if (thread->trigger(DamageInflicted, this, damage.to, data))
				break;
			damage = data.value<DamageStruct>();

			prevented = false;
			m_damageStack.push_back(damage);
			setTag("CurrentDamageStruct", data);

			//thread->trigger(PreDamageDone, this, damage.to, data);

			if (damage.from){
				doAnimate(1,damage.from->objectName(),damage.to->objectName());
				addPlayerMark(damage.from, "damage_point_round", damage.damage);
				if (damage.from != damage.to) addPlayerMark(damage.from, "damage_point_turn-Clear", damage.damage);
				if (damage.from->getPhase() == Player::Play) addPlayerMark(damage.from, "damage_point_play_phase", damage.damage);
			}else
				doAnimate(1,"tablePile",damage.to->objectName());

			thread->trigger(DamageDone, this, damage.to, data);
			damage = data.value<DamageStruct>();

			if (damage.from&&!damage.from->hasFlag("Global_DebutFlag"))
				thread->trigger(Damage, this, damage.from, data);

			if (!damage.to->hasFlag("Global_DebutFlag"))
				thread->trigger(Damaged, this, damage.to, data);
		} while (false);

		if(prevented&&damage.to->tag.contains("TransferDamage")){
			// Make sure that the trigger in which 'TransferDamage' tag is set returns TRUE
			DamageStruct transfer = damage.to->tag["TransferDamage"].value<DamageStruct>();
			damage.to->tag.remove("TransferDamage");
			transfer.transfer = true;
			this->damage(transfer);
		}

		damage.prevented = prevented;
		data.setValue(damage);

		thread->trigger(DamageComplete, this, damage.to, data);

		if (!prevented){
			m_damageStack.pop();
			if (m_damageStack.isEmpty()) removeTag("CurrentDamageStruct");
			else setTag("CurrentDamageStruct", QVariant::fromValue(m_damageStack.first()));
		}
	} catch (TriggerEvent triggerEvent){
		if (triggerEvent == StageChange || triggerEvent == TurnBroken){
			removeTag("CurrentDamageStruct");
			m_damageStack.clear();
		}
		throw triggerEvent;
	}
}

bool Room::hasWelfare(const ServerPlayer*player) const
{
	if (mode == "06_3v3")
		return player->isLord() || player->getRole() == "renegade";
	if (mode == "03_1v2")
		return player->isLord();
	if (Config.EnableHegemony || mode == "06_XMode" || mode == "06_ol" || mode == "05_ol")
		return false;
	return player->isLord()&&player_count > 4;
}

ServerPlayer*Room::getFront(ServerPlayer*a, ServerPlayer*b) const
{
	QList<ServerPlayer*> players = getAllPlayers(true);
	if (players.indexOf(a) < players.indexOf(b))
		return a;
	return b;
}

void Room::reconnect(ServerPlayer*player, ClientSocket*socket)
{
	player->setSocket(socket);
	player->setState("online");

	marshal(player);

	broadcastProperty(player, "state");
}

void Room::marshal(ServerPlayer*player)
{
	notifyProperty(player, player, "objectName");
	notifyProperty(player, player, "role");
	notifyProperty(player, player, "flags", "marshalling");

	QStringList player_circle;
	foreach(ServerPlayer*p, m_players){
		if (p != player) p->introduceTo(player);
		player_circle << p->objectName();
	}

	doNotify(player, S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(player_circle));

	doNotify(player, S_COMMAND_START_IN_X_SECONDS, QVariant(0));

	foreach(ServerPlayer*p, m_players){
		notifyProperty(player, p, "general");

		if (p->getGeneral2())
			notifyProperty(player, p, "general2");

		notifyProperty(player, p, "state");
	}

	if (game_state>0)
		doNotify(player, S_COMMAND_GAME_START, JsonUtils::toJsonArray(Sanguosha->getRandomCards()));

	foreach(ServerPlayer*p, m_players)
		p->marshal(player);

	notifyProperty(player, player, "flags", "-marshalling");

	if (game_state>0){
		doNotify(player, S_COMMAND_UPDATE_PILE, QVariant(m_drawPile->length()));

		if (m_fillAGarg.length()>0){
			doNotify(player, S_COMMAND_FILL_AMAZING_GRACE, m_fillAGarg);
			foreach(JsonArray takeAGarg, m_takeAGargs)
				doNotify(player, S_COMMAND_TAKE_AMAZING_GRACE, takeAGarg);
		}

		doNotify(player, S_COMMAND_SYNCHRONIZE_DISCARD_PILE, JsonUtils::toJsonArray(*m_discardPile));
	}
}

void Room::startGame()
{
	m_alivePlayers = m_players;/*
	if (mode == "08_defense"){
		QList<int> next_list;
		next_list << 0 << 7 << 1 << 6 << 2 << 5 << 3 << 4;
		for (int i = 0; i < player_count - 1; i++)
			m_players[next_list[i]]->setNext(m_players[next_list[i+1]]);
		m_players[4]->setNext(m_players.first());
	} else {*/
		for (int i = 0; i < player_count - 1; i++)
			m_players[i]->setNext(m_players[i+1]);
		m_players.last()->setNext(m_players.first());
	//}

	foreach(ServerPlayer*player, m_players){
		if (player->getGeneral()){
			int max_hp = player->getGeneralMaxHp();

			player->setMaxHp(max_hp);
			player->setHp(qMin(player->getGeneralStartHp(),max_hp));

			if (!Config.EnableBasara){
				broadcastProperty(player, "general");
				if(player->getGeneral2())
					broadcastProperty(player, "general2");
			}
			if (mode == "02_1v1")
				doBroadcastNotify(getOtherPlayers(player, true), S_COMMAND_REVEAL_GENERAL, JsonArray() << player->objectName() << player->getGeneralName());				

			broadcastProperty(player, "hp");
			broadcastProperty(player, "maxhp");
			int hujia = player->getGeneralStartHujia();
			if (hujia > 0) addPlayerMark(player, "@HuJia", hujia);

			//if (mode == "06_3v3" || mode == "06_XMode")
				//broadcastProperty(player, "role");
		}
		// setup AI
		AI*ai = cloneAI(player);
		ais << ai;
		player->setAI(ai);
	}

	preparePlayers();

	doBroadcastNotify(S_COMMAND_GAME_START, JsonUtils::toJsonArray(*m_drawPile));

	game_state = 1;

	Server*server = qobject_cast<Server*>(parent());
	foreach(ServerPlayer*player, m_players){
		if (player->getState() == "online")
			server->signupPlayer(player);
	}

	current = m_alivePlayers.first();

	// initialize the place_map and owner_map;
	foreach(int card_id,*m_drawPile)
		setCardMapping(card_id, nullptr, Player::DrawPile);

	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());

	if(!thread) thread = new RoomThread(this);

	if(scenario){
		const ScenarioRule*rule = scenario->getRule();
		if (rule) thread->addTriggerSkill(rule);
	}

	if (mode != "02_1v1"&&mode != "06_3v3"&&mode != "06_XMode")
		_m_roomState.reset();
	connect(thread, SIGNAL(started()), this, SIGNAL(game_start()));

	if (!_virtual) thread->start();
}

bool Room::notifyProperty(ServerPlayer*player, const ServerPlayer*owner, const char*property_name, const QString&value)
{
	JsonArray arg;
	if (owner == player)
		arg << QSanProtocol::S_PLAYER_SELF_REFERENCE_ID;
	else arg << owner->objectName();
	arg << property_name << (value.isEmpty()?owner->property(property_name).toString():value);
	return doNotify(player, S_COMMAND_SET_PROPERTY, arg);
}

bool Room::broadcastProperty(ServerPlayer*owner, const char*property_name, const QString&value)
{
	if (strcmp(property_name, "role") == 0)
		owner->setShownRole(true);
	owner->addProperty(property_name);

	JsonArray arg;
	arg << owner->objectName() << property_name << (value.isEmpty()?owner->property(property_name).toString():value);
	doBroadcastNotify(S_COMMAND_SET_PROPERTY, arg);

	if (strcmp(property_name, "hp") == 0&&owner->getHp() > 0&&owner->hasFlag("Global_Dying")){
		setPlayerFlag(owner, "-Global_Dying");
		QStringList currentdying = getTag("CurrentDying").toStringList();
		currentdying.removeOne(owner->objectName());
		setTag("CurrentDying", currentdying);
	}
	return true;
}

QList<int> Room::drawCardsList(ServerPlayer*player, int n, const QString&reason, bool isTop, bool visible)
{
	if (n<1||(!player->isAlive()&&reason!="reform")) return QList<int>();
	
	DrawStruct draw;
	draw.who = player;
	draw.num = n;
	draw.reason = reason;
	draw.top = isTop;
	draw.visible = visible;
	QVariant data = QVariant::fromValue(draw);
	thread->trigger(DrawNCards, this, draw.who, data);
	draw = data.value<DrawStruct>();
	if (draw.num<1||!draw.who->isAlive()) return QList<int>();
	
	CardsMoveStruct move;
	move.card_ids = getNCards(draw.num, false, draw.top);
	move.open = draw.visible;
	move.from = nullptr;
	move.to = draw.who;
	move.to_place = Player::PlaceHand;
	move.reason = CardMoveReason(CardMoveReason::S_REASON_DRAW, draw.who->objectName(), reason, "");
	moveCardsAtomic(move, visible);
	draw.card_ids = move.card_ids;
	data.setValue(draw);
	thread->trigger(AfterDrawNCards, this, draw.who, data);
	
	return move.card_ids;
}

void Room::drawCards(ServerPlayer*player, int n, const QString&reason, bool isTop, bool visible)
{
	drawCards(QList<ServerPlayer*>()<<player, n, reason, isTop, visible);
}

void Room::drawCards(QList<ServerPlayer*> players, int n, const QString&reason, bool isTop, bool visible)
{
	drawCards(players, QList<int>()<<n, reason, isTop, visible);
}

void Room::drawCards(QList<ServerPlayer*> players, QList<int> n_list, const QString&reason, bool isTop, bool visible)
{
	QVariantList datas;
	QList<CardsMoveStruct> moves;
	for (int i = 0; i < players.length(); i++){
		DrawStruct draw;
		draw.who = players[i];
		if (!draw.who->isAlive()&&reason != "reform") continue;
		if(i<n_list.length()) draw.num = n_list[i];
		else draw.num = n_list.last();
		if (draw.num<1) continue;
		draw.reason = reason;
		draw.top = isTop;
		draw.visible = visible;
		QVariant data = QVariant::fromValue(draw);
		thread->trigger(DrawNCards, this, draw.who, data);
		draw = data.value<DrawStruct>();
		if (draw.num<1||!draw.who->isAlive()) continue;
		
		CardsMoveStruct move;
		move.card_ids = getNCards(draw.num, false, draw.top);
		move.open = draw.visible;
		move.to = draw.who;
		move.to_place = Player::PlaceHand;
		move.reason = CardMoveReason(CardMoveReason::S_REASON_DRAW, draw.who->objectName(), reason, "");
		moves.append(move);
		
		draw.card_ids = move.card_ids;
		datas << QVariant::fromValue(draw);
	}
	moveCardsAtomic(moves, visible);
	
	for (int i = 0; i < moves.length(); i++)
		thread->trigger(AfterDrawNCards, this, (ServerPlayer*)moves[i].to, datas[i]);
}

void Room::throwCard(const Card*card, ServerPlayer*who, ServerPlayer*thrower)
{
	CardMoveReason reason(CardMoveReason::S_REASON_THROW,who?who->objectName():"");
	if (thrower){
		reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
		reason.m_targetId = who ? who->objectName() : "";
		reason.m_playerId = thrower->objectName();
	}
	reason.m_extraData = QVariant::fromValue(card);
	reason.m_skillName = card->getSkillName();
	throwCard(card, reason, who, thrower);
}

void Room::throwCard(const Card*card, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower)
{
	throwCard(card->getSubcards(), reason, who, thrower);
}

void Room::throwCard(QList<int> card_ids, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower)
{
	if (card_ids.isEmpty()) return;

	LogMessage log;
	log.type = "$EnterDiscardPile";
	if (who){
		log.type = "$DiscardCard";
		log.from = who;
		if (thrower){
			log.type = "$DiscardCardByOther";
			log.from = thrower;
			log.to << who;
		}
	}
	log.card_str = ListI2S(card_ids).join("+");
	sendLog(log);
	moveCardsAtomic(CardsMoveStruct(card_ids, who, nullptr, Player::PlaceUnknown, Player::DiscardPile, reason), true);
}

void Room::throwCard(int card_id, ServerPlayer*who, ServerPlayer*thrower)
{
	throwCard(QList<int>() << card_id, "", who, thrower);
}

void Room::throwCard(int card_id, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	throwCard(QList<int>() << card_id, skill_name, who, thrower);
}

void Room::throwCard(const Card*card, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	CardMoveReason reason(CardMoveReason::S_REASON_THROW,who?who->objectName():"",skill_name,"");
	reason.m_extraData = QVariant::fromValue(card);
	if (thrower){
		reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
		reason.m_targetId = who ? who->objectName() : "";
		reason.m_playerId = thrower->objectName();
	}
	throwCard(card, reason, who, thrower);
}

void Room::throwCard(QList<int> card_ids, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	CardMoveReason reason(CardMoveReason::S_REASON_THROW,who?who->objectName():"",skill_name,"");
	if (thrower){
		reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
		reason.m_targetId = who ? who->objectName() : "";
		reason.m_playerId = thrower->objectName();
	}
	throwCard(card_ids, reason, who, thrower);
}

RoomThread*Room::getThread() const
{
	return thread;
}

void Room::moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, bool visible, bool guanxin)
{
	moveCardTo(card, dstPlayer, dstPlace, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, ""), visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const CardMoveReason&reason, bool visible, bool guanxin)
{
	moveCardTo(card, nullptr, dstPlayer, dstPlace, "", reason, visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const CardMoveReason&reason, bool visible, bool guanxin)
{
	moveCardTo(card, srcPlayer, dstPlayer, dstPlace, "", reason, visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const QString&pileName, const CardMoveReason&reason, bool visible, bool guanxin)
{
	CardsMoveStruct move;
	if (card->isVirtualCard()){
		move.card_ids = card->getSubcards();
		if (move.card_ids.isEmpty()) return;
	} else
		move.card_ids << card->getId();
	move.to = dstPlayer;
	move.to_place = dstPlace;
	move.to_pile_name = pileName;
	move.from = srcPlayer;
	move.reason = reason;
	moveCardsAtomic(move, visible, guanxin);
}

static bool CompareByActionOrder_OneTime(CardsMoveOneTimeStruct move1, CardsMoveOneTimeStruct move2)
{
	Player*a = move1.from,*b = move2.from;
	if (a == nullptr) a = move1.to;
	if (b == nullptr) b = move2.to;

	if (a == nullptr || b == nullptr) return a != nullptr;
	if (a == b) return false;
	ServerPlayer*sa = (ServerPlayer*)a;
	return sa->getRoom()->getFront(sa, (ServerPlayer*)b) == sa;
}

static bool CompareByActionOrder(CardsMoveStruct move1, CardsMoveStruct move2)
{
	Player*a = move1.from,*b = move2.from;
	if (a == nullptr) a = move1.to;
	if (b == nullptr) b = move2.to;

	if (a == nullptr || b == nullptr) return a != nullptr;
	if (a == b) return false;
	ServerPlayer*sa = (ServerPlayer*)a;
	return sa->getRoom()->getFront(sa, (ServerPlayer*)b) == sa;
}

void Room::_fillMoveInfo(CardsMoveStruct&moves, int id) const
{
	ServerPlayer*owner = getCardOwner(id);
	if(owner&&moves.from!=owner) moves.from = owner;
	moves.from_place = getCardPlace(id);
	if (moves.from){ // Hand/Equip/Judge
		moves.from_player_name = moves.from->objectName();
		if (moves.from_place == Player::PlaceSpecial || moves.from_place == Player::PlaceTable)
			moves.from_pile_name = moves.from->getPileName(id);
	}
	if (moves.to){
		moves.to_player_name = moves.to->objectName();
		if (moves.to_place == Player::PlaceSpecial || moves.to_place == Player::PlaceTable)
			moves.to_pile_name = moves.to->getPileName(id);
	}
}

QList<CardsMoveStruct> Room::_breakDownCardMoves(QList<CardsMoveStruct> cards_moves)
{
	QList<int> ids;
	QList<CardsMoveStruct> all_sub_moves;
	foreach(CardsMoveStruct move, cards_moves){
		QMap<_MoveSourceClassifier, QList<int> > moveMap;
		foreach(int id, move.card_ids){
			if(ids.contains(id)) continue;
			_fillMoveInfo(move, id);
			moveMap[_MoveSourceClassifier(move)] << id;
			ids << id;
		}
		foreach(_MoveSourceClassifier cls, moveMap.keys()){
			cls.copyTo(move);
			if (move.from!=move.to||move.from_place!=move.to_place||move.from_pile_name!=move.to_pile_name){
				move.card_ids = moveMap[cls];
				all_sub_moves << move;
			}
		}
	}
	return all_sub_moves;
}

QList<CardsMoveOneTimeStruct> Room::_mergeMoves(QList<CardsMoveStruct> cards_moves)
{
	QList<CardsMoveOneTimeStruct> result;
	QMap<_MoveMergeClassifier, QList<CardsMoveStruct> > moveMap;
	foreach(CardsMoveStruct cards_move, cards_moves)
		moveMap[_MoveMergeClassifier(cards_move)].append(cards_move);
	foreach(_MoveMergeClassifier cls, moveMap.keys()){
		CardsMoveOneTimeStruct moveOneTime;
		moveOneTime.from = cls.m_from;
		moveOneTime.to = cls.m_to;
		moveOneTime.reason = cls.m_reason;
		moveOneTime.to_place = cls.m_to_place;
		moveOneTime.to_pile_name = cls.m_to_pile_name;
		foreach(CardsMoveStruct move, moveMap[cls]){
			moveOneTime.card_ids.append(move.card_ids);
			moveOneTime.last_hand_suits << move.last_hand_suits;
			moveOneTime.is_last_handcard = move.is_last_handcard;
			for (int i = 0; i < move.card_ids.length(); i++){
				moveOneTime.from_places.append(move.from_place);
				moveOneTime.from_pile_names.append(move.from_pile_name);
				moveOneTime.open.append(move.open);
			}
		}
		result.append(moveOneTime);
	}
	if (result.length()>1)
		std::sort(result.begin(), result.end(), CompareByActionOrder_OneTime);
	return result;
}

QList<CardsMoveStruct> Room::_separateMoves(QList<CardsMoveOneTimeStruct> moveOneTimes)
{
	QList<CardsMoveStruct> card_moves;/*
	QList<QList<int> > ids;
	QList<_MoveSeparateClassifier> classifiers;
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		for (int i = 0; i < moveOneTime.card_ids.length(); i++){
			_MoveSeparateClassifier cls(moveOneTime, i);
			if (classifiers.contains(cls))
				ids[classifiers.indexOf(cls)] << moveOneTime.card_ids[i];
			else {
				classifiers << cls;
				QList<int> new_ids;
				new_ids << moveOneTime.card_ids[i];
				ids << new_ids;
			}
		}
	}
	foreach(_MoveSeparateClassifier cls, classifiers){
		CardsMoveStruct card_move;
		card_move.from = cls.m_from;
		card_move.to = cls.m_to;
		if (cls.m_from) card_move.from_player_name = cls.m_from->objectName();
		if (cls.m_to) card_move.to_player_name = cls.m_to->objectName();
		card_move.from_place = cls.m_from_place;
		card_move.to_place = cls.m_to_place;
		card_move.from_pile_name = cls.m_from_pile_name;
		card_move.to_pile_name = cls.m_to_pile_name;
		card_move.open = cls.m_open;
		card_move.card_ids = ids.takeFirst();
		card_move.reason = cls.m_reason;

		if (cls.m_from_place==Player::PlaceHand){
			QList<int> hands = cls.m_from->handCards();
			foreach(int id, card_move.card_ids){
				hands.removeOne(id);
				QString str = Sanguosha->getCard(id)->getSuitString();
				if (card_move.last_hand_suits.contains(str)) continue;
				card_move.last_hand_suits << str;
				foreach(int hand_id, hands){
					if (Sanguosha->getCard(hand_id)->getSuitString() == str){
						card_move.last_hand_suits.removeOne(str);
						break;
					}
				}
			}
			card_move.is_last_handcard = hands.isEmpty();
		}
		card_moves.append(card_move);
	}*/
	QMap<_MoveSeparateClassifier, QList<int> > moveMap;
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		for (int i = 0; i < moveOneTime.card_ids.length(); i++)
			moveMap[_MoveSeparateClassifier(moveOneTime,i)] << moveOneTime.card_ids[i];
	}
	foreach(_MoveSeparateClassifier cls, moveMap.keys()){
		//if(moveMap[cls].isEmpty()) continue;
		CardsMoveStruct card_move(moveMap[cls],cls.m_from,cls.m_to,cls.m_from_place,cls.m_to_place,cls.m_reason);
		if (cls.m_from) card_move.from_player_name = cls.m_from->objectName();
		if (cls.m_to) card_move.to_player_name = cls.m_to->objectName();
		card_move.from_pile_name = cls.m_from_pile_name;
		card_move.to_pile_name = cls.m_to_pile_name;
		card_move.open = cls.m_open;
		
		if (cls.m_from_place==Player::PlaceHand){
			QList<int> hands = cls.m_from->handCards();
			foreach(int id, card_move.card_ids){
				hands.removeOne(id);
				QString str = Sanguosha->getCard(id)->getSuitString();
				if (card_move.last_hand_suits.contains(str)) continue;
				card_move.last_hand_suits << str;
				foreach(int hand_id, hands){
					if (Sanguosha->getCard(hand_id)->getSuitString() == str){
						card_move.last_hand_suits.removeOne(str);
						break;
					}
				}
			}
			card_move.is_last_handcard = hands.isEmpty();
		}
		card_moves.append(card_move);
	}
	if (card_moves.length()>1)
		std::sort(card_moves.begin(), card_moves.end(), CompareByActionOrder);
	return card_moves;
}

void Room::moveCardsAtomic(CardsMoveStruct cards_move, bool visible, bool guanxing)
{
	moveCardsAtomic(QList<CardsMoveStruct>() << cards_move, visible, guanxing);
}

void Room::moveCardsAtomic(QList<CardsMoveStruct> cards_moves, bool visible, bool guanxing)
{
	cards_moves = _breakDownCardMoves(cards_moves);
	if(cards_moves.isEmpty()) return;
	QList<CardsMoveOneTimeStruct> moveOneTimes = _mergeMoves(cards_moves);
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(BeforeCardsMove, this, p, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}/*
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		thread->trigger(BeforeCardsMove, this, current, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}*/
	cards_moves = _separateMoves(moveOneTimes);
	if(cards_moves.isEmpty()) return;
	notifyMoveCards(true, cards_moves, visible);
	// First, process remove card
	foreach(CardsMoveStruct move, cards_moves){
		foreach(int id, move.card_ids){
			if (move.from) move.from->removeCard(id, move.from_place);
			switch (move.from_place){
			case Player::DiscardPile: m_discardPile->removeAll(id); break;
			case Player::DrawPile: m_drawPile->removeAll(id); break;
			case Player::PlaceSpecial: table_cards.removeAll(id); break;
			default: break;
			}
			setCardMapping(id, (ServerPlayer*)move.to, move.to_place);
		}
		updateCardsChange(move);
	}
	notifyMoveCards(false, cards_moves, visible);
	foreach(CardsMoveStruct move, cards_moves){
		foreach(int id, move.card_ids){//intReverse(move.card_ids)){
			clearCardTip(id);
			if (visible) setCardFlag(id, "visible");
			else if(move.from_place!=Player::DrawPile) setCardFlag(id, "-visible");
			if (move.to) move.to->addCard(id, move.to_place);
			switch (move.to_place){
			case Player::DiscardPile: m_discardPile->prepend(id); break;
			case Player::DrawPile: m_drawPile->prepend(id); break;
			case Player::PlaceSpecial: table_cards.append(id); break;
			default: break;
			}
		}
		if (move.from_place == Player::DrawPile || move.to_place == Player::DrawPile){
			doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
			if(guanxing&&move.to_place == Player::DrawPile){
				ServerPlayer*from = findChild<ServerPlayer*>(move.reason.m_playerId);
				if (!from) from = (ServerPlayer*)move.from;
				if (from&&from->isAlive()) askForGuanxing(from, getNCards(move.card_ids.length(), false), GuanxingUpOnly, false);
			}
		}
	}
	if(cards_moves.first().reason.m_skillName=="InitialHandCards"&&cards_moves.first().reason.m_reason==CardMoveReason::S_REASON_DRAW){
		askForLuckCard(cards_moves);
	}
	moveOneTimes = _mergeMoves(cards_moves);
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(CardsMoveOneTime, this, p, data);
	}/*
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		thread->trigger(CardsMoveOneTime, this, current, data);
	}*/
}

void Room::moveCardsToEndOfDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible, bool guanxing)
{
	if(card_ids.isEmpty()) return;
	QList<CardsMoveStruct> moves;
	moves << CardsMoveStruct(card_ids, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT_END, player->objectName(), skill_name, ""));
	moves = _breakDownCardMoves(moves);
	QList<CardsMoveOneTimeStruct> moveOneTimes = _mergeMoves(moves);
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(BeforeCardsMove, this, p, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}/*
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		thread->trigger(BeforeCardsMove, this, current, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}*/
	moves = _separateMoves(moveOneTimes);
	if(moves.isEmpty()) return;
	notifyMoveCards(true, moves, visible);
	// First, process remove card
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			if (move.from) move.from->removeCard(id, move.from_place);
			switch (move.from_place){
			case Player::DiscardPile: m_discardPile->removeAll(id); break;
			case Player::DrawPile: m_drawPile->removeAll(id); break;
			case Player::PlaceSpecial: table_cards.removeAll(id); break;
			default: break;
			}
			setCardMapping(id, (ServerPlayer*)move.to, move.to_place);
		}
		updateCardsChange(move);
	}
	notifyMoveCards(false, moves, visible);
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			clearCardTip(id);
			if (visible)
				setCardFlag(id, "visible");
			else if(move.from_place!=Player::DrawPile)
				setCardFlag(id, "-visible");
			if (move.to) move.to->addCard(id, move.to_place);
			switch (move.to_place){
			case Player::DiscardPile: m_discardPile->prepend(id); break;
			case Player::DrawPile: m_drawPile->append(id); break;
			case Player::PlaceSpecial: table_cards.append(id); break;
			default: break;
			}
		}
		if (move.from_place == Player::DrawPile || move.to_place == Player::DrawPile){
			doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
			if(guanxing&&move.to_place == Player::DrawPile){
				ServerPlayer*from = findChild<ServerPlayer*>(move.reason.m_playerId);
				if (!from) from = (ServerPlayer*)move.from;
				if (from&&from->isAlive()) askForGuanxing(from, getNCards(move.card_ids.length(), false, false), GuanxingDownOnly, false);
			}
		}
	}
	moveOneTimes = _mergeMoves(moves);
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(CardsMoveOneTime, this, p, data);
	}/*
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		thread->trigger(CardsMoveOneTime, this, current, data);
	}*/
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, const Card*card, const QString&skill_name, int n, bool visible)
{
	QList<int> card_ids;
	if (card->isVirtualCard())
		card_ids = card->getSubcards();
	else card_ids << card->getId();
	return moveCardsInToDrawpile(player, card_ids, skill_name, n, visible);
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, int card_id, const QString&skill_name, int n, bool visible)
{
	//Q_ASSERT(card_id >= 0);
	return moveCardsInToDrawpile(player, QList<int>() << card_id, skill_name, n, visible);
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, int n, bool visible)
{
	//Q_ASSERT(card_ids.length()>0);
	if (n <= 0) n = qrand()%m_drawPile->length() + 1;
	if (n >= m_drawPile->length()) return moveCardsToEndOfDrawpile(player, card_ids, skill_name, visible);
	QList<CardsMoveStruct> moves;
	moves << CardsMoveStruct(card_ids, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_SHUFFLE, player->objectName(), skill_name, ""));
	moves = _breakDownCardMoves(moves);
	if(moves.isEmpty()) return;
	QList<CardsMoveOneTimeStruct> moveOneTimes = _mergeMoves(moves);
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(BeforeCardsMove, this, p, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}/*
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		thread->trigger(BeforeCardsMove, this, current, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}*/
	moves = _separateMoves(moveOneTimes);
	if(moves.isEmpty()) return;
	notifyMoveCards(true, moves, visible);
	// First, process remove card
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			if (move.from) // Hand/Equip/Judge
				move.from->removeCard(id, move.from_place);
			switch (move.from_place){
			case Player::DiscardPile: m_discardPile->removeAll(id); break;
			case Player::DrawPile: m_drawPile->removeAll(id); break;
			case Player::PlaceSpecial: table_cards.removeAll(id); break;
			default: break;
			}
			setCardMapping(id, (ServerPlayer*)move.to, move.to_place);
		}
		updateCardsChange(move);
	}
	notifyMoveCards(false, moves, visible);
	//QList<int> ncards;
	//for (int m = 1; m < n; m++)
	//ncards << m_drawPile->takeFirst();
	// Now, process add cards
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			//m_drawPile->prepend(id);
			clearCardTip(id);
			if (visible)
				setCardFlag(id, "visible");
			else if(move.from_place!=Player::DrawPile)
				setCardFlag(id, "-visible");
			switch (move.to_place){
			case Player::DiscardPile: m_discardPile->prepend(id); break;
			case Player::DrawPile: m_drawPile->insert(n-1,id); break;
			case Player::PlaceSpecial: table_cards.append(id); break;
			default: break;
			}
		}
	}
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
	//returnToTopDrawPile(ncards);
	moveOneTimes = _mergeMoves(moves);
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(CardsMoveOneTime, this, p, data);
	}
}

void Room::shuffleIntoDrawPile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible)
{
	QList<CardsMoveStruct> moves;
	moves << CardsMoveStruct(card_ids, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_SHUFFLE, player ? player->objectName() : "", skill_name, ""));
	moves = _breakDownCardMoves(moves);
	if(moves.isEmpty()) return;
	QList<CardsMoveOneTimeStruct> moveOneTimes = _mergeMoves(moves);
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(BeforeCardsMove, this, p, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}/*
	for (int i = 0; i < moveOneTimes.length(); i++){
		QVariant data = QVariant::fromValue(moveOneTimes[i]);
		thread->trigger(BeforeCardsMove, this, current, data);
		moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
	}*/
	moves = _separateMoves(moveOneTimes);
	if(moves.isEmpty()) return;
	notifyMoveCards(true, moves, visible);
	// First, process remove card
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			if (move.from) // Hand/Equip/Judge
				move.from->removeCard(id, move.from_place);
			switch (move.from_place){
			case Player::DiscardPile: m_discardPile->removeAll(id); break;
			case Player::DrawPile: m_drawPile->removeAll(id); break;
			case Player::PlaceSpecial: table_cards.removeAll(id); break;
			default: break;
			}
			setCardMapping(id, (ServerPlayer*)move.to, move.to_place);
		}
		updateCardsChange(move);
	}
	notifyMoveCards(false, moves, visible);
	// Now, process add cards
	foreach(CardsMoveStruct move, moves){
		foreach(int id, move.card_ids){
			clearCardTip(id);
			if (visible)
				setCardFlag(id, "visible");
			else if(move.from_place!=Player::DrawPile)
				setCardFlag(id, "-visible");
			if (move.to) move.to->addCard(id, move.to_place);
			switch (move.to_place){
			case Player::DiscardPile: m_discardPile->prepend(id); break;
			case Player::DrawPile: m_drawPile->insert(qrand()%m_drawPile->length(),id); break;
			case Player::PlaceSpecial: table_cards.append(id); break;
			default: break;
			}/*
			QList<int> ncards;
			int n = m_drawPile->length();
			if(n>0){
				n = qrand()%n;
				for (int m = 0; m < n; m++)
					ncards << m_drawPile->takeFirst();
			}
			m_drawPile->prepend(id);
			returnToTopDrawPile(ncards);*/
		}
	}
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
	moveOneTimes = _mergeMoves(moves);
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		foreach(ServerPlayer*p, getAllPlayers())
			thread->trigger(CardsMoveOneTime, this, p, data);
	}/*
	foreach(CardsMoveOneTimeStruct moveOneTime, moveOneTimes){
		QVariant data = QVariant::fromValue(moveOneTime);
		thread->trigger(CardsMoveOneTime, this, current, data);
	}*/
}

void Room::removeDerivativeCards()
{
	foreach(int id,*m_drawPile){
		const Card*card = Sanguosha->getEngineCard(id);
		if (card->objectName().startsWith("_") || card->property("DerivativeCard").toBool()){
			setCardMapping(id, nullptr, Player::PlaceTable);
			m_drawPile->removeAll(id);
		}
	}
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

void Room::updateCardsChange(const CardsMoveStruct&move)
{
	//区域失去
	if(move.from_place==Player::PlaceTable){
		QVariantList ren = tag["ren_pile"].toList();
		if(ren.length()>0){
			foreach(int id, move.card_ids){
				if(ren.contains(QVariant(id)))
					ren.removeAll(id);
			}
			setTag("ren_pile",ren);
		}
	}else if(move.from_place==Player::DiscardPile&&move.to){
		foreach(int cardId, move.card_ids)
			clearCardFlag(cardId);
	}

	//区域获得
	if(move.to_pile_name=="ren_pile"){
		QVariantList ren = tag["ren_pile"].toList();
		foreach(int id, move.card_ids){
			if(!ren.contains(QVariant(id)))
				ren.append(id);
		}
		setTag("ren_pile",ren);
	}
	if(move.to_place==Player::DiscardPile){
		foreach(int cardId, move.card_ids){
			if (Sanguosha->getWrappedCard(cardId)->isModified())
				broadcastResetCard(m_players, cardId);
		}
	}else if (move.to_place == Player::PlaceDelayedTrick){
		foreach(int cardId, move.card_ids){
			WrappedCard*wrapped = Sanguosha->getWrappedCard(cardId);
			if (wrapped->isModified()) broadcastUpdateCard(m_players, cardId, wrapped);/*
			const Card*engine_card = Sanguosha->getEngineCard(cardId);
			if (wrapped->getSuit() != engine_card->getSuit() || wrapped->getNumber() != engine_card->getNumber()){
				Card*trick = Sanguosha->cloneCard(wrapped);
				trick->setNumber(engine_card->getNumber());
				trick->setSuit(engine_card->getSuit());
				wrapped->takeOver(trick);
				broadcastUpdateCard(m_players, cardId, wrapped);
			}*/
		}
	}else if(move.to){
		QList<const Card*> cards;
		foreach(int cardId, move.card_ids)
			cards.append(Sanguosha->getCard(cardId));
		filterCards((ServerPlayer*)move.to, cards, true);
		if(move.to_place == Player::PlaceJudge){
			LogMessage log;
			log.type = "#FilterJudge";
			foreach(const Card*card, cards){
				log.arg = card->getSkillName();
				if(log.arg.isEmpty()) continue;
				log.from = (ServerPlayer*)move.to;
				broadcastSkillInvoke(log.arg,log.from);
				log.card_str = card->toString();
				sendLog(log);
			}
		}
	}
}

bool Room::notifyMoveCards(bool isLostPhase, QList<CardsMoveStruct>&moves, bool visible, QList<ServerPlayer*> players)
{
	// Notify clients
	int moveId;
	if (isLostPhase) moveId = _m_lastMovementId++;
	else moveId = --_m_lastMovementId;
	//Q_ASSERT(_m_lastMovementId >= 0);
	if (players.isEmpty()) players = m_players;
	foreach(ServerPlayer*player, players){
		if (player->isOffline()) continue;
		JsonArray arg;
		arg << moveId;
		for (int i = 0; i < moves.length(); i++){
			moves[i].open = visible || moves[i].isRelevant(player);
			arg << moves[i].toVariant();
		}
		doNotify(player, isLostPhase ? S_COMMAND_LOSE_CARD : S_COMMAND_GET_CARD, arg);
	}
	return true;
}

void Room::giveCard(ServerPlayer*from, ServerPlayer*to, const Card*card, const QString&reason, bool visible)
{
	CardMoveReason reason1(CardMoveReason::S_REASON_GIVE, from->objectName(), to->objectName(), reason, "");
	reason1.m_extraData = QVariant::fromValue(card);
	obtainCard(to, card, reason1, visible);
}

void Room::giveCard(ServerPlayer*from, ServerPlayer*to, QList<int> give_ids, const QString&reason, bool visible)
{
	DummyCard*dummy = new DummyCard(give_ids);
	dummy->deleteLater();
	return giveCard(from, to, dummy, reason, visible);
}

void Room::swapCards(ServerPlayer*first, ServerPlayer*second, const QString&flags, const QString&reason, bool visible)
{
	if(flags.contains("h"))
		swapCards(first,second,first->handCards(),second->handCards(),reason,visible);
	if(flags.contains("e"))
		swapCards(first,second,first->getEquipsId(),second->getEquipsId(),reason,visible);
	if(flags.contains("j"))
		swapCards(first,second,first->getJudgingAreaID(),second->getJudgingAreaID(),reason,visible);
}

void Room::swapCards(ServerPlayer*first, ServerPlayer*second, QList<int> first_ids, QList<int> second_ids, const QString&reason, bool visible)
{
	QList<CardsMoveStruct> exchangeMove;
	foreach(int id, first_ids){
		CardsMoveStruct move(id, first, second, getCardPlace(id), getCardPlace(id),
			CardMoveReason(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), reason, ""));
		exchangeMove << move;
	}
	foreach(int id, second_ids){
		CardsMoveStruct move(id, second, first, getCardPlace(id), getCardPlace(id),
			CardMoveReason(CardMoveReason::S_REASON_SWAP, second->objectName(), first->objectName(), reason, ""));
		exchangeMove << move;
	}
	moveCardsAtomic(exchangeMove, visible);
}

void Room::setPlayerChained(ServerPlayer*player)
{
	if (thread->trigger(ChainStateChange, this, player))
		return;
	setEmotion(player, "chain");
	player->setChained(!player->isChained());
	LogMessage log;
	log.from = player;
	log.type = player->isChained() ? "#PlayerChained" : "#PlayerNotChained";
	sendLog(log);
	broadcastProperty(player, "chained");
	thread->delay(Config.AIDelay / 3);
	thread->trigger(ChainStateChanged, this, player);
}

void Room::setPlayerChained(ServerPlayer*player, bool is_chained)
{
	if (is_chained == player->isChained()) return;
	setPlayerChained(player);
}

void Room::notifySkillInvoked(ServerPlayer*player, const QString&skill_name)
{
	if(player->hasSkill(skill_name,true)||player->hasEquipSkill(skill_name)){
		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_SKILL_INVOKED << player->objectName() << skill_name;
		doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
	}
	QVariant data = "notifyInvoked:"+skill_name;
	thread->trigger(ChoiceMade, this, player, data);
	tag[data.toString()] = true;
}

void Room::broadcastSkillInvoke(const QString&skill_name, const QString&category)
{
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_PLAY_EFFECT << skill_name << category << -1 << "";
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::broadcastSkillInvoke(const QString&skill_name, const ServerPlayer*player)
{
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_PLAY_EFFECT << skill_name << true << -1;
	if (!player) player = findPlayerBySkillName(skill_name, true);
	args << (player ? player->objectName() : "");
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::broadcastSkillInvoke(const QString&skill_name, int type, const ServerPlayer*player)
{
	if (type==0) return;
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_PLAY_EFFECT << skill_name << true << type;
	if (!player) player = findPlayerBySkillName(skill_name, true);
	args << (player ? player->objectName() : "");
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::broadcastSkillInvoke(const QString&skill_name, bool isMale, int type)
{
	if (type==0) return;
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_PLAY_EFFECT << skill_name << isMale << type << "";
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::broadcastSkillInvoke(const Skill*skill, int type, const ServerPlayer*player)
{
	if (skill) broadcastSkillInvoke(skill->objectName(), type, player);
}

void Room::doLightbox(const QString&lightboxName, int duration, int pixelSize)
{
	if (Config.AIDelay < 1) return;
	doAnimate(S_ANIMATE_LIGHTBOX, lightboxName, QString("%1:%2").arg(duration).arg(pixelSize));
	thread->delay(duration / 1.2);
}

void Room::doSuperLightbox(const QString&heroName, const QString&skillName, bool delay)
{
	if (Config.AIDelay<1) return;
	int n = Config.value("HeroSkin/"+heroName).toInt();
	if (n>0){
		QString skin = QString("image/animate/%1_%2.png").arg(heroName).arg(n);
		if (QFile::exists(skin)){
			doAnimate(S_ANIMATE_LIGHTBOX,"skill=Animate:"+skin,skillName);
			if(delay) thread->delay(4500);
			return;
		}else{
			skin = QString("image/heroskin/fullskin/generals/full/%1_%2.jpg").arg(heroName).arg(n);
			if (QFile::exists(skin)){
				doAnimate(S_ANIMATE_LIGHTBOX,"skill="+skin,skillName);
				if(delay) thread->delay(4500);
				return;
			}
		}
	}
	if (QFile::exists("image/animate/"+heroName+".png")){
		doAnimate(S_ANIMATE_LIGHTBOX,"skill=Animate:image/animate/"+heroName+".png",skillName);
		if(delay) thread->delay(4500);
	}else if (QFile::exists("image/fullskin/generals/full/"+heroName+".jpg")){
		doAnimate(S_ANIMATE_LIGHTBOX,"skill=image/fullskin/generals/full/"+heroName+".jpg",skillName);
		if(delay) thread->delay(4500);
	}
}

void Room::doSuperLightbox(ServerPlayer*player, const QString&skillName, bool delay)
{
	if (player->getGeneral2()&&player->getGeneral2()->hasSkill(skillName))
		doSuperLightbox(player->getGeneral2Name(),skillName,delay);
	else doSuperLightbox(player->getGeneralName(),skillName,delay);
}

void Room::doAnimate(QSanProtocol::AnimateType type, const QString&arg1, const QString&arg2,
	QList<ServerPlayer*> players)
{
	// TODO(AI/Future): 预留“沙盒模式/模拟模式”动画副作用保护点。
	// 当前主分支尚未具备 m_simulation_mode / markSimulationEffect 等完整基础设施，
	// 暂不直接引入行为改动，避免破坏现有流程。
	//
	// 未来若引入沙盒机制，可在这里接入（示意）：
	// if (m_simulation_mode) {
	// 	DEBUG_LOG("SANDBOX", QString("doAnimate detected: type=%1, arg1=%2 -> INTERRUPT").arg(type).arg(arg1));
	// 	markSimulationEffect();
	// 	return;
	// }
	//
	// 接入前请先确保：
	// 1) Room 成员与生命周期：m_simulation_mode / m_simulation_has_effect
	// 2) 接口：enterSimulation()/exitSimulation()/markSimulationEffect()/simulationHasEffect()
	// 3) RoomThread 触发链路的异常与回滚策略已联动。

	JsonArray arg;
	arg << (int)type << arg1 << arg2;
	if (players.isEmpty()) players = m_players;
	doBroadcastNotify(players, S_COMMAND_ANIMATE, arg);
}

void Room::preparePlayers()
{
	foreach(ServerPlayer*player, m_players){
		const General*gen = player->getGeneral();
		if(!gen) continue;
		player->setGender(gen->getGender());
		foreach(const Skill*skill, gen->getSkillList()){
			if(player->hasSkill(skill->objectName(),true)) continue;
			player->addSkill(skill->objectName());
			if (skill->inherits("ViewAsEquipSkill")){
				const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill->objectName());
				QString view = vaes->viewAsEquip(player);
				if(view!=""){
					foreach(QString equip_name, view.split(",")){
						if (Sanguosha->getViewAsSkill(equip_name))
							attachSkillToPlayer(player,equip_name);
					}
				}
			}
		}
		gen = player->getGeneral2();
		if (gen){
			foreach(const Skill*skill, gen->getSkillList()){
				if(player->hasSkill(skill->objectName(),true)) continue;
				player->addSkill(skill->objectName());
				if (skill->inherits("ViewAsEquipSkill")){
					const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill->objectName());
					QString view = vaes->viewAsEquip(player);
					if(view!=""){
						foreach(QString equip_name, view.split(",")){
							if (Sanguosha->getViewAsSkill(equip_name))
								attachSkillToPlayer(player,equip_name);
						}
					}
				}
			}
		}
	}
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_PREPARE_SKILL;
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::changePlayerGeneral(ServerPlayer*player, const QString&new_general)
{
	const General*gen = player->getGeneral();
	QStringList sks;
	if (gen){
		foreach(const Skill*skill, gen->getSkillList()){
			sks << skill->objectName();
			player->loseSkill(sks.last());
			if (skill->isChangeSkill()){
				foreach(QString mark, player->getMarkNames()){
					if (mark.startsWith("&" + sks.last())&&mark.endsWith("_num"))
						setPlayerMark(player, mark, 0);
				}
			}
			QString limit_mark = skill->getLimitMark();
			if(limit_mark!="") setPlayerMark(player,limit_mark,0);
			if (skill->inherits("ViewAsEquipSkill")){
				const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(sks.last());
				QString view = vaes->viewAsEquip(player);
				if(view.isEmpty()) continue;
				foreach(QString equip_name, view.split(",")){
					if (Sanguosha->getViewAsSkill(equip_name))
						detachSkillFromPlayer(player,equip_name,true);
				}
			}
		}
	}
	foreach(const Card*c, player->getCards("he")){
		if (sks.contains(c->getSkillName()))
			filterCards(player, QList<const Card*>() << c, true);
	}
	setPlayerProperty(player, "general", new_general);
	gen = player->getGeneral();
	player->setGender(gen->getGender());
	setPlayerProperty(player,"kingdom",gen->getKingdom());
	foreach(const Skill*skill, gen->getSkillList()){
		if (player->hasSkill(skill->objectName(),true)) continue;
		player->addSkill(skill->objectName());
	}
	filterCards(player, player->getCards("he"), false);
}

void Room::changePlayerGeneral2(ServerPlayer*player, const QString&new_general)
{
	const General*gen = player->getGeneral2();
	QStringList sks;
	if (gen){
		foreach(const Skill*skill, gen->getSkillList()){
			sks << skill->objectName();
			player->loseSkill(sks.last());
			if (skill->isChangeSkill()){
				foreach(QString mark, player->getMarkNames()){
					if (mark.startsWith("&" + sks.last())&&mark.endsWith("_num"))
						setPlayerMark(player, mark, 0);
				}
			}
			QString limit_mark = skill->getLimitMark();
			if(limit_mark!="") setPlayerMark(player,limit_mark,0);
			if (skill->inherits("ViewAsEquipSkill")){
				const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(sks.last());
				QString view = vaes->viewAsEquip(player);
				if(view.isEmpty()) continue;
				foreach(QString equip_name, view.split(",")){
					if (Sanguosha->getViewAsSkill(equip_name))
						detachSkillFromPlayer(player,equip_name,true);
				}
			}
		}
	}
	foreach(const Card*c, player->getCards("he")){
		if (sks.contains(c->getSkillName()))
			filterCards(player, QList<const Card*>() << c, true);
	}
	setPlayerProperty(player, "general2", new_general);
	gen = player->getGeneral2();
	if (gen){
		foreach(const Skill*skill, gen->getSkillList()){
			if (player->hasSkill(skill->objectName(),true)) continue;
			player->addSkill(skill->objectName());
		}
	}
	filterCards(player, player->getCards("he"), false);
}

void Room::filterCards(ServerPlayer*player, QList<const Card*> cards, bool refilter)
{
	QList<const FilterSkill*> filterSkills;
	foreach(const Skill*skill, player->getSkillList(true, false)){
		if (skill->inherits("FilterSkill"))
			filterSkills << qobject_cast<const FilterSkill*>(skill);
	}
	//setTag("CurrentFilterCardsPlayer", QVariant::fromValue(player));
	for (int i = 0; i < cards.length(); i++){
		int cardId = cards[i]->getEffectiveId();
		if(getCardPlace(cardId) == Player::PlaceSpecial) continue;
		WrappedCard*wrapped = Sanguosha->getWrappedCard(cardId);
		if(refilter&&wrapped->isModified())
			broadcastResetCard(m_players, cardId);
		const Card*card = nullptr;
		foreach(const FilterSkill*skill, filterSkills){
			if (skill->viewFilter(cards[i])&&player->hasSkill(skill->objectName())){
				if(card) delete card;
				card = skill->viewAs(cards[i]);
			}
		}
		if (card==nullptr) continue;
		wrapped->takeOver((Card*)card);
		if (getCardPlace(cardId) == Player::PlaceHand) notifyUpdateCard(player, cardId, card);
		else broadcastUpdateCard(m_players, cardId, card);
	}
}

void Room::acquireSkill(ServerPlayer*player, const Skill*skill, bool open, bool getmark, bool event_and_log)
{
	//Q_ASSERT(skill != nullptr);
	if (!skill||player->hasSkill(skill->objectName(), true)) return;
	player->acquireSkill(skill->objectName());
	/*if (Sanguosha->getSkill(skill_name)==nullptr)
		Sanguosha->addSkills(QList<const Skill*>() << skill);*/
	if (skill->inherits("TriggerSkill"))
		thread->addTriggerSkill(qobject_cast<const TriggerSkill*>(skill));
	else if (skill->inherits("ViewAsEquipSkill")){
		const ViewAsEquipSkill*vaes = Sanguosha->getViewAsEquipSkill(skill->objectName());
		QString view = vaes->viewAsEquip(player);
		if(view!=""){
			foreach(QString equip_name, view.split(",")){
				if (Sanguosha->getViewAsSkill(equip_name))
					attachSkillToPlayer(player,equip_name);
			}
		}
	}
	if (skill->isVisible()){
		if (getmark&&!skill->getLimitMark().isEmpty())
			setPlayerMark(player, skill->getLimitMark(), 1);
		if (open){
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_ACQUIRE_SKILL << player->objectName() << skill->objectName();
			doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
			if (event_and_log){
				LogMessage log;
				log.from = player;
				log.type = "#AcquireSkill";
				log.arg = skill->objectName();
				sendLog(log);
			}
		}
		foreach(const Skill*rs, Sanguosha->getRelatedSkills(skill->objectName()))
			acquireSkill(player, rs);
		if (event_and_log){
			QVariant data = skill->objectName();
			thread->trigger(EventAcquireSkill, this, player, data);
		}
	}
}

void Room::acquireSkill(ServerPlayer*player, const QString&skill_name, bool open, bool getmark, bool event_and_log)
{
	acquireSkill(player, Sanguosha->getSkill(skill_name), open, getmark, event_and_log);
}

void Room::setTag(const QString&key, const QVariant&value)
{
	tag.insert(key, value);
	if (scenario) scenario->onTagSet(this, key);
}

QVariant Room::getTag(const QString&key) const
{
	return tag.value(key);
}

void Room::removeTag(const QString&key)
{
	tag.remove(key);
}

void Room::setEmotion(ServerPlayer*target, const QString&emotion)
{
	JsonArray arg;
	arg << target->objectName();
	arg << (emotion.isEmpty() ? QString(".") : emotion);
	doBroadcastNotify(S_COMMAND_SET_EMOTION, arg);
}

void Room::changeTableBg(const QString&tableBg)
{
	QString tb = QString("image/system/backdrop/%1.jpg").arg(tableBg);
	if (!QFile::exists(tb)) return;
	JsonArray arg;
	arg << tb;
	doBroadcastNotify(S_COMMAND_CHANGE_TABLE_BG, arg);
}

void Room::activate(ServerPlayer*player, CardUseStruct&card_use)
{
	tryPause();

	if (player->getPhase()!=Player::Play||player->hasFlag("Global_PlayPhaseTerminated")){
		setPlayerFlag(player, "-Global_PlayPhaseTerminated");
		return;
	}

	notifyMoveFocus(player, S_COMMAND_PLAY_CARD);
	_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
	_m_roomState.setCurrentCardUsePattern("");

	card_use.from = player;

	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		ai->activate(card_use);
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());/*
		else if(Config.OperationTimeout*1000-timer.elapsed()<0)
			card_use.card = nullptr;*/
	} else {
		bool success = doRequest(player, S_COMMAND_PLAY_CARD, player->objectName(), true);

		if (m_surrenderRequestReceived){
			makeSurrender(player);
			if (game_state>0) activate(player, card_use);
		} else if(Config.EnableCheat&&makeCheat(player)){
			if (player->isAlive()) activate(player, card_use);
		}else if(success){
			const QVariant&clientReply = player->getClientReply();
			//if (clientReply.isNull()) return;
			if (!card_use.tryParse(clientReply, this)){
				JsonArray client = clientReply.value<JsonArray>();
				emit room_message(tr("Card cannot be parsed:\n %1").arg(client[0].toString()));
			}
		}else{
			ai = player->getAI();
			if(ai) ai->activate(card_use);
		}
	}
	/*if (!card_use.isValid("")) return;
	QVariant data = QVariant::fromValue(card_use);
	thread->trigger(ChoiceMade, this, player, data);*/
}

void Room::askForLuckCard(QList<CardsMoveStruct>&cards_moves)
{
	int luck = Config.value("LuckCardTimes").toInt();
	if (luck < 0) luck = INT_MAX;

	tryPause();

	QList<ServerPlayer*> players;
	foreach(CardsMoveStruct move, cards_moves)
		players << (ServerPlayer*)move.to;
	for (int i = 0; i < luck; i++){
		foreach(ServerPlayer*player, players){
			if (player->isOnline()) player->m_commandArgs = QVariant();
			else players.removeOne(player);
		}
		if (players.isEmpty())
			break;
		Countdown countdown;
		countdown.max = ServerInfo.getCommandTimeout(S_COMMAND_LUCK_CARD, S_CLIENT_INSTANCE);
		countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
		notifyMoveFocus(players, S_COMMAND_LUCK_CARD, countdown);
		doBroadcastRequest(players, S_COMMAND_LUCK_CARD);

		LogMessage log;
		log.type = "#UseLuckCard";
		foreach(ServerPlayer*player, players){
			if(player->m_isClientResponseReady){
				const QVariant&clientReply = player->getClientReply();
				if(JsonUtils::isBool(clientReply)&&clientReply.toBool()){
					log.from = player;
					sendLog(log);
					continue;
				}
			}
			players.removeOne(player);
		}

		QList<int> draw_list;
		foreach(ServerPlayer*player, players){
			draw_list << player->getHandcardNum();

			CardsMoveStruct move;
			move.from = player;
			move.from_place = Player::PlaceHand;
			move.to_place = Player::DrawPile;
			move.card_ids = player->handCards();
			move.reason = CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "luck_card", "");
			QList<CardsMoveStruct> moves;
			moves << move;
			moves = _breakDownCardMoves(moves);

			QList<ServerPlayer*> tmp_list;
			tmp_list << player;

			notifyMoveCards(true, moves, false, tmp_list);
			foreach(int id, move.card_ids)
				player->removeCard(id, Player::PlaceHand);

			foreach(int id, move.card_ids)
				setCardMapping(id, nullptr, Player::DrawPile);
			//updateCardsChange(move);

			notifyMoveCards(false, moves, false, tmp_list);
			foreach(int id, move.card_ids)
				m_drawPile->insert(qrand()%m_drawPile->length(),id);
				//m_drawPile->append(id);
		}
		//qShuffle(*m_drawPile);
		foreach(ServerPlayer*player, players){
			CardsMoveStruct move;
			move.from_place = Player::DrawPile;
			move.to = player;
			move.to_place = Player::PlaceHand;
			move.card_ids = getNCards(draw_list.takeFirst(), false);
			QList<CardsMoveStruct> moves;
			moves << move;
			moves = _breakDownCardMoves(moves);

			QList<ServerPlayer*> tmp_list;
			tmp_list << player;

			notifyMoveCards(true, moves, false, tmp_list);

			foreach(int id, move.card_ids)
				setCardMapping(id, player, Player::PlaceHand);
			updateCardsChange(move);

			notifyMoveCards(false, moves, false, tmp_list);
			foreach(int id, move.card_ids)
				player->addCard(id, Player::PlaceHand);
		}
		doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
	}
	for (int i = 0; i < cards_moves.length(); i++){
		if (!cards_moves[i].to) {
			continue;
		}
		cards_moves[i].card_ids = cards_moves[i].to->handCards();
		//cards_moves[i].to->setProperty("InitialHandCards", ListI2V(cards_moves[i].card_ids));
		this->setPlayerProperty(this->findPlayerByObjectName(cards_moves[i].to->objectName()), "InitialHandCards", ListI2V(cards_moves[i].card_ids));
	}
}

Card::Suit Room::askForSuit(ServerPlayer*player, const QString&reason)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_SUIT);

	Card::Suit suit = Card::AllSuits[qrand() % 4];
	AI*ai = player->getAI();
	if (ai) suit = ai->askForSuit(reason);
	else if (doRequest(player, S_COMMAND_CHOOSE_SUIT, QVariant(), true)){
		if (player->getClientReply().toString() == "spade") suit = Card::Spade;
		else if (player->getClientReply().toString() == "club") suit = Card::Club;
		else if (player->getClientReply().toString() == "heart") suit = Card::Heart;
		else if (player->getClientReply().toString() == "diamond") suit = Card::Diamond;
	}else{
		ai = player->getAI();
		if (ai) suit = ai->askForSuit(reason);
	}
	return suit;
}

QString Room::askForKingdom(ServerPlayer*player, const QString&reason, QStringList kingdoms, bool send_log)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_KINGDOM);
	if (reason.endsWith("_ChooseKingdom")){
		QString new_reason = reason;
		new_reason.remove("_ChooseKingdom");
		const General*general = Sanguosha->getGeneral(new_reason);
		if (general) kingdoms = general->getKingdoms().split("+");
	}
	if(kingdoms.isEmpty()||kingdoms.first().isEmpty()){
		kingdoms = Sanguosha->getKingdoms();
		//kingdoms.removeOne(player->getKingdom());
		kingdoms.removeOne("demon");
		kingdoms.removeOne("god");
	}
	if (kingdoms.isEmpty()) return "god";
	QString result = kingdoms.first();
	AI*ai = player->getAI();
	if (ai){
		if (reason.isEmpty()||reason.contains("gamerule_")) result = ai->askForKingdom(kingdoms);
		else result = ai->askForChoice(reason, kingdoms.join("+"), QVariant());
	} else {
		JsonArray arg;
		arg << kingdoms.join("+");
		if (doRequest(player, S_COMMAND_CHOOSE_KINGDOM, arg, true)){
			const QVariant&clientReply = player->getClientReply();
			if (JsonUtils::isString(clientReply))
				result = clientReply.toString();
		}else{
			ai = player->getAI();
			if(ai){
				if (reason.isEmpty()||reason.contains("gamerule_")) result = ai->askForKingdom(kingdoms);
				else result = ai->askForChoice(reason, kingdoms.join("+"), QVariant());
			}
		}
	}
	if(send_log){
		LogMessage log;
		log.type = "#ChooseKingdom";
		log.from = player;
		log.arg = result;
		sendLog(log);
	}
	return result;
}

QString Room::askForKingdom(ServerPlayer*player, const QString&reason, const QString&kingdoms, bool send_log)
{
	return askForKingdom(player,reason,kingdoms.split("+"),send_log);
}

Card*Room::askForDiscard(ServerPlayer*player, const QString&reason, int discard_num, int min_num,
	bool optional, bool include_equip, const QString&prompt, const QString&pattern, const QString&skill_name)
{
	if (!player->isAlive())
		return nullptr;
	tryPause();
	notifyMoveFocus(player, S_COMMAND_DISCARD_CARD);
	min_num = qMin(min_num, discard_num);
	QList<int> to_discard, jilei_list;
	QStringList ignore_list;
	QList<const Card*> cards = player->getHandcards();
	if (include_equip) cards << player->getEquips();
	foreach(const Card*c, cards){
		if (reason=="gamerule"&&player->isCardLimited(c,Card::MethodIgnore)){
			setPlayerCardLimitation(player, "discard", c->toString(), false);
			ignore_list << c->toString();
		}else if(Sanguosha->matchExpPattern(pattern,player, c)){
			if(player->isJilei(c,!include_equip)) jilei_list << c->getId();
			else to_discard << c->getId();
		}
	}
	if(to_discard.length()>min_num||(optional&&to_discard.length()==min_num)){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			to_discard = ai->askForDiscard(reason, discard_num, min_num, optional, include_equip, pattern);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			to_discard.clear();
			JsonArray ask_str;
			ask_str << discard_num << min_num << optional << include_equip << prompt << pattern;
			if(doRequest(player, S_COMMAND_DISCARD_CARD, ask_str, true)){
				JsonUtils::tryParse(player->getClientReply(),to_discard);
			}else{
				ai = player->getAI();
				if(ai) to_discard = ai->askForDiscard(reason, discard_num, min_num, optional, include_equip, pattern);
				//else if(!optional) to_discard = player->forceToDiscard(min_num, include_equip, true, pattern);
			}
		}
	}
	foreach(QString str, ignore_list)
		removePlayerCardLimitation(player, "discard", str);
	if(to_discard.length()<min_num){
		if(optional)
			to_discard.clear();
		else{
			qShuffle(cards);
			foreach(const Card*card, cards){
				if(to_discard.contains(card->getId())||jilei_list.contains(card->getId())||ignore_list.contains(card->toString())) continue;
				if(Sanguosha->matchExpPattern(pattern,player,card)){
					to_discard << card->getId();
					if(to_discard.length()>=min_num) break;
				}
			}
			if(to_discard.length()<min_num&&jilei_list.length()>0){
				foreach(int id, jilei_list){
					WrappedCard*wrapped = Sanguosha->getWrappedCard(id);
					if (wrapped->isModified()) broadcastUpdateCard(m_players, id, wrapped);
					//else broadcastResetCard(m_players, id);
					setCardFlag(id, "visible");
				}
				LogMessage log;
				log.type = "$JileiShowAllCards";
				log.from = player;
				log.card_str = ListI2S(jilei_list).join("+");
				sendLog(log);
				JsonArray gongxinArgs;
				gongxinArgs << player->objectName() << false << JsonUtils::toJsonArray(jilei_list);
				doBroadcastNotify(S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
				QVariant data = log.card_str;
				thread->trigger(ShowCards, this, player, data);
			}
		}
	}
	if (to_discard.isEmpty()) return nullptr;
	CardMoveReason mreason(CardMoveReason::S_REASON_THROW, player->objectName(), reason, "");
	if (reason == "gamerule") mreason.m_reason = CardMoveReason::S_REASON_RULEDISCARD;
	if (skill_name.isEmpty()){
		throwCard(to_discard, mreason, player);
	} else {
		LogMessage log;
		log.from = player;
		log.arg = skill_name;
		if(skill_name.contains("$")){
			ignore_list = skill_name.split("$");
			log.arg = ignore_list.first();
			broadcastSkillInvoke(log.arg,ignore_list.last().toInt(),player);
		}
		log.type = "$DiscardCardWithSkill";
		log.card_str = ListI2S(to_discard).join("+");
		sendLog(log);
		notifySkillInvoked(player, log.arg);
		moveCardsAtomic(CardsMoveStruct(to_discard, nullptr, Player::DiscardPile, mreason), true);
	}
	DummyCard*dummy_card = new DummyCard(to_discard);
	QVariant data = QString("cardDiscard:%1:%2").arg(reason).arg(dummy_card->toString());
	thread->trigger(ChoiceMade, this, player, data);
	dummy_card->deleteLater();
	return dummy_card;
}

Card*Room::askForExchange(ServerPlayer*player, const QString&reason, int exchange_num, int min_num,
	bool include_equip, const QString&prompt, bool optional, const QString&pattern)
{
	if (!player->isAlive())
		return nullptr;
	tryPause();
	notifyMoveFocus(player, S_COMMAND_EXCHANGE_CARD);

	if(player->getCardCount(include_equip)<1) return nullptr;
	QList<const Card*> cards = player->getHandcards();
	if (include_equip) cards << player->getEquips();
	min_num = qMin(min_num, exchange_num);
	QList<int> to_exchange;
	foreach(const Card*card, cards){
		if(Sanguosha->matchExpPattern(pattern,player,card)){
			to_exchange << card->getId();
			if(to_exchange.length()>min_num) break;
		}
	}
	if(to_exchange.length()>min_num||(optional&&to_exchange.length()==min_num)){
		AI*ai = player->getAI();
		player->setFlags("Global_AIDiscardExchanging");
		if (ai){// share the same callback interface
			QElapsedTimer timer;
			timer.start();
			to_exchange = ai->askForDiscard(reason, exchange_num, min_num, optional, include_equip, pattern);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			to_exchange.clear();
			JsonArray exchange_str;
			exchange_str << exchange_num << min_num << include_equip << prompt << optional << pattern;
			if (doRequest(player, S_COMMAND_EXCHANGE_CARD, exchange_str, true))
				JsonUtils::tryParse(player->getClientReply(), to_exchange);
			else{
				ai = player->getAI();
				if(ai) to_exchange = ai->askForDiscard(reason, exchange_num, min_num, optional, include_equip, pattern);
				//else if(!optional) to_exchange = player->forceToDiscard(min_num, include_equip, false, pattern);
			}
		}
		player->setFlags("-Global_AIDiscardExchanging");
	}
	if (to_exchange.length()<min_num){
		if(optional)
			to_exchange.clear();
		else{
			qShuffle(cards);
			foreach(const Card*card, cards){
				if(to_exchange.contains(card->getId())) continue;
				if(Sanguosha->matchExpPattern(pattern,player,card)){
					to_exchange << card->getId();
					if(to_exchange.length()>=min_num) break;
				}
			}
		}
	}
	if(to_exchange.isEmpty()) return nullptr;
	DummyCard*card = new DummyCard(to_exchange);
	card->deleteLater();
	return card;
}

void Room::setCardMapping(int card_id, ServerPlayer*owner, Player::Place place)
{
	owner_map.insert(card_id, owner);
	place_map.insert(card_id, place);
}

ServerPlayer*Room::getCardOwner(int card_id) const
{
	return owner_map.value(card_id,nullptr);
}

Player::Place Room::getCardPlace(int card_id) const
{
	if(card_id<0) return Player::PlaceUnknown;
	return place_map.value(card_id,Player::PlaceTable);
}

ServerPlayer*Room::getLord() const
{
	if (mode == "04_2v2") return nullptr;
	foreach(ServerPlayer*player, m_players){
		if (player->getRole() == "lord")
			return player;
	}
	return nullptr;
}

QList<int> Room::askForGuanxing(ServerPlayer*zhuge, const QList<int>&cards, GuanxingType guanxing_type, bool sendLod)
{
	if (cards.isEmpty()) return cards;

	tryPause();
	notifyMoveFocus(zhuge, S_COMMAND_SKILL_GUANXING);
	QList<int> top_cards, bottom_cards;

	if(cards.length() < 2&&guanxing_type != GuanxingBothSides){
		if(guanxing_type == GuanxingUpOnly) top_cards = cards;
		else bottom_cards = cards;
	}else{
		AI*ai = zhuge->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			ai->askForGuanxing(cards, top_cards, bottom_cards, guanxing_type);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			JsonArray guanxingArgs;
			guanxingArgs << JsonUtils::toJsonArray(cards) << guanxing_type;
			if(doRequest(zhuge, S_COMMAND_SKILL_GUANXING, guanxingArgs, true)){
				guanxingArgs = zhuge->getClientReply().value<JsonArray>();
				if (guanxingArgs.size() > 1){
					JsonUtils::tryParse(guanxingArgs[0], top_cards);
					JsonUtils::tryParse(guanxingArgs[1], bottom_cards);
					if (guanxing_type == GuanxingDownOnly){
						bottom_cards << top_cards;
						top_cards.clear();
					}
				}
			}else{
				ai = zhuge->getAI();
				if(ai) ai->askForGuanxing(cards, top_cards, bottom_cards, guanxing_type);
			}
		}
	}/*
	if ((top_cards+bottom_cards).toSet()!=cards.toSet()){
		if (guanxing_type == GuanxingDownOnly){
			bottom_cards = cards;
			top_cards.clear();
		} else {
			top_cards = cards;
			bottom_cards.clear();
		}
	}*/
	if(sendLod){
		LogMessage log;
		log.from = zhuge;
		//if (guanxing_type == GuanxingBothSides){
			log.type = "#GuanxingResult";
			log.arg = QString::number(top_cards.length());
			log.arg2 = QString::number(bottom_cards.length());
			sendLog(log);
		//}
		if (top_cards.length()>0){
			log.type = "$GuanxingTop";
			log.card_str = ListI2S(top_cards).join("+");
			sendLog(log, zhuge);
		}
		if (bottom_cards.length()>0){
			log.type = "$GuanxingBottom";
			log.card_str = ListI2S(bottom_cards).join("+");
			sendLog(log, zhuge);
		}
	}
	if(getCardPlace(cards.first())==Player::DrawPile){
		QList<int> tops = top_cards;
		while (tops.length()>0){
			int id = tops.takeLast();
			m_drawPile->removeAll(id);
			m_drawPile->prepend(id);
		}
		while (bottom_cards.length()>0){
			int id = bottom_cards.takeLast();
			m_drawPile->removeAll(id);
			m_drawPile->append(id);
		}/*
		QListIterator<int> i(top_cards);
		i.toBack();
		while (i.hasPrevious()){
			int id = i.previous();
			m_drawPile->removeAll(id);
			m_drawPile->prepend(id);
		}
		i = bottom_cards;
		while (i.hasNext()){
			int id = i.next();
			m_drawPile->removeAll(id);
			m_drawPile->append(id);
		}*/
		doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
	}
	return top_cards;
}

void Room::returnToTopDrawPile(QList<int> cards)
{
	while (cards.length()>0){
		int id = cards.takeLast();
		m_drawPile->removeAll(id);
		setCardMapping(id, nullptr, Player::DrawPile);
		m_drawPile->prepend(id);
	}
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

void Room::returnToEndDrawPile(QList<int> cards)
{
	while (cards.length()>0){
		int id = cards.takeLast();
		m_drawPile->removeAll(id);
		setCardMapping(id, nullptr, Player::DrawPile);
		m_drawPile->append(id);
	}
	doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

int Room::doGongxin(ServerPlayer*shenlvmeng, ServerPlayer*target, QList<int> enabled_ids, QString skill_name)
{
	//Q_ASSERT(!target->isKongcheng());
	tryPause();
	notifyMoveFocus(shenlvmeng, S_COMMAND_SKILL_GONGXIN);

	QList<int> hand = target->handCards();
	if (hand.length()>0){
		LogMessage log;
		log.type = "$ViewAllCards";
		log.from = shenlvmeng;
		log.to << target;
		log.card_str = ListI2S(hand).join("+");
		sendLog(log, shenlvmeng);
		QVariant data = QString("viewCards:%1:%2").arg(target->objectName()).arg(log.card_str);
		thread->trigger(ChoiceMade, this, shenlvmeng, data);
	}

	int card_id = -1;
	shenlvmeng->tag[skill_name] = QVariant::fromValue(target);
	AI*ai = shenlvmeng->getAI();
	if (ai){
		card_id = ai->askForAG(enabled_ids, true, skill_name);
	} else {/*
		foreach(int cardId, hand){
			WrappedCard*card = Sanguosha->getWrappedCard(cardId);
			if(card->isModified()) notifyUpdateCard(shenlvmeng,cardId,card);
			//else notifyResetCard(shenlvmeng, cardId);
		}*/
		JsonArray args;
		args << target->objectName() << true << JsonUtils::toJsonArray(hand) << JsonUtils::toJsonArray(enabled_ids);
		if(doRequest(shenlvmeng, S_COMMAND_SKILL_GONGXIN, args, true)){
			const QVariant&clientReply = shenlvmeng->getClientReply();
			if(JsonUtils::isNumber(clientReply))
				card_id = clientReply.toInt();
		}else{
			ai = shenlvmeng->getAI();
			if(ai) card_id = ai->askForAG(enabled_ids, true, skill_name);
		}
	}
	return card_id; // Do remember to remove the tag later!
}

const Card*Room::askForPindian(ServerPlayer*player, ServerPlayer*from, const QString&reason)
{
	if (!from->isAlive() || !player->isAlive())
		return nullptr;
	//Q_ASSERT(!player->isKongcheng());
	tryPause();
	notifyMoveFocus(player, S_COMMAND_PINDIAN);

	if (player->getHandcardNum() == 1)
		return player->getHandcards().first();
	const Card*card = nullptr;
	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		card = ai->askForPindian(from, reason);
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	}else if (doRequest(player, S_COMMAND_PINDIAN, JsonArray() << from->objectName() << player->objectName(), true)){
		JsonArray clientReply = player->getClientReply().value<JsonArray>();
		if (clientReply.size()>0) card = Card::Parse(clientReply[0].toString());
	}else{
		ai = player->getAI();
		if(ai) card = ai->askForPindian(from, reason);
	}
	if(!card) card = player->getRandomHandCard();
	return card;
}

QList<const Card*> Room::askForPindianRace(ServerPlayer*from, ServerPlayer*to, const QString&reason)
{
	if (!from->isAlive() || !to->isAlive())
		return QList<const Card*>() << nullptr << nullptr;
	//Q_ASSERT(!from->isKongcheng()&&!to->isKongcheng());
	tryPause();
	Countdown countdown;
	countdown.max = ServerInfo.getCommandTimeout(S_COMMAND_PINDIAN, S_CLIENT_INSTANCE);
	countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
	notifyMoveFocus(QList<ServerPlayer*>() << from << to, S_COMMAND_PINDIAN, countdown);

	const Card*from_card = nullptr,*to_card = nullptr;

	if (from->getHandcardNum() == 1)
		from_card = from->getHandcards().first();
	if (to->getHandcardNum() == 1)
		to_card = to->getHandcards().first();

	QElapsedTimer timer;
	timer.start();
	if (!from_card){
		AI*ai = from->getAI();
		if (ai) from_card = ai->askForPindian(from, reason);
	}
	if (!to_card){
		AI*ai = to->getAI();
		if (ai) to_card = ai->askForPindian(from, reason);
	}
	QList<ServerPlayer*> players;
	if (!from_card){
		JsonArray arr;
		arr << from->objectName() << to->objectName();
		from->m_commandArgs = arr;
		players << from;
	}
	if (!to_card){
		JsonArray arr;
		arr << from->objectName() << to->objectName();
		to->m_commandArgs = arr;
		players << to;
	}
	if(players.isEmpty()){
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	}else{
		doBroadcastRequest(players, S_COMMAND_PINDIAN);
		foreach(ServerPlayer*player, players){
			const Card*card = nullptr;
			if(player->m_isClientResponseReady){
				JsonArray clientReply = player->getClientReply().value<JsonArray>();
				if(clientReply.size()>0) card = Card::Parse(clientReply[0].toString());
			}else{
				AI*ai = player->getAI();
				if (ai) card = ai->askForPindian(from, reason);
			}
			if (card==nullptr) card = player->getRandomHandCard();
			if (player == from) from_card = card;
			else to_card = card;
		}
	}
	return QList<const Card*>() << from_card << to_card;
}

ServerPlayer*Room::askForPlayerChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const QString&skillName,
	const QString&prompt, bool optional, bool notify_skill)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_PLAYER);
	if(targets.isEmpty()) return nullptr;
	ServerPlayer*choice = targets.first();
	LogMessage log;
	log.arg = skillName;
	if(skillName.contains("$"))
		log.arg = skillName.split("$").first();
	if(targets.length()>1||optional){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			choice = ai->askForPlayerChosen(targets, log.arg);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			choice = nullptr;
			JsonArray req, req_targets;
			foreach(ServerPlayer*target, targets)
				req_targets << target->objectName();
			req << QVariant(req_targets) << log.arg << prompt << 1 << (optional ? 0 : 1);
			if(doRequest(player, S_COMMAND_CHOOSE_PLAYER, req, true)){
				const QVariant&clientReply = player->getClientReply();
				if(JsonUtils::isString(clientReply))
					choice = findChild<ServerPlayer*>(clientReply.toString());
			}else{
				ai = player->getAI();
				if(ai) choice = ai->askForPlayerChosen(targets, log.arg);
			}
		}
		if (!choice&&!optional)
			choice = targets.at(qrand() % targets.length());
	}
	if (choice){
		if (notify_skill){
			if(skillName.contains("$"))
				broadcastSkillInvoke(log.arg,skillName.split("$").last().toInt(),player);
			doAnimate(S_ANIMATE_INDICATE, player->objectName(), choice->objectName());
			log.type = "#ChoosePlayerWithSkill";
			log.from = player;
			log.to << choice;
			sendLog(log);
			notifySkillInvoked(player, log.arg);
			QVariant decisionData = "skillInvoke:" + log.arg + ":yes";
			thread->trigger(ChoiceMade, this, player, decisionData);
		}
		QVariant data = QString("playerChosen:%1:%2").arg(log.arg).arg(choice->objectName());
		thread->trigger(ChoiceMade, this, player, data);
	}
	return choice;
}

QList<ServerPlayer*> Room::askForPlayersChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const QString&skillName,
					int min_num, int max_num, const QString&prompt, bool notify_skill, bool sort_ActionOrder)
{
	tryPause();
	min_num = qMin(min_num, targets.length());
	max_num = qMin(max_num, targets.length());
	notifyMoveFocus(player, S_COMMAND_CHOOSE_PLAYER);
	LogMessage log;
	log.arg = skillName;
	log.to = targets;
	if(skillName.contains("$"))
		log.arg = skillName.split("$").first();
	if (targets.length()>min_num){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			log.to = ai->askForPlayersChosen(targets, log.arg, max_num, min_num);
			if (Config.AIDelay>timer.elapsed())
				thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			log.to.clear();
			JsonArray req, req_targets;
			foreach(ServerPlayer*target, targets)
				req_targets << target->objectName();
			req << QVariant(req_targets) << log.arg << prompt << max_num << min_num;
			if(doRequest(player, S_COMMAND_CHOOSE_PLAYER, req, true)){
				const QVariant&clientReply = player->getClientReply();
				if(JsonUtils::isString(clientReply)){
					foreach(const QString&name, clientReply.toString().split("+")){
						ServerPlayer*p = findChild<ServerPlayer*>(name);
						if (targets.contains(p)) log.to << p;
					}
				}
			}else{
				ai = player->getAI();
				if(ai) log.to = ai->askForPlayersChosen(targets, log.arg, max_num, min_num);
			}
		}
		if (log.to.length() < min_num){
			QList<ServerPlayer*> copy = targets;
			foreach(ServerPlayer*p, log.to)
				copy.removeOne(p);
			while (log.to.length()<min_num&&copy.length()>0)
				log.to << copy.takeAt(qrand()%copy.length());
		}else if(min_num<0&&log.to.length()!=max_num)
			log.to.clear();
	}
	if (log.to.length()>0){
		if (sort_ActionOrder)
			sortByActionOrder(log.to);
		if (notify_skill){
			if(skillName.contains("$"))
				broadcastSkillInvoke(log.arg,skillName.split("$").last().toInt(),player);
			foreach(ServerPlayer*p, log.to)
				doAnimate(S_ANIMATE_INDICATE, player->objectName(), p->objectName());
			log.type = "#ChoosePlayerWithSkill";
			log.from = player;
			sendLog(log);
			notifySkillInvoked(player, log.arg);
			QVariant decisionData = "skillInvoke:" + log.arg + ":yes";
			thread->trigger(ChoiceMade, this, player, decisionData);
		}
		QStringList names;
		foreach(ServerPlayer*p, log.to)
			names.append(p->objectName());
		QVariant data = QString("playerChosen:%1:%2").arg(log.arg).arg(names.join("+"));
		thread->trigger(ChoiceMade, this, player, data);
	}
	return log.to;
}

void Room::_setupChooseGeneralRequestArgs(ServerPlayer*player)
{
	QStringList selected = player->getSelected();

	JsonArray options = JsonUtils::toJsonArray(selected).value<JsonArray>();
	if (Config.EnableBasara) options.append("anjiang(lord)");
	else if(getLord()&&mode!="03_1v2") options.append(getLord()->getGeneralName()+"(lord)");
	player->m_commandArgs = options;
}

QString Room::askForGeneral(ServerPlayer*player, const QStringList&generals, const QString&default_choice)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_GENERAL);

	if(generals.isEmpty()) return "caocao";
	else if(generals.length()<2) return generals.first();

	if(game_state!=1){
		QStringList hidden;
		for (int i = 0; i < generals.length(); i++) hidden << "unknown";
		doAnimate(S_ANIMATE_HUASHEN, player->objectName(), hidden.join(":"));
		if(!thread) thread = new RoomThread(this);
		thread->delay();
	}

	if (player->isOnline()){
		if (doRequest(player, S_COMMAND_CHOOSE_GENERAL, JsonUtils::toJsonArray(generals), true)){
			QVariant clientResponse = player->getClientReply();
			if (JsonUtils::isString(clientResponse)){
				if (generals.contains(clientResponse.toString())||Config.FreeChoose||mode.startsWith("_mini_")||mode=="custom_scenario")
					return clientResponse.toString();
			}
		}
	}
	if (default_choice.isEmpty())
		return generals.at(qrand()%generals.length());
	return generals.first();
}

QString Room::askForGeneral(ServerPlayer*player, const QString&generals, const QString&default_choice)
{
	return askForGeneral(player, generals.split("+"), default_choice); // For Lua only!!!
}

bool Room::makeCheat(ServerPlayer*player)
{
	JsonArray arg = player->m_cheatArgs.value<JsonArray>();
	if (arg.isEmpty() || !JsonUtils::isNumber(arg[0])) return false;
	player->m_cheatArgs = QVariant();

	CheatCode code = (CheatCode)arg[0].toInt();
	if (code == S_CHEAT_KILL_PLAYER){
		JsonArray arg1 = arg[1].value<JsonArray>();
		if (!JsonUtils::isStringArray(arg1, 0, 1)) return false;
		makeKilling(arg1[0].toString(), arg1[1].toString());

	} else if (code == S_CHEAT_MAKE_DAMAGE){
		JsonArray arg1 = arg[1].value<JsonArray>();
		if (arg1.size() != 4 || !JsonUtils::isStringArray(arg1, 0, 1)
			|| !JsonUtils::isNumber(arg1[2]) || !JsonUtils::isNumber(arg1[3]))
			return false;
		makeDamage(arg1[0].toString(), arg1[1].toString(), (QSanProtocol::CheatCategory)arg1[2].toInt(), arg1[3].toInt());

	} else if (code == S_CHEAT_REVIVE_PLAYER){
		if (!JsonUtils::isString(arg[1])) return false;
		makeReviving(arg[1].toString());

	} else if (code == S_CHEAT_RUN_SCRIPT){
		if (!JsonUtils::isString(arg[1])) return false;
		QByteArray data = QByteArray::fromBase64(arg[1].toString().toLatin1());
		data = qUncompress(data);
		doScript(data);

	} else if (code == S_CHEAT_GET_ONE_CARD){
		if (!JsonUtils::isNumber(arg[1])) return false;
		int card_id = arg[1].toInt();

		LogMessage log;
		log.type = "$CheatCard";
		log.from = player;
		log.card_str = QString::number(card_id);
		sendLog(log);

		const Card*c = Sanguosha->getCard(card_id);
		if (c->objectName().startsWith("_"))
			obtainCard(player, c, CardMoveReason(CardMoveReason::S_REASON_EXCLUSIVE, player->objectName()));
		else
			obtainCard(player, c);

	} else if (code == S_CHEAT_CHANGE_GENERAL){
		if (!JsonUtils::isString(arg[1]) || !JsonUtils::isBool(arg[2]))
			return false;
		QString generalName = arg[1].toString();
		bool isSecondaryHero = arg[2].toBool();
		changeHero(player, generalName, false, true, isSecondaryHero);

	} else if (code == S_CHEAT_STATE_EDITOR){
		JsonArray arg1 = arg[1].value<JsonArray>();
		if (arg1.size() != 3 || !JsonUtils::isString(arg1[0])
			|| !JsonUtils::isNumber(arg1[1]) || !JsonUtils::isNumber(arg1[2]))
			return false;
		stateChange(arg1[0].toString(), (QSanProtocol::StateEditorCheat)arg1[1].toInt(), arg1[2].toInt());
	}
	return true;
}

void Room::makeDamage(const QString&source, const QString&target, QSanProtocol::CheatCategory nature, int point)
{
	ServerPlayer*sourcePlayer = findChild<ServerPlayer*>(source);
	ServerPlayer*targetPlayer = findChild<ServerPlayer*>(target);
	if (targetPlayer == nullptr) return;

	if (nature == S_CHEAT_HP_LOSE){
		loseHp(targetPlayer, point, false, sourcePlayer, "cheat");
		return;
	} else if (nature == S_CHEAT_MAX_HP_LOSE){
		loseMaxHp(targetPlayer, point, "cheat");
		return;
	} else if (nature == S_CHEAT_HP_RECOVER){
		recover(targetPlayer, RecoverStruct(sourcePlayer, nullptr, point, "cheat"));
		return;
	} else if (nature == S_CHEAT_MAX_HP_RESET){
		setPlayerProperty(targetPlayer, "maxhp", point);
		return;
	} else if (nature == S_CHEAT_HUJIA_GET){
		targetPlayer->gainHujia(point);
		return;
	} else if (nature == S_CHEAT_HUJIA_LOSE){
		int hujia = targetPlayer->getHujia();
		point = qMin(point, hujia);
		targetPlayer->loseHujia(point);
		return;
	}

	static QMap<QSanProtocol::CheatCategory, DamageStruct::Nature> nature_map;
	if (nature_map.isEmpty()){
		nature_map[S_CHEAT_NORMAL_DAMAGE] = DamageStruct::Normal;
		nature_map[S_CHEAT_THUNDER_DAMAGE] = DamageStruct::Thunder;
		nature_map[S_CHEAT_FIRE_DAMAGE] = DamageStruct::Fire;
		nature_map[S_CHEAT_ICE_DAMAGE] = DamageStruct::Ice;
		nature_map[S_CHEAT_GOD_DAMAGE] = DamageStruct::God;
	}

	if (targetPlayer == nullptr) return;
	damage(DamageStruct("cheat", sourcePlayer, targetPlayer, point, nature_map[nature]));
}

void Room::stateChange(const QString&target, QSanProtocol::StateEditorCheat nature, int point)
{
	ServerPlayer*targetPlayer = findChild<ServerPlayer*>(target);
	if (targetPlayer == nullptr || point == 0) return;
	if (nature == S_CHEAT_CHANGE_MAXCARDS){
		addMaxCards(targetPlayer, point, false);
	} else if (nature == S_CHEAT_CHANGE_DISTANCE){
		addDistance(targetPlayer, point, false, false);
	} else if (nature == S_CHEAT_CHANGE_DISTANCE_TO_OTHERS){
		addDistance(targetPlayer, point, true, false);
	} else if (nature == S_CHEAT_CHANGE_ATTACKRANGE){
		addAttackRange(targetPlayer, point, false);
	} else if (nature == S_CHEAT_CHANGE_SLASHCISHU){
		addSlashCishu(targetPlayer, point, false);
	} else if (nature == S_CHEAT_CHANGE_SLASHJULI){
		addSlashJuli(targetPlayer, point, false);
	} else if (nature == S_CHEAT_CHANGE_SLASHMUBIAO){
		addSlashMubiao(targetPlayer, point, false);
	} else if (nature == S_CHEAT_DrawCards){
		drawCards(targetPlayer, point, "cheat");
	} else if (nature == S_CHEAT_ThrowAllEquips){
		targetPlayer->throwAllEquips("cheat");
	} else if (nature == S_CHEAT_ThrowAllHandCards){
		targetPlayer->throwAllHandCards("cheat");
	} else if (nature == S_CHEAT_ThrowAllHandCardsAndEquips){
		targetPlayer->throwAllHandCardsAndEquips("cheat");
	} else if (nature == S_CHEAT_ThrowAllCards){
		targetPlayer->throwAllCards("cheat");
	} else if (nature == S_CHEAT_ThrowCards){
		askForDiscard(targetPlayer, "cheat", point, point, false, true);
	} else if (nature == S_CHEAT_ThrowCardsWithoutEquips){
		askForDiscard(targetPlayer, "cheat", point, point);
	} else if (nature == S_CHEAT_SetChained){
		setPlayerChained(targetPlayer);
	} else if (nature == S_CHEAT_TurnOver){
		targetPlayer->turnOver();
	} else if (nature == S_CHEAT_UseAnaleptic){
		Card*ana = Sanguosha->cloneCard("analeptic");
		ana->setSkillName("cheat");
		ana->deleteLater();
		if (!targetPlayer->isCardLimited(ana, Card::MethodUse)&&!targetPlayer->isProhibited(targetPlayer, ana))
			useCard(CardUseStruct(ana, targetPlayer));
	}
}

void Room::makeKilling(const QString&killerName, const QString&victimName)
{
	ServerPlayer*killer = findChild<ServerPlayer*>(killerName),*victim = findChild<ServerPlayer*>(victimName);
	if (victim == nullptr) return;
	if (killer == nullptr) return killPlayer(victim);
	DamageStruct damage("cheat", killer, victim);
	killPlayer(victim,&damage);
}

void Room::makeReviving(const QString&name)
{
	ServerPlayer*player = findChild<ServerPlayer*>(name);
	//Q_ASSERT(player);
	revivePlayer(player);
	removeTag("HpChangedData");
	//setPlayerProperty(player, "maxhp", player->getGeneralMaxHp());
	//setPlayerProperty(player, "hp", player->getMaxHp());
	int max_hp = player->getGeneralMaxHp();
	player->setMaxHp(max_hp);
	player->setHp(qMin(player->getGeneralStartHp(),max_hp));
	broadcastProperty(player, "maxhp");
	broadcastProperty(player, "hp");
}

void Room::fillAG(const QList<int>&card_ids, ServerPlayer*who, const QList<int>&disabled_ids)
{
	m_fillAGarg.clear();
	m_fillAGarg << JsonUtils::toJsonArray(card_ids) << JsonUtils::toJsonArray(disabled_ids);
	if (who) doNotify(who, S_COMMAND_FILL_AMAZING_GRACE, m_fillAGarg);
	else doBroadcastNotify(S_COMMAND_FILL_AMAZING_GRACE, m_fillAGarg);
}

void Room::takeAG(ServerPlayer*player, int card_id, bool move_cards, QList<ServerPlayer*> to_notify)
{
	if (to_notify.isEmpty()) to_notify = m_players;

	JsonArray arg;
	arg << (player ? QVariant(player->objectName()) : QVariant());
	arg << card_id << move_cards;

	if (player){
		CardsMoveOneTimeStruct move;
		if (move_cards){
			move.from = nullptr;
			move.from_places << Player::DrawPile;
			move.to = player;
			move.to_place = Player::PlaceHand;
			move.card_ids << card_id;
			QVariant data = QVariant::fromValue(move);
			foreach(ServerPlayer*p, getAllPlayers())
				thread->trigger(BeforeCardsMove, this, p, data);
			//thread->trigger(BeforeCardsMove, this, current, data);
			move = data.value<CardsMoveOneTimeStruct>();
			arg[0] = move.to ? QVariant(move.to->objectName()) : QVariant();
			foreach(int id, move.card_ids){
				clearCardTip(id);
				if(move.to){
					setCardFlag(id, "visible");
					move.to->addCard(id, Player::PlaceHand);
					setCardMapping(id, (ServerPlayer*)move.to, Player::PlaceHand);
					filterCards((ServerPlayer*)move.to, QList<const Card*>()<<Sanguosha->getCard(id), false);
				}
				arg[1] = id;
			}
			arg[2] = move.card_ids.length()>0;
		}
		foreach(ServerPlayer*p, to_notify)
			doNotify(p, S_COMMAND_TAKE_AMAZING_GRACE, arg);
		if (move.card_ids.length()>0){
			QVariant data = QVariant::fromValue(move);
			foreach(ServerPlayer*p, getAllPlayers())
				thread->trigger(CardsMoveOneTime, this, p, data);
			//thread->trigger(CardsMoveOneTime, this, current, data);
		}
	} else {
		foreach(ServerPlayer*p, to_notify)
			doNotify(p, S_COMMAND_TAKE_AMAZING_GRACE, arg);
		if (!move_cards) return;
		LogMessage log;
		log.type = "$EnterDiscardPile";
		log.card_str = QString::number(card_id);
		sendLog(log);

		m_discardPile->prepend(card_id);
		setCardMapping(card_id, nullptr, Player::DiscardPile);
	}
	m_takeAGargs << arg;
}

void Room::clearAG(ServerPlayer*player)
{
	m_fillAGarg.clear();
	m_takeAGargs.clear();
	if (player) doNotify(player, S_COMMAND_CLEAR_AMAZING_GRACE, QVariant());
	else doBroadcastNotify(S_COMMAND_CLEAR_AMAZING_GRACE, QVariant());
}

void Room::provide(const Card*card, QList<ServerPlayer*> tos)
{
	CardUseStruct use;
	use.card = card;
	use.to = tos;
	setTag("provided", QVariant::fromValue(use));
}

QList<ServerPlayer*> Room::getLieges(const QString&kingdom, ServerPlayer*lord) const
{
	QList<ServerPlayer*> lieges;
	foreach(ServerPlayer*p, getAllPlayers()){
		if (p != lord&&p->getKingdom() == kingdom)
			lieges << p;
	}
	return lieges;
}

void Room::sendLog(const LogMessage&log, QList<ServerPlayer*>players)
{
	if (log.type.isEmpty()) return;
	if (players.isEmpty()) doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());
	else doBroadcastNotify(players, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());
}

void Room::sendLog(const LogMessage&log, ServerPlayer*player)
{
	sendLog(log,QList<ServerPlayer*>()<<player);
}

void Room::sendCompulsoryTriggerLog(ServerPlayer*player, const QString&skill_name, bool notify_skill, bool broadcast, int type)
{
	if (broadcast) broadcastSkillInvoke(skill_name, type, player);
	LogMessage log;
	log.type = "#TriggerSkill";
	log.arg = skill_name;
	log.from = player;
	sendLog(log);
	if (notify_skill) notifySkillInvoked(player, skill_name);
}

void Room::sendCompulsoryTriggerLog(ServerPlayer*player, const Skill*skill, int type)
{
	if (skill) sendCompulsoryTriggerLog(player, skill->objectName(), true, true, type);
}

void Room::sendShimingLog(ServerPlayer*player, const QString&skill_name, bool finish_or_failed, int index)
{
	LogMessage log;
	log.from = player;
	log.arg = skill_name;
	log.type = finish_or_failed ? "#FinishShiMing" : "#ShiMingFailed";
	if (index <= 0) index = finish_or_failed ? 2 : 3;
	broadcastSkillInvoke(skill_name, index, player);
	addPlayerMark(player, skill_name);
	sendLog(log);
	notifySkillInvoked(player, skill_name);
}

void Room::sendShimingLog(ServerPlayer*player, const Skill*skill, bool finish_or_failed, int index)
{
	if (skill) sendShimingLog(player, skill->objectName(), finish_or_failed, index);
}

void Room::showCard(ServerPlayer*player, QList<int> card_ids, ServerPlayer*only_viewer, bool self_can_see)
{
	/*foreach(int card_id, card_ids){
		if (getCardOwner(card_id) != player)
			card_ids.removeAll(card_id);
	}
	if (card_ids.isEmpty()) return;

	tryPause();
	notifyMoveFocus(player);
	JsonArray show_arg;
	show_arg << player->objectName() << ListI2S(card_ids).join("+");

	foreach(int card_id, card_ids){
		WrappedCard*wrapped = Sanguosha->getWrappedCard(card_id);
		if (only_viewer){
			if (wrapped->isModified()) notifyUpdateCard(only_viewer, card_id, wrapped);
			//else notifyResetCard(only_viewer, card_id);
		} else {
			setCardFlag(card_id, "visible");
			if (wrapped->isModified()) broadcastUpdateCard(m_players, card_id, wrapped);
			//else broadcastResetCard(m_players, card_id);
		}
	}

	QVariant data = show_arg[1];//ListI2V(card_ids);
	if (only_viewer){
		QList<ServerPlayer*> players;
		players << only_viewer;
		if (self_can_see) players << player;
		doBroadcastNotify(players, S_COMMAND_SHOW_CARD, show_arg);
		data = QString("viewCards:%1:%2").arg(player->objectName()).arg(ListI2S(card_ids).join("+"));
		thread->trigger(ChoiceMade, this, only_viewer, data);
	}else{
		doBroadcastNotify(S_COMMAND_SHOW_CARD, show_arg);
		thread->trigger(ShowCards, this, player, data);
	}*/
	QList<ServerPlayer*> players;
	players << only_viewer;
	if (self_can_see) players << player;
	showCard(player, card_ids, players);
}

void Room::showCard(ServerPlayer*player, int card_id, ServerPlayer*only_viewer, bool self_can_see)
{
	showCard(player, QList<int>()<<card_id, only_viewer, self_can_see);
}

void Room::showCard(ServerPlayer*player, int card_id, QList<ServerPlayer*> players)
{
	showCard(player, QList<int>()<<card_id, players);
}

void Room::showCard(ServerPlayer*player, QList<int> card_ids, QList<ServerPlayer*> players)
{
	foreach(int card_id, card_ids){
		if (getCardOwner(card_id) != player)
			card_ids.removeAll(card_id);
	}
	if (card_ids.isEmpty()) return;

	tryPause();
	notifyMoveFocus(player);
	JsonArray show_arg;
	show_arg << player->objectName() << ListI2S(card_ids).join("+");

	if(players.isEmpty()) players = m_players;
	doBroadcastNotify(players, S_COMMAND_SHOW_CARD, show_arg);
	QVariant data = show_arg[1];
	thread->trigger(ShowCards, this, player, data);
	data = QString("viewCards:%1:%2").arg(player->objectName()).arg(show_arg[1].toString());
	foreach(ServerPlayer*p, players)
		thread->trigger(ChoiceMade, this, p, data);
}

void Room::showAllCards(ServerPlayer*player, ServerPlayer*to)
{
	showAllCards(player,QList<ServerPlayer*>()<<to);
}

void Room::showAllCards(ServerPlayer*player, QList<ServerPlayer*> players)
{
	QList<int> has_ids = player->handCards();
	if (has_ids.isEmpty()) return;
	tryPause();/*

	foreach(int cardId, has_ids){
		WrappedCard*wrapped = Sanguosha->getWrappedCard(cardId);
		if(to){
			if(wrapped->isModified()) notifyUpdateCard(to, cardId, wrapped);
			//else notifyResetCard(to, cardId);
		}else{
			if(wrapped->isModified()) broadcastUpdateCard(m_players, cardId, wrapped);
			//else broadcastResetCard(m_players, cardId);
		}
	}*/
	JsonArray gongxinArgs;
	gongxinArgs << player->objectName() << false << JsonUtils::toJsonArray(has_ids);

	LogMessage log;
	log.card_str = ListI2S(has_ids).join("+");
	if(players.isEmpty()){
		log.type = "$ShowAllCards";
		log.from = player;
		sendLog(log);
		foreach(int id, has_ids)
			setCardFlag(id, "visible");
		players = getOtherPlayers(player);
	}else{
		log.type = "$ViewAllCards";
		log.to << player;
		foreach(ServerPlayer*p, players){
			log.from = p;
			sendLog(log, p);
		}
	}
	doBroadcastNotify(players, S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
	QVariant data = log.card_str;
	thread->trigger(ShowCards, this, player, data);
	data = QString("viewCards:%1:%2").arg(player->objectName()).arg(log.card_str);
	foreach(ServerPlayer*p, players)
		thread->trigger(ChoiceMade, this, p, data);
	/*if (to){
		log.type = "$ViewAllCards";
		log.from = to;
		log.to << player;
		sendLog(log, to);

		QVariant decisionData = QString("viewCards:%1:%2").arg(player->objectName()).arg(log.card_str);
		thread->trigger(ChoiceMade, this, to, decisionData);

		doNotify(to, S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
	} else {
		log.type = "$ShowAllCards";
		log.from = player;
		sendLog(log);
		foreach(int id, has_ids)
			setCardFlag(id, "visible");

		doBroadcastNotify(getOtherPlayers(player), S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);

		QVariant data = log.card_str;//ListI2V(has_ids);
		thread->trigger(ShowCards, this, player, data);
	}*/
}

void Room::retrial(const Card*card, ServerPlayer*player, JudgeStruct*judge, const QString&skill_name, bool exchange)
{
	CardResponseStruct resp(card, judge->who);
	resp.m_isHandcard = player->handCards().contains(card->getEffectiveId());
	resp.m_isRetrial = true;
	QVariant data = QVariant::fromValue(resp);
	
	//if (resp.m_isHandcard)
		thread->trigger(PreCardResponded, this, player, data);
	
	QList<CardsMoveStruct> moves;
	moves << CardsMoveStruct(card->getEffectiveId(), judge->who, Player::PlaceJudge,
		CardMoveReason(CardMoveReason::S_REASON_RETRIAL, player->objectName(), skill_name, ""));
	if(getCardPlace(judge->card->getEffectiveId())==Player::PlaceJudge){
		int reasonType = exchange ? CardMoveReason::S_REASON_OVERRIDE : CardMoveReason::S_REASON_JUDGEDONE;
		CardMoveReason reason(reasonType, player->objectName(), exchange ? skill_name : "", "");
		if (judge->retrial_by_response) reason.m_extraData = QVariant::fromValue(judge->retrial_by_response);
		moves << CardsMoveStruct(judge->card->getEffectiveId(), judge->who, exchange ? player : nullptr,
			Player::PlaceUnknown, exchange ? Player::PlaceHand : Player::DiscardPile, reason);
	}
	judge->retrial_by_response = player;
	
	judge->card = Sanguosha->getCard(card->getEffectiveId());
	
	LogMessage log;
	log.type = "$ChangedJudge";
	log.arg = skill_name;
	log.from = player;
	log.to << judge->who;
	log.card_str = judge->card->toString();
	sendLog(log);
	notifySkillInvoked(player, skill_name);
	moveCardsAtomic(moves, true);
	judge->updateResult();
	
	//if (resp.m_isHandcard){
		thread->trigger(CardResponded, this, player, data);
		thread->trigger(PostCardResponded, this, player, data);
	//}
	
	thread->trigger(AfterRetrial, this, player, data);
}

ServerPlayer*Room::askForYiji(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name,
	bool is_preview, bool visible, bool optional, int max_num, QList<ServerPlayer*> players,
	CardMoveReason reason, const QString&prompt, bool notify_skill)
{
	CardsMoveStruct yiji = askForYijiStruct(guojia,cards,skill_name,is_preview,visible,optional,max_num,players,reason,prompt,notify_skill);
	return (ServerPlayer*)yiji.to;
}

QList<int> Room::askForyiji(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name,
	bool is_preview, bool visible, bool optional, int max_num, QList<ServerPlayer*> players,
	CardMoveReason reason, const QString&prompt, bool notify_skill)
{
	CardsMoveStruct yiji = askForYijiStruct(guojia,cards,skill_name,is_preview,visible,optional,max_num,players,reason,prompt,notify_skill);
	return yiji.card_ids;
}

CardsMoveStruct Room::askForYijiStruct(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name,
	bool is_preview, bool visible, bool optional, int max_num, QList<ServerPlayer*> players,
	CardMoveReason reason, const QString&prompt, bool notify_skill, bool get)
{
	CardsMoveStruct move;
	if (max_num == -1) max_num = cards.length();
	if (cards.isEmpty() || max_num == 0) return move;
	if (players.isEmpty()) players = getOtherPlayers(guojia);
	if (reason.m_reason == CardMoveReason::S_REASON_UNKNOWN){
		// when we use ? : here, compiling error occurs under debug mode...
		if (is_preview) reason.m_reason = CardMoveReason::S_REASON_PREVIEWGIVE;
		else reason.m_reason = CardMoveReason::S_REASON_GIVE;
		reason.m_playerId = guojia->objectName();
		reason.m_eventName = "yiji_give";
		reason.m_skillName = skill_name;
	}
	tryPause();
	notifyMoveFocus(guojia, S_COMMAND_SKILL_YIJI);
	ServerPlayer*target = nullptr;
	AI*ai = guojia->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		int card_id = -1;
		QStringList player_names;
		foreach(ServerPlayer*p, players)
			player_names << p->objectName();
		guojia->tag["yijiForAI"] = player_names;
		target = ai->askForYiji(cards, skill_name, card_id);
		if (card_id>=0) move.card_ids << card_id;
		if (Config.AIDelay>timer.elapsed())
			thread->delay(Config.AIDelay-timer.elapsed());
	} else {
		JsonArray arg, player_names;
		arg << JsonUtils::toJsonArray(cards) << optional << max_num;
		foreach(ServerPlayer*p, players)
			player_names << p->objectName();
		arg << QVariant(player_names) << prompt;
		if (doRequest(guojia, S_COMMAND_SKILL_YIJI, arg, true)){
			arg = guojia->getClientReply().value<JsonArray>();
			if (arg.size()>1&&JsonUtils::tryParse(arg[0],move.card_ids))
				target = findChild<ServerPlayer*>(arg[1].toString());
		}else{
			ai = guojia->getAI();
			if(ai){
				int card_id = -1;
				QStringList player_names;
				foreach(ServerPlayer*p, players)
					player_names << p->objectName();
				guojia->tag["yijiForAI"] = player_names;
				target = ai->askForYiji(cards, skill_name, card_id);
				if (card_id>=0) move.card_ids << card_id;
			}
		}
	}
	if (!target) return move;
	reason.m_targetId = target->objectName();
	foreach(int id, move.card_ids)
		cards.removeOne(id);
	move.reason = reason;
	move.to = target;
	move.to_place = Player::PlaceHand;
	QVariant decisionData = QString("Yiji:%1:%2:%3").arg(skill_name)
		.arg(target->objectName()).arg(ListI2S(move.card_ids).join("+"));
	thread->trigger(ChoiceMade, this, guojia, decisionData);
	if (notify_skill){
		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = guojia;
		log.arg = skill_name;
		sendLog(log);
		const Skill*skill = Sanguosha->getSkill(skill_name);
		if (skill){
			broadcastSkillInvoke(skill_name, skill->getEffectIndex(guojia, dummyCard(move.card_ids)));
			notifySkillInvoked(guojia, skill_name);
		}
	}
	if (get)
		moveCardsAtomic(move, visible);
	return move;
}

void Room::addMaxCards(ServerPlayer*player, int num, bool one_turn)
{
	if (num == 0) return;
	if (one_turn) addPlayerMark(player, "ExtraBfMaxCards-Clear", num);
	else addPlayerMark(player, "ExtraBfMaxCards", num);
}

void Room::addAttackRange(ServerPlayer*player, int num, bool one_turn)
{
	if (num == 0) return;
	if (one_turn) addPlayerMark(player, "ExtraBfAttackRange-Clear", num);
	else addPlayerMark(player, "ExtraBfAttackRange", num);
}

void Room::addSlashCishu(ServerPlayer*player, int num, bool one_turn)
{
	if (one_turn) addPlayerMark(player, "ExtraBfSlashCishu-Clear", num);
	else addPlayerMark(player, "ExtraBfSlashCishu", num);
}

void Room::addSlashJuli(ServerPlayer*player, int num, bool one_turn)
{
	if (one_turn) addPlayerMark(player, "ExtraBfSlashJuli-Clear", num);
	else addPlayerMark(player, "ExtraBfSlashJuli", num);
}

void Room::addSlashMubiao(ServerPlayer*player, int num, bool one_turn)
{
	if (one_turn) addPlayerMark(player, "ExtraBfSlashMubiao-Clear", num);
	else addPlayerMark(player, "ExtraBfSlashMubiao", num);
}

void Room::addSlashBuff(ServerPlayer*player, const QString&flags, int num, bool one_turn)
{
	if (num == 0) return;
	QString buff = flags;
	if (buff.isEmpty())
		buff = "cjm";  //c means cishu, j means juli, m means mubiao

	if (buff.contains("c"))
		addSlashCishu(player, num, one_turn);
	if (buff.contains("j"))
		addSlashJuli(player, num, one_turn);
	if (buff.contains("m"))
		addSlashMubiao(player, num, one_turn);
}

void Room::addDistance(ServerPlayer*player, int num, bool player_isfrom ,bool one_turn)
{
	if (player_isfrom){
		if (one_turn) addPlayerMark(player, "ExtraBfDistanceFrom-Clear", num);
		else addPlayerMark(player, "ExtraBfDistanceFrom", num);
	} else {
		if (one_turn) addPlayerMark(player, "ExtraBfDistanceTo-Clear", num);
		else addPlayerMark(player, "ExtraBfDistanceTo", num);
	}
}

QList<int> Room::getAvailableCardList(ServerPlayer*player, const QString&flags, const QString&skill_name, const Card*card, bool except_delayedtrick)
{
	QList<int> list;
	QStringList names, ban = Sanguosha->getBanPackages();
	for (int id = 0; id < Sanguosha->getCardCount(); id++){
		const Card*c = Sanguosha->getEngineCard(id);
		if (names.contains(c->objectName())||(except_delayedtrick&&c->isKindOf("DelayedTrick"))
			||c->objectName().startsWith("_")||ban.contains(c->getPackage())) continue;
		if (flags.contains(c->getType())){
			Card*dc = Sanguosha->cloneCard(c->objectName());
			if (card) dc->addSubcard(card);
			dc->setSkillName(skill_name);
			if (dc->isAvailable(player)){
				names << c->objectName();
				list << id;
			}
			delete dc;
		}
	}
	return list;
}

QList<ServerPlayer*> Room::getCardTargets(ServerPlayer*from, const Card*card, QList<ServerPlayer*> except_players)
{
	QList<ServerPlayer*> targets;
	//if (!card->isAvailable(from)) return targets;  //【杀】、【酒】就不能获取目标了
	//这个函数获得的是所有可以成为目标的角色，所以【无中生有】、【桃】、【酒】、装备牌等不会只return自己
	foreach(ServerPlayer*p, m_alivePlayers){
		if (except_players.contains(p)) continue;
		int x = 0;
		if (card->targetFilter(QList<const Player*>(),p,from,x)||x>0)
			targets << p;
		else if(card->isKindOf("Slash")&&from->canSlash(p,card))
			targets << p;
	}
	return targets;
}

bool Room::canMoveField(const QString&flags, QList<ServerPlayer*> froms, QList<ServerPlayer*> tos)
{
	if (froms.isEmpty()) froms = m_alivePlayers;
	foreach(ServerPlayer*p, froms){
		QList<ServerPlayer*> new_tos = tos;
		if (new_tos.isEmpty()) new_tos = getOtherPlayers(p);
		foreach(const Card*c, p->getCards(flags)){
			foreach(ServerPlayer*d, new_tos){
				if (c->isKindOf("EquipCard")){
					if (!d->getEquip(((const EquipCard*)c->getRealCard())->location())&&!p->isProhibited(d, c))
						return true;
				} else if (c->isKindOf("DelayedTrick")){
					if (!p->isProhibited(d, c))
						return true;
				}
			}
		}
	}
	return false;
}

bool Room::moveField(ServerPlayer*player, const QString&reason, bool optional, const QString&flags, QList<ServerPlayer*> froms,
					QList<ServerPlayer*> tos)
{

	QList<ServerPlayer*> from_players;
	if (froms.isEmpty()) froms = m_alivePlayers;

	foreach(ServerPlayer*p, froms){
		QList<ServerPlayer*> newFroms;
		newFroms << p;
		if (canMoveField(flags, newFroms, tos))
			from_players << p;
	}

	QString prompt = "@movefield-from";
	if (flags.contains("e")&&!flags.contains("j"))
		prompt = "@movefield-equip-from";
	else if (flags.contains("j")&&!flags.contains("e"))
		prompt = "@movefield-judge-from";

	if (optional) prompt = prompt + "-optional";
	ServerPlayer*from = askForPlayerChosen(player, from_players, reason + "_from", prompt, optional);
	if(!from) return false;

	QList<int> disabled_ids;
	if (tos.isEmpty()) tos = getOtherPlayers(from);
	foreach(const Card*c, from->getCards(flags)){
		bool has = true;
		foreach(ServerPlayer*d, tos){
			if (player->isProhibited(d, c)) continue;
			if (c->isKindOf("EquipCard")){
				if (d->getEquip(((const EquipCard*)c->getRealCard())->location()))
					continue;
			}
			has = false;
			break;
		}
		if (has)
			disabled_ids << c->getId();
	}
	doAnimate(S_ANIMATE_INDICATE, player->objectName(), from->objectName());
	int id = askForCardChosen(player, from, flags, reason, false, Card::MethodNone, disabled_ids);
	Player::Place place = getCardPlace(id);
	const Card*c = Sanguosha->getCard(id);

	QList<ServerPlayer*> to_players;
	foreach(ServerPlayer*p, tos){
		if (player->isProhibited(p, c)) continue;
		if (place == Player::PlaceEquip){
			if (p->getEquip(((const EquipCard*)c->getRealCard())->location()))
				continue;
		}
		to_players << p;
	}
	if (to_players.isEmpty()) return false;

	ServerPlayer*to = askForPlayerChosen(player, to_players, reason + "_to", "@movefield-to:" + c->objectName());
	doAnimate(S_ANIMATE_INDICATE, player->objectName(), to->objectName());
	moveCardTo(c, from, to, place, CardMoveReason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), reason, ""), true);
	return true;
}

void Room::changeTranslation(ServerPlayer*player, const QString&skill_name, const QString&new_translation, int num)
{
	//Sanguosha->addTranslationEntry(":"+skill_name,new_translation);
	JsonArray args1;
	args1 << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL << player->objectName() << skill_name;
	if (num>0){
		args1 << num;
		setPlayerProperty(player,("changeTranslation"+skill_name).toStdString().c_str(),num);
	}else{
		args1 << new_translation.toUtf8().toBase64();
		setPlayerProperty(player,("changeTranslation"+skill_name).toStdString().c_str(),args1.last());
	}
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args1);
	//更新技能图标上的技能描述
	if (player->hasSkill(skill_name, true))
		doNotify(player, S_COMMAND_UPDATE_SKILL, skill_name);  //自带更新武将图上的技能描述的功能，但有时会失灵，不知道为何
}

void Room::changeTranslation(ServerPlayer*player, const QString&skill_name, int num)
{
	QString new_translation = ":"+skill_name;
	if(num==0) new_translation = Sanguosha->translate(new_translation, true);
	else new_translation = Sanguosha->translate(new_translation+QString::number(num));
	changeTranslation(player, skill_name, new_translation, num);
}

int Room::getChangeSkillState(ServerPlayer*player, const QString&skill_name)
{
	QString str = "ChangeSkill_" + skill_name + "_State";
	int n = player->property(str.toStdString().c_str()).toInt();
	if (n <= 0) n = 1;
	return n;
}

void Room::setChangeSkillState(ServerPlayer*player, const QString&skill_name, int n)
{
	if (player->isDead()) return;
	int m = getChangeSkillState(player, skill_name);

	if (n <= 0) n = 1;

	setPlayerProperty(player, ("ChangeSkill_"+skill_name+"_State").toStdString().c_str(), n);

	changeTranslation(player, skill_name, n);
	setPlayerMark(player, QString("&%1+%2_num").arg(skill_name).arg(m), 0);
	if (player->hasSkill(skill_name, true))
		setPlayerMark(player, QString("&%1+%2_num").arg(skill_name).arg(n), 1);
}

bool Room::CardInPlace(const Card*card, Player::Place place)
{
	QList<int> list;
	if (card->isVirtualCard())
		list = card->getSubcards();
	else list << card->getId();

	if (list.isEmpty()) return false;

	foreach(int id, list){
		if (getCardPlace(id) != place)
			return false;
	}
	return true;
}

bool Room::CardInTable(const Card*card)
{
	//Q_ASSERT(card != nullptr);
	return card&&CardInPlace(card, Player::PlaceTable);
}

bool Room::hasCurrent(bool need_alive)
{
	return current&&(!need_alive||current->hasFlag("CurrentPlayer"));
}

QList<int> Room::showDrawPile(ServerPlayer*player, int num, const QString&skill_name, bool liangchu, bool isTop)
{
	if (num <= 0) return QList<int>();
	QList<int> ids = getNCards(num, liangchu, isTop);
	if (liangchu){
		CardsMoveStruct move(ids,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_REASON_TURNOVER,player->objectName(),skill_name,""));
		moveCardsAtomic(move, true);
	} else {
		JsonArray arg;
		arg << "." << false << JsonUtils::toJsonArray(ids);
		doBroadcastNotify(QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, arg);
		foreach(int id, ids)
			setCardFlag(id,"visible");
		LogMessage log;
		log.type = isTop ? "$TurnOver" : "$ShowEnd";
		log.from = player;
		log.card_str = ListI2S(ids).join("+");
		sendLog(log);
		if (isTop) returnToTopDrawPile(ids);
		else returnToEndDrawPile(ids);
	}
	return ids;
}

void Room::ignoreCards(ServerPlayer*player, QList<int> ids)
{
	foreach(int id, ids)
		setPlayerCardLimitation(player, "ignore", QString::number(id), true);
}

void Room::ignoreCards(ServerPlayer*player, int id)
{
	return ignoreCards(player, QList<int>() << id);
}

void Room::ignoreCards(ServerPlayer*player, const Card*card)
{
	QList<int> ids;
	if (card->isVirtualCard())
		ids = card->getSubcards();
	else ids << card->getId();
	return ignoreCards(player, ids);
}

void Room::breakCard(QList<int> ids,ServerPlayer*player)
{
	if (ids.isEmpty()) return;
	LogMessage log;
	log.type = player?"$BreakCard":"$BreakCard2";
	log.from = player;
	log.card_str = ListI2S(ids).join("+");
	sendLog(log);
	QVariantList bds = getTag("BreakCard").toList();
	foreach(QVariant qv, bds){
		if(getCardPlace(qv.toInt())!=Player::PlaceTable)
			bds.removeAll(qv);
	}
	foreach(int id, ids){
		if(!bds.contains(QVariant(id)))
			bds << id;
	}
	setTag("BreakCard",bds);
	CardsMoveStruct move;
	move.card_ids = ids;
	move.to_place = Player::PlaceTable;
	move.reason = CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, player?player->objectName():"", "BreakCard", "");
	moveCardsAtomic(move, true, false);
}

void Room::breakCard(int id,ServerPlayer*player)
{
	return breakCard(QList<int>()<<id,player);
}

void Room::breakCard(const Card*card,ServerPlayer*player)
{
	QList<int> ids;
	if (card->isVirtualCard())
		ids = card->getSubcards();
	else ids << card->getId();
	return breakCard(ids,player);
}

void Room::notifyMoveToPile(ServerPlayer*player, const QList<int>&cards, const QString&reason, Player::Place place, bool in, bool visible)
{
	QList<CardsMoveStruct> moves;
	if (in){
		foreach(int id, ListV2I(player->tag[reason + "ForAI"].toList())){
			CardsMoveStruct move = CardsMoveStruct(id, player, getCardOwner(id), Player::PlaceSpecial, getCardPlace(id), CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
			move.from_pile_name = "#" + reason;
			move.to_pile_name = "#" + reason;
			moves << move;
		}
		foreach(int id, cards){
			/*const Card*card = Sanguosha->getCard(id);
			QStringList info;//为了处理锁定视为技影响的卡牌，先用这个蠢方法
			info << "CardInformationHelper" << card->getSuitString() << QString::number(card->getNumber());
			setCardFlag(card, info.join("|"));*/
			if(place==Player::PlaceUnknown) place = getCardPlace(id);
			CardsMoveStruct move = CardsMoveStruct(id, getCardOwner(id), player, place, Player::PlaceSpecial, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
			move.to_pile_name = "#" + reason;
			moves << move;
		}
		player->tag[reason + "ForAI"] = ListI2V(cards);/*
		CardsMoveStruct move = CardsMoveStruct(cards, getCardOwner(cards.first()), player, place, Player::PlaceSpecial, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
		move.to_pile_name = "#" + reason;
		moves << move;*/
	} else {
		/*foreach(int id, cards){
			const Card*card = Sanguosha->getCard(id);
			foreach(QString flag, card->getFlags()){
				if (flag.startsWith("CardInformationHelper|"))
					setCardFlag(card, "-" + flag);
			}
		}
		CardsMoveStruct move = CardsMoveStruct(cards, player, getCardOwner(cards.first()), Player::PlaceSpecial, place, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
		move.from_pile_name = "#" + reason;
		moves << move;*/
		foreach(int id, ListV2I(player->tag[reason + "ForAI"].toList())){
			Player::Place place_ = place;
			if(place==Player::PlaceUnknown) place_ = getCardPlace(id);
			CardsMoveStruct move = CardsMoveStruct(id, player, getCardOwner(id), Player::PlaceSpecial, place_, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
			move.from_pile_name = "#" + reason;
			move.to_pile_name = "#" + reason;
			moves << move;
		}
		player->tag.remove(reason + "ForAI");
	}
	QList<ServerPlayer*> v_player;
	v_player << player;
	notifyMoveCards(true, moves, visible, v_player);
	notifyMoveCards(false, moves, visible, v_player);
}

QString Room::ZhizheCardViewAsEquip(const Card*card)
{
	QString info = getTag("ZhizheFilter_"+card->toString()).toString();
	if (info.contains("+")) return info.split("+").first();
	return "";
}

void Room::notifyWeaponRange(const QString&weapon_name, int range)
{
	JsonArray args;
	args << weapon_name << range;
	doBroadcastNotify(QSanProtocol::S_COMMAND_WEAPON_RANGE, args);
	Weapon*w = Sanguosha->findChild<Weapon*>(weapon_name);
	if(w) w->setRange(range);
	QString translated = Sanguosha->translate(":"+weapon_name+"1");
	translated.replace("%src", QString::number(range));
	Sanguosha->addTranslationEntry(":"+weapon_name,translated);
	doBroadcastNotify(S_COMMAND_UPDATE_SKILL, QVariant(weapon_name));
}

QString Room::generatePlayerName()
{
	static unsigned int id = 1;
	return QString("sgs%1").arg(id++);
}

QString Room::askForOrder(ServerPlayer*player, const QString&default_choice)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_ORDER);
	if(!player->getAI()&&doRequest(player, S_COMMAND_CHOOSE_ORDER, (int)S_REASON_CHOOSE_ORDER_TURN, true)){
		QVariant clientReply = player->getClientReply();
		if(JsonUtils::isNumber(clientReply))
			return (Game3v3Camp)clientReply.toInt() == S_CAMP_WARM ? "warm" : "cool";
	}
	return default_choice;
}

QString Room::askForRole(ServerPlayer*player, const QStringList&roles, const QString&scheme)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_CHOOSE_ROLE_3V3);

	QStringList squeezed = QSet<QString>(roles.begin(), roles.end()).values();
	QString result = "abstain";

	JsonArray arg;
	arg << scheme << JsonUtils::toJsonArray(squeezed);
	if(doRequest(player, S_COMMAND_CHOOSE_ROLE_3V3, arg, true)){
		QVariant clientReply = player->getClientReply();
		if(JsonUtils::isString(clientReply))
			result = clientReply.toString();
	}
	return result;
}

void Room::networkDelayTestCommand(ServerPlayer*player, const QVariant&)
{
	qint64 delay = player->endNetworkDelayTest();
	QString reportStr = QString("<font color=#EEB422>网络延迟为%1毫秒</font>").arg(delay);
	//tr("<font color=#EEB422>The network delay of player <b>%1</b> is %2 milliseconds.</font>").arg(player->screenName()).arg(delay);
	speakCommand(player, reportStr.toUtf8().toBase64());
}

void Room::sortByActionOrder(QList<ServerPlayer*>&players)
{
	if (players.size()>1){
		QList<ServerPlayer*> newplayers;
		foreach(ServerPlayer*p, getAllPlayers(true)){
			while (players.contains(p)){
				players.removeOne(p);
				newplayers << p;
			}
		}
		players = newplayers;
	}
	//std::sort(players.begin(), players.end(), ServerPlayer::CompareByActionOrder);
}

int Room::getBossModeExpMult(int level) const
{
	// m_lua is owned exclusively by this Room thread — no engine-level lock needed
	lua_getglobal(m_lua, "bossModeExpMult");
	lua_pushinteger(m_lua, level);
	int res = 0;
	if (lua_pcall(m_lua, 1, 1, 0) == 0){
		res = lua_tointeger(m_lua, -1);
		lua_pop(m_lua, 1);
	}
	return res;
}

