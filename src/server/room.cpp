#include "room.h"
#include "qt-collection-utils.h"
#include "runtime-paths.h"
#include "card-lifetime-manager.h"
#include "ai-decision-coordinator.h"
#include "card-movement-service.h"
#include "card-state-service.h"
#include "extra-turn-scheduler.h"
#include "game-session-controller.h"
#include "game-snapshot-service.h"
#include "request-coordinator.h"
#include "player-decision-service.h"
#include "room-notifier.h"
#include "room-roster.h"
#include "player-lifecycle-service.h"
#include "player-state-service.h"
#include "skill-runtime-coordinator.h"
#include "protocol/card-provenance-message.h"
#include "protocol/session/session-payloads.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "protocol/switch-context-message.h"
#include "protocol/sync-pile-message.h"
#include "engine.h"
#include "settings.h"
#include "standard.h"
#include "ai.h"
#include "card-lifetime-manager.h"
#include "scenario.h"
#include "gamerule.h"
#include "banpair.h"
#include "roomthread3v3.h"
#include "roomthreadxmode.h"
#include "roomthread1v1.h"
#include "server.h"
#include "generalselector.h"
#include "miniscenarios.h"
#include "lua.hpp"
#include "lua-runtime.h"
#include "lua-wrapper.h"
#include "exppattern.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "server-info.h"
#include "skill-instance-utils.h"
#include <algorithm>
#include <limits>
#include <QDateTime>
#include <QDir>
#include <QSet>

#ifdef QSAN_UI_LIBRARY_AVAILABLE
#pragma message WARN("UI elements detected in server side!!!")
#endif

using namespace QSanProtocol;

namespace {

static const char *kControllerNameTag = "Controller_Name";

static QVariantMap makeChatMessage(const QString &speaker, const QString &text)
{
	ChatMessagePayload payload;
	payload.speaker = speaker;
	payload.text = text;
	return payload.toVariant();
}

static int getConvertedPhysicalCardId(const Card *card)
{
	if (card == nullptr || card->isVirtualCard() || !card->isModified())
		return -1;

	const int cardId = card->getEffectiveId();
	if (cardId < 0 || cardId >= Sanguosha->getCardCount())
		return -1;

	const Card *originalCard = Sanguosha->getEngineCard(cardId);
	if (originalCard == nullptr)
		return -1;
	if (originalCard->objectName() == card->objectName()
		&& !card->getSkillName(false).startsWith("_"))
		return -1;

	return cardId;
}

static bool hasGenericActiveSkillUsage(const ViewAsSkillV2 *skill)
{
	return skill && skill->getLimitScope() != Skill::Limit_None
		&& skill->getLimitScope() != Skill::Limit_Custom;
}

static bool hasViewAsSkillEffect(const Player *player, const QString &skillName)
{
	return player && player->getMark("ViewAsSkill_" + skillName + "Effect") > 0;
}

static QString getControllerMarkName(const QString &controllerName)
{
	return controllerName.isEmpty() ? QString() : "&controllby+#" + controllerName + "+sys_";
}

static QString getControllerVisibleHandMarkName(const QString &targetName)
{
	return targetName.isEmpty() ? QString() : "HandcardVisible_" + targetName + "+dualcontrol+sys_";
}

static void setViewerHandcardVisible(Room *room, ServerPlayer *viewer, const QString &markName, bool visible)
{
	if (room == nullptr || viewer == nullptr || markName.isEmpty())
		return;

	QList<ServerPlayer *> onlyViewers;
	onlyViewers << viewer;
	room->setPlayerMark(viewer, markName, visible ? 1 : 0, onlyViewers);
}

static void syncKnownHandcards(Room *room, ServerPlayer *viewer, ServerPlayer *target)
{
	if (room == nullptr || viewer == nullptr || target == nullptr)
		return;

	JsonArray knownCardsArg;
	knownCardsArg << target->objectName() << JsonUtils::toJsonArray(target->handCards());
	room->doNotify(viewer, S_COMMAND_SET_KNOWN_CARDS, knownCardsArg);
}

static void clearControllerRelationImpl(Room *room, ServerPlayer *player)
{
	if (room == nullptr || player == nullptr)
		return;

	QString controllerName = player->getTag(kControllerNameTag).toString();
	ServerPlayer *controller = controllerName.isEmpty() ? nullptr : room->findPlayerByObjectName(controllerName, true);
	if (controller != nullptr && controller != player) {
		setViewerHandcardVisible(room, controller, getControllerVisibleHandMarkName(player->objectName()), false);
	}

	QString controllerMark = getControllerMarkName(controllerName);
	if (!controllerMark.isEmpty())
		room->setPlayerMark(player, controllerMark, 0);

	player->removeTag(kControllerNameTag);
	QString controlledMark = getControllerMarkName(player->objectName());

	foreach (ServerPlayer *candidate, room->getAllPlayers(true)) {
		if (candidate == player)
			continue;
		if (candidate->getTag(kControllerNameTag).toString() != player->objectName())
			continue;

		setViewerHandcardVisible(room, player, getControllerVisibleHandMarkName(candidate->objectName()), false);
		room->setPlayerMark(candidate, controlledMark, 0);
		candidate->removeTag(kControllerNameTag);
		if (candidate->getState() == "offline") {
			room->setPlayerProperty(candidate, "state", "trust");
			room->resetAI(candidate);
		}
	}
}

}

void Room::syncControllerPileVisible(ServerPlayer *target, ServerPlayer *controller)
{
	if (!target || !controller)
		return;

	foreach (QString pile_name, target->getPileNames()) {
		target->setPileOpen(pile_name, controller->objectName());
	}

	foreach (QString pile_name, target->getPileNames()) {
		QList<int> pile_cards = target->getPile(pile_name);
		if (!pile_cards.isEmpty()) {
			SyncPileMessage message;
			message.playerName = target->objectName();
			message.pileName = pile_name;
			message.cardIds = pile_cards;
			doNotify(controller, S_COMMAND_SYNC_PILE, message.toVariant());
		}
	}
}

void Room::clearControllerPileVisible(ServerPlayer *target, ServerPlayer *controller)
{
	if (!target || !controller)
		return;

	foreach (QString pile_name, target->getPileNames()) {
		target->removePileOpen(pile_name, controller->objectName());
	}
}

Room::Room(QObject*parent, const QString&mode, const GameSessionConfig &sessionConfig)
	: QThread(parent), m_runtime(std::make_unique<RoomRuntime>(this)),
	m_skillRuntime(std::make_unique<SkillRuntimeCoordinator>(*this)),
	m_aiDecisions(std::make_unique<AiDecisionCoordinator>(*this, *m_skillRuntime)),
	m_extraTurns(std::make_unique<ExtraTurnScheduler>(*this)),
	m_notifier(std::make_unique<RoomNotifier>(*this)),
	m_requests(std::make_unique<RequestCoordinator>(*this)),
	m_playerDecisions(std::make_unique<PlayerDecisionService>(
		*this, static_cast<EventDispatcher &>(*this))),
	m_cardMovement(std::make_unique<CardMovementService>(*this)),
	m_playerState(std::make_unique<PlayerStateService>(
		*this, *m_runtime, *m_notifier, *m_aiDecisions, *m_cardMovement,
		static_cast<EventDispatcher &>(*this))),
	m_cardState(std::make_unique<CardStateService>(*m_notifier)),
	m_snapshotService(std::make_unique<GameSnapshotService>(*this)),
	m_roster(std::make_unique<RoomRoster>()),
	m_playerLifecycle(std::make_unique<PlayerLifecycleService>(
		*this, *m_roster, *m_skillRuntime, *m_cardMovement, *m_notifier,
		static_cast<EventDispatcher &>(*this))),
	m_gameSession(std::make_unique<GameSessionController>(*this)),
	mode(mode), player_count(Sanguosha->getPlayerCount(mode)), current(nullptr),
	game_paused(false),
	thread(nullptr),//game_started(false), game_finished(false),
	thread_3v3(nullptr), thread_xmode(nullptr), thread_1v1(nullptr),
	scenario(Sanguosha->getScenario(mode)), m_surrenderRequestReceived(false), _virtual(false),
	m_sessionConfig(sessionConfig)
{
	static int s_global_room_id = 0;
	_m_Id = s_global_room_id++;
	_m_lastMovementId = 0;

	m_runtime->seedRandom(m_sessionConfig.seed);
	GameRng::Binding rngBinding(m_runtime->rng());
	QString runtimeError;
	const bool runtimeReady = m_runtime->initialize(&runtimeError);
	if (!runtimeReady) {
		qCritical("Room Lua runtime initialization failed: %s", qUtf8Printable(runtimeError));
		m_gameSession->abort(GameSessionController::TerminationCause::InitializationFailure);
		m_runtime->shutdownForInitFailure();
	} else {
		LuaRuntime::Binding luaBinding(m_runtime->lua());
		EngineRuntimeContextScope contextScope(*Sanguosha, this);
		m_cardMovement->drawPile() = Sanguosha->getRandomCards(true);
	}

	//connect(this,SIGNAL(signalSetProperty(ServerPlayer*,const char*,QVariant)),this, SLOT(slotSetProperty(ServerPlayer*,const char*,QVariant)),Qt::QueuedConnection);
	connect(this, SIGNAL(signalSetProperty(ServerPlayer*, const char*, QVariant)), this, SLOT(slotSetProperty(ServerPlayer*, const char*, QVariant)), Qt::BlockingQueuedConnection);
}

Room::~Room()
{
    globalCardLifetimeManager().releaseVariantTags(this);
	if (!stopGameThreads(10000))
		qFatal("Room worker did not stop before runtime destruction");
	if (m_runtime)
		m_runtime->shutdownFinal();
	delete thread_3v3.data();
	delete thread_xmode.data();
	delete thread_1v1.data();
	thread_3v3 = nullptr;
	thread_xmode = nullptr;
	thread_1v1 = nullptr;
	foreach(ServerPlayer*player, getPlayers())
		delete player;
	if (thread != nullptr)
		delete thread;
}

bool Room::dispatch(TriggerEvent event, ServerPlayer *target, QVariant &data)
{
	Q_ASSERT_X(thread != nullptr, "Room::dispatch", "ordinary RoomThread must be ready");
	return thread->trigger(event, this, target, data);
}

void Room::registerTriggerSkill(const TriggerSkill *skill)
{
	Q_ASSERT_X(thread != nullptr, "Room::registerTriggerSkill", "ordinary RoomThread must be ready");
	thread->addTriggerSkill(skill);
}

void Room::clearControllerRelation(ServerPlayer *player)
{
	clearControllerRelationImpl(this, player);
}

bool Room::stopGameThreads(int timeoutMs)
{
	QList<QThread *> workers;
	workers << thread_3v3.data() << thread_xmode.data() << thread_1v1.data() << thread;
	foreach (QThread *worker, workers) {
		if (worker)
			disconnect(worker, nullptr, this, nullptr);
	}

	m_gameSession->abort(GameSessionController::TerminationCause::Shutdown);
	{
		QMutexLocker locker(&m_mutex);
		game_paused = false;
		m_waitCond.wakeAll();
	}
	foreach (ServerPlayer *player, getPlayers())
		player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	m_requests->unblockWaits();

	foreach (QThread *worker, workers) {
		if (!worker || worker == QThread::currentThread())
			continue;
		worker->requestInterruption();
		worker->quit();
	}

	QElapsedTimer timer;
	timer.start();
	foreach (QThread *worker, workers) {
		if (!worker || worker == QThread::currentThread() || !worker->isRunning())
			continue;
		const int remaining = qMax(0, timeoutMs - int(timer.elapsed()));
		if (remaining == 0 || !worker->wait(remaining))
			return false;
	}
	return true;
}

void Room::abortWaitingRequests()
{
	m_gameSession->abort(GameSessionController::TerminationCause::Shutdown);
	{
		QMutexLocker locker(&m_mutex);
		game_paused = false;
		m_waitCond.wakeAll();
	}
	foreach (ServerPlayer *player, getPlayers())
		player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	m_requests->unblockWaits();
}

ServerPlayer*Room::getCurrent() const
{
	return current;
}

void Room::setCurrent(ServerPlayer*current)
{
	if (this->current == current)
		return;
	this->current = current;
	m_runtime->advanceStateRevision(RoomRuntime::TurnStateChanged);
}

int Room::scheduleExtraTurn(ServerPlayer *player, const QString &reason,
                            QList<Player::Phase> phases, int times)
{
    return m_extraTurns->schedule(player, reason, phases, times);
}

int Room::scheduleExtraTurn(ServerPlayer *player, const SkillInstanceRef &sourceRef,
                            QList<Player::Phase> phases, int times)
{
    return m_extraTurns->schedule(player, sourceRef, phases, times);
}

bool Room::isCurrentExtraTurn() const
{
    return m_extraTurns->isCurrentExtraTurn();
}

QString Room::getCurrentExtraTurnReason() const
{
    return m_extraTurns->currentReason();
}

SkillInstanceRef Room::getCurrentExtraTurnSourceRef() const
{
    return m_extraTurns->currentSourceRef();
}

void Room::processScheduledExtraTurns()
{
    m_extraTurns->process();
}

void Room::executeExtraTurn(ServerPlayer *player, QList<Player::Phase> phases,
                            const QString &reason, const SkillInstanceRef &sourceRef)
{
    m_extraTurns->execute(player, phases, reason, sourceRef);
}

int Room::alivePlayerCount() const
{
	return m_roster->aliveCount();
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
	return m_roster->players();
}

QList<ServerPlayer*> Room::getAllPlayers(bool include_dead) const
{
	return m_roster->orderedFrom(current, include_dead);
}

void Room::restPlayer(ServerPlayer *player, const QString &reason, bool discard_cards)
{
	m_playerLifecycle->restPlayer(player, reason, discard_cards);
}

void Room::directRestPlayer(ServerPlayer *player, const QString &reason, bool discard_cards)
{
	m_playerLifecycle->directRestPlayer(player, reason, discard_cards);
}

void Room::unrestPlayer(ServerPlayer *player, bool restore_full_hp, bool restore_original_skills)
{
	m_playerLifecycle->unrestPlayer(player, restore_full_hp, restore_original_skills);
}

bool Room::isRest(ServerPlayer *player) const
{
	return m_playerLifecycle->isRest(player);
}

QList<ServerPlayer *> Room::getRestPlayers() const
{
	return m_playerLifecycle->getRestPlayers();
}
QList<ServerPlayer*> Room::getOtherPlayers(ServerPlayer*except, bool include_dead) const
{
	return m_roster->otherPlayers(current, except, include_dead);
}

QList<ServerPlayer*> Room::getAlivePlayers() const
{
	return m_roster->alivePlayers();
}

QList<ServerPlayer *> Room::getPathBetween(ServerPlayer *from, ServerPlayer *to, bool include_from, bool include_to) const
{
    return m_roster->pathBetween(from, to, include_from, include_to);
}

QList<ServerPlayer *> Room::getClockwisePath(ServerPlayer *from, ServerPlayer *to, bool include_from, bool include_to) const
{
    return m_roster->clockwisePath(from, to, include_from, include_to);
}

QList<ServerPlayer *> Room::getCounterclockwisePath(ServerPlayer *from, ServerPlayer *to, bool include_from, bool include_to) const
{
    return m_roster->counterclockwisePath(from, to, include_from, include_to);
}

void Room::output(const QString&message)
{
	if (Server::isHeadlessMode)
		Server::writeHeadlessLog(message);
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
	player->removeTag("MyDyingSaver");
}

ServerPlayer*Room::getCurrentDyingPlayer() const
{
	QStringList currentdying = getTag("CurrentDying").toStringList();
	if (currentdying.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, getAlivePlayers()){
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
	m_playerLifecycle->revivePlayer(player, sendlog, throw_mark, visible_only);
}

static bool CompareByRole(ServerPlayer*player1, ServerPlayer*player2)
{
	int role1 = player1->getRoleEnum();
	int role2 = player2->getRoleEnum();

	if (role1 != role2)
		return role1 < role2;
	if (role1 == Player::UnknownRole && player1->getRole() != player2->getRole())
		return player1->getRole() < player2->getRole();
	return player1->isAlive() && !player2->isAlive();
}

void Room::updateStateItem()
{
	QString roles;
	QList<ServerPlayer*> players = getPlayers();
	std::sort(players.begin(), players.end(), CompareByRole);
	foreach(ServerPlayer*p, players){
		QString abbreviation = Sanguosha->getRoleAbbreviation(p->getRole());
		if (abbreviation.isEmpty()) {
			qWarning("Cannot update role state: role '%s' has no registered abbreviation.",
				qPrintable(p->getRole()));
			continue;
		}
		QChar c = abbreviation.at(0);
		if (p->isDead()&&!p->property("RestPlayer").toBool()) c = c.toLower();
		roles.append(c);
	}

	doBroadcastNotify(S_COMMAND_UPDATE_STATE_ITEM, roles);
}

void Room::killPlayer(ServerPlayer*victim, DamageStruct*reason, HpLostStruct*hplost)
{
	m_playerLifecycle->killPlayer(victim, reason, hplost);
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
		judge(judge_struct);//终止并重新判定
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
	return m_cardMovement->getNCards(n, update_pile_number, isTop);
}

QStringList Room::aliveRoles(ServerPlayer*except) const
{
	QStringList roles;
	foreach(ServerPlayer*p, getPlayers()){
		if (p != except&&(p->isAlive() || p->property("RestPlayer").toBool()))
			roles << p->getRole();
	}
	return roles;
}

void Room::gameOver(const QString&winner)
{
	m_gameSession->gameOver(winner);
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
	m_skillRuntime->attachSkillToPlayer(player, skill_name);
}

SkillInstanceRef Room::attachSkillToPlayer(ServerPlayer *player, const QString &skillName, const SkillInstanceRef &parentRef)
{
	return m_skillRuntime->attachSkillToPlayer(player, skillName, parentRef);
}

bool Room::detachAttachedSkill(const SkillInstanceRef &ref)
{
	return m_skillRuntime->detachAttachedSkill(ref);
}

void Room::notifySkillInstanceSnapshot(ServerPlayer *receiver)
{
	m_skillRuntime->notifySkillInstanceSnapshot(receiver);
}

void Room::notifySkillInstanceUpsert(ServerPlayer *owner, const SkillInstance &instance)
{
	m_skillRuntime->notifySkillInstanceUpsert(owner, instance);
}

void Room::notifySkillInstanceRemove(ServerPlayer *owner, const SkillInstance &instance)
{
	m_skillRuntime->notifySkillInstanceRemove(owner, instance);
}

int Room::detachSkillFromPlayer(ServerPlayer *player, const QString &skill_name, bool is_equip,
                                bool acquire_only, bool event_and_log)
{
	return m_skillRuntime->detachSkillFromPlayer(player, skill_name, is_equip,
		acquire_only, event_and_log);
}

int Room::discardSkillInstance(ServerPlayer *chooser, ServerPlayer *owner, const QString &skill_name,
                               bool event_and_log)
{
	return m_skillRuntime->discardSkillInstance(chooser, owner, skill_name, event_and_log);
}

void Room::handleAcquireDetachSkills(ServerPlayer*player, const QStringList&skill_names, bool acquire_only, bool getmark, bool event_and_log)
{
	m_skillRuntime->handleAcquireDetachSkills(player, skill_names, acquire_only,
		getmark, event_and_log);
}

void Room::handleAcquireDetachSkills(ServerPlayer*player, const QString&skill_names, bool acquire_only, bool getmark, bool event_and_log)
{
	handleAcquireDetachSkills(player, skill_names.split("|"), acquire_only, getmark, event_and_log);
}

void Room::acquireOneTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names)
{
	QString st = "OneTurnSkill_" + skill_name;
	QStringList skilllist = player->getTag("OneTurnSkill").toStringList();
	QStringList list = player->getTag(st).toStringList();

	if (!skilllist.contains(st)){
		skilllist << st;
		player->setTag("OneTurnSkill", skilllist);
	}
	foreach(QString str, skill_names){
		if (list.contains(str) || player->hasSkill(str, true)) continue;
		list << str;
	}
	player->setTag(st, list);
	handleAcquireDetachSkills(player, list);
}

void Room::acquireOneTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names)
{
	acquireOneTurnSkills(player, skill_name, skill_names.split("|"));
}

void Room::acquireNextTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names)
{
	QString st = "NextTurnSkill_" + skill_name;
	QStringList skilllist = player->getTag("NextTurnSkill").toStringList();
	QStringList list = player->getTag(st).toStringList();

	if (!skilllist.contains(st)){
		skilllist << st;
		player->setTag("NextTurnSkill", skilllist);
	}
	foreach(QString str, skill_names){
		if (list.contains(str) || player->hasSkill(str, true)) continue;
		list << str;
	}
	player->setTag(st, list);
	handleAcquireDetachSkills(player, list);
}

void Room::acquireNextTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names)
{
	acquireNextTurnSkills(player, skill_name, skill_names.split("|"));
}

bool Room::doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, bool wait)
{
	return m_requests->request(player, command, arg,
		ServerInfo.getCommandTimeout(command, S_SERVER_INSTANCE), wait);
}

ServerPlayer *Room::getActualController(ServerPlayer *player) const
{
	if (player == nullptr)
		return nullptr;

	QSet<ServerPlayer *> visited;
	ServerPlayer *current = player;
	while (current != nullptr) {
		if (visited.contains(current)) {
			QString controllerMark = getControllerMarkName(current->getTag(kControllerNameTag).toString());
			if (!controllerMark.isEmpty())
				const_cast<Room *>(this)->setPlayerMark(current, controllerMark, 0);
			current->removeTag(kControllerNameTag);
			return current;
		}

		visited.insert(current);
		QString controllerName = current->getTag(kControllerNameTag).toString();
		if (controllerName.isEmpty())
			return current;

		ServerPlayer *next = findPlayerByObjectName(controllerName, true);
		if (next == nullptr) {
			const_cast<Room *>(this)->setPlayerMark(current, getControllerMarkName(controllerName), 0);
			current->removeTag(kControllerNameTag);
			return current;
		}

		current = next;
	}

	return player;
}

void Room::setPlayerController(ServerPlayer *target, ServerPlayer *controller)
{
	if (target == nullptr)
		return;

	QString oldControllerName = target->getTag(kControllerNameTag).toString();
	QString newControllerName = (controller == nullptr || controller == target) ? QString() : controller->objectName();
	ServerPlayer *oldController = oldControllerName.isEmpty() ? nullptr : findPlayerByObjectName(oldControllerName, true);
	QString visibleHandMark = getControllerVisibleHandMarkName(target->objectName());

	if (controller != nullptr && controller != target && getActualController(controller) == target)
		return;

	if (oldController != nullptr && oldController != target && oldController != controller) {
		setViewerHandcardVisible(this, oldController, visibleHandMark, false);
		clearControllerPileVisible(target, oldController);
	}

	if (!oldControllerName.isEmpty() && oldControllerName != newControllerName)
		setPlayerMark(target, getControllerMarkName(oldControllerName), 0);

	if (controller == nullptr || controller == target) {
		target->removeTag(kControllerNameTag);
		if (oldController != nullptr && oldController != target) {
			syncKnownHandcards(this, oldController, oldController);
			clearControllerPileVisible(target, oldController);
		}
		if (target->getState() == "offline") {
			setPlayerProperty(target, "state", "trust");
			resetAI(target);
		}
		return;
	}

	target->setTag(kControllerNameTag, newControllerName);
	if (oldControllerName != newControllerName)
		addPlayerMark(target, getControllerMarkName(newControllerName));
	setViewerHandcardVisible(this, controller, visibleHandMark, true);

	syncKnownHandcards(this, controller, target);
	syncControllerPileVisible(target, controller);
}

ServerPlayer *Room::getRequestTarget(ServerPlayer *player) const
{
	return m_requests->requestTarget(player);
}

bool Room::doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, time_t timeOut, bool wait)
{
	return m_requests->request(player, command, arg, timeOut, wait);
}

bool Room::doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command)
{
	return m_requests->broadcastRequest(players, command,
		ServerInfo.getCommandTimeout(command, S_SERVER_INSTANCE));
}

bool Room::doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut)
{
	return m_requests->broadcastRequest(players, command, timeOut);
}

ServerPlayer*Room::doBroadcastRaceRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command,
	time_t timeOut, ResponseVerifyFunction validateFunc, void*funcArg)
{
	return m_requests->raceRequest(players, command, timeOut, validateFunc, funcArg);
}

ServerPlayer*Room::getRaceResult(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut,
	ResponseVerifyFunction validateFunc, void*funcArg)
{
	return m_requests->getRaceResult(players, command, timeOut, validateFunc, funcArg);
}

bool Room::doNotify(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg)
{
	return m_notifier->doNotify(player, static_cast<int>(command), arg);
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, QSanProtocol::CommandType command, const QVariant&arg)
{
	return m_notifier->doBroadcastNotify(players, static_cast<int>(command), arg);
}

bool Room::doBroadcastNotify(QSanProtocol::CommandType command, const QVariant&arg)
{
	return m_notifier->doBroadcastNotify(static_cast<int>(command), arg);
}

// the following functions for Lua
bool Room::doNotify(ServerPlayer*player, int command, const char*arg)
{
	return m_notifier->doNotify(player, command, arg);
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const char*arg)
{
	return m_notifier->doBroadcastNotify(players, command, arg);
}

bool Room::doBroadcastNotify(int command, const char*arg)
{
	return m_notifier->doBroadcastNotify(command, arg);
}

bool Room::doNotify(ServerPlayer*player, int command, const QVariant&arg)
{
	return m_notifier->doNotify(player, command, arg);
}

bool Room::doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const QVariant&arg)
{
	return m_notifier->doBroadcastNotify(players, command, arg);
}

bool Room::doBroadcastNotify(int command, const QVariant&arg)
{
	return m_notifier->doBroadcastNotify(command, arg);
}

// end for Lua

bool Room::getResult(ServerPlayer*player, time_t timeOut)
{
	return m_requests->getResult(player, timeOut);
}

bool Room::verifyRaceReply(ServerPlayer *player, const QVariant &reply, void *funcArg)
{
	return m_requests->verifyRaceReply(player, reply, funcArg);
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
	return m_playerDecisions->askForSkillInvoke(player, skill_name, data, notify);
}

QString Room::askForChoice(ServerPlayer*player, const QString&skill_name, const QString&choices, const QVariant&data,
							const QString&except_choices, const QString&tip)
{
	return m_playerDecisions->askForChoice(player, skill_name, choices, data, except_choices, tip);
}

QString Room::askForTriggerOrder(ServerPlayer*player, const QString&reason, QList<SkillContext> &contexts,
                               bool optional, const QVariant&data)
{
	return m_playerDecisions->askForTriggerOrder(player, reason, contexts, optional, data);
}

void Room::obtainCard(ServerPlayer*target, const Card*card, const CardMoveReason&reason, bool visible)
{
	m_cardMovement->obtainCard(target, card, reason, visible);
}

void Room::obtainCard(ServerPlayer*target, const Card*card, bool visible)
{
	m_cardMovement->obtainCard(target, card, visible);
}

void Room::obtainCard(ServerPlayer*target, int card_id, bool visible)
{
	m_cardMovement->obtainCard(target, card_id, visible);
}

void Room::obtainCard(ServerPlayer*target, const Card*card, const QString&skill_name, bool visible)
{
	m_cardMovement->obtainCard(target, card, skill_name, visible);
}

void Room::obtainCard(ServerPlayer*target, int card_id, const QString&skill_name, bool visible)
{
	m_cardMovement->obtainCard(target, card_id, skill_name, visible);
}

void Room::recastCard(ServerPlayer *player, const Card *card, const QString &skill_name)
{
    m_cardMovement->recastCard(player, card, skill_name);
}

void Room::recastCard(ServerPlayer *player, int card_id, const QString &skill_name)
{
    m_cardMovement->recastCard(player, card_id, skill_name);
}

void Room::recastCards(ServerPlayer *player, const QList<int> &card_ids, const QString &skill_name)
{
    m_cardMovement->recastCards(player, card_ids, skill_name);
}
void Room::recastCardWithDraw(ServerPlayer *player, const Card *card, int draw_count, const QString &skill_name)
{
    m_cardMovement->recastCardWithDraw(player, card, draw_count, skill_name);
}

void Room::recastCardWithDraw(ServerPlayer *player, int card_id, int draw_count, const QString &skill_name)
{
    m_cardMovement->recastCardWithDraw(player, card_id, draw_count, skill_name);
}

void Room::recastCardsWithDraw(ServerPlayer *player, const QList<int> &card_ids, int draw_count, const QString &skill_name)
{
    m_cardMovement->recastCardsWithDraw(player, card_ids, draw_count, skill_name);
}

bool Room::useNullified(const Card*use_card)
{
	return tag["UseHistory"+use_card->toString()].value<CardUseStruct>().nullified_list.contains("_ALL_TARGETS");
}

const Card*Room::isCanceled(const CardEffectStruct&effect)
{
	if (effect.offset_num<1||effect.no_offset||effect.no_respond) return nullptr;
	if (effect.card->isKindOf("TrickCard")&&effect.card->isCancelable(effect)){
		effect.to->setTag("TrickEffectData", QVariant::fromValue(effect));
		return askForNullification(effect.card, effect.from, effect.to, true);
	}else if (effect.card->isKindOf("Slash")){
		setTag("SlashData", QVariant::fromValue(effect));
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

bool Room::verifyNullificationResponse(ServerPlayer *player, const QVariant &response, void *arg)
{
	return m_playerDecisions->verifyNullificationResponse(player, response, arg);
}

const Card*Room::askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	return m_playerDecisions->askForNullification(trick, from, to, positive);
}

const Card*Room::_askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	return m_playerDecisions->_askForNullification(trick, from, to, positive);
}

int Room::askForCardChosen(ServerPlayer*player, ServerPlayer*who, const QString&flags, const QString&reason,
	bool handcard_visible, Card::HandlingMethod method, const QList<int>&disabled_ids, bool can_cancel)
{
	return m_playerDecisions->askForCardChosen(player, who, flags, reason, handcard_visible, method,
		disabled_ids, can_cancel);
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
	return m_playerDecisions->askForCard(player, pattern, prompt, data, method, m_who, isRetrial,
		skill_name, isProvision, m_toCard);
}

const Card*Room::askForUseCard(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index,
	Card::HandlingMethod method, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseCardStruct(player, pattern, prompt, notice_index, method, addHistory, who, whocard, flag).card;
}

CardUseStruct Room::askForUseCardStruct(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index,
	Card::HandlingMethod method, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return m_playerDecisions->askForUseCardStruct(player, pattern, prompt, notice_index, method,
		addHistory, who, whocard, flag);
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
	return m_playerDecisions->askForUseSlashToStruct(slasher, victims, prompt, distance_limit,
		disable_extra, addHistory, who, whocard, flag);
}

CardUseStruct Room::askForUseSlashToStruct(ServerPlayer*slasher, ServerPlayer*victim, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	return askForUseSlashToStruct(slasher, QList<ServerPlayer*>()<<victim, prompt, distance_limit, disable_extra, addHistory, who, whocard, flag);
}

int Room::askForAG(ServerPlayer*player, const QList<int>&card_ids, bool refusable, const QString&reason, const QString&prompt)
{
	return m_playerDecisions->askForAG(player, card_ids, refusable, reason, prompt);
}

const Card*Room::askForCardShow(ServerPlayer*player, ServerPlayer*requestor, const QString&reason)
{
	return m_playerDecisions->askForCardShow(player, requestor, reason);
}

const Card*Room::askForSinglePeach(ServerPlayer*player, ServerPlayer*dying)
{
	return m_playerDecisions->askForSinglePeach(player, dying);
}

void Room::addPlayerHistory(ServerPlayer*player, const QString&key, int times)
{
	if (player){
		if (key == ".") player->clearHistory();
		else if (times == 0) player->clearHistory(key);
		else player->addHistory(key, times);
	}

	JsonArray arg;
	if (player) {
		arg << player->objectName() << key << times;
		doNotify(player, S_COMMAND_ADD_HISTORY, arg);
	}
	else {
		arg << key << times;
		doBroadcastNotify(S_COMMAND_ADD_HISTORY, arg);
	}
}

void Room::playAudioEffect(const QString&filename, bool superpose)
{
	JsonArray arg;
	arg << filename << superpose;

	doBroadcastNotify(S_COMMAND_PLAY_AUDIO, arg);
}

void Room::setPlayerFlag(ServerPlayer*player, const QString&flag)
{
	m_playerState->setPlayerFlag(player, flag);
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
	m_playerState->setPlayerProperty(player, property_name, value);
}

void Room::slotSetProperty(ServerPlayer*player, const char*property_name, const QVariant&value)
{
	player->setProperty(property_name, value);
	playerPropertySet = true;
}

void Room::safeSetPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value)
{
	m_playerState->safeSetPlayerProperty(player, property_name, value);
}

void Room::setPlayerMark(ServerPlayer*player, const QString&mark, int value, QList<ServerPlayer*> only_viewers)
{
	m_playerState->setPlayerMark(player, mark, value, only_viewers);
}

void Room::clearClub(const QString &club_name){
    foreach(ServerPlayer *p, getAlivePlayers()){
        if (p->hasClub(club_name)){
            p->removeCurrentClub();
        }
    }
}

QList<ServerPlayer *> Room::getPlayersByClub(const QString &club_name) const{
    QList<ServerPlayer *> ps;
    foreach(ServerPlayer *p, getAlivePlayers()){
        if (p->hasClub(club_name))
            ps.append(p);
    }
    return ps;
}

QList<ServerPlayer *> Room::getPlayersWithNoClub() const{
    QList<ServerPlayer *> ps;
    foreach(ServerPlayer *p, getAlivePlayers()){
        if (!p->hasClub())
            ps.append(p);
    }
    return ps;
}

void Room::addPlayerMark(ServerPlayer*player, const QString&mark, int add_num, QList<ServerPlayer*> only_viewers)
{
	m_playerState->addPlayerMark(player, mark, add_num, only_viewers);
}

void Room::removePlayerMark(ServerPlayer*player, const QString&mark, int remove_num)
{
	m_playerState->removePlayerMark(player, mark, remove_num);
}

void Room::setPlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
	const QString&pattern, bool single_turn, const QString&reason)
{
	m_playerState->setPlayerCardLimitation(player, limit_list, pattern, single_turn, reason);
}

void Room::removePlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
	const QString&pattern, const QString&reason)
{
	m_playerState->removePlayerCardLimitation(player, limit_list, pattern, reason);
}

void Room::removePlayerCardLimitationByReason(ServerPlayer*player, const QString&reason)
{
	m_playerState->removePlayerCardLimitationByReason(player, reason);
}

void Room::clearPlayerCardLimitation(ServerPlayer*player, bool single_turn)
{
	m_playerState->clearPlayerCardLimitation(player, single_turn);
}

void Room::setPlayerEquipsNullified(ServerPlayer*player, const QString&pattern,
	const QString&reason, bool single_turn)
{
	m_playerState->setPlayerEquipsNullified(player, pattern, reason, single_turn);
}

void Room::removePlayerEquipsNullified(ServerPlayer*player, const QString&pattern, const QString&reason)
{
	m_playerState->removePlayerEquipsNullified(player, pattern, reason);
}

void Room::addCardMark(int card_id, const QString&mark, int add_num, ServerPlayer*who)
{
	m_cardState->addCardMark(card_id, mark, add_num, who);
}

void Room::addCardMark(const Card*card, const QString&mark, int add_num, ServerPlayer*who)
{
	m_cardState->addCardMark(card, mark, add_num, who);
}

void Room::removeCardMark(int card_id, const QString&mark, int remove_num)
{
	m_cardState->removeCardMark(card_id, mark, remove_num);
}

void Room::removeCardMark(const Card*card, const QString&mark, int remove_num)
{
	m_cardState->removeCardMark(card, mark, remove_num);
}

void Room::setCardMark(const Card*card, const QString&mark, int value, ServerPlayer*who)
{
	m_cardState->setCardMark(card, mark, value, who);
}

void Room::setCardMark(int card_id, const QString&mark, int value, ServerPlayer*who)
{
	m_cardState->setCardMark(card_id, mark, value, who);
}

void Room::setCardFlag(const Card*card, const QString&flag, ServerPlayer*who)
{
	m_cardState->setCardFlag(card, flag, who);
}

void Room::setCardFlag(int card_id, const QString&flag, ServerPlayer*who)
{
	m_cardState->setCardFlag(card_id, flag, who);
}

void Room::clearCardFlag(const Card*card, ServerPlayer*who)
{
	m_cardState->clearCardFlag(card, who);
}

void Room::clearCardFlag(int card_id, ServerPlayer*who)
{
	m_cardState->clearCardFlag(card_id, who);
}

void Room::setCardTip(int card_id, const QString&tip)
{
	m_cardState->setCardTip(card_id, tip);
}

void Room::clearCardTip(int card_id)
{
	m_cardState->clearCardTip(card_id);
}

void Room::addPlayerToRoster(ServerPlayer *player)
{
	m_roster->add(player);
}

void Room::removePlayerFromRoster(ServerPlayer *player)
{
	m_roster->remove(player);
}

void Room::replacePlayerOrder(const QList<ServerPlayer *> &players)
{
	m_roster->replacePlayers(players);
}

ServerPlayer*Room::addSocket(ClientSocket*socket)
{
	return m_playerLifecycle->addSocket(socket);
}

ServerPlayer*Room::addAIPlayer()
{
	return m_playerLifecycle->addAIPlayer();
}

bool Room::isFull() const
{
	return getPlayers().length() >= player_count;
}

bool Room::isFinished() const
{
	return m_gameSession->isTerminal();
}

bool Room::hasGameStarted() const
{
	return m_gameSession->hasGameStarted();
}

bool Room::isGamePlaying() const
{
	return m_gameSession->isPlaying();
}

void Room::markGameReadyCompleted()
{
	m_gameSession->markGameReadyCompleted();
}

bool Room::canPause(ServerPlayer*player) const
{
	if (!player) return false;

	if (player->isOwner()&&isFull()){
		foreach(ServerPlayer*p, getAlivePlayers()){
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
	return player_count - getPlayers().length();
}

QString Room::getMode() const
{
	return mode;
}

const Scenario*Room::getScenario() const
{
	return scenario;
}

void Room::swapPile()
{
	m_cardMovement->swapPile();
}

QList<int> Room::getDiscardPile()
{
	return m_cardMovement->discardPile();
}

QList<int> &Room::getDrawPile()
{
	return m_cardMovement->drawPile();
}

ServerPlayer*Room::findPlayer(const QString&general_name, bool include_dead) const
{
	return m_roster->findByGeneral(general_name, include_dead);
}

QList<ServerPlayer*>Room::findPlayersBySkillName(const QString&skill_name) const
{
	return m_roster->findBySkill(skill_name, current);
}

ServerPlayer*Room::findPlayerBySkillName(const QString&skill_name, bool include_lose) const
{
	return m_roster->findFirstBySkill(skill_name, current, include_lose);
}

ServerPlayer*Room::findPlayerByObjectName(const QString&objectName, bool include_dead) const
{
	return m_roster->findByObjectName(objectName, current, include_dead);
}

void Room::installEquip(ServerPlayer*player, const QString&equip_name)
{
	int card_id = getCardFromPile(equip_name);
	if (card_id>-1){
		CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, player->objectName());
		QList<CardsMoveStruct> moves;
		const Card*card = Sanguosha->getCard(card_id);
		const EquipCard*equip = (const EquipCard*)card->getRealCard();
		QList<int> occupy_slots = equip->getOccupyLocations();
		foreach(int slot, occupy_slots){
			if(!player->hasEquipArea(slot)) return;
		}
		QList<int> replaced_ids;
        foreach(int slot, occupy_slots) {
            QList<const Card*> slot_equips;
            foreach(const Card* c, player->getEquips()) {
                const EquipCard* e = qobject_cast<const EquipCard*>(c->getRealCard());
                if (e && e->getOccupyLocations().contains(slot))
                    slot_equips << c;
            }

            if (slot_equips.length() >= player->getEquipArea(slot)) {
                foreach(const Card* c, slot_equips) {
                    const EquipCard* e = qobject_cast<const EquipCard*>(c->getRealCard());
                    if (!replaced_ids.contains(e->getEffectiveId())) {
                        reason.m_reason = CardMoveReason::S_REASON_PUT;
                        moves << CardsMoveStruct(e->getEffectiveId(), nullptr, Player::DiscardPile, reason);
                        replaced_ids.append(e->getEffectiveId());
                        break;
                    }
                }
            }
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
		//delete smart_ai;  changeHero在非主线程会闪退
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
	m_playerLifecycle->changeHero(player, new_general, full_state, invokeStart,
		isSecondaryHero, sendLog, start_hp);
}

lua_State*Room::getLuaState() const
{
	return m_runtime ? m_runtime->lua().state() : nullptr;
}

bool Room::hasLuaRuntime() const
{
	return m_runtime && m_runtime->lua().rawState();
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
	return m_cardMovement->drawCard(isTop);
}

void Room::prepareForStart()
{
	m_gameSession->prepareForStart();
}

void Room::reportDisconnection()
{
	ServerPlayer*player = qobject_cast<ServerPlayer*>(sender());
	if (player == nullptr) return;
	clearControllerRelation(player);

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
		removePlayerFromRoster(player);
	} else if (player->getRole().isEmpty()){
		// second case
		if (getPlayers().length() < player_count){
			player->setParent(nullptr);
			removePlayerFromRoster(player);

			if (player->getState() != "robot"){
				QString leaveStr = "<font color=#000000>已离开游戏</font>";//tr("<font color=#000000>Player <b>%1</b> left the game</font>").arg(player->screenName());
				ChatPayload leave;
				leave.text = leaveStr;
				speakCommand(player, leave.toVariant());
			}

			doBroadcastNotify(S_COMMAND_REMOVE_PLAYER, player->objectName());
		} else {
			// 房間已滿且 Room::run 已啟動 (Game Seed 已送出), 但身份尚未分配。
			// 舊邏輯不移除玩家也不標 offline, doRequest 會對空 socket 等到逾時,
			// 殘留 RoomThread 再與下一局 Room 建構在 main 上互鎖。
			if (player->m_isWaitingReply)
				player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
			m_gameSession->abort(GameSessionController::TerminationCause::Disconnected);
			emit game_over("");
			return;
		}
	} else {
		// fourth case
		if (player->m_isWaitingReply)
			player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
		setPlayerProperty(player, "state", "offline");

		// isOffline() also covers robots, so counting it here ended the room the
		// moment the only human dropped -- taking Server::gameOver() and the
		// reconnect registration with it. A human seat is kept alive whatever its
		// current connection state; the trust AI plays on and the room is reaped
		// when the game really finishes.
		bool human_seat_remains = false;
		foreach(ServerPlayer*p, getPlayers()){
			if (p->getState() == "robot") continue;
			human_seat_remains = true;
			break;
		}

		if (!human_seat_remains){
			m_gameSession->abort(GameSessionController::TerminationCause::Disconnected);
			emit game_over("");
			return;
		}
	}

	if (player->isOwner()){
		player->setOwner(false);
		broadcastProperty(player, "owner");
		foreach(ServerPlayer*p, getPlayers()){
			if (p->isOffline()) continue;
			p->setOwner(true);
			broadcastProperty(p, "owner");
			break;
		}
	}
}

void Room::trustCommand(ServerPlayer*player, const QVariant&arg)
{
	TrustPayload payload;
	if (!TrustPayload::parse(arg, &payload))
		return;

	ServerPlayer *target = getActualController(player);
	if (target == nullptr)
		target = player;

	QList<ServerPlayer *> waitingPlayers;
	target->acquireLock(ServerPlayer::SEMA_MUTEX);
	bool enteringTrust = payload.trusted && target->isOnline();
	target->releaseLock(ServerPlayer::SEMA_MUTEX);

	if (enteringTrust) {
		foreach (ServerPlayer *candidate, getAllPlayers(true)) {
			if (candidate == nullptr)
				continue;
			if (getActualController(candidate) != target)
				continue;

			candidate->acquireLock(ServerPlayer::SEMA_MUTEX);
			bool isWaitingReply = candidate->m_isWaitingReply;
			candidate->releaseLock(ServerPlayer::SEMA_MUTEX);
			if (isWaitingReply)
				waitingPlayers << candidate;
		}
	}

	target->acquireLock(ServerPlayer::SEMA_MUTEX);
	target->setState(payload.trusted ? "trust" : "online");
	target->releaseLock(ServerPlayer::SEMA_MUTEX);

	foreach (ServerPlayer *waitingPlayer, waitingPlayers)
		waitingPlayer->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);

	broadcastProperty(target, "state");
	return;
}

void Room::pauseCommand(ServerPlayer*player, const QVariant&arg)
{
	PausePayload payload;
	if (!PausePayload::parse(arg, &payload))
		return;

	if (canPause(player)){
		QMutexLocker locker(&m_mutex);
		if (game_paused != payload.paused){
			game_paused = payload.paused;
			JsonArray json;
			json << S_GAME_EVENT_PAUSE << payload.paused;
			doNotify(player, S_COMMAND_LOG_EVENT, json);
			if (!game_paused) m_waitCond.wakeAll();
		}
	}
}

void Room::processRequestCheat(ServerPlayer*player, const QVariant&arg)
{
	player->m_cheatArgs = QVariant();
	if (!Config.EnableCheat) return;
	CheatRequestPayload payload;
	if (!CheatRequestPayload::parse(arg, &payload))
		return;
	//@todo: synchronize this
	player->m_cheatArgs = payload.toVariant();
	player->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
	return;
}

bool Room::makeSurrender(ServerPlayer*initiator)
{
	return m_gameSession->makeSurrender(initiator);
}

void Room::processRequestSurrender(ServerPlayer*player, const QVariant &arg)
{
	SurrenderRequestPayload payload;
	if (!SurrenderRequestPayload::parse(arg, &payload))
		return;

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

void Room::processClientPacket(
	const QString &request, const ProtocolMessage &message)
{
	ServerPlayer*player = qobject_cast<ServerPlayer*>(sender());
#ifdef LOGNETWORK
	emit Sanguosha->logNetworkMessage("recv "+player->objectName()+":"+request);
#endif // LOGNETWORK
	if (isFinished()){
		if (player&&player->isOnline()) {
			DiagnosticPayload diagnostic;
			diagnostic.code = QStringLiteral("game_over");
			diagnostic.message = QStringLiteral("GAME_OVER");
			diagnostic.fatal = false;
			doNotify(player, S_COMMAND_WARN, diagnostic.toVariant());
		}
		return;
	}
	m_requests->processClientPacket(player, message, request);
}

void Room::addRobotCommand(ServerPlayer*player, const QVariant&arg)
{
	GameRng::Binding rngBinding(m_runtime->rng());
	if (player&&!player->isOwner())
		return;

	AddRobotPayload payload;
	if (!AddRobotPayload::parse(arg, &payload))
		return;

	int r = 0;
	const int add_num = payload.fillRemaining
		? player_count - getPlayers().length()
		: payload.count;
	if (Server::isHeadlessMode)
		Server::writeHeadlessLog(QString("[AUTOTEST] addRobot fill_remaining=%1 count=%2 fill=%3 players=%4/%5 owner=%6")
			.arg(payload.fillRemaining).arg(payload.count).arg(add_num)
			.arg(getPlayers().length()).arg(player_count)
			.arg(player ? (player->isOwner() ? "yes" : "no") : "null"));
	foreach(ServerPlayer*p, getPlayers()){
		if (p->getState() == "robot") r++;
	}

	QStringList devs;
	QStringList all_generals;
	foreach(const General*general, Sanguosha->findChildren<const General*>()) {
		all_generals << general->objectName();
		if (general->objectName().contains("dev_"))
			devs << general->objectName();
	}
	qsanShuffle(devs);

	for (int i = 0; i < add_num; i++){
		if (isFull()) break;
		ServerPlayer*robot = new ServerPlayer(this);
		robot->setState("robot");

		addPlayerToRoster(robot);

		//QString robot_name = tr("Computer %1").arg(QChar('A' + r));
		r++;
		QString avatar;
		if (!devs.isEmpty()) {
			avatar = devs.takeFirst();
		} else {
			if (!all_generals.isEmpty()) {
				avatar = all_generals.at(qsanRandomBounded(all_generals.size()));
			} else {
				avatar = "";
			}
		}
		signup(robot, QString("神小杀0%1号").arg(r), avatar, true);

		//speakCommand(robot, robot_name);

		broadcastProperty(robot, "state");
	}
}

ServerPlayer*Room::getOwner() const
{
	foreach(ServerPlayer*player, getPlayers())
		if (player->isOwner()) return player;
	return nullptr;
}

void Room::setReadyCommand(ServerPlayer *, const QVariant &payload)
{
    ReadyPayload readyPayload;
    if (!ReadyPayload::parse(payload, &readyPayload))
        return;

	if (readyPayload.ready && isFull() && m_gameSession->requestStart())
		start();
}

void Room::signup(ServerPlayer*player, const QString&screen_name, const QString&avatar, bool is_robot)
{
	m_playerLifecycle->signup(player, screen_name, avatar, is_robot);
}



void Room::chooseGenerals(QList<ServerPlayer*> players)
{
	m_gameSession->chooseGenerals(players);
}


bool Room::changeBGM(const QString&bgm_name, bool reset, QList<ServerPlayer*> to_assign)
{
	QString bgm = QString("audio/system/BGM/%1.ogg").arg(bgm_name);
	if (!QFile::exists(bgm)) return false;
	if(to_assign.isEmpty()) to_assign = getPlayers();
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
	m_gameSession->run();
}

void Room::assignRoles()
{
	m_gameSession->assignRoles();
}

void Room::swapSeat(ServerPlayer*a, ServerPlayer*b)
{
	m_roster->swapSeats(a, b);
	const QList<ServerPlayer *> players = getPlayers();
	QStringList player_circle;
	foreach(ServerPlayer*player, players)
		player_circle << player->objectName();
	doBroadcastNotify(S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(player_circle));

	foreach (ServerPlayer *player, players) {
		broadcastProperty(player, "seat");
		broadcastProperty(player, "player_seat");
	}
}

void Room::adjustSeats()
{
	m_roster->adjustSeats(mode == "02_1v1");
	const QList<ServerPlayer *> players = getPlayers();

	QStringList player_circle;
	for (int i = 0; i < players.length(); i++){
		broadcastProperty(players[i], "player_seat");
		player_circle << players[i]->objectName();
	}

	// tell the players about the seat, and the first is always the lord
	doBroadcastNotify(S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(player_circle));
}

int Room::getCardFromPile(const QString&card_pattern)
{
	return m_cardMovement->getCardFromPile(card_pattern);
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
	const QVariantMap nbbody = makeChatMessage(player->objectName(), sentence);\
	doNotify(player, S_COMMAND_SPEAK, nbbody);\
}
	bool broadcast = true;
	if (player){
		ChatPayload payload;
		if (!ChatPayload::parse(arg, &payload))
			return;
		const QString sentence = payload.text;
		if (sentence.startsWith("$")){
			QString new_sentence = sentence;
			new_sentence = new_sentence.mid(1);
			QStringList _new_sentence = new_sentence.split(":");

			if (_new_sentence.length() == 1){
				QString audio = QString("audio/skill/%1.ogg").arg(new_sentence);
				if (QFile::exists(audio)){
					if (Sanguosha->translate(sentence) != sentence){
						const QVariantMap body = makeChatMessage(
							player->objectName(), Sanguosha->translate(sentence));
						m_chatHistory << body;
						doBroadcastNotify(S_COMMAND_SPEAK, body);
					}
					broadcastSkillInvoke(new_sentence);
				}
			} else if (_new_sentence.length() == 2){
				QString audio = QString("audio/skill/%1%2.ogg").arg(_new_sentence.first()).arg(_new_sentence.last());
				if (QFile::exists(audio)){
					QString _sentence = "$" + _new_sentence.first() + _new_sentence.last();
					if (Sanguosha->translate(_sentence) != _sentence){
						const QVariantMap body = makeChatMessage(
							player->objectName(), Sanguosha->translate(_sentence));
						m_chatHistory << body;
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
				const QVariantMap body = makeChatMessage(
					player->objectName(), Sanguosha->translate(sentence));
				m_chatHistory << body;
				doBroadcastNotify(S_COMMAND_SPEAK, body);
			}
			Sanguosha->playAudioEffect(filename);
			return;
		}
		if (sentence.startsWith(".flower ") || sentence.startsWith(".egg ")){
			QString command = sentence.split(" ").first();
			QString target_name = sentence.mid(command.length() + 1).trimmed();

			ServerPlayer *target = NULL;
			foreach (ServerPlayer *p, getPlayers()){
				if (p->objectName() == target_name ||
					p->getGeneralName() == target_name ||
					p->screenName() == target_name){
					target = p;
					break;
				}
			}

			if (target && target != player){
				if (command == ".flower"){
					showFlower(player->objectName(), target->objectName());
				} else if (command == ".egg"){
					showEgg(player->objectName(), target->objectName());
				}

				QString friendly_msg;
				if (command == ".flower"){
					friendly_msg = QString("%1 送了一朵花给 %2").arg(player->screenName()).arg(target->screenName());
				} else {
					friendly_msg = QString("%1 砸了一个蛋给 %2").arg(player->screenName()).arg(target->screenName());
				}

				const QVariantMap body = makeChatMessage(QStringLiteral("server"), friendly_msg);
				m_chatHistory << body;
				doBroadcastNotify(S_COMMAND_SPEAK, body);
			}
			return;
		}
		if(Config.EnableCheat){
			if (sentence == ".BroadcastRoles"){
				_NO_BROADCAST_SPEAKING
				foreach(ServerPlayer*p, getAlivePlayers())
					broadcastProperty(p, "role", p->getRole());
			} else if (sentence.startsWith(".BroadcastRoles=")){
				_NO_BROADCAST_SPEAKING
				QString name = sentence.mid(12);
				foreach(ServerPlayer*p, getAlivePlayers()){
					if (p->objectName() == name || p->getGeneralName() == name){
						broadcastProperty(p, "role", p->getRole());
						break;
					}
				}
			} else if (sentence == ".ShowHandCards"){
				_NO_BROADCAST_SPEAKING
	
				const QVariantMap body = makeChatMessage(
					player->objectName(), QStringLiteral("----------"));
				doNotify(player, S_COMMAND_SPEAK, body);
	
				foreach(ServerPlayer*p, getAlivePlayers()){
					if (!p->isKongcheng()){
						QStringList handcards;
						foreach(int id, p->handCards())
							handcards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
						const QVariantMap body = makeChatMessage(
							p->objectName(), handcards.join("，"));
						doNotify(player, S_COMMAND_SPEAK, body);
					}
				}
				doNotify(player, S_COMMAND_SPEAK, body);
			} else if (sentence.startsWith(".ShowHandCards=")){
				_NO_BROADCAST_SPEAKING
				QString name = sentence.mid(15);
				foreach(ServerPlayer*p, getAlivePlayers()){
					if (p->objectName() == name || p->getGeneralName() == name){
						if (!p->isKongcheng()){
							QStringList handcards;
							foreach(int id, p->handCards())
								handcards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
							const QVariantMap body = makeChatMessage(
								p->objectName(), handcards.join("，"));
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
					foreach(ServerPlayer*p, getAlivePlayers()){
						if (p->objectName() == name || p->getGeneralName() == name){
							if (!p->getPile(pile_name).isEmpty()){
								QStringList pile_cards;
								foreach(int id, p->getPile(pile_name))
									pile_cards << QString("<b>%1</b>").arg(Sanguosha->getEngineCard(id)->getLogName());
	
								const QVariantMap body = makeChatMessage(
									p->objectName(), pile_cards.join("，"));
								doNotify(player, S_COMMAND_SPEAK, body);
							}
							break;
						}
					}
				}
			} else if (sentence == ".ShowHuashen"){
				_NO_BROADCAST_SPEAKING
				foreach(ServerPlayer*zuoci, getAlivePlayers()){
					if(zuoci->property("Huashens").toString().isEmpty()) continue;
					QStringList huashen_name;
					foreach(QString name, zuoci->property("Huashens").toString().split("+"))
						huashen_name << QString("<b>%1</b>").arg(Sanguosha->translate(name));
	
					const QVariantMap body = makeChatMessage(
						zuoci->objectName(), huashen_name.join("，"));
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
				PausePayload pausePayload;
				pausePayload.paused = true;
				pauseCommand(player, pausePayload.toVariant());
			} else if (sentence == ".Resume"){
				_NO_BROADCAST_SPEAKING
				PausePayload pausePayload;
				pausePayload.paused = false;
				pauseCommand(player, pausePayload.toVariant());
			}
		}
		if (broadcast){
			const QVariantMap body = makeChatMessage(player->objectName(), sentence);
			m_chatHistory << body;
			doBroadcastNotify(S_COMMAND_SPEAK, body);
		}
	}
	return;
#undef _NO_BROADCAST_SPEAKING
}

CardUseStruct Room::getUseStruct(const Card*card)
{
	return tag["UseHistory"+card->toString()].value<CardUseStruct>();
}

SkillInstanceRef Room::resolveSkillInstanceRootRef(const SkillInstanceRef &ref) const
{
	return m_skillRuntime->resolveSkillInstanceRootRef(ref);
}

bool Room::resolveCardSkillInstance(CardUseStruct &use)
{
	return m_skillRuntime->resolveCardSkillInstance(use);
}

bool Room::areCardTargetsLegal(const CardUseStruct &use) const
{
	if (!use.card || !use.from) return false;
	if (use.activationRef.isValid()) {
		const ViewAsSkillV2 *activeSkill = dynamic_cast<const ViewAsSkillV2 *>(
			Sanguosha->getViewAsSkill(use.activationRef.key.skillName));
		// resolveActiveSkillRequest() has already checked V2's independent target
		// contract; its proxy card intentionally rejects generic targetFilter().
		if (activeSkill) return true;
	}

	QList<const Player *> selected;
	if (use.card->targetFixed())
		return use.to.isEmpty() && use.card->targetsFeasible(selected, use.from);

	foreach (ServerPlayer *target, use.to) {
		if (!target) return false;
		int maxVotes = 0;
		if (!use.card->targetFilter(selected, target, use.from, maxVotes) || maxVotes < 1)
			return false;
		selected << target;
	}
	return use.card->targetsFeasible(selected, use.from);
}

const Card *Room::resolveActiveSkillRequest(ServerPlayer *player, const ViewAsSkillV2 *skill,
                                             const ActiveSkillRequest &request) const
{
	if (!player || !skill || request.initiator != player || !request.activationRef.isValid())
		return nullptr;
	const bool hasActivationInstance = player->hasSkillInstance(
		request.activationRef.key.skillName, request.activationRef.key.instanceID);
	const bool continuesViewAsEffect = hasViewAsSkillEffect(player, skill->objectName());
	if (request.activationRef.ownerObjectName != player->objectName()
		|| request.activationRef.key.skillName != skill->objectName()
		|| (!hasActivationInstance && !continuesViewAsEffect))
		return nullptr;
	if (!skill->canActivate(request) || !skill->cardSelectionFeasible(request))
		return nullptr;
	{
		// 額度（limit_scope）必須在 create 前攔截；不可只靠後續 reserve
		// （cost 在 reserve 之前，否則會先 askForChoice 再失敗）。
		SkillContext usageCtx;
		usageCtx.invoker = player;
		usageCtx.owner = player;
		usageCtx.initiator = player;
		usageCtx.activationRef = request.activationRef;
		usageCtx.instanceID = request.activationRef.key.instanceID;
		if (!skill->isUsable(usageCtx))
			return nullptr;
	}
	if (skill->targetMode() == ViewAsSkillV2::NoTarget && !request.selectedTargetNames.isEmpty())
		return nullptr;
	QList<const Player *> selectedTargets;
	QSet<QString> selectedTargetNames;
	if (skill->targetMode() == ViewAsSkillV2::SelectTargets) {
		foreach (const QString &name, request.selectedTargetNames) {
			if (selectedTargetNames.contains(name)) return nullptr;
			ServerPlayer *candidate = findPlayerByObjectName(name);
			if (!candidate || !skill->canSelectTarget(request, selectedTargets, candidate))
				return nullptr;
			selectedTargetNames.insert(name);
			selectedTargets << candidate;
		}
		if (!skill->targetsFeasible(request, selectedTargets)) return nullptr;
	}

	QSet<int> selectable;
	foreach (const Card *owned, player->getCards("he"))
		selectable.insert(owned->getEffectiveId());
	if (skill->isResponseOrUse()) {
		foreach (int id, player->getHandPile())
			selectable.insert(id);
	}
	foreach (int id, skill->getExpandPileCardIds(player))
		selectable.insert(id);
	ActiveSkillRequest checked = request;
	checked.selectedCardIds.clear();
	foreach (int id, request.selectedCardIds) {
		// Removing on acceptance also rejects a forged request that repeats one card ID.
		if (!selectable.remove(id)) return nullptr;
		const Card *candidate = Sanguosha->getCard(id);
		if (!candidate || !skill->canSelectCard(checked, candidate)) return nullptr;
		checked.selectedCardIds << id;
	}
	const Card *card = skill->createCard(checked);
	if (!card) return nullptr;
	Card *mutableCard = const_cast<Card *>(card);
	mutableCard->setActivationSkill(checked.activationRef.key.skillName,
		checked.activationRef.key.instanceID);
	SkillInstanceRef sourceRef = resolveSkillInstanceRootRef(checked.activationRef);
	if (!sourceRef.isValid() && continuesViewAsEffect)
		sourceRef = checked.activationRef;
	if (!sourceRef.isValid()) return nullptr;
	mutableCard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
	return card;
}

bool Room::isAIMarkVisibleTo(const ServerPlayer *owner, const QString &mark,
                             const ServerPlayer *viewer) const
{
    return m_aiDecisions->isMarkVisibleTo(owner, mark, viewer);
}

AIWorldView Room::buildAIWorldView(ServerPlayer *viewer) const
{
    return m_aiDecisions->buildWorldView(viewer);
}

AIRequest Room::makeAIRequest(ServerPlayer *player, AIRequest::DecisionKind kind,
                              CardUseStruct::CardUseReason reason, const QString &pattern,
                              const QString &prompt, Card::HandlingMethod method) const
{
	return m_aiDecisions->makeRequest(player, kind, reason, pattern, prompt, method);
}

bool Room::buildAiSkillActionRequest(ServerPlayer *player, const SkillInstance &instance,
                                     CardUseStruct::CardUseReason reason, const QString &pattern,
                                     const QString &prompt, Card::HandlingMethod method,
                                     AIRequest &aiRequest) const
{
	return m_aiDecisions->buildSkillActionRequest(player, instance, reason, pattern,
		prompt, method, aiRequest);
}

bool Room::applyAIResult(ServerPlayer *player, const AIRequest &request,
                         const AIResult &result, CardUseStruct &cardUse) const
{
	return m_aiDecisions->applyResult(player, request, result, cardUse);
}

bool Room::decideAiAction(ServerPlayer *player, const AIRequest &request,
                          CardUseStruct &cardUse) const
{
	return m_aiDecisions->decide(player, request, cardUse);
}

bool Room::decideAiSkillAction(ServerPlayer *player, CardUseStruct::CardUseReason reason,
                               const QString &pattern, const QString &prompt,
                               Card::HandlingMethod method, CardUseStruct &cardUse) const
{
	return m_aiDecisions->decideSkillAction(player, reason, pattern, prompt, method, cardUse);
}

int Room::getAiSkillActionInstanceId(ServerPlayer *player, const QString &skillName) const
{
	return m_aiDecisions->skillActionInstanceId(player, skillName);
}

AiLegacyRequestView Room::getAiSkillActionContext(ServerPlayer *player,
                                                  const QString &skillName) const
{
	return m_aiDecisions->skillActionContext(player, skillName,
		CardUseStruct::CARD_USE_REASON_PLAY, QString(), QString(), Card::MethodUse);
}

AiLegacyRequestView Room::getAiSkillActionContext(ServerPlayer *player,
                                                  const QString &skillName,
                                                  CardUseStruct::CardUseReason reason,
                                                  const QString &pattern,
                                                  const QString &prompt,
                                                  Card::HandlingMethod method) const
{
	return m_aiDecisions->skillActionContext(player, skillName, reason, pattern, prompt, method);
}

bool Room::reserveActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context)
{
	return m_skillRuntime->reserveActiveSkillUsage(skill, context);
}

void Room::releaseActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context)
{
	m_skillRuntime->releaseActiveSkillUsage(skill, context);
}

void Room::commitActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context)
{
	m_skillRuntime->commitActiveSkillUsage(skill, context);
}

SkillExecutionRegistry::Guard Room::beginSkillExecution(const QVariant &backingData)
{
	return m_skillRuntime->beginSkillExecution(backingData);
}

SkillExecutionRegistry::Guard Room::beginSkillExecution(SkillContext &context, const QVariant &backingData)
{
	return m_skillRuntime->beginSkillExecution(context, backingData);
}

SkillExecutionRegistry::Entry *Room::findSkillExecution(qint64 executionID) const
{
	return m_skillRuntime->findSkillExecution(executionID);
}

SkillContext Room::getSkillExecutionContext(qint64 executionID) const
{
	return m_skillRuntime->getSkillExecutionContext(executionID);
}

void Room::setSkillExecutionContext(qint64 executionID, const SkillContext &context)
{
	m_skillRuntime->setSkillExecutionContext(executionID, context);
}

void Room::recordSkillExecutionAudit(const SkillContext &context, SkillExecutionResult result) const
{
	QVariantMap audit;
	audit["schema"] = 1;
	audit["executionID"] = context.executionID;
	audit["event"] = int(context.current_event);
	audit["result"] = int(result);
	audit["skill"] = context.skill_name;
	audit["sourceOwner"] = context.sourceRef.ownerObjectName;
	audit["sourceSkill"] = context.sourceRef.key.skillName;
	audit["sourceID"] = context.sourceRef.key.instanceID;
	audit["activationOwner"] = context.activationRef.ownerObjectName;
	audit["activationSkill"] = context.activationRef.key.skillName;
	audit["activationID"] = context.activationRef.key.instanceID;
	audit["initiator"] = context.initiator ? context.initiator->objectName() : QString();
	audit["invoker"] = context.invoker ? context.invoker->objectName() : QString();
	audit["owner"] = context.owner ? context.owner->objectName() : QString();
	audit["canceled"] = context.is_canceled;
	audit["bypassCost"] = context.bypass_cost;
	QStringList interceptorKeys;
	foreach (const QString &key, context.interceptor_data.keys()) interceptorKeys << key;
	audit["interceptors"] = interceptorKeys;
	qDebug() << "SkillExecutionAudit" << audit;
}

void Room::notifyCardProvenance(const QString &kind, ServerPlayer *initiator, const Card *card,
                                const SkillInstanceRef &sourceRef, const SkillInstanceRef &activationRef)
{
	if (!initiator || !card) return;
	CardProvenanceMessage message;
	message.kind = kind;
	message.initiator = initiator->objectName();
	message.card = card->toString();
	message.sourceOwner = sourceRef.ownerObjectName;
	message.sourceSkill = sourceRef.key.skillName;
	message.sourceInstanceId = sourceRef.key.instanceID;
	message.activationOwner = activationRef.ownerObjectName;
	message.activationSkill = activationRef.key.skillName;
	message.activationInstanceId = activationRef.key.instanceID;
	doBroadcastNotify(S_COMMAND_CARD_PROVENANCE, message.toVariant());
}

bool Room::useCard(const CardUseStruct&use, bool add_history)
{
	CardUseStruct new_use = use;
	return useCard(new_use, add_history);
}

bool Room::useCard(CardUseStruct&use, bool add_history)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	if (!resolveCardSkillInstance(use)) return false;
	// Revalidate only client/AI-selected targets. Server-created uses may carry
	// authoritative targets for target-fixed cards, such as Peach in dying rescue.
	if (use.m_validateTargets && !areCardTargetsLegal(use)) return false;
	if ((!use.card->canRecast()||use.from->isCardLimited(use.card,Card::MethodRecast))&&use.from->isCardLimited(use.card,use.card->getHandlingMethod()))
		return false;
	use.m_addHistory = add_history;
	bool residue_unlimited = false;
	if (use.to.isEmpty()) {
		residue_unlimited = Sanguosha->hasResidueUnlimited(use.from, use.card, nullptr);
	} else {
		foreach (ServerPlayer *target, use.to) {
			if (Sanguosha->hasResidueUnlimited(use.from, use.card, target)) {
				residue_unlimited = true;
				break;
			}
		}
	}
	if (residue_unlimited)
		use.m_addHistory = false;
	const Card*card = use.card->validate(use);
	if(card==nullptr) return false;
	notifyCardProvenance("use", use.from, card, use.sourceRef, use.activationRef);

	bool isSkillCard = card->isKindOf("SkillCard");
	bool isViewAsCard = !isSkillCard && card->isVirtualCard() && !card->getSkillName().isEmpty();
	SkillContext skillCardCtx;
	SkillContext skillCardIdentity;
	ServerPlayer *skillCardInvoker = nullptr;
	QVariant skillCardCtxData;
	SkillExecutionRegistry::Guard skillExecution;
	bool skipOnUse = false;
	bool skillUsageReserved = false;
	bool skillUsageCommitted = false;
	bool skillExecutionFinishStarted = false;
	bool cardProcessingStarted = false;
	bool cardProcessingCompleted = false;
	const ViewAsSkillV2 *activeSkill = nullptr;
	QString tagKey;
	QList<int> ids;
	QString key;
	auto loadSkillContext = [&]() {
		return skillExecution.executionID() > 0
			? getSkillExecutionContext(skillExecution.executionID())
			: getTag(tagKey).value<SkillContext>();
	};
	auto restoreSkillCardIdentity = [&](SkillContext &context) {
		context.skill_name = skillCardIdentity.skill_name;
		context.sourceRef = skillCardIdentity.sourceRef;
		context.activationRef = skillCardIdentity.activationRef;
		context.initiator = skillCardIdentity.initiator;
		context.instanceID = skillCardIdentity.instanceID;
	};
	auto saveSkillContext = [&](const SkillContext &context) {
		if (skillExecution.executionID() > 0)
			setSkillExecutionContext(skillExecution.executionID(), context);
		else
			setTag(tagKey, QVariant::fromValue(context));
	};
	auto finishSkillExecution = [&](SkillExecutionResult result) {
		if (!(isSkillCard || isViewAsCard) || skillExecutionFinishStarted) return;
		skillExecutionFinishStarted = true;
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillEffectFinished;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		try {
			thread->trigger(EventSkillEffectFinished, this, use.from, skillCardCtxData);
		} catch (TriggerEvent) {
			skillCardCtx = skillCardCtxData.value<SkillContext>();
			restoreSkillCardIdentity(skillCardCtx);
			if (skillCardInvoker) skillCardCtx.invoker = skillCardInvoker;
			saveSkillContext(skillCardCtx);
			if (skillExecution.executionID() > 0) {
				recordSkillExecutionAudit(skillCardCtx, result);
				skillExecution.finish(result);
			} else {
				removeTag(tagKey);
			}
			throw;
		}
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		if (skillCardInvoker) skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
		if (skillExecution.executionID() > 0) {
			recordSkillExecutionAudit(skillCardCtx, result);
			skillExecution.finish(result);
		} else {
			removeTag(tagKey);
		}
	};

	try {
	if (isSkillCard || isViewAsCard) {
		const SkillCard *skillCard = isSkillCard ? qobject_cast<const SkillCard*>(card->getRealCard()) : nullptr;

		skillCardCtx.skill_name = use.sourceRef.isValid() ? use.sourceRef.key.skillName
								  : (card->getSkillName().isEmpty() ? card->objectName() : card->getSkillName());
		skillCardCtx.invoker = use.from;
		skillCardCtx.owner = skillCard ? skillCard->getSkillOwner() : nullptr;
		if (!skillCardCtx.owner) skillCardCtx.owner = use.from;
		skillCardCtx.targets = use.to;
		skillCardCtx.instanceID = use.activationRef.isValid() ? use.activationRef.key.instanceID
								: (skillCard ? skillCard->getSkillInstanceId() : 0);
		skillCardCtx.use_card = card;
		skillCardCtx.sourceRef = use.sourceRef;
		skillCardCtx.activationRef = use.activationRef;
		skillCardCtx.initiator = use.from;
		activeSkill = dynamic_cast<const ViewAsSkillV2 *>(
			Sanguosha->getViewAsSkill(skillCardCtx.activationRef.key.skillName));
		if (activeSkill) {
			bool amountOk = false;
			skillCardCtx.amount = getSkillInstanceAmount(
				activeSkill->getAmountRef(skillCardCtx), &amountOk);
			if (!amountOk) skillCardCtx.amount = activeSkill->getBaseAmount();
		}
		skillCardIdentity = skillCardCtx;
		skillExecution = beginSkillExecution(skillCardCtx, QVariant::fromValue(use));
		use.skillExecutionID = skillExecution.executionID();

		QString prefix = isViewAsCard ? "ViewAsContext_" : "SkillCardContext_";
		tagKey = prefix + skillCardCtx.skill_name + "_"
				 + QString::number(skillCardCtx.instanceID);

		saveSkillContext(skillCardCtx);
		if (activeSkill && !skillCardCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_runtime->state().getCurrentCardUsePattern();
			request.initiator = skillCardCtx.initiator;
			request.activationRef = skillCardCtx.activationRef;
			request.selectedCardIds = use.card->getSubcards();
			foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
			const bool paidCost = activeSkill->cost(this, skillCardCtx, request);
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardIdentity.invoker;
			if (!paidCost) {
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}

		skillCardCtx.current_event = EventSkillWillInvoke;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		thread->trigger(EventSkillWillInvoke, this, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		if (skillCardCtx.is_canceled) {
			finishSkillExecution(SkillExecutionInvalidTargetUpdate);
			return false;
		}
		skillCardInvoker = skillCardCtx.invoker ? skillCardCtx.invoker : skillCardIdentity.invoker;
		skillCardCtx.invoker = skillCardInvoker;
		if (skillCardCtx.invoker && skillCardCtx.invoker != use.from) {
			if (!skillCardCtx.invoker->isAlive() || skillCardCtx.invoker->isCardLimited(card, card->getHandlingMethod())) {
				finishSkillExecution(SkillExecutionInvalidTargetUpdate);
				return false;
			}
			use.from = skillCardCtx.invoker;
		}
		if (skillCardCtx.updated_card) {
			if (use.from->isCardLimited(skillCardCtx.updated_card, skillCardCtx.updated_card->getHandlingMethod())) {
				finishSkillExecution(SkillExecutionInvalidTargetUpdate);
				return false;
			}
			use.card = skillCardCtx.updated_card;
			card = skillCardCtx.updated_card;
		}

		if (activeSkill && !reserveActiveSkillUsage(activeSkill, skillCardCtx)) {
			finishSkillExecution(SkillExecutionPayFailed);
			return false;
		}
		skillUsageReserved = hasGenericActiveSkillUsage(activeSkill);
		if (!skillCardCtx.bypass_cost) {
			skillCardCtx.current_event = EventSkillPay;
			skillCardCtxData = QVariant::fromValue(skillCardCtx);
			thread->trigger(EventSkillPay, this, use.from, skillCardCtxData);
			skillCardCtx = skillCardCtxData.value<SkillContext>();
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardInvoker;
			if (skillCardCtx.is_canceled) {
				if (skillUsageReserved)
					releaseActiveSkillUsage(activeSkill, skillCardCtx);
				skillUsageReserved = false;
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}
		if (activeSkill && !skillCardCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_runtime->state().getCurrentCardUsePattern();
			request.initiator = skillCardCtx.initiator;
			request.activationRef = skillCardCtx.activationRef;
			request.selectedCardIds = use.card->getSubcards();
			foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
			const bool paid = activeSkill->pay(this, skillCardCtx, request);
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardInvoker;
			if (!paid || skillCardCtx.is_canceled) {
				if (skillUsageReserved)
					releaseActiveSkillUsage(activeSkill, skillCardCtx);
				skillUsageReserved = false;
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}
		if (activeSkill) {
			commitActiveSkillUsage(activeSkill, skillCardCtx);
			skillUsageCommitted = skillUsageReserved;
			skillUsageReserved = false;
		}
		use.bypass_cost = skillCardCtx.bypass_cost;

		skillCardCtx.updated_targets = skillCardCtx.targets;
		skillCardCtx.current_event = EventSkillTargetConfirming;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		thread->trigger(EventSkillTargetConfirming, this, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		if (!skillCardCtx.updated_targets.isEmpty()) {
			use.to = skillCardCtx.updated_targets;
		}

		saveSkillContext(skillCardCtx);
	}

	if (use.card->isVirtualCard()) ids = use.card->getSubcards();
	else ids << use.card->getId();
	foreach(int id, ids){
		use.m_isHandcard = use.from->handCards().contains(id);
		if (!use.m_isHandcard) break;
	}
	key = use.card->getClassName();
	tag.remove("UseHistory"+use.card->toString());
	if (use.card->inherits("LuaSkillCard")) key = "#"+use.card->objectName();
	const ViewAsSkillV2 *historySkill = dynamic_cast<const ViewAsSkillV2 *>(
		Sanguosha->getViewAsSkill(use.activationRef.key.skillName));
	if (historySkill) {
		ActiveSkillRequest request;
		request.reason = m_runtime->state().getCurrentCardUseReason();
		request.pattern = m_runtime->state().getCurrentCardUsePattern();
		request.initiator = use.from;
		request.activationRef = use.activationRef;
		request.selectedCardIds = use.card->getSubcards();
		foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
		key = historySkill->historyKey(request);
	}
	//use.m_isOwnerUse = (ids.isEmpty()&&use.m_isOwnerUse)||getCardOwner(ids.first())==use.from;
	if(m_runtime->state().getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY)
		addPlayerHistory(nullptr, "pushPile");
	if (use.m_addHistory){
		add_history = true;
		addPlayerHistory(use.from, key);
	}

	if (isSkillCard || isViewAsCard) {
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillInvoking;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		thread->trigger(EventSkillInvoking, this, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
	}

	if (isSkillCard || isViewAsCard) {
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillEffect;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		skipOnUse = thread->trigger(EventSkillEffect, this, use.from, skillCardCtxData);
		if (skipOnUse) {
			if (card->property("LegacyOnUseLimited").toBool()) {
				// Explicitly marked monolithic cards cannot safely enter their custom onUse.
				use.skipSkillEffect = false;
			} else {
				use.skipSkillEffect = true;
				skipOnUse = false;
			}
		}
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
	}

		if (use.card->getRealCard() == card){
			if (!use.card->isVirtualCard()){
				WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
				if (wrapped->isModified()) broadcastUpdateCard(getPlayers(), ids.first(), wrapped);
				//else broadcastResetCard(getPlayers(), ids.first());
				if (getConvertedPhysicalCardId(use.card) >= 0) {
					ServerPlayer *displayTarget = use.to.size() == 1 && use.to.first() != use.from
						? use.to.first() : nullptr;
					showVirtualCard(use.from, use.card, displayTarget);
				}
			} else if (use.card->getTypeId() != Card::TypeSkill) {
				ServerPlayer *displayTarget = use.to.size() == 1 && use.to.first() != use.from
					? use.to.first() : nullptr;
				showVirtualCard(use.from, use.card, displayTarget);
			}
			if (!skipOnUse) {
				cardProcessingStarted = true;
				use.card->onUse(this, use);
			}
		} else {
			use.card = card;
			if (!skipOnUse) {
				cardProcessingStarted = true;
				use.card->onUse(this, use);
			}
		}
	cardProcessingCompleted = cardProcessingStarted;

	finishSkillExecution(skipOnUse ? SkillExecutionEffectSkipped : SkillExecutionCompleted);
	} catch (TriggerEvent triggerEvent){
		if (skillUsageReserved && !skillUsageCommitted)
			releaseActiveSkillUsage(activeSkill, skillCardCtx);
		skillUsageReserved = false;
		if (triggerEvent == StageChange || triggerEvent == TurnBroken){
			if (cardProcessingStarted && !cardProcessingCompleted) {
				bool processBrokenFlagSet = false;
				try {
					CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(), use.card->getSkillName(), "");
					if (use.to.size() == 1) reason.m_targetId = use.to.first()->objectName();
					foreach(int id, ids){
						if (getCardPlace(id) != Player::PlaceTable)
							ids.removeOne(id);
					}
					moveCardsAtomic(CardsMoveStruct(ids, use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
					QVariant data = QVariant::fromValue(use);
					use.from->setFlags("Global_ProcessBroken");
					processBrokenFlagSet = true;
					try {
						thread->trigger(CardFinished, this, use.from, data);
					} catch (TriggerEvent) {
						// Cleanup must not replace the original control event.
					}
					use.from->setFlags("-Global_ProcessBroken");
					processBrokenFlagSet = false;
					foreach(ServerPlayer*p, getAlivePlayers())
						p->removeQinggangTag(use.card);
					foreach(int id, m_cardMovement->primaryPile()){
						if (getCardPlace(id) == Player::PlaceJudge)
							moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile, true);
						setCardFlag(id, "-using");
					}
				} catch (TriggerEvent) {
					if (processBrokenFlagSet)
						use.from->setFlags("-Global_ProcessBroken");
				}
			}
			try {
				finishSkillExecution(SkillExecutionNoResult);
			} catch (TriggerEvent) {
				// The original StageChange/TurnBroken remains authoritative.
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
	foreach(QString skill_name, player->getTag(reason + "_SKILLCANWAKE").toStringList())
		setPlayerMark(player, "&" + skill_name + "+:+" + reason, 0);
	player->removeTag(reason + "_SKILLCANWAKE");
	return player->getMark("@waked") > n;
}

void Room::recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion)
{
	if (player->isDead() || recover.recover <= 0) return;

	QVariant data = QVariant::fromValue(recover);
	if (thread->trigger(StartHpRecover,this,player,data)||player->getLostHp()<=0||thread->trigger(PreHpRecover,this,player,data)) return;

	if (player->hasFlag("Global_Dying")&&recover.who){
		QStringList list = player->getTag("MyDyingSaver").toStringList();
		list << recover.who->objectName();
		player->setTag("MyDyingSaver", list);
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
		QVariantMap map = GetValueFromLuaState(getLuaState(), "config", "kingdom_colors").toMap();
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
	QStringList list = player->getTag("MyDyingSaver").toStringList();
	if (list.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, getAlivePlayers()){
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

		if(prevented&&damage.to->getTag("TransferDamage").isValid()){
			// Make sure that the trigger in which 'TransferDamage' tag is set returns TRUE
			DamageStruct transfer = damage.to->getTag("TransferDamage").value<DamageStruct>();
			damage.to->removeTag("TransferDamage");
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
		if (triggerEvent == StageChange || triggerEvent == TurnBroken
			|| triggerEvent == GameFinished){
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
	if (Sanguosha->isCustomGameMode(mode)) {
		GameModeStruct gameMode = Sanguosha->getGameMode(mode);
		return player->isLord() && gameMode.isValid() && gameMode.lord_welfare;
	}
	return player->isLord()&&player_count > 4;
}

void Room::notifySkillInstanceAmount(ServerPlayer *owner, const SkillInstance &instance)
{
	m_skillRuntime->notifySkillInstanceAmount(owner, instance);
}

void Room::notifySkillInstanceCorrectState(ServerPlayer *owner, const SkillInstance &instance,
                                           const QString &operation, const QString &key,
                                           const QVariant &value)
{
	m_skillRuntime->notifySkillInstanceCorrectState(owner, instance, operation, key, value);
}

void Room::notifySkillInstanceState(ServerPlayer *owner, const SkillInstance &instance,
                                    const QString &operation, const QString &key,
                                    const QVariant &value)
{
	m_skillRuntime->notifySkillInstanceState(owner, instance, operation, key, value);
}

int Room::getSkillInstanceAmount(const SkillInstanceRef &ref, bool *ok) const
{
	return m_skillRuntime->getSkillInstanceAmount(ref, ok);
}

bool Room::setSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref, int amount,
                                  const QString &reason)
{
	return m_skillRuntime->setSkillInstanceAmount(source, ref, amount, reason);
}

bool Room::addSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref, int delta,
                                  const QString &reason)
{
	return m_skillRuntime->addSkillInstanceAmount(source, ref, delta, reason);
}

bool Room::resetSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref,
                                    const QString &reason)
{
	return m_skillRuntime->resetSkillInstanceAmount(source, ref, reason);
}

bool Room::setSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref,
                                        const QString &key, const QVariant &value)
{
	return m_skillRuntime->setSkillInstanceCorrectState(source, ref, key, value);
}

bool Room::removeSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref,
                                           const QString &key)
{
	return m_skillRuntime->removeSkillInstanceCorrectState(source, ref, key);
}

bool Room::clearSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref)
{
	return m_skillRuntime->clearSkillInstanceCorrectState(source, ref);
}

bool Room::hasPendingSummons() const
{
	return m_playerLifecycle->hasPendingSummons();
}

void Room::requestSummonBetween(ServerPlayer *before, ServerPlayer *after,
                                const QString &general_name)
{
	m_playerLifecycle->requestSummonBetween(before, after, general_name);
}

void Room::processPendingSummons()
{
	m_playerLifecycle->processPendingSummons();
}

ServerPlayer* Room::insertPlayerMidGame(ServerPlayer *before, ServerPlayer *after,
                                       const QString &general_name)
{
	return m_playerLifecycle->insertPlayerMidGame(before, after, general_name);
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
	m_playerLifecycle->reconnect(player, socket);
}

void Room::marshal(ServerPlayer*player)
{
	m_playerLifecycle->marshal(player);
}

void Room::startGame()
{
	m_gameSession->startGame();
}

bool Room::notifyProperty(ServerPlayer*player, const ServerPlayer*owner, const char*property_name, const QString&value)
{
	return m_playerState->notifyProperty(player, owner, property_name, value);
}

bool Room::broadcastProperty(ServerPlayer*owner, const char*property_name, const QString&value)
{
	return m_playerState->broadcastProperty(owner, property_name, value);
}

void Room::broadcastTagProperty(ServerPlayer *owner, const QString &tagKey, const QVariant &value)
{
	m_notifier->broadcastTagProperty(owner, tagKey, value);
}

void Room::notifyPlayerUIState(ServerPlayer *owner, const PlayerUIState &state)
{
	m_notifier->notifyPlayerUIState(owner, state);
}

void Room::notifyPlayerUIState(ServerPlayer *receiver, const ServerPlayer *owner,
	const PlayerUIState &state)
{
	m_notifier->notifyPlayerUIState(receiver, owner, state);
}

QList<int> Room::drawCardsList(ServerPlayer*player, int n, const QString&reason, bool isTop, bool visible)
{
	return m_cardMovement->drawCardsList(player, n, reason, isTop, visible);
}

void Room::drawCards(ServerPlayer*player, int n, const QString&reason, bool isTop, bool visible)
{
	m_cardMovement->drawCards(player, n, reason, isTop, visible);
}

void Room::drawCards(QList<ServerPlayer*> players, int n, const QString&reason, bool isTop, bool visible)
{
	m_cardMovement->drawCards(players, n, reason, isTop, visible);
}

void Room::drawCards(QList<ServerPlayer*> players, QList<int> n_list, const QString&reason, bool isTop, bool visible)
{
	m_cardMovement->drawCards(players, n_list, reason, isTop, visible);
}

void Room::throwCard(const Card*card, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card, who, thrower);
}

void Room::throwCard(const Card*card, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card, reason, who, thrower);
}

void Room::throwCard(QList<int> card_ids, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card_ids, reason, who, thrower);
}

void Room::throwCard(int card_id, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card_id, who, thrower);
}

void Room::throwCard(int card_id, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card_id, skill_name, who, thrower);
}

void Room::throwCard(const Card*card, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card, skill_name, who, thrower);
}

void Room::throwCard(QList<int> card_ids, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower)
{
	m_cardMovement->throwCard(card_ids, skill_name, who, thrower);
}

RoomThread*Room::getThread() const
{
	return thread;
}

void Room::moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, bool visible, bool guanxin)
{
	m_cardMovement->moveCardTo(card, dstPlayer, dstPlace, visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const CardMoveReason&reason, bool visible, bool guanxin)
{
	m_cardMovement->moveCardTo(card, dstPlayer, dstPlace, reason, visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const CardMoveReason&reason, bool visible, bool guanxin)
{
	m_cardMovement->moveCardTo(card, srcPlayer, dstPlayer, dstPlace, reason, visible, guanxin);
}

void Room::moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
	const QString&pileName, const CardMoveReason&reason, bool visible, bool guanxin)
{
	m_cardMovement->moveCardTo(card, srcPlayer, dstPlayer, dstPlace,
		pileName, reason, visible, guanxin);
}

QList<CardsMoveStruct> Room::_breakDownCardMoves(QList<CardsMoveStruct> cards_moves)
{
	return m_cardMovement->normalizeMoves(cards_moves);
}

void Room::moveCardsAtomic(CardsMoveStruct cards_move, bool visible, bool guanxing)
{
	m_cardMovement->moveCardsAtomic(cards_move, visible, guanxing);
}

void Room::moveCardsAtomic(QList<CardsMoveStruct> cards_moves, bool visible, bool guanxing)
{
	m_cardMovement->moveCardsAtomic(cards_moves, visible, guanxing);
}

void Room::moveCardsToEndOfDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible, bool guanxing)
{
	m_cardMovement->moveCardsToEndOfDrawpile(player, card_ids, skill_name, visible, guanxing);
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, const Card*card, const QString&skill_name, int n, bool visible)
{
	m_cardMovement->moveCardsInToDrawpile(player, card, skill_name, n, visible);
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, int card_id, const QString&skill_name, int n, bool visible)
{
	m_cardMovement->moveCardsInToDrawpile(player, card_id, skill_name, n, visible);
}

void Room::moveCardsInToDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, int n, bool visible)
{
	m_cardMovement->moveCardsInToDrawpile(player, card_ids, skill_name, n, visible);
}

void Room::shuffleIntoDrawPile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible)
{
	m_cardMovement->shuffleIntoDrawPile(player, card_ids, skill_name, visible);
}

void Room::removeDerivativeCards()
{
	m_cardMovement->removeDerivativeCards();
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
				broadcastResetCard(getPlayers(), cardId);
		}
	}else if (move.to_place == Player::PlaceDelayedTrick){
		foreach(int cardId, move.card_ids){
			WrappedCard*wrapped = Sanguosha->getWrappedCard(cardId);
			if (wrapped->isModified()) broadcastUpdateCard(getPlayers(), cardId, wrapped);/*
			const Card*engine_card = Sanguosha->getEngineCard(cardId);
			if (wrapped->getSuit() != engine_card->getSuit() || wrapped->getNumber() != engine_card->getNumber()){
				Card*trick = Sanguosha->cloneCard(wrapped);
				trick->setNumber(engine_card->getNumber());
				trick->setSuit(engine_card->getSuit());
				wrapped->takeOver(trick);
				broadcastUpdateCard(getPlayers(), cardId, wrapped);
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
	if (players.isEmpty()) players = getPlayers();
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
	m_notifier->broadcastSkillInvoke(skill_name, category);
}

void Room::broadcastSkillInvoke(const QString&skill_name, const ServerPlayer*player)
{
	if (!player) player = findPlayerBySkillName(skill_name, true);
	m_notifier->broadcastSkillInvoke(skill_name, true, -1,
		player ? player->objectName() : QString());
}

void Room::broadcastSkillInvoke(const QString&skill_name, int type, const ServerPlayer*player)
{
	if (type==0) return;
	if (!player) player = findPlayerBySkillName(skill_name, true);
	m_notifier->broadcastSkillInvoke(skill_name, true, type,
		player ? player->objectName() : QString());
}

void Room::broadcastSkillInvoke(const QString&skill_name, bool isMale, int type)
{
	if (type==0) return;
	m_notifier->broadcastSkillInvoke(skill_name, isMale, type, QString());
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
		QString skin = QString("hero-skin/%1/%2/full.png").arg(heroName).arg(n);
		if (QFile::exists(skin)){
			doAnimate(S_ANIMATE_LIGHTBOX,"skill="+skin,skillName);
			if(delay) thread->delay(4500);
			return;
		}else{
			skin = QString("image/animate/%1_%2.png").arg(heroName).arg(n);
			if (QFile::exists(skin)){
				doAnimate(S_ANIMATE_LIGHTBOX,"skill=Animate:"+skin,skillName);
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
	if (players.isEmpty()) players = getPlayers();
	doBroadcastNotify(players, S_COMMAND_ANIMATE, arg);
}

void Room::showFlower(const QString &from, const QString &to, QList<ServerPlayer *> players)
{
	if (from == to)
		return;

	doAnimate(S_ANIMATE_FLOWER, from, to, players);
}

void Room::showEgg(const QString &from, const QString &to, QList<ServerPlayer *> players)
{
	if (from == to)
		return;

	doAnimate(S_ANIMATE_EGG, from, to, players);
}

void Room::doBattleArrayAnimate(ServerPlayer *player, ServerPlayer *target)
{
	if (getAlivePlayers().length() < 4)
		return;
	if (target == nullptr) {
		QStringList names;
		foreach (const Player *p, player->getFormation())
			names << p->objectName();
		if (names.length() > 1)
			doAnimate(QSanProtocol::S_ANIMATE_BATTLEARRAY, player->objectName(), names.join("+"));
	} else {
		foreach (ServerPlayer *p, getOtherPlayers(player))
			if (p->inSiegeRelation(player, target))
				doAnimate(QSanProtocol::S_ANIMATE_BATTLEARRAY, player->objectName(), QString("%1+%2").arg(p->objectName(), player->objectName()));
	}
}

void Room::showGeneral(ServerPlayer *player, const QString &position)
{
	if (position == "h") {
		setPlayerProperty(player, "general_showed", true);
	} else if (position == "d") {
		setPlayerProperty(player, "general2_showed", true);
	}
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_SHOW_GENERAL;
	args << player->objectName();
	args << position;
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void Room::preparePlayers()
{
    foreach(ServerPlayer*player, getPlayers()){
        const General*gen = player->getGeneral();
        if(!gen) continue;
        player->setGender(gen->getGender());
		QSet<QString> relatedNames;
		foreach (const Skill *parent, gen->getSkillList()) {
			foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
				relatedNames.insert(related->objectName());
		}
        foreach(const Skill*skill, gen->getSkillList()){
			if (relatedNames.contains(skill->objectName())) continue;
            player->addSkill(skill->objectName(), true);
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
			relatedNames.clear();
			foreach (const Skill *parent, gen->getSkillList()) {
				foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
					relatedNames.insert(related->objectName());
			}
            foreach(const Skill*skill, gen->getSkillList()){
				if (relatedNames.contains(skill->objectName())) continue;
                player->addSkill(skill->objectName(), false);
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
	m_playerLifecycle->changePlayerGeneral(player, new_general);
}

void Room::changePlayerGeneral2(ServerPlayer*player, const QString&new_general)
{
	m_playerLifecycle->changePlayerGeneral2(player, new_general);
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
			broadcastResetCard(getPlayers(), cardId);
		const Card*card = nullptr;
		foreach(const FilterSkill*skill, filterSkills){
			if (skill->viewFilter(cards[i])&&player->hasSkill(skill->objectName())){
				if(card) const_cast<Card *>(card)->deleteLater();
				card = skill->viewAs(cards[i]);
			}
		}
		if (card==nullptr) continue;
		wrapped->takeOver((Card*)card);
		if (getCardPlace(cardId) == Player::PlaceHand) notifyUpdateCard(player, cardId, card);
		else broadcastUpdateCard(getPlayers(), cardId, card);
	}
}

int Room::acquireSkill(ServerPlayer*player, const Skill*skill, bool open, bool getmark, bool event_and_log)
{
	return m_skillRuntime->acquireSkill(player, skill, open, getmark, event_and_log);
}

int Room::acquireSkill(ServerPlayer*player, const QString&skill_name, bool open, bool getmark, bool event_and_log)
{
	return m_skillRuntime->acquireSkill(player, skill_name, open, getmark, event_and_log);
}

void Room::addSkillInvalidity(ServerPlayer *target, const QString &skillName, const QString &sourceName, const QString &reason, int instanceId)
{
	m_skillRuntime->addSkillInvalidity(target, skillName, sourceName, reason, instanceId);
}

void Room::removeSkillInvalidity(ServerPlayer *target, const QString &skillName, const QString &sourceName, const QString &reason, int instanceId)
{
	m_skillRuntime->removeSkillInvalidity(target, skillName, sourceName, reason, instanceId);
}

void Room::clearSkillInvalidityBySource(ServerPlayer *source)
{
	m_skillRuntime->clearSkillInvalidityBySource(source);
}

void Room::setTag(const QString&key, const QVariant&value)
{
	QByteArray error;
	if (!globalCardLifetimeManager().retainVariantTag(this, key.toUtf8(), value, &error)) {
		qWarning("Room tag '%s' rejected: %s", qPrintable(key), error.constData());
		return;
	}
	tag.insert(key, value);
	if (scenario) scenario->onTagSet(this, key);
}

QVariant Room::getTag(const QString&key) const
{
	return tag.value(key);
}

void Room::removeTag(const QString&key)
{
	globalCardLifetimeManager().releaseVariantTag(this, key.toUtf8());
	tag.remove(key);
}

void Room::setEmotion(ServerPlayer*target, const QString&emotion)
{
	if (!target) return;
	JsonArray arg;
	arg << target->objectName();
	arg << (emotion.isEmpty() ? QString(".") : emotion);
	doBroadcastNotify(S_COMMAND_SET_EMOTION, arg);
}

void Room::setLoopEmotion(ServerPlayer*target, const QString&emotion)
{
    // TODO: setLoopEmotion 尚未正確實現
    // 預期功能：在玩家頭像區域附加或移除持續性的動態視覺特效（如鐵索連環狀態）
    // 現況：目前只是發送 "loopmove=xxx" 到客戶端，會被當作普通文字顯示
    // 正確實現需要：在 doLightboxAnimation 中新增 loopmove= 處理分支，
    //               在玩家頭像區域顯示持續性視覺特效，支援 "." 參數移除特效
    if (!target) return;
    doAnimate(S_ANIMATE_LIGHTBOX, QString("loopmove=%1").arg(emotion), target->objectName());
}

void Room::changeTableBg(const QString&tableBg)
{
	QString tb = QString("image/system/backdrop/%1.jpg").arg(tableBg);
	if (!QFile::exists(tb)) return;
	JsonArray arg;
	arg << tb;
	doBroadcastNotify(S_COMMAND_CHANGE_TABLE_BG, arg);
}

void Room::changeBackground(const QString name, QList<ServerPlayer *> players)
{
	if (players.isEmpty()) players = getPlayers();
	doAnimate(S_ANIMATE_LIGHTBOX, "background=" + name, "", players);
}

void Room::setAura(ServerPlayer* player, QString aura)
{
	if (hasAura(aura)){
		return;
	}

	setTag("aura", QVariant::fromValue(aura));
	setTag("aura_player", QVariant::fromValue(player));
	changeBGM(aura, false);
	changeBackground(aura);
	doAnimate(QSanProtocol::S_ANIMATE_LIGHTBOX, "lani=aura", QString("%1:%2").arg(3000).arg(0));
}

bool Room::hasAura(){
	return getTag("aura").value<QString>().length() > 0;
}

bool Room::hasAura(QString aura){
	return getTag("aura").value<QString>() == aura;
}

QString Room::getAura(){
	return getTag("aura").value<QString>();
}

ServerPlayer* Room::getAuraPlayer(){
	return getTag("aura_player").value<ServerPlayer *>();
}

void Room::clearAura(){
	if (!hasAura()){
		return;
	}
	changeBGM("", false);
	changeBackground("");
	removeTag("aura");
	removeTag("aura_player");
}

bool Room::doAura(ServerPlayer* player, QString aura){
	if (hasAura(aura)){
		return false;
	}
	setAura(player, aura);
	return true;
}

void Room::reversePlayOrder()
{
	m_roster->reversePlayOrder();
	int count = getPlayers().length();
	if (count < 2) return;

	LogMessage log;
	log.type = m_roster->isPlayOrderReversed() ? "#ReversePlayOrder" : "#RestorePlayOrder";
	sendLog(log);
}

bool Room::isPlayOrderReversed() const
{
	return m_roster->isPlayOrderReversed();
}

void Room::updateCardDescription(const QString &card_name, const QVariantMap &placeholders)
{
	JsonArray arg;
	arg << card_name << placeholders;
	doBroadcastNotify(S_COMMAND_UPDATE_CARD_DESC, arg);
}

QVariant Room::askForQml(ServerPlayer *player, const QString &qmlPath, const QVariantMap &params, int timeout)
{
	tryPause();
	notifyMoveFocus(player, S_COMMAND_QML_INTERACT);

	QVariantMap payload;
	payload.insert(QStringLiteral("schema_version"), 1);
	payload.insert(QStringLiteral("type"), QStringLiteral("qsanguosha.qml"));
	payload.insert(QStringLiteral("title"), QString());
	QVariantMap rendererPayload;
	rendererPayload.insert(QStringLiteral("qml_path"), qmlPath);
	rendererPayload.insert(QStringLiteral("parameters"), params);
	payload.insert(QStringLiteral("payload"), rendererPayload);
	payload.insert(QStringLiteral("response_schema"),
		QVariantMap{{QStringLiteral("type"), QStringLiteral("json")}});

	if (doRequest(player, S_COMMAND_QML_INTERACT, payload, timeout, true)) {
		return player->getClientReply();
	}

	return QVariant();
}

void Room::activate(ServerPlayer*player, CardUseStruct&card_use)
{
	m_playerDecisions->activate(player, card_use);
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
				m_cardMovement->drawPile().insert(qsanRandomBounded(m_cardMovement->drawPile().length()),id);
		}
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
		doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_cardMovement->drawPile().length());
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
	return m_playerDecisions->askForSuit(player, reason);
}

QString Room::askForKingdom(ServerPlayer*player, const QString&reason, QStringList kingdoms, bool send_log)
{
	return m_playerDecisions->askForKingdom(player, reason, kingdoms, send_log);
}

QString Room::askForKingdom(ServerPlayer*player, const QString&reason, const QString&kingdoms, bool send_log)
{
	return askForKingdom(player,reason,kingdoms.split("+"),send_log);
}

Card*Room::askForDiscard(ServerPlayer*player, const QString&reason, int discard_num, int min_num,
	bool optional, bool include_equip, const QString&prompt, const QString&pattern, const QString&skill_name)
{
	return m_playerDecisions->askForDiscard(player, reason, discard_num, min_num, optional,
		include_equip, prompt, pattern, skill_name);
}

Card*Room::askForExchange(ServerPlayer*player, const QString&reason, int exchange_num, int min_num,
	bool include_equip, const QString&prompt, bool optional, const QString&pattern)
{
	return m_playerDecisions->askForExchange(player, reason, exchange_num, min_num, include_equip,
		prompt, optional, pattern);
}

YishiStruct Room::askForYishi(ServerPlayer *initiator, const QList<ServerPlayer *> &participants, const QString &reason)
{
	QList<ServerPlayer *> valid_participants;
	foreach (ServerPlayer *participant, participants) {
		if (participant && participant->isAlive() && !participant->isKongcheng()
			&& !valid_participants.contains(participant))
			valid_participants << participant;
	}

	YishiStruct yishi(initiator, valid_participants, reason);
	if (!initiator || !initiator->isAlive() || valid_participants.length() < 2)
		return yishi;

	yishi.started = true;
	LogMessage log;
	log.type = "$askYishi";
	log.from = initiator;
	log.to = valid_participants;
	log.arg = reason;
	sendLog(log);
	foreach (ServerPlayer *participant, valid_participants)
		doAnimate(1, initiator->objectName(), participant->objectName());

	auto trigger_yishi_event = [this, &yishi](TriggerEvent event) {
		QList<ServerPlayer *> targets = yishi.participants;
		if (yishi.initiator && !targets.contains(yishi.initiator))
			targets << yishi.initiator;
		sortByActionOrder(targets);
		QVariant data = QVariant::fromValue(&yishi);
		foreach (ServerPlayer *target, targets) {
			if (target && target->isAlive())
				thread->trigger(event, this, target, data);
		}
	};

	trigger_yishi_event(YishiStart);
	foreach (ServerPlayer *participant, valid_participants) {
		const int count = qBound(1, yishi.getCardCount(participant), participant->getHandcardNum());
		if (count != yishi.getCardCount(participant))
			yishi.setCardCount(participant, count);

		QList<int> selected = yishi.getCards(participant);
		QSet<int> unique_ids;
		bool preset_is_valid = selected.length() == count;
		foreach (int id, selected) {
			if (id < 0 || unique_ids.contains(id) || getCardOwner(id) != participant
				|| getCardPlace(id) != Player::PlaceHand) {
				preset_is_valid = false;
				break;
			}
			unique_ids.insert(id);
		}

		if (!preset_is_valid) {
			Card *chosen = askForExchange(participant, reason + "_yishi", count, count, false,
				QString("askyishicard:%1").arg(count));
			selected = chosen ? chosen->getSubcards() : QList<int>();
		}
		yishi.setCards(participant, selected);
	}

	foreach (ServerPlayer *participant, valid_participants) {
		QStringList opinions;
		foreach (int id, yishi.getCards(participant)) {
			const Card *card = Sanguosha->getCard(id);
			const QString color = card ? card->getColorString() : QString();
			opinions << (color == "red" || color == "black" ? color : "no_opinion");
		}
		yishi.setOpinions(participant, opinions);
	}

	trigger_yishi_event(YishiBeforeReveal);
	thread->delay(800);
	foreach (ServerPlayer *participant, valid_participants) {
		const QList<int> ids = yishi.getCards(participant);
		if (!ids.isEmpty())
			showCard(participant, ids);
	}

	trigger_yishi_event(YishiResultDetermining);
	const int red = yishi.opinionCount("red");
	const int black = yishi.opinionCount("black");
	if (yishi.forced_result == "red" || yishi.forced_result == "black"
		|| yishi.forced_result == "no_result") {
		yishi.result = yishi.forced_result;
	} else if (red > black) {
		yishi.result = "red";
	} else if (black > red) {
		yishi.result = "black";
	} else {
		yishi.result = "no_result";
	}

	thread->delay(1200);
	log.type = "$yishiresult";
	log.from = initiator;
	log.to.clear();
	log.arg = yishi.result;
	sendLog(log);
	doLightbox("$keyishi" + yishi.result);
	trigger_yishi_event(YishiResultConfirmed);
	trigger_yishi_event(YishiFinished);
	return yishi;
}

void Room::setCardMapping(int card_id, ServerPlayer*owner, Player::Place place)
{
	m_cardMovement->setCardMapping(card_id, owner, place);
}

ServerPlayer*Room::getCardOwner(int card_id) const
{
	return m_cardMovement->getCardOwner(card_id);
}

Player::Place Room::getCardPlace(int card_id) const
{
	return m_cardMovement->getCardPlace(card_id);
}

ServerPlayer*Room::getLord() const
{
	if (mode == "04_2v2") return nullptr;
	foreach(ServerPlayer*player, getPlayers()){
		if (player->getRole() == "lord")
			return player;
	}
	return nullptr;
}

QList<int> Room::askForGuanxing(ServerPlayer*zhuge, const QList<int>&cards, GuanxingType guanxing_type, bool sendLod)
{
	if (cards.isEmpty()) return cards;

	PlayerDecisionService::GuanxingSelection selected =
		m_playerDecisions->askForGuanxingSelection(zhuge, cards, int(guanxing_type));
	QList<int> top_cards = selected.top;
	QList<int> bottom_cards = selected.bottom;

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
			m_cardMovement->drawPile().removeAll(id);
			m_cardMovement->drawPile().prepend(id);
		}
		while (bottom_cards.length()>0){
			int id = bottom_cards.takeLast();
			m_cardMovement->drawPile().removeAll(id);
			m_cardMovement->drawPile().append(id);
		}/*
		QListIterator<int> i(top_cards);
		i.toBack();
		while (i.hasPrevious()){
			int id = i.previous();
			m_cardMovement->drawPile().removeAll(id);
			m_cardMovement->drawPile().prepend(id);
		}
		i = bottom_cards;
		while (i.hasNext()){
			int id = i.next();
			m_cardMovement->drawPile().removeAll(id);
			m_cardMovement->drawPile().append(id);
		}*/
		doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_cardMovement->drawPile().length());
	}
	return top_cards;
}

void Room::returnToTopDrawPile(QList<int> cards)
{
	m_cardMovement->returnToTopDrawPile(cards);
}

void Room::returnToEndDrawPile(QList<int> cards)
{
	m_cardMovement->returnToEndDrawPile(cards);
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
	shenlvmeng->setTag(skill_name, QVariant::fromValue(target));
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
	return m_playerDecisions->askForPindian(player, from, reason);
}

QList<const Card*> Room::askForPindianRace(ServerPlayer*from, ServerPlayer*to, const QString&reason)
{
	return m_playerDecisions->askForPindianRace(from, to, reason);
}

ServerPlayer*Room::askForPlayerChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const QString&skillName,
	const QString&prompt, bool optional, bool notify_skill)
{
	return m_playerDecisions->askForPlayerChosen(player, targets, skillName, prompt, optional, notify_skill);
}

QList<ServerPlayer*> Room::askForPlayersChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const QString&skillName,
					int min_num, int max_num, const QString&prompt, bool notify_skill, bool sort_ActionOrder)
{
	return m_playerDecisions->askForPlayersChosen(player, targets, skillName, min_num, max_num, prompt,
		notify_skill, sort_ActionOrder);
}




QString Room::askForGeneral(ServerPlayer*player, const QStringList&generals, const QString&default_choice, const QString&reason)
{
	return m_playerDecisions->askForGeneral(player, generals, default_choice, reason);
}

QString Room::askForGeneral(ServerPlayer*player, const QString&generals, const QString&default_choice, const QString&reason)
{
	return askForGeneral(player, generals.split("+"), default_choice, reason); // For Lua only!!!
}

bool Room::makeCheat(ServerPlayer*player)
{
	CheatRequestPayload arg;
	if (!CheatRequestPayload::parse(player->m_cheatArgs, &arg)) return false;
	player->m_cheatArgs = QVariant();

	if (arg.action == QLatin1String("kill")) {
		makeKilling(arg.sourcePlayer, arg.targetPlayer);
	} else if (arg.action == QLatin1String("damage")) {
		makeDamage(arg.sourcePlayer, arg.targetPlayer,
			static_cast<QSanProtocol::CheatCategory>(arg.nature), arg.points);
	} else if (arg.action == QLatin1String("revive")) {
		makeReviving(arg.playerName);
	} else if (arg.action == QLatin1String("run_script")) {
		doScript(arg.scriptData.toUtf8());
	} else if (arg.action == QLatin1String("get_card")) {
		int card_id = arg.cardId;

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

	} else if (arg.action == QLatin1String("change_general")) {
		changeHero(player, arg.generalName, false, true, arg.secondaryGeneral);
	} else if (arg.action == QLatin1String("state_editor")) {
		stateChange(arg.targetPlayer,
			static_cast<QSanProtocol::StateEditorCheat>(arg.stateType), arg.points);
	} else if (arg.action == QLatin1String("reverse_play_order")) {
		reversePlayOrder();
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
	if (to_notify.isEmpty()) to_notify = getPlayers();

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
		doBroadcastNotify(to_notify, S_COMMAND_TAKE_AMAZING_GRACE, arg);
		if (move.card_ids.length()>0){
			QVariant data = QVariant::fromValue(move);
			foreach(ServerPlayer*p, getAllPlayers())
				thread->trigger(CardsMoveOneTime, this, p, data);
			//thread->trigger(CardsMoveOneTime, this, current, data);
		}
	} else {
		doBroadcastNotify(to_notify, S_COMMAND_TAKE_AMAZING_GRACE, arg);
		if (!move_cards) return;
		LogMessage log;
		log.type = "$EnterDiscardPile";
		log.card_str = QString::number(card_id);
		sendLog(log);

		m_cardMovement->discardPile().prepend(card_id);
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
	m_notifier->sendLog(log, players);
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

void Room::setShimingStatus(ServerPlayer*player, const QString&skillName, int status)
{
	QString successMark = skillName + "__success";
	QString failMark = skillName + "__fail";

	if (status == 1) {
		if (player->getMark(successMark) > 0) return;
		removePlayerMark(player, failMark, player->getMark(failMark));
		addPlayerMark(player, successMark);

		LogMessage log;
		log.type = "#FinishShiMing";
		log.from = player;
		log.arg = skillName;
		sendLog(log);
		broadcastSkillInvoke(skillName, 2, player);
		notifySkillInvoked(player, skillName);

		QVariant data = skillName;
		thread->trigger(EventShimingSuccess, this, player, data);

		const Skill *skill = Sanguosha->getSkill(skillName);
		if (skill) {
			const TriggerSkillV2 *v2Skill = qobject_cast<const TriggerSkillV2 *>(skill);
			if (v2Skill) {
				v2Skill->onShimingSuccess(this, player);
			}
		}
	} else if (status == 2) {
		if (player->getMark(failMark) > 0) return;
		removePlayerMark(player, successMark, player->getMark(successMark));
		addPlayerMark(player, failMark);

		LogMessage log;
		log.type = "#ShiMingFailed";
		log.from = player;
		log.arg = skillName;
		sendLog(log);
		broadcastSkillInvoke(skillName, 3, player);
		notifySkillInvoked(player, skillName);

		QVariant data = skillName;
		thread->trigger(EventShimingFail, this, player, data);

		const Skill *skill = Sanguosha->getSkill(skillName);
		if (skill) {
			const TriggerSkillV2 *v2Skill = qobject_cast<const TriggerSkillV2 *>(skill);
			if (v2Skill) {
				v2Skill->onShimingFail(this, player);
			}
		}
	} else {
		removePlayerMark(player, successMark, player->getMark(successMark));
		removePlayerMark(player, failMark, player->getMark(failMark));
	}
}

int Room::getShimingStatus(ServerPlayer*player, const QString&skillName) const
{
	QString successMark = skillName + "__success";
	QString failMark = skillName + "__fail";

	if (player->getMark(successMark) > 0) return 1;
	if (player->getMark(failMark) > 0) return 2;
	return 0;
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
	const QString cardIds = ListI2S(card_ids).join("+");

	foreach(int card_id, card_ids){
		WrappedCard*wrapped = Sanguosha->getWrappedCard(card_id);
		if (only_viewer){
			if (wrapped->isModified()) notifyUpdateCard(only_viewer, card_id, wrapped);
			//else notifyResetCard(only_viewer, card_id);
		} else {
			setCardFlag(card_id, "visible");
			if (wrapped->isModified()) broadcastUpdateCard(getPlayers(), card_id, wrapped);
			//else broadcastResetCard(getPlayers(), card_id);
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
	const QString cardIds = ListI2S(card_ids).join("+");

	if (players.isEmpty()) {
		foreach (int card_id, card_ids)
			setCardFlag(card_id, "visible");
	}

	if(players.isEmpty()) players = getPlayers();
	m_notifier->showCard(player->objectName(), cardIds, players);
	QVariant data = cardIds;
	thread->trigger(ShowCards, this, player, data);
	data = QString("viewCards:%1:%2").arg(player->objectName()).arg(cardIds);
	foreach(ServerPlayer*p, players)
		thread->trigger(ChoiceMade, this, p, data);
}

void Room::showVirtualCard(ServerPlayer *player, const Card *card, ServerPlayer *target)
{
	if (player == nullptr || card == nullptr)
		return;

	QList<int> displaySubcards = card->getSubcards();
	if (displaySubcards.isEmpty()) {
		const int physicalCardId = getConvertedPhysicalCardId(card);
		if (physicalCardId >= 0)
			displaySubcards << physicalCardId;
	}

	tryPause();
	notifyMoveFocus(player);
	m_notifier->showVirtualCard(player->objectName(), card->objectName(),
		Card::Suit2String(card->getSuit()), card->getNumber(), card->getSkillName(),
		ListI2S(displaySubcards).join("+"), target ? target->objectName() : QString());
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
			if(wrapped->isModified()) broadcastUpdateCard(getPlayers(), cardId, wrapped);
			//else broadcastResetCard(getPlayers(), cardId);
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
	return m_playerDecisions->askForYijiStruct(guojia, cards, skill_name, is_preview, visible, optional,
		max_num, players, reason, prompt, notify_skill, get);
}

void Room::addMaxCards(ServerPlayer*player, int num, bool one_turn, const QString& reason, ServerPlayer* source)
{
    if (num == 0) return;
    
    QString mark_name = "ExtraBfMaxCards";

    if (!reason.isEmpty()) {
        mark_name += "_" + reason;
        if (source) {
            mark_name += "_" + source->objectName();
        }
    }
    
    if (one_turn) {
        mark_name += "-Clear";
    }

    addPlayerMark(player, mark_name, num);
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
			dc->deleteLater();
		}
	}
	return list;
}

QList<ServerPlayer*> Room::getCardTargets(ServerPlayer*from, const Card*card, QList<ServerPlayer*> except_players)
{
	QList<ServerPlayer*> targets;
	//if (!card->isAvailable(from)) return targets;  //【杀】、【酒】就不能获取目标了
	//这个函数获得的是所有可以成为目标的角色，所以【无中生有】、【桃】、【酒】、装备牌等不会只return自己
	foreach(ServerPlayer*p, getAlivePlayers()){
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
	if (froms.isEmpty()) froms = getAlivePlayers();
	foreach(ServerPlayer*p, froms){
		QList<ServerPlayer*> new_tos = tos;
		if (new_tos.isEmpty()) new_tos = getOtherPlayers(p);
		foreach(const Card*c, p->getCards(flags)){
			if (!p->canMove(p, c->getEffectiveId())) continue;
			foreach(ServerPlayer*d, new_tos){
				if (c->isKindOf("EquipCard")){
					const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
					QList<int> occupy_slots = equip->getOccupyLocations();
					bool all_slots_empty = true;
					foreach(int slot, occupy_slots){
						if(d->getEquip(slot)){
							all_slots_empty = false;
							break;
						}
					}
					if (all_slots_empty && !p->isProhibited(d, c))
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
	if (froms.isEmpty()) froms = getAlivePlayers();

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
		if (!from->canMove(from, c->getEffectiveId())){
			disabled_ids << c->getId();
			continue;
		}
		bool has = true;
		foreach(ServerPlayer*d, tos){
			if (player->isProhibited(d, c)) continue;
			if (c->isKindOf("EquipCard")){
				const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
				QList<int> occupy_slots = equip->getOccupyLocations();
				bool all_slots_empty = true;
				foreach(int slot, occupy_slots){
					if(d->getEquip(slot)){
						all_slots_empty = false;
						break;
					}
				}
				if (all_slots_empty)
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
	if (id < 0) return false;	// 無卡可選（from 的卡全在 disabled_ids，或 AI 回 -1）→ 直接失敗，避免 getCard(-1) nullptr deref
	Player::Place place = getCardPlace(id);
	const Card*c = Sanguosha->getCard(id);

	QList<ServerPlayer*> to_players;
	foreach(ServerPlayer*p, tos){
		if (player->isProhibited(p, c)) continue;
		if (place == Player::PlaceEquip){
			const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
			QList<int> occupy_slots = equip->getOccupyLocations();
			bool all_slots_empty = true;
			foreach(int slot, occupy_slots){
				if(p->getEquip(slot)){
					all_slots_empty = false;
					break;
				}
			}
			if (all_slots_empty)
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

void Room::swapEquips(ServerPlayer*first, ServerPlayer*second, const QString&skill_name)
{
	QList<int> ids1, ids2;
	foreach(const Card *equip, first->getEquips())
		ids1.append(equip->getId());
	foreach(const Card *equip, second->getEquips())
		ids2.append(equip->getId());

	QList<CardsMoveStruct> exchangeMove;

	CardsMoveStruct move1(ids1, second, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), skill_name, ""));
	CardsMoveStruct move2(ids2, first, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, second->objectName(), first->objectName(), skill_name, ""));
	exchangeMove.append(move2);
	exchangeMove.append(move1);

	moveCardsAtomic(exchangeMove, false);
}

void Room::changeTranslation(ServerPlayer*player, const QString&skill_name, const QString&new_translation, int num, int instanceId)
{
	//Sanguosha->addTranslationEntry(":"+skill_name,new_translation);
	JsonArray args1;
	args1 << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL << player->objectName() << skill_name;
	QString propKey = instanceId > 0 ? QString("changeTranslation%1#%2").arg(skill_name).arg(instanceId) : QString("changeTranslation"+skill_name);
	if (num>0){
		args1 << num;
		setPlayerProperty(player,propKey.toStdString().c_str(),num);
	}else{
		args1 << new_translation;
		setPlayerProperty(player,propKey.toStdString().c_str(),args1.last());
	}
	doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args1);
	//更新技能图标上的技能描述
	QString notifyName = instanceId > 0 ? QString("%1#%2").arg(skill_name).arg(instanceId) : skill_name;
	if (player->hasSkill(notifyName, true))
		doNotify(player, S_COMMAND_UPDATE_SKILL, notifyName);  //自带更新武将图上的技能描述的功能，但有时会失灵，不知道为何
}

void Room::changeTranslation(ServerPlayer*player, const QString&skill_name, int num, int instanceId)
{
	QString new_translation = ":"+skill_name;
	if(num==0) new_translation = Sanguosha->translate(new_translation, true);
	else new_translation = Sanguosha->translate(new_translation+QString::number(num));
	changeTranslation(player, skill_name, new_translation, num, instanceId);
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
		foreach(int id, ListV2I(player->getTag(reason + "ForAI").toList())){
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
		player->setTag(reason + "ForAI", ListI2V(cards));/*
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
		foreach(int id, ListV2I(player->getTag(reason + "ForAI").toList())){
			Player::Place place_ = place;
			if(place==Player::PlaceUnknown) place_ = getCardPlace(id);
			CardsMoveStruct move = CardsMoveStruct(id, player, getCardOwner(id), Player::PlaceSpecial, place_, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName()));
			move.from_pile_name = "#" + reason;
			move.to_pile_name = "#" + reason;
			moves << move;
		}
		player->removeTag(reason + "ForAI");
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
	return m_playerDecisions->askForOrder(player, default_choice);
}

QString Room::askForRole(ServerPlayer*player, const QStringList&roles, const QString&scheme)
{
	return m_playerDecisions->askForRole(player, roles, scheme);
}

void Room::networkDelayTestCommand(ServerPlayer*player, const QVariant &arg)
{
	NetworkDelayPayload delayPayload;
	if (!NetworkDelayPayload::parse(arg, &delayPayload))
		return;

	qint64 delay = player->endNetworkDelayTest();
	QString reportStr = QString("<font color=#EEB422>网络延迟为%1毫秒</font>").arg(delay);
	//tr("<font color=#EEB422>The network delay of player <b>%1</b> is %2 milliseconds.</font>").arg(player->screenName()).arg(delay);
	ChatPayload chatPayload;
	chatPayload.text = reportStr;
	speakCommand(player, chatPayload.toVariant());
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
	lua_getglobal(getLuaState(), "bossModeExpMult");
	lua_pushinteger(getLuaState(), level);
	int res = 0;
	LuaRuntime::LuaInvocationScope invocation(m_runtime->lua());
	if (lua_pcall(getLuaState(), 1, 1, 0) == 0){
		res = lua_tointeger(getLuaState(), -1);
		lua_pop(getLuaState(), 1);
	} else {
		const QString error_msg = luaErrorWithTraceback(getLuaState());
		lua_pop(getLuaState(), 1);
		const_cast<Room *>(this)->output("bossModeExpMult error: " + error_msg);
	}
	return res;
}

void Room::handleAnytimeSkillRequest(ServerPlayer *player, const QVariant &arg)
{
	AnytimeSkillPayload payload;
	if (!player || !AnytimeSkillPayload::parse(arg, &payload)) return;
	QString skill_name = payload.skillName;
	const AnytimeSkill *skill = qobject_cast<const AnytimeSkill *>(Sanguosha->getSkill(skill_name));
	if (!skill || !player->hasSkill(skill_name)) return;
	if (!skill->canTrigger(player)) return;
	player->addPendingAnytimeSkill(skill_name);
}

void Room::processPendingAnytimeSkills()
{
	foreach (ServerPlayer *player, getAlivePlayers()) {
		QStringList pending = player->getPendingAnytimeSkills();
		if (pending.isEmpty()) continue;
		foreach (const QString &skill_name, pending) {
			const AnytimeSkill *skill = qobject_cast<const AnytimeSkill *>(Sanguosha->getSkill(skill_name));
			if (!skill) continue;
			skill->onTrigger(this, player);
			notifyAnytimeSkillDone(player, skill_name);
		}
		player->clearPendingAnytimeSkills();
	}
}

void Room::notifyAnytimeSkillDone(ServerPlayer *player, const QString &skill_name)
{
	AnytimeSkillPayload payload;
	payload.skillName = skill_name;
	doNotify(player, S_COMMAND_ANYTIME_SKILL_DONE, payload.toVariant());
}

void Room::saveSnapshot(const QString &type, const QString &playerName)
{
	m_snapshotService->saveSnapshot(type, playerName);
}

GameSnapshot* Room::getSnapshot(int turnCount) const
{
	return m_snapshotService->getSnapshot(turnCount);
}

QString Room::getSnapshotDir() const
{
	return m_snapshotService->getSnapshotDir();
}

void Room::setReplayPath(const QString &path)
{
	m_snapshotService->setReplayPath(path);
}

QString Room::getReplayPath() const
{
	return m_snapshotService->getReplayPath();
}

void Room::initializeReplayRecordPath()
{
	if (!Config.value("recorder/autosave", true).toBool())
		return;

	if (Config.value("recorder/networkonly", true).toBool()) {
		bool hasNonRobot = false;
		foreach (ServerPlayer *player, getPlayers()) {
			if (player->getState() != QStringLiteral("robot")) {
				hasNonRobot = true;
				break;
			}
		}
		if (!hasNonRobot)
			return;
	}

	// replay 係使用者資料,唔可以寫入安裝樹/AppImage(唯讀)。
	const QString recordDir = QSanRuntimePaths::recordDir();

	const QString replayPath = recordDir + QStringLiteral("/")
		+ QDateTime::currentDateTime().toString(QStringLiteral("yyyy年MM月dd日HH时mm分ss秒"))
		+ QStringLiteral(".txt");
	setReplayPath(replayPath);
}

void Room::registerTestOverride(ServerPlayer *player, const QString &queryType, const QString &key, const QVariant &answer)
{
	m_playerDecisions->registerTestOverride(player, queryType, key, answer);
}

void Room::clearTestOverrides()
{
	m_playerDecisions->clearTestOverrides();
}

void Room::initializeLuaTestEnvironment()
{
	// Lua 測試執行器以 ROOM 存取目前房間。
	doScript("ROOM = R");
}

QVariant Room::findTestOverride(ServerPlayer *player, const QString &queryType, const QString &key) const
{
	return m_playerDecisions->findTestOverride(player, queryType, key);
}
