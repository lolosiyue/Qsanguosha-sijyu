#include "client.h"
#include "client-core.h"
#include "client-prompt.h"
#include "client-live-session.h"
#include "client-game-state-reducer.h"
#include "desktop-interaction-view.h"
#include "interaction-descriptor-registry.h"
#include "interaction-request-factory.h"
#include "interaction-reply-coordinator.h"
#include "interaction-reply-encoder.h"
#include "runtime-paths.h"
#include "settings.h"
#include "ui-rng.h"
#include "engine.h"
#include "lua.hpp"
#include "choosegeneraldialog.h"
#include "nativesocket.h"
#include "recorder.h"
#include "json.h"
#include "clientplayer.h"
#include "clientstruct.h"
#include "wrapped-card.h"
#include "skill-instance-utils.h"
#include "protocol/card-provenance-message.h"
#include "protocol/gameplay/simple-choice-payloads.h"
#include "protocol/protocol-payload-registry.h"
#include "protocol/session/session-payloads.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "protocol/switch-context-message.h"
#include "protocol/sync-pile-message.h"
#include <QSet>
#include <QJsonDocument>
#include <QDebug>

using namespace std;
using namespace QSanProtocol;

Client *ClientInstance = nullptr;
static bool recorder_eventsave = false;

static ClientPlayer *getControlRootPlayer(ClientPlayer *player)
{
	if (player == nullptr)
		return nullptr;

	QSet<QString> visited;
	ClientPlayer *current = player;
	while (current != nullptr) {
		QString currentName = current->objectName();
		if (visited.contains(currentName))
			return current;

		visited.insert(currentName);
		QString controllerName = current->getTag("Controller_Name").toString();
		if (controllerName.isEmpty())
			return current;

		ClientPlayer *next = ClientInstance != nullptr ? ClientInstance->getPlayer(controllerName) : nullptr;
		if (next == nullptr)
			return current;

		current = next;
	}

	return player;
}

Client::Client(QObject *parent, const QString &filename, ClientSocket *injectedSocket,
               bool takeoverRecord, bool initialReconnectRequested)
	: QObject(parent), m_isDiscardActionRefusable(true), m_bossLevel(0),
	status(NotActive), alive_count(1), swap_pile(0), add_round(0), _m_roomState(true),
	m_client_lua(nullptr), m_original_self(nullptr),
	m_liveSession(nullptr), m_interactionCore(nullptr), m_desktopInteractionView(nullptr),
	m_dispatchingRequestId(0)
{
	ClientInstance = this;
	m_isGameOver = false;
	m_isDisconnected = true;

	// Init Local Lua VM
	m_client_lua = CreateLuaState();
	DoLuaScript(m_client_lua, "lua/config.lua");

	m_callbacks[S_COMMAND_CHECK_VERSION] = &Client::checkVersion;
	m_callbacks[S_COMMAND_SETUP] = &Client::setup;
	m_callbacks[S_COMMAND_NETWORK_DELAY_TEST] = &Client::networkDelayTest;
	m_callbacks[S_COMMAND_ADD_PLAYER] = &Client::addPlayer;
	m_callbacks[S_COMMAND_ADD_PLAYER_DYNAMIC] = &Client::onPlayerAddedMidGame;
	m_callbacks[S_COMMAND_REMOVE_PLAYER] = &Client::removePlayer;
	m_callbacks[S_COMMAND_START_IN_X_SECONDS] = &Client::startInXs;
	m_callbacks[S_COMMAND_ARRANGE_SEATS] = &Client::arrangeSeats;
	m_callbacks[S_COMMAND_WARN] = &Client::warn;
	m_callbacks[S_COMMAND_SPEAK] = &Client::speak;

	m_callbacks[S_COMMAND_GAME_START] = &Client::startGame;
	m_callbacks[S_COMMAND_GAME_OVER] = &Client::gameOver;

	m_callbacks[S_COMMAND_CHANGE_HP] = &Client::hpChange;
	m_callbacks[S_COMMAND_CHANGE_MAXHP] = &Client::maxhpChange;
	m_callbacks[S_COMMAND_KILL_PLAYER] = &Client::killPlayer;
	m_callbacks[S_COMMAND_REVIVE_PLAYER] = &Client::revivePlayer;
	m_callbacks[S_COMMAND_SHOW_CARD] = &Client::showCard;
	m_callbacks[S_COMMAND_SHOW_VIRTUAL_CARD] = &Client::showVirtualCard;
	m_callbacks[S_COMMAND_CARD_PROVENANCE] = &Client::cardProvenance;
	m_callbacks[S_COMMAND_UPDATE_PLAYER_UI_STATE] = &Client::updatePlayerUIState;
	m_callbacks[S_COMMAND_STATE_SYNC] = &Client::stateSync;
	m_callbacks[S_COMMAND_UPDATE_CARD] = &Client::updateCard;
	m_callbacks[S_COMMAND_SET_MARK] = &Client::setMark;
	m_callbacks[S_COMMAND_LOG_SKILL] = &Client::log;
	m_callbacks[S_COMMAND_ATTACH_SKILL] = &Client::attachSkill;
	m_callbacks[S_COMMAND_SKILL_INSTANCE] = &Client::syncSkillInstances;
	m_callbacks[S_COMMAND_MOVE_FOCUS] = &Client::moveFocus;
	m_callbacks[S_COMMAND_SET_EMOTION] = &Client::setEmotion;
	m_callbacks[S_COMMAND_CHANGE_TABLE_BG] = &Client::changeTableBg;
	m_callbacks[S_COMMAND_INVOKE_SKILL] = &Client::skillInvoked;
	m_callbacks[S_COMMAND_SHOW_ALL_CARDS] = &Client::showAllCards;
	m_callbacks[S_COMMAND_SKILL_GONGXIN] = &Client::askForGongxin;
	m_callbacks[S_COMMAND_LOG_EVENT] = &Client::handleGameEvent;
	m_callbacks[S_COMMAND_ADD_HISTORY] = &Client::addHistory;
	m_callbacks[S_COMMAND_ANIMATE] = &Client::animate;
	m_callbacks[S_COMMAND_FIXED_DISTANCE] = &Client::setFixedDistance;
	m_callbacks[S_COMMAND_ATTACK_RANGE] = &Client::setAttackRangePair;
	m_callbacks[S_COMMAND_CARD_LIMITATION] = &Client::cardLimitation;
	m_callbacks[S_COMMAND_NULLIFICATION_ASKED] = &Client::setNullification;
	m_callbacks[S_COMMAND_ENABLE_SURRENDER] = &Client::enableSurrender;
	m_callbacks[S_COMMAND_EXCHANGE_KNOWN_CARDS] = &Client::exchangeKnownCards;
	m_callbacks[S_COMMAND_SET_KNOWN_CARDS] = &Client::setKnownCards;
	m_callbacks[S_COMMAND_SWITCH_CONTEXT] = &Client::processContextSwitch;
	m_callbacks[S_COMMAND_VIEW_GENERALS] = &Client::viewGenerals;
	m_callbacks[S_COMMAND_PLAY_AUDIO] = &Client::playAudio;

	m_callbacks[S_COMMAND_UPDATE_BOSS_LEVEL] = &Client::updateBossLevel;
	m_callbacks[S_COMMAND_UPDATE_STATE_ITEM] = &Client::updateStateItem;
	m_callbacks[S_COMMAND_AVAILABLE_CARDS] = &Client::setAvailableCards;

	m_callbacks[S_COMMAND_GET_CARD] = &Client::getCards;
	m_callbacks[S_COMMAND_LOSE_CARD] = &Client::loseCards;
	m_callbacks[S_COMMAND_SET_PROPERTY] = &Client::updateProperty;
	m_callbacks[S_COMMAND_RESET_PILE] = &Client::resetPiles;
	m_callbacks[S_COMMAND_UPDATE_PILE] = &Client::setPileNumber;
	m_callbacks[S_COMMAND_SYNCHRONIZE_DISCARD_PILE] = &Client::synchronizeDiscardPile;
	m_callbacks[S_COMMAND_SYNC_PILE] = &Client::syncPile;
	m_callbacks[S_COMMAND_CARD_MARK] = &Client::setCardMark;
	m_callbacks[S_COMMAND_CARD_FLAG] = &Client::setCardFlag;
	m_callbacks[S_COMMAND_OPERATION_TIMEOUT] = &Client::setTimeout;
	m_callbacks[S_COMMAND_WEAPON_RANGE] = &Client::updateWeaponRange;
	m_callbacks[S_COMMAND_MIRROR_GUANXING_STEP] = &Client::mirrorGuanxingStep;

	// interactive methods

	m_callbacks[S_COMMAND_FILL_AMAZING_GRACE] = &Client::fillAG;
	m_callbacks[S_COMMAND_TAKE_AMAZING_GRACE] = &Client::takeAG;
	m_callbacks[S_COMMAND_CLEAR_AMAZING_GRACE] = &Client::clearAG;

	// 3v3 mode & 1v1 mode

	m_callbacks[S_COMMAND_FILL_GENERAL] = &Client::fillGenerals;
	m_callbacks[S_COMMAND_TAKE_GENERAL] = &Client::takeGeneral;
	m_callbacks[S_COMMAND_RECOVER_GENERAL] = &Client::recoverGeneral;
	m_callbacks[S_COMMAND_REVEAL_GENERAL] = &Client::revealGeneral;
	m_callbacks[S_COMMAND_UPDATE_SKILL] = &Client::updateSkill;
	m_callbacks[S_COMMAND_ADD_ROUND] = &Client::addRound;
	m_callbacks[S_COMMAND_SKILL_DESCRIPTION_SWAP] = &Client::setSkillDescriptionSwap;
	m_callbacks[S_COMMAND_ADD_EQUIP_AREA] = &Client::addEquipArea;
	m_callbacks[S_COMMAND_SET_EQUIP_AREA_COUNT] = &Client::setEquipAreaCount;
	m_callbacks[S_COMMAND_UPDATE_CARD_DESC] = &Client::updateCardDescription;
	m_callbacks[S_COMMAND_ANYTIME_SKILL_DONE] = &Client::handleAnytimeSkillDone;
	m_callbacks[S_COMMAND_SET_SHOWN_HANDCARD] = &Client::setShownHandCards;
	m_callbacks[S_COMMAND_SET_BROKEN_EQUIP] = &Client::setBrokenEquips;
	m_callbacks[S_COMMAND_PRESHOW] = &Client::preshow;
	for (const ClientInteractionDescriptor &descriptor : InteractionDescriptorRegistry::descriptors())
		m_interactions.insert(descriptor.command, descriptor.builder);
	Q_ASSERT_X(m_interactions.size() == static_cast<int>(InteractionDescriptorRegistry::descriptors().size()),
		"Client::Client", "interactive command inventory size mismatch");
	for (const ClientInteractionDescriptor &descriptor : InteractionDescriptorRegistry::descriptors()) {
		Q_ASSERT_X(m_interactions.value(descriptor.command) == descriptor.builder,
			"Client::Client", "interactive command descriptor was not registered");
	}

	m_noNullificationThisTime = false;
	m_noNullificationTrickName = ".";
	m_respondingUseFixedTarget = nullptr;

	recorder_eventsave = Config.value("recorder/eventsave").toBool();

	Self = new ClientPlayer(this);
	setEngineSelf(Self);
	Self->setScreenName(Config.UserName);
	Self->setProperty("avatar", Config.UserAvatar);
	m_original_self = Self;
	connect(Self, SIGNAL(phase_changed()), this, SLOT(alertFocus()));
	connect(Self, SIGNAL(role_changed(QString)), this, SLOT(notifyRoleChange(QString)));

	m_players << Self;

	// F1:interaction 中間層。Client 建立佢同 desktop adapter,係因為 Client
	// 本身就係 GUI 客戶端;一個 text／Android／WASM front-end 只需要
	// interactionCore()->setView(自己嗰個 view) 就可以換走 desktop adapter。
	m_interactionCore = new ClientCore(this);
	m_customInteractionRegistry.registerType(QStringLiteral("qsanguosha.qml"), 1,
		QStringLiteral("desktop.qml-loader"));
	m_desktopInteractionView = new DesktopInteractionView(this);
	m_interactionCore->setView(m_desktopInteractionView);
	syncInteractionState();

	lines_doc = new QTextDocument(this);

	prompt_doc = new QTextDocument(this);
	prompt_doc->setTextWidth(350);
#ifdef Q_OS_LINUX
	prompt_doc->setDefaultFont(QFont("DroidSansFallback"));
#else
	prompt_doc->setDefaultFont(QFont("SimHei"));
#endif

	if (injectedSocket)
		injectedSocket->setParent(this);

	if (filename.isEmpty()) {
		recorder = new Recorder(this, takeoverRecord);
		m_isDisconnected = false;

		replayer = nullptr;
		m_liveSession = new ClientLiveSession(m_interactionCore, this);
		connect(m_liveSession, &ClientLiveSession::transportConnected,
			this, &Client::socket_connected);
		connect(m_liveSession, &ClientLiveSession::frontendMessageReceived,
			this, &Client::processLiveProtocolMessage);
		connect(m_liveSession, &ClientLiveSession::disconnected, this, [this]() {
			m_isDisconnected = true;
			emit socket_disconnected();
		});
		connect(m_liveSession, &ClientLiveSession::fatalError, this,
			[this](int, const QString &, const QString &detail) {
				emit error_message(detail);
			});

		ClientLiveSessionOptions options;
		options.screenName = Config.UserName;
		options.avatar = Config.UserAvatar;
		options.reconnectRequested = initialReconnectRequested;
		options.fallbackToFreshSignup = initialReconnectRequested;
		options.automaticSignup = false;
		options.host = Config.HostAddress;
		options.port = Config.value("ServerPort", "9527").toString().toUShort();
		const qsizetype separator = options.host.lastIndexOf(QLatin1Char(':'));
		if (separator > 0 && options.host.indexOf(QLatin1Char(':')) == separator) {
			bool portOk = false;
			const quint16 explicitPort = options.host.mid(separator + 1).toUShort(&portOk);
			if (portOk) {
				options.port = explicitPort;
				options.host.truncate(separator);
			}
		}
		m_liveSession->connectToServer(options, injectedSocket);
	} else {
		recorder = nullptr;

		replayer = new Replayer(this, filename);
		connect(replayer, &Replayer::command_parsed,
			this, &Client::processReplayMessage);
	}
}

Client::~Client()
{
	// View 一定要死喺 core 之前:core 唔可以 present 落一嚿死物度。
	delete m_desktopInteractionView;
	m_desktopInteractionView = nullptr;
	setEngineSelf(nullptr);
	Self = nullptr;
	foreach (const ClientPlayer *p, m_players)
		delete p;
	if (m_client_lua) {
		lua_close(m_client_lua);
		m_client_lua = nullptr;
	}
	ClientInstance = nullptr;
}

void Client::setSelf(ClientPlayer *newSelf)
{
	if (newSelf == nullptr)
		newSelf = m_original_self;
	if (newSelf == nullptr)
		return;

	ClientPlayer *oldSelf = Self;
	if (oldSelf != nullptr && oldSelf != newSelf && !newSelf->canSeeHandcard(oldSelf))
		oldSelf->retainVisibleKnownHandcards();

	if (oldSelf != nullptr && oldSelf != newSelf) {
		disconnect(oldSelf, SIGNAL(phase_changed()), this, SLOT(alertFocus()));
		disconnect(oldSelf, SIGNAL(role_changed(QString)), this, SLOT(notifyRoleChange(QString)));
	}

	Self = newSelf;
	setEngineSelf(Self);
	if (m_noNullificationTrickName == ".")
		m_noNullificationThisTime = false;
	else
		m_noNullificationThisTime = m_noNullificationPlayers.contains(Self->objectName());
	connect(Self, SIGNAL(phase_changed()), this, SLOT(alertFocus()), Qt::UniqueConnection);
	connect(Self, SIGNAL(role_changed(QString)), this, SLOT(notifyRoleChange(QString)), Qt::UniqueConnection);
	emit switch_control_context(Self->objectName());
}

void Client::processContextSwitch(const QVariant &target_name)
{
	SwitchContextMessage message;
	if (!message.tryParse(target_name))
		return;
	ClientPlayer *target = getPlayer(message.playerName);
	if (target == nullptr || target->isDead()) {
		setSelf(m_original_self);
		return;
	}

	setSelf(target);
}

void Client::updateCard(const QVariant &val)
{
	const QVariantMap wire = val.toMap();
	const QString action = wire.value(QStringLiteral("action")).toString();
	if (action == QLatin1String("reset")) {
		// reset card
		int cardId = wire.value(QStringLiteral("card_id")).toInt();/*
		WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
		if (wrapped && wrapped->isModified())*/
			_m_roomState.resetCard(cardId);
	} else {
		// update card
		int cardId = wire.value(QStringLiteral("card_id")).toInt();
		Card::Suit suit = static_cast<Card::Suit>(
			wire.value(QStringLiteral("suit")).toInt());
		int number = wire.value(QStringLiteral("number")).toInt();
		QString cardName = wire.value(QStringLiteral("card_name")).toString();
		QString skillName = wire.value(QStringLiteral("skill_name")).toString();
		QString objectName = wire.value(QStringLiteral("object_name")).toString();
		QStringList flags;
		JsonUtils::tryParse(wire.value(QStringLiteral("flags")), flags);

		Card *card = Sanguosha->cloneCard(cardName, suit, number, flags);
		card->setId(cardId);
		card->setSkillName(skillName);
		card->setObjectName(objectName);
		WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
		//Q_ASSERT(wrapped != nullptr);
		wrapped->copyEverythingFrom(card);
	}
}

void Client::signup()
{
	if (replayer)
	{
		if (!replayer->isValid()) {
			emit error_message(replayer->errorString());
			return;
		}
		replayer->start();
	}
	else {
		if (m_original_self == nullptr)
			m_original_self = Self;
		QString error;
		if (m_liveSession == nullptr || !m_liveSession->requestSignup(&error)) {
			failProtocol(error);
		}
	}
}

void Client::networkDelayTest(const QVariant &arg)
{
	NetworkDelayPayload payload;
	QString error;
	if (!NetworkDelayPayload::parse(arg, &payload, &error)) {
		failProtocol(error);
		return;
	}
	requestServer(S_COMMAND_NETWORK_DELAY_TEST, payload.toVariant());
}

void Client::replyToServer(CommandType command, const QVariant &arg)
{
	if (m_liveSession != nullptr) {
		if (m_dispatchingRequestId == 0) {
			qWarning().noquote() << "Refusing uncorrelated Protocol V2 reply" << command;
			return;
		}
		QString error;
		if (!m_liveSession->sendReply(command, arg, m_dispatchingRequestId, &error))
			failProtocol(error);
	}
	emit server_reply(static_cast<int>(command));
}

void Client::handleGameEvent(const QVariant &arg)
{
	if(recorder_eventsave){
		save(QSanRuntimePaths::recordDir()+"/debug.txt");
	}
	const JsonArray args = arg.value<JsonArray>();
	if (args.size() >= 5 && args[0].toString() == QStringLiteral("guhuo_box")) {
		emit guhuoBox(args[1].toString(), args[2].toString(),
		              args[3].toString(), args[4].toInt());
		return;
	}
	emit event_received(arg);
}

void Client::requestServer(CommandType command, const QVariant &arg)
{
	if (m_liveSession == nullptr)
		return;
	QString error;
	if (!m_liveSession->sendRequest(command, arg, &error))
		failProtocol(error);
}

void Client::notifyServer(CommandType command, const QVariant &arg)
{
	if (m_liveSession == nullptr)
		return;
	QString error;
	if (!m_liveSession->sendControl(command, arg, &error))
		failProtocol(error);
}

void Client::checkVersion(const QVariant &server_version)
{
	ServerHelloPayload payload;
	QString error;
	if (!ServerHelloPayload::parse(server_version, &payload, &error)) {
		failProtocol(error);
		return;
	}
	emit version_checked(payload.gameVersion, payload.modName, payload.cardCount);
}

void Client::setup(const QVariant &setup_json)
{
	if (m_liveSession != nullptr && !m_liveSession->isActive())
		return;

	SetupPayload payload;
	QString error;
	if (!SetupPayload::parse(setup_json, &payload, &error)) {
		failProtocol(error);
		return;
	}
	ServerInfo.Name = payload.serverName;
	ServerInfo.GameMode = payload.gameMode;
	ServerInfo.GameRuleMode = payload.gameRuleMode;
	ServerInfo.OperationTimeout = payload.operationTimeout;
	ServerInfo.NullificationCountDown = payload.nullificationCountdown;
	ServerInfo.ServerTimeoutGraciousPeriod = payload.serverTimeoutGraciousPeriod;
	ServerInfo.BanPackages = payload.banPackages;
	ServerInfo.RandomSeat = payload.randomSeat;
	ServerInfo.EnableCheat = payload.enableCheat;
	ServerInfo.FreeChoose = payload.freeChoose;
	ServerInfo.Enable2ndGeneral = payload.enableSecondGeneral;
	ServerInfo.EnableSame = payload.enableSame;
	ServerInfo.EnableBasara = payload.enableBasara;
	ServerInfo.EnableHegemony = payload.enableHegemony;
	ServerInfo.EnableMeleeMode = payload.enableMeleeMode;
	ServerInfo.EnableAI = payload.enableAi;
	ServerInfo.DisableChat = payload.disableChat;
	ServerInfo.MaxHpScheme = payload.maxHpScheme;
	ServerInfo.Scheme0Subtraction = payload.scheme0Subtraction;
	ServerInfo.DuringGame = true;
	if (replayer) {
		emit server_connected();
		return;
	}

	emit server_connected();
}

void Client::disconnectFromHost()
{
	// 斷線之後冇人收得到答案,pending request 即刻作廢,唔好留住一個永遠
	// 完成唔到嘅 request。
	cancelInteraction(InteractionType::None, InteractionCancelReason::Disconnected);
	if (!m_isDisconnected && m_liveSession != nullptr) {
		m_liveSession->disconnectGracefully();
		m_isDisconnected = true;
	}
}

void Client::processReplayMessage(const ProtocolMessage &message)
{
	dispatchProtocolMessage(message, true);
}

void Client::processLiveProtocolMessage(const ProtocolMessage &message)
{
	if (m_isGameOver)
		return;
	if (message.command == S_COMMAND_SIGNUP
		&& message.type == ProtocolMessageType::Reply) {
		return;
	}

	if (recorder != nullptr
		&& ProtocolPayloadRegistry::isReplayEligible(message, false)) {
		QString replayError;
		if (!recorder->recordMessage(message, &replayError)) {
			failProtocol(QStringLiteral("Replay recording failed: %1").arg(replayError));
			return;
		}
	}
	dispatchProtocolMessage(message, false);
}

bool Client::dispatchProtocolMessage(const ProtocolMessage &message, bool replayInput)
{
	if (message.type == ProtocolMessageType::Notification) {
		if (replayInput && message.source == ProtocolEndpoint::Room
			&& message.destination == ProtocolEndpoint::Client) {
			ClientGameState *targetState = m_interactionCore->state();
			if (message.command == S_COMMAND_STATE_SYNC) {
				StateSyncPayload sync;
				QString error;
				if (!StateSyncPayload::parse(message.payload, &sync, &error)) {
					failProtocol(error);
					return false;
				}
				if (sync.phase == QLatin1String("begin")) {
					m_pendingStateSyncState = *targetState;
					m_pendingStateSyncState.resetGameplayState();
					m_stateSyncActive = true;
					m_stateSyncId = sync.syncId;
					targetState = &m_pendingStateSyncState;
				} else if (!m_stateSyncActive || sync.syncId != m_stateSyncId) {
					failProtocol(QStringLiteral("STATE_SYNC end does not match an active snapshot"));
					return false;
				} else {
					targetState = &m_pendingStateSyncState;
				}
			} else if (m_stateSyncActive) {
				targetState = &m_pendingStateSyncState;
			}

			const ClientStateReduction reduction
				= ClientGameStateReducer::applyNotification(targetState,
					message.command, message.payload);
			if (!reduction.success) {
				failProtocol(reduction.detail);
				return false;
			}
			if (message.command == S_COMMAND_STATE_SYNC
				&& message.payload.toMap().value(QStringLiteral("phase")) == QLatin1String("end")) {
				*m_interactionCore->state() = m_pendingStateSyncState;
				m_stateSyncActive = false;
				m_stateSyncId.clear();
			}
		}
		Callback callback = m_callbacks.value(static_cast<CommandType>(message.command), nullptr);
		if (callback)
			(this->*callback)(message.payload);
		return true;
	}
	if (message.type == ProtocolMessageType::Request && !replayInput) {
		return processServerRequest(message);
	}
	// Normal playback is passive: requests are rendered but never answered.
	return replayInput && message.type == ProtocolMessageType::Request;
}

void Client::stateSync(const QVariant &)
{
    // Shared reducer commits the snapshot atomically before GUI presentation callbacks run.
}

void Client::failProtocol(const QString &detail)
{
	qWarning().noquote() << detail;
	emit error_message(detail);
	if (m_liveSession != nullptr)
		m_liveSession->disconnectGracefully();
}

bool Client::processServerRequest(const ProtocolMessage &message)
{
	setStatus(NotActive);
	cancelInteraction(InteractionType::None, InteractionCancelReason::Superseded);
	const CommandType command = static_cast<CommandType>(message.command);
	const QVariant msg = message.payload;

	if (!replayer) {
		Countdown countdown;
		countdown.current = 0;
		countdown.type = Countdown::S_COUNTDOWN_USE_DEFAULT;
		countdown.max = ServerInfo.getCommandTimeout(command, S_CLIENT_INSTANCE);
		setCountdown(countdown);
	}

	Callback callback = m_interactions[command];
	if (!callback) return false;
	emit server_request(static_cast<int>(command));
	m_dispatchingRequestId = message.messageId;
	(this->*callback)(msg);
	m_dispatchingRequestId = 0;
	return true;
}

// ── Client Architecture F1:ClientCore plumbing ─────────────────────────
//
// server 嘅每一個 request 到埗,舊嗰個就作廢:server 已經行咗落去,遲到嘅答案
// 唔應該再被當成有效。未遷移嘅 interaction 亦因此唔會撞到殘留嘅 core request
// (例如 choose direction 同 choice 共用 onPlayerMakeChoice() 呢個 slot)。
void Client::cancelInteraction(InteractionType type, InteractionCancelReason reason)
{
	if (m_interactionCore == nullptr)
		return;
	if (type != InteractionType::None && !m_interactionCore->hasActiveRequest(type))
		return;
	m_interactionCore->cancelActiveRequest(reason);
}

// core 拎嚟做 reply 驗證同 snapshot 嘅最小客戶端狀態。刻意保持「寬」:
// 攞唔到就唔填,寧可少驗一樣,都唔可以攔錯一個合法回覆。
void Client::syncInteractionState()
{
	if (m_interactionCore == nullptr)
		return;

	ClientGameState *state = m_interactionCore->state();
	state->setSelfName(Self != nullptr ? Self->objectName() : QString());

	QStringList names;
	foreach (const ClientPlayer *player, m_players) {
		if (player != nullptr)
			names << player->objectName();
	}
	state->setPlayerNames(names);

	// 卡 id 就係 Engine 卡表嘅 index,所以任何合法 id 都細過卡數。
	if (Sanguosha != nullptr)
		state->setCardIdSpace(Sanguosha->getCardCount());
}

void Client::beginInteraction(InteractionRequest request)
{
	if (m_interactionCore == nullptr)
		return;

	syncInteractionState();
	if (request.requestId == 0)
		request.requestId = m_dispatchingRequestId;

	// 死線刻意用 server 嗰個(client timeout + gracious period)再加一段
	// margin,而唔係 UI 倒數用嗰個 client timeout:RoomScene::doTimeout() 就係
	// 喺 client timeout 嗰刻先送安全預設答案,如果 core 喺同一刻過期,呢個
	// 答案就會被自己攔住,一局變成要等 server timeout 先行得落去。
	// 過咗呢條線嘅答案,server 一定已經放棄咗,送出去亦冇意義。
	if (request.timeoutMs <= 0 && request.command != 0) {
		const time_t serverTimeout = ServerInfo.getCommandTimeout(
			static_cast<CommandType>(request.command), S_SERVER_INSTANCE);
		if (serverTimeout > 0)
			request.timeoutMs = static_cast<qint64>(serverTimeout) + 5000;
	}

	m_interactionCore->beginRequest(request);
}

InteractionRequest Client::makeInteractionRequest(InteractionType type,
	InteractionPayload payload, bool cancelable) const
{
	const ClientInteractionDescriptor *descriptor = InteractionDescriptorRegistry::find(type);
	if (descriptor == nullptr)
		return InteractionRequest();
	return InteractionRequestFactory::create(descriptor->type, descriptor->command,
		descriptor->responseShape, std::move(payload), cancelable);
}

bool Client::submitInteractionResponse(InteractionResponse response)
{
	if (m_interactionCore == nullptr || !m_interactionCore->hasActiveRequest())
		return false;
	if (m_liveSession != nullptr) {
		QString error;
		const bool accepted = m_liveSession->submitInteractionResponse(
			std::move(response), &error);
		if (!accepted && !error.isEmpty())
			qWarning().noquote() << error;
		return accepted;
	}
	const ClientInteractionDescriptor *descriptor
		= InteractionDescriptorRegistry::find(m_interactionCore->activeRequest().type);
	if (descriptor == nullptr || descriptor->replyEncoder == nullptr)
		return false;
	return InteractionReplyCoordinator::submit(m_interactionCore,
		descriptor->replyEncoder, std::move(response),
		[this](const InteractionWireReply &reply) {
			if (reply.command == S_COMMAND_UNKNOWN || reply.replyTo == 0)
				return;
			m_dispatchingRequestId = reply.replyTo;
            replyToServer(reply.command, reply.payload);
			m_dispatchingRequestId = 0;
		});
}

// ── DesktopInteractionView 嘅呈現 port ──────────────────────────────────

void Client::presentGeneralChoice(const InteractionRequest &request)
{
	QStringList generals;
	if (const OptionInteractionPayload *payload = request.payloadAs<OptionInteractionPayload>()) {
		for (const InteractionOption &option : payload->options)
			generals << option.value;
	}
	emit generals_got(generals);
	setStatus(ExecDialog);
}

void Client::presentOptionChoice(const InteractionRequest &request)
{
	QStringList enabled;
	QStringList disabled;
	QString tip;
	if (const OptionInteractionPayload *payload = request.payloadAs<OptionInteractionPayload>()) {
		tip = payload->tip;
		for (const InteractionOption &option : payload->options) {
			if (option.metadata.value(QStringLiteral("synthetic_cancel")).toBool())
				continue;
			(option.enabled ? enabled : disabled) << option.value;
		}
	}
	emit options_got(request.skillName, enabled, disabled.join(QLatin1Char('+')), tip);
	setStatus(ExecDialog);
}

void Client::presentPlayerChoice(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForPlayerChoose);
}

void Client::presentSkillInvoke(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForSkillInvoke);
}

void Client::presentCardResponse(const InteractionRequest &request)
{
	// 呢個 request 嘅 prompt 由 builder 直接砌落 prompt_doc:當中「附加技能
	// Notice」嗰步要讀返 document 已經 render 好嘅 HTML(prompt_doc->toHtml()),
	// 唔可以喺呢度用一個純字串重砌。所以呢個 view 只負責狀態切換。
	Status requested = Responding;
	if (const CardInteractionPayload *payload = request.payloadAs<CardInteractionPayload>()) {
		switch (static_cast<Card::HandlingMethod>(payload->selection.handlingMethod)) {
		case Card::MethodPlay: requested = Playing; break;
		case Card::MethodDiscard: requested = RespondingForDiscard; break;
		case Card::MethodUse: requested = RespondingUse; break;
		case Card::MethodResponse: requested = Responding; break;
		default: requested = RespondingNonTrigger; break;
		}
	}
	setStatus(requested);
}

void Client::presentRoleAssignment(const InteractionRequest &)
{
	emit assign_asked();
}

void Client::presentDirectionChoice(const InteractionRequest &)
{
	emit directions_got();
	setStatus(ExecDialog);
}

void Client::presentCardExchange(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(Exchanging);
}

void Client::presentCardDiscard(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(Discarding);
}

void Client::presentRespondingUse(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(RespondingUse);
}

void Client::presentShowOrPindian(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForShowOrPindian);
}

void Client::presentPlayCard(const InteractionRequest &)
{
	setStatus(Playing);
}

void Client::presentGuanxing(const InteractionRequest &request)
{
	const RearrangeCardsInteractionPayload *payload
		= request.payloadAs<RearrangeCardsInteractionPayload>();
	if (payload != nullptr) {
		int legacyMode = 0;
		if (payload->mode == RearrangementMode::UpOnly)
			legacyMode = 1;
		else if (payload->mode == RearrangementMode::DownOnly)
			legacyMode = -1;
		emit guanxing(payload->cardIds, legacyMode);
	}
	setStatus(AskForGuanxing);
}

void Client::presentGongxin(const InteractionRequest &request)
{
	const GongxinInteractionPayload *payload = request.payloadAs<GongxinInteractionPayload>();
	if (payload != nullptr)
		emit gongxin(payload->visibleCards, payload->allowHeartOperation,
			payload->selectableCards);
	setStatus(AskForGongxin);
}

void Client::presentYiji(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForYiji);
}

void Client::presentSuitChoice(const InteractionRequest &request)
{
	QStringList values;
	if (const OptionInteractionPayload *payload = request.payloadAs<OptionInteractionPayload>())
		for (const InteractionOption &option : payload->options) values << option.value;
	emit suits_got(values);
	setStatus(ExecDialog);
}

void Client::presentKingdomChoice(const InteractionRequest &request)
{
	QStringList values;
	if (const OptionInteractionPayload *payload = request.payloadAs<OptionInteractionPayload>())
		for (const InteractionOption &option : payload->options) values << option.value;
	emit kingdoms_got(values);
	setStatus(ExecDialog);
}

void Client::presentTriggerOrder(const InteractionRequest &request)
{
	QVariantList options;
	if (const TriggerOrderInteractionPayload *payload
		= request.payloadAs<TriggerOrderInteractionPayload>()) {
		for (const TriggerOrderOption &option : payload->options) {
			QVariantMap detail;
			detail.insert(QStringLiteral("skill"), option.skillName);
			detail.insert(QStringLiteral("instanceID"), option.instanceId);
			detail.insert(QStringLiteral("invoker"), option.invoker);
			detail.insert(QStringLiteral("owner"), option.owner);
			if (!option.preferredTarget.isEmpty()) {
				detail.insert(QStringLiteral("preferredtarget"), option.preferredTarget);
				detail.insert(QStringLiteral("preferredtargetseat"), option.preferredTargetSeat);
			}
			options << detail;
		}
	}
	emit trigger_order_got(options, request.cancelable);
	setStatus(AskForTriggerOrder);
}

void Client::presentAmazingGrace(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForAG);
}

void Client::presentChooseCard(const InteractionRequest &request)
{
	const CardInteractionPayload *payload = request.payloadAs<CardInteractionPayload>();
	ClientPlayer *player = payload != nullptr ? getPlayer(payload->sourcePlayer) : nullptr;
	if (payload != nullptr && player != nullptr) {
		emit cards_got(player, payload->zoneFlags, request.skillName,
			payload->handCardsVisible,
			static_cast<Card::HandlingMethod>(payload->selection.handlingMethod),
			payload->selection.disabledCards, request.cancelable);
	}
	setStatus(ExecDialog);
}

void Client::presentOrderChoice(const InteractionRequest &request)
{
	const ChooseOrderInteractionPayload *payload
		= request.payloadAs<ChooseOrderInteractionPayload>();
	if (payload != nullptr)
		emit orders_got(static_cast<Game3v3ChooseOrderCommand>(payload->reason));
	setStatus(ExecDialog);
}

void Client::presentRole3v3(const InteractionRequest &request)
{
	if (const OptionInteractionPayload *payload = request.payloadAs<OptionInteractionPayload>()) {
		QStringList roles;
		for (const InteractionOption &option : payload->options) roles << option.value;
		emit roles_got(payload->scheme, roles);
	}
	setStatus(ExecDialog);
}

void Client::presentBooleanPrompt(const InteractionRequest &request)
{
	prompt_doc->setHtml(request.prompt);
	setStatus(AskForSkillInvoke);
}

void Client::presentDraftGeneral(const InteractionRequest &)
{
	emit general_asked();
	setStatus(AskForGeneralTaken);
}

void Client::presentArrangeGeneral(const InteractionRequest &request)
{
	if (const ArrangeGeneralsInteractionPayload *payload
		= request.payloadAs<ArrangeGeneralsInteractionPayload>())
		emit arrange_started(payload->arrangement);
	setStatus(AskForArrangement);
}

void Client::presentQmlInteraction(const InteractionRequest &request)
{
	const CustomInteractionPayload *payload = request.payloadAs<CustomInteractionPayload>();
	if (payload != nullptr && payload->typeName == QLatin1String("qsanguosha.qml")) {
		const QString qmlPath = payload->payload.value(QStringLiteral("qml_path")).toString();
		const QVariantMap parameters = payload->payload
			.value(QStringLiteral("parameters")).toObject().toVariantMap();
		if (qmlPath.isEmpty()) {
			submitInteractionResponse(InteractionResponse::makeCustom(0,
				payload->schemaVersion, payload->typeName, QVariant()));
			return;
		}
		emit qml_interact(qmlPath, parameters);
		setStatus(AskForQml);
	}
}

QJsonArray Client::interactionInventory() const
{
	return InteractionDescriptorRegistry::inventory();
}

void Client::addPlayer(const QVariant &player_info)
{
	const QVariantMap info = player_info.toMap();
	QString name = info.value(QStringLiteral("player_name")).toString();
	QString screen_name = info.value(QStringLiteral("screen_name")).toString();
	QString avatar = info.value(QStringLiteral("avatar")).toString();

	ClientPlayer *player = new ClientPlayer(this);
	player->setObjectName(name);
	player->setScreenName(screen_name);
	player->setProperty("avatar", avatar);

	m_players << player;
	//alive_count++;
	emit player_added(player);
}

void Client::onPlayerAddedMidGame(const QVariant &player_info)
{
	const QVariantMap info = player_info.toMap();
	QString name = info.value(QStringLiteral("player_name")).toString();
	QString screen_name = info.value(QStringLiteral("screen_name")).toString();
	QString avatar = info.value(QStringLiteral("avatar")).toString();

	if (getPlayer(name)) return;

	ClientPlayer *player = new ClientPlayer(this);
	player->setObjectName(name);
	player->setScreenName(screen_name);
	player->setProperty("avatar", avatar);
	player->setSeat(m_players.length() + 1);

	m_players << player;
	emit player_added(player);
}

void Client::updateProperty(const QVariant &arg)
{
	const QVariantMap object = arg.toMap();
	ClientPlayer *player = getPlayer(object.value(QStringLiteral("player_name")).toString());
	if (player){
		const QString action = object.value(QStringLiteral("action")).toString();
		if (action == QLatin1String("tag")) {
			const QString tagKey = object.value(QStringLiteral("tag_name")).toString();
			const QString valueKind = object.value(QStringLiteral("value_kind")).toString();
			if (valueKind == QLatin1String("string_list")) {
				player->setTag(tagKey, object.value(QStringLiteral("value")).toStringList());
			} else if (valueKind == QLatin1String("removed")) {
				player->removeTag(tagKey);
			} else {
				player->setTag(tagKey, object.value(QStringLiteral("value")));
			}
			return;
		}
		if (action == QLatin1String("general_pile")) {
			const QString pile_name = object.value(QStringLiteral("pile_name")).toString();
			const QStringList general_names = object.value(QStringLiteral("general_names")).toStringList();
			const bool add = object.value(QStringLiteral("add")).toBool();
			player->changeGeneralPile(pile_name, add, general_names);
			return;
		}
		if (action != QLatin1String("property"))
			return;
		const QString propName = object.value(QStringLiteral("property_name")).toString();
		player->setProperty(propName.toLatin1().constData(),
			object.value(QStringLiteral("string_value")).toString());
		if(propName.endsWith("area")){
			emit update_areas(object.value(QStringLiteral("player_name")).toString());
		}
		 if(propName == "View_As_Equips_List"){
            emit player->state_changed();
        }
	}
}

void Client::updatePlayerUIState(const QVariant &value)
{
	PlayerUIStateMessage message;
	if (!message.tryParse(value)) {
		qWarning() << "Invalid PlayerUIStateMessage";
		return;
	}

	ClientPlayer *player = getPlayer(message.playerName);
	if (!player || player->uiState() == message.state)
		return;

	const int previousHandMax = player->uiState().handMax;
	player->setUIState(message.state);
	emit player->state_changed();
	if (previousHandMax != message.state.handMax)
		emit update_handcards(message.playerName);
}

void Client::removePlayer(const QVariant &player_name)
{
	const QString playerName = player_name.toMap()
		.value(QStringLiteral("player_name")).toString();
	ClientPlayer *player = findChild<ClientPlayer *>(playerName);
	if (player) {
		if (player == Self && m_original_self != nullptr && player != m_original_self)
			setSelf(m_original_self);
		foreach (const ClientPlayer *p, m_players){
			if(p==player) m_players.removeOne(p);
		}
		player->setParent(nullptr);
		player->deleteLater();
		//alive_count--;
		emit player_removed(playerName);
	}
}

bool Client::_getSingleCard(int card_id, CardsMoveStruct move)
{
	if (move.to_place == Player::DrawPile) pile_num++;
	else if (move.to_place == Player::DiscardPile) discarded_list.prepend(card_id);
	else if (move.to) move.to->addCard(card_id, move.to_place);
	return true;
}

void Client::getCards(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	const QVariantList args = wire.value(QStringLiteral("moves")).toList();
	QList<CardsMoveStruct> moves;
	for (int i = 0; i < args.length(); i++) {
		CardsMoveStruct move;
		QList<int> actual_card_ids;
		// V2 wire 的每個 move 係具名欄位 map；card_ids 係未遮罩嘅原始 id 列表
		JsonUtils::tryParse(args[i].toMap().value(QStringLiteral("card_ids")), actual_card_ids);
		if (move.tryParse(args[i])){
			ClientPlayer *to = getPlayer(move.to_player_name);
			move.from = getPlayer(move.from_player_name);
			move.to = to;
			if (move.to_place == Player::PlaceHand && to == Self)
				move.card_ids = actual_card_ids;
			if (move.to_place == Player::PlaceSpecial)
				to->changePile(move.to_pile_name, true, move.card_ids);
			else {
				if(move.to_place == Player::PlaceHand)
					to->addHandIds(actual_card_ids);
				foreach(int card_id, move.card_ids){
					if (move.to_place == Player::DrawPile) pile_num++;
					else if (move.to_place == Player::DiscardPile) discarded_list.prepend(card_id);
					else if (move.to) to->addCard(card_id, move.to_place);
					//_getSingleCard(card_id, move); // DDHEJ->DDHEJ, DDH/EJ->EJ
				}
			}
			moves.append(move);
			foreach(int card_id, actual_card_ids){
				owner_map.insert(card_id, to);
				place_map.insert(card_id, move.to_place);
			}
		}
	}
	updatePileNum();
	if(recorder_eventsave){
		save(QSanRuntimePaths::recordDir()+"/debug.txt");
	}
	emit move_cards_got(wire.value(QStringLiteral("move_id")).toInt(), moves);
}

bool Client::_loseSingleCard(int card_id, CardsMoveStruct move)
{
	if (move.from_place == Player::DiscardPile)
		discarded_list.removeOne(card_id);
	else if (move.from_place == Player::DrawPile){
		if(!Self->hasFlag("marshalling"))
			pile_num--;
	}else if (move.from)
		move.from->removeCard(card_id, move.from_place);
	return true;
}

void Client::loseCards(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	const QVariantList args = wire.value(QStringLiteral("moves")).toList();
	QList<CardsMoveStruct> moves;
	for (int i = 0; i < args.length(); i++) {
		CardsMoveStruct move;
		QList<int> actual_card_ids;
		// V2 wire 的每個 move 係具名欄位 map；card_ids 係未遮罩嘅原始 id 列表
		JsonUtils::tryParse(args[i].toMap().value(QStringLiteral("card_ids")), actual_card_ids);
		if (move.tryParse(args[i])){
			ClientPlayer *from = getPlayer(move.from_player_name);
			ClientPlayer *to = getPlayer(move.to_player_name);
			move.from = from;
			move.to = to;
			if (move.from_place == Player::PlaceHand && from == Self)
				move.card_ids = actual_card_ids;
			if (move.from_place == Player::PlaceSpecial)
				from->changePile(move.from_pile_name, false, move.card_ids);
			else {
				bool SWAP = move.reason.m_reason==CardMoveReason::S_REASON_SWAP
					&& move.from_place==Player::PlaceHand
					&& move.card_ids.length()==from->getHandcardNum();
				if(SWAP){
					from->setFlags("S_REASON_SWAP");
					if(i==0){
						QList<const Card*>fcards = from->getKnownCards(),tcards = to->getKnownCards();
						from->setKnownCards(tcards);
						to->setKnownCards(fcards);
					}
				}
				foreach (int card_id, move.card_ids){
					if (move.from_place == Player::DiscardPile)
						discarded_list.removeAll(card_id);
					else if (move.from_place == Player::DrawPile){
						if(!Self->hasFlag("marshalling"))
							pile_num--;
					}else if (move.from)
						from->removeCard(card_id, move.from_place);
					//_loseSingleCard(card_id, move); // DDHEJ->DDHEJ, DDH/EJ->EJ
				}
				if(move.from_place == Player::PlaceHand)
					from->removeHandIds(actual_card_ids);
				if(SWAP) from->setFlags("-S_REASON_SWAP");
			}
			moves.append(move);
		}
	}
	updatePileNum();
	emit move_cards_lost(wire.value(QStringLiteral("move_id")).toInt(), moves);
}

const Player *Client::getCardOwner(int card_id) const
{
	return owner_map.value(card_id);
}

Player::Place Client::getCardPlace(int card_id) const
{
	if(card_id<0) return Player::PlaceUnknown;
	return place_map.value(card_id,Player::PlaceTable);
}

void Client::onPlayerChooseGeneral(const QString &item_name)
{
	setStatus(NotActive);
	if (item_name.isEmpty()) {
		// 舊行為:空名唔會送任何 reply。喺 core 度就係本機放棄呢個 request。
		cancelInteraction(InteractionType::ChooseGeneral, InteractionCancelReason::Abandoned);
		return;
	}
	if (!submitInteractionResponse(InteractionResponse::makeOption(0, item_name)))
		return;
	if(available_cards.isEmpty())
		Sanguosha->playSystemAudioEffect("choose-item");
}

void Client::requestCheatRunScript(const QString &script)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("run_script");
	payload.scriptData = script;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatRevive(const QString &name)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("revive");
	payload.playerName = name;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatDamage(const QString &source, const QString &target, DamageStruct::Nature nature, int points)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("damage");
	payload.sourcePlayer = source;
	payload.targetPlayer = target;
	payload.nature = static_cast<int>(nature);
	payload.points = points;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatchangestate(const QString &target, int type, int points)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("state_editor");
	payload.targetPlayer = target;
	payload.stateType = type;
	payload.points = points;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatKill(const QString &killer, const QString &victim)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("kill");
	payload.sourcePlayer = killer;
	payload.targetPlayer = victim;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatGetOneCard(int card_id)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("get_card");
	payload.cardId = card_id;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::requestCheatChangeGeneral(const QString &name, bool isSecondaryHero)
{
	CheatRequestPayload payload;
	payload.action = QStringLiteral("change_general");
	payload.generalName = name;
	payload.secondaryGeneral = isSecondaryHero;
	requestServer(S_COMMAND_CHEAT, payload.toVariant());
}

void Client::addRobot(int num)
{
	AddRobotPayload payload;
	payload.fillRemaining = num == -1;
	payload.count = payload.fillRemaining ? 0 : qMax(0, num);
	notifyServer(S_COMMAND_ADD_ROBOT, payload.toVariant());
}

void Client::onPlayerResponseCard(const Card *card, const QList<const Player *> &targets)
{
	if (m_interactionCore == nullptr || !m_interactionCore->hasActiveRequest())
		return;
	const InteractionType activeType = m_interactionCore->activeRequest().type;
	if (activeType != InteractionType::PlayCard
		&& activeType != InteractionType::ResponseCard
		&& activeType != InteractionType::AskPeach
		&& activeType != InteractionType::Nullification
		&& activeType != InteractionType::ShowCard
		&& activeType != InteractionType::Pindian)
		return;
	if ((status & ClientStatusBasicMask) == Responding)
		_m_roomState.setCurrentCardUsePattern("");

	// 一次回應永遠係「一張牌」:實牌就係佢自己嘅 id,virtual card 冇實 id,
	// 靠 toString() 上線,子卡放喺 payload 度畀其他 front-end 睇。
	InteractionResponse response;
	if (card) {
		QList<int> cardIds;
		if (!card->isVirtualCard())
			cardIds << card->getEffectiveId();
		response = InteractionResponse::makeCards(0, cardIds, card->toString());
		InteractionResponse::CardSelectionData *answer
			= std::get_if<InteractionResponse::CardSelectionData>(&response.payload);
		foreach (int subcardId, card->getSubcards())
			answer->subcardIds << subcardId;

		foreach (const Player *target, targets)
			answer->targets << target->objectName();
		answer->activationSkillInstanceId = card->getActivationSkillInstanceId();
		if (answer->activationSkillInstanceId > 0)
			answer->activationSkillName = card->getActivationSkillName();
	} else {
		response = InteractionResponse::makeCancel(0);
	}

	if (!submitInteractionResponse(response))
		return;

	if (card) {
		if (card->isVirtualCard() && !card->parent())
			const_cast<Card *>(card)->deleteLater();
	}

	setStatus(NotActive);
}

void Client::startInXs(const QVariant &left_seconds)
{
	int seconds = left_seconds.toMap().value(QStringLiteral("seconds")).toInt();
	if (seconds > 0)
		lines_doc->setHtml(tr("<p align = \"center\">Game will start in <b>%1</b> seconds...</p>").arg(seconds));
	else
		lines_doc->setHtml("");

	emit start_in_xs();
	if (seconds == 0 && Sanguosha->getScenario(ServerInfo.GameMode) == nullptr) {
		emit avatars_hiden();
	}
}

void Client::arrangeSeats(const QVariant &seats_arr)
{
	QStringList player_names;
	if (!JsonUtils::tryParse(
			seats_arr.toMap().value(QStringLiteral("player_names")), player_names))
		return;
	m_players.clear();

	for (int i = 0; i < player_names.length(); i++) {
		ClientPlayer *player = findChild<ClientPlayer *>(player_names.at(i));

		//Q_ASSERT(player != nullptr);
		player->setSeat(i + 1);
		m_players << player;
	}

	QList<const ClientPlayer *> seats;
	int self_index = m_players.indexOf(Self);

	//Q_ASSERT(self_index != -1);
	for (int i = self_index + 1; i < m_players.length(); i++)
		seats.append(m_players.at(i));
	for (int i = 0; i < self_index; i++)
		seats.append(m_players.at(i));

	//Q_ASSERT(seats.length() == m_players.length() - 1);
	emit seats_arranged(seats);
}

void Client::notifyRoleChange(const QString &new_role)
{
	if (isNormalGameMode(ServerInfo.GameMode) && !new_role.isEmpty()) {
		QString prompt_str = tr("Your role is %1").arg(Sanguosha->translate(new_role));
		if (new_role != "lord") prompt_str += tr("\n wait for the lord player choosing general, please");
		lines_doc->setHtml(QString("<p align = \"center\">%1</p>").arg(prompt_str));
	}
}

void Client::activate(const QVariant &)
{
	_m_roomState.setCurrentCardUsePattern("");
	CardInteractionPayload payload;
	payload.selection.minSelection = 0;
	payload.selection.maxSelection = 1;
	payload.cardTextAllowed = true;
	payload.virtualCardAllowed = true;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::PlayCard, payload, true);
	beginInteraction(request);
}

void Client::startGame(const QVariant &pile)
{
	Sanguosha->registerRoom(this);
	_m_roomState.reset();

	setAvailableCards(pile.toMap());
	//alive_count = findChildren<ClientPlayer *>().count();

	emit game_started();
}

void Client::hpChange(const QVariant &change_str)
{
	const QVariantMap change = change_str.toMap();
	emit hp_changed(change.value(QStringLiteral("player_name")).toString(),
		change.value(QStringLiteral("delta")).toInt(),
		change.value(QStringLiteral("nature")).toInt(),
		change.value(QStringLiteral("lost_hp")).toInt());
}

void Client::maxhpChange(const QVariant &change_str)
{
	const QVariantMap change = change_str.toMap();
	QString who = change.value(QStringLiteral("player_name")).toString();
	int delta = change.value(QStringLiteral("delta")).toInt();
	emit maxhp_changed(who, delta);
}

void Client::setStatus(Status status)
{
	Status old_status = this->status;
	this->status = status;
	if (status == Client::Playing)
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
	else if (status == Responding)
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE);
	else if (status == RespondingUse)
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	else
		_m_roomState.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_UNKNOWN);
	if(recorder_eventsave){
		save(QSanRuntimePaths::recordDir()+"/debug.txt");
	}
	emit status_changed(old_status, status);
}

Client::Status Client::getStatus() const
{
	return status;
}

void Client::cardLimitation(const QVariant &limit)
{
	const QVariantMap object = limit.toMap();
	const QString action = object.value(QStringLiteral("action")).toString();
	if (action == QLatin1String("remove_by_reason")) {
		Self->removeCardLimitationByReason(
			object.value(QStringLiteral("reason")).toString());
		return;
	}
	if (action == QLatin1String("clear")) {
		Self->clearCardLimitation(object.value(QStringLiteral("single_turn")).toBool());
		return;
	}
	QStringList methods;
	if (!JsonUtils::tryParse(object.value(QStringLiteral("methods")), methods))
		return;
	const QString methodList = methods.join(QLatin1Char(','));
	const QString pattern = object.value(QStringLiteral("pattern")).toString();
	const QString reason = object.value(QStringLiteral("reason")).toString();
	if (action == QLatin1String("set")) {
		Self->setCardLimitation(methodList, pattern, reason,
			object.value(QStringLiteral("single_turn")).toBool());
	} else if (action == QLatin1String("remove")) {
		Self->removeCardLimitation(methodList, pattern, reason);
	}
}

void Client::setNullification(const QVariant &str)
{
	QString astr = str.toMap().value(QStringLiteral("trick_name")).toString();
	if (astr != ".") {
		if (m_noNullificationTrickName == ".") {
			m_noNullificationPlayers.clear();
			m_noNullificationTrickName = astr;
			emit nullification_asked(true);
		}
		m_noNullificationThisTime = Self != nullptr && m_noNullificationPlayers.contains(Self->objectName());
	} else {
		m_noNullificationPlayers.clear();
		m_noNullificationThisTime = false;
		m_noNullificationTrickName = ".";
		emit nullification_asked(false);
	}
}

void Client::enableSurrender(const QVariant &enabled)
{
	bool en = enabled.toMap().value(QStringLiteral("enabled")).toBool();
	emit surrender_enabled(en);
}

void Client::exchangeKnownCards(const QVariant &players)
{
	const QVariantMap wire = players.toMap();
	ClientPlayer *a = getPlayer(wire.value(QStringLiteral("first_player")).toString());
	ClientPlayer *b = getPlayer(wire.value(QStringLiteral("second_player")).toString());
	if (a == nullptr || b == nullptr) return;
	QList<const Card *> a_known = a->getKnownCards(), b_known = b->getKnownCards();
	a->setKnownCards(b_known);
	b->setKnownCards(a_known);
}

void Client::setKnownCards(const QVariant &set_str)
{
	const QVariantMap set = set_str.toMap();
	ClientPlayer *player = getPlayer(set.value(QStringLiteral("player_name")).toString());
	if (player == nullptr) return;
	QList<int> ids;
	JsonUtils::tryParse(set.value(QStringLiteral("card_ids")), ids);
	player->setKnownCards(ids);
}

void Client::viewGenerals(const QVariant &arg)
{
	const QVariantMap args = arg.toMap();
	QStringList names;
	if (!JsonUtils::tryParse(args.value(QStringLiteral("general_names")), names)) return;
	QString reason = args.value(QStringLiteral("reason")).toString();
	emit generals_viewed(reason, names);
}

Replayer *Client::getReplayer() const
{
	return replayer;
}

QString Client::getPlayerName(const QString &str)
{
	static const QRegularExpression rx(
		QStringLiteral("^sgs\\d+$"),
		QRegularExpression::UseUnicodePropertiesOption);
	if (rx.match(str).hasMatch()) {
		const ClientPlayer *player = getPlayer(str);
		if (player) return player->getLogName();
	}
	return Sanguosha->translate(str);
}

QString Client::getSkillNameToInvoke() const
{
	return skill_to_invoke;
}

QString Client::getSkillNameToInvokeData() const
{
	return skill_to_invoke_data;
}

void Client::onPlayerInvokeSkill(bool invoke)
{
	if (!submitInteractionResponse(InteractionResponse::makeOption(0,
			invoke ? QStringLiteral("yes") : QStringLiteral("no"))))
		return;
	setStatus(NotActive);
}

QString Client::formatPromptList(const QStringList &texts)
{
	return formatClientPromptList(texts,
		[](const QString &key) { return Sanguosha->translate(key); },
		[this](const QString &name) { return getPlayerName(name); });
}

QString Client::setPromptList(const QStringList &texts)
{
	QString prompt = formatPromptList(texts);
	prompt_doc->setHtml(prompt);
	return prompt;
}

void Client::commandFormatWarning(const QString &str, const QRegularExpression &rx, const char *command)
{
	QString text = tr("The argument (%1) of command %2 does not conform the format %3")
		.arg(str).arg(command).arg(rx.pattern());
	QMessageBox::warning(nullptr, tr("Command format warning"), text);
}

QString Client::_processCardPattern(const QString &pattern)
{
	const QChar c = pattern.at(pattern.length() - 1);
	if (c == '!' || c.isNumber())
		return pattern.left(pattern.length() - 1);

	return pattern;
}

void Client::askForCardOrUseCard(const QVariant &cardUsage)
{
	const QVariantMap wire = cardUsage.toMap();
	QString card_pattern = wire.value(QStringLiteral("pattern")).toString();
	_m_roomState.setCurrentCardUsePattern(card_pattern);
	QString textsString = wire.value(QStringLiteral("prompt")).toString();
	QStringList texts = textsString.split(":");
	int index = -1;
	if (wire.contains(QStringLiteral("notice_index"))
		&& wire.value(QStringLiteral("notice_index")).toInt() > 0)
		index = wire.value(QStringLiteral("notice_index")).toInt();

	if (texts.isEmpty())
		return;
	else
		setPromptList(texts);

	m_isDiscardActionRefusable = !card_pattern.endsWith("!");

	QString text = _processCardPattern(card_pattern);
	static const QRegularExpression rx(
		QStringLiteral("^@@?(\\w+)(-card)?$"),
		QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpressionMatch match = rx.match(text);
	if (match.hasMatch()) {
		const Skill *skill = Sanguosha->getSkill(match.captured(1));
		if (skill) {
			text = prompt_doc->toHtml();
			textsString = skill->getNotice(index);
			if (!textsString.startsWith("~"))
				text.append(tr("<br/> <b>Notice</b>: %1<br/>").arg(textsString));
			prompt_doc->setHtml(text);
		}
	}

	int handlingMethod = -1;
	m_respondingUseFixedTarget = nullptr;
	if (wire.contains(QStringLiteral("handling_method")))
		handlingMethod = wire.value(QStringLiteral("handling_method")).toInt();

	CardInteractionPayload cardPayload;
	// 合法牌嘅集合係 pattern 配對嘅結果,而 pattern 配對係 engine 規則:
	// server 冇喺 request 入面列出可選牌,ClientCore 亦唔應該扮規則引擎自己
	// 猜一份出嚟(猜錯就會攔住一個合法回覆)。所以呢類 request 唔枚舉,
	// core 只執行數量、取消權、卡 id 值域同 exactly-once。
	cardPayload.selection.enumerated = false;
	cardPayload.selection.pattern = card_pattern;
	cardPayload.selection.handlingMethod = handlingMethod;
	cardPayload.selection.minSelection = 1;
	cardPayload.selection.maxSelection = 1;
	cardPayload.cardTextAllowed = true;
	cardPayload.virtualCardAllowed = true;
	// pattern 尾巴嘅 "!" 就係「唔准唔覆」,同 m_isDiscardActionRefusable 同一件事。
	InteractionRequest request = makeInteractionRequest(InteractionType::ResponseCard,
		cardPayload, m_isDiscardActionRefusable);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForSkillInvoke(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	QString skill_name = wire.value(QStringLiteral("skill_name")).toString();
	QString data = wire.value(QStringLiteral("data")).toString();

	skill_to_invoke = skill_name;
	skill_to_invoke_data = data;

	QString text;
	if (data.isEmpty()) {
		text = tr("Do you want to invoke skill [%1] ?").arg(Sanguosha->translate(skill_name));
	} else if (data.startsWith("playerdata:")) {
		QString name = getPlayerName(data.split(":").last());
		text = tr("Do you want to invoke skill [%1] to %2 ?").arg(Sanguosha->translate(skill_name)).arg(name);
	} else if (skill_name.startsWith("cv_")) {
		text = formatPromptList(QStringList() << "@sp_convert" << "" << "" << data);
	} else {
		QStringList texts = data.split(":");
		text = QString("%1:%2").arg(skill_name).arg(texts.first());
		texts.replace(0, text);
		text = formatPromptList(texts);
	}

	OptionInteractionPayload payload;
	payload.options << InteractionOption(QStringLiteral("yes"))
			<< InteractionOption(QStringLiteral("no"));
	InteractionRequest request = makeInteractionRequest(
		InteractionType::SkillInvoke, payload, true);
	request.skillName = skill_name;
	request.prompt = text;
	// 發動技能係一條 yes／no 題。Dashboard 嘅 Cancel 掣就係 "no",所以佢
	// 永遠答得起,cancelable 亦因此係 true。
	beginInteraction(request);
}

void Client::onPlayerMakeChoice()
{
	QString option = sender()->objectName();
	if (m_interactionCore == nullptr || !m_interactionCore->hasActiveRequest())
		return;
	if (!submitInteractionResponse(InteractionResponse::makeOption(0, option)))
		return;
	setStatus(NotActive);
}

void Client::askForSurrender(const QVariant &initiator)
{
	const QString initiatorGeneral = initiator.toMap()
		.value(QStringLiteral("initiator_general")).toString();

	QString text = tr("%1 initiated a vote for disadvataged side to claim "
		"capitulation. Click \"OK\" to surrender or \"Cancel\" to resist.")
		.arg(Sanguosha->translate(initiatorGeneral));
	text.append(tr("<br/> <b>Notice</b>: if all people on your side decides to surrender. "
		"You'll lose this game."));
	skill_name = "surrender";
	skill_to_invoke = skill_name;
	skill_to_invoke_data.clear();
	OptionInteractionPayload payload;
	payload.options << InteractionOption(QStringLiteral("yes"))
		<< InteractionOption(QStringLiteral("no"));
	InteractionRequest request = makeInteractionRequest(
		InteractionType::Surrender, payload, true);
	request.skillName = skill_name;
	request.prompt = text;
	beginInteraction(request);
}

void Client::askForLuckCard(const QVariant &)
{
	skill_to_invoke = "luck_card";
	skill_to_invoke_data = "";
	skill_name = "luck_card";
	OptionInteractionPayload payload;
	payload.options << InteractionOption(QStringLiteral("yes"))
		<< InteractionOption(QStringLiteral("no"));
	InteractionRequest request = makeInteractionRequest(
		InteractionType::LuckCard, payload, true);
	request.skillName = skill_name;
	request.prompt = tr("Do you want to use the luck card?");
	beginInteraction(request);
}

void Client::askForNullification(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	const ClientPlayer *target_player = getPlayer(
		wire.value(QStringLiteral("target_player")).toString());

	if (!target_player || !target_player->getGeneral()) return;

	QString trick_name = wire.value(QStringLiteral("trick_name")).toString();
	ClientPlayer *source = getPlayer(wire.value(QStringLiteral("source_player")).toString());
	auto beginNullification = [this](const QString &prompt) {
		CardInteractionPayload payload;
		payload.selection.pattern = QStringLiteral("nullification");
		payload.selection.minSelection = 1;
		payload.selection.maxSelection = 1;
		payload.cardTextAllowed = true;
		payload.virtualCardAllowed = true;
		InteractionRequest request = makeInteractionRequest(
			InteractionType::Nullification, payload, true);
		request.prompt = prompt;
		beginInteraction(request);
	};

	if (Config.NeverNullifyMyTrick && source == Self) {
		const Card *trick_card = Sanguosha->findChild<const Card *>(trick_name);
		if (trick_card->isKindOf("SingleTargetTrick") || !trick_card->targetFixed()) {
			beginNullification(QString());
			onPlayerResponseCard(nullptr);
			return;
		}
	}
	QString currentPlayerName = Self != nullptr ? Self->objectName() : QString();
	m_noNullificationThisTime = !currentPlayerName.isEmpty()
		&& m_noNullificationTrickName == trick_name
		&& m_noNullificationPlayers.contains(currentPlayerName);
	if (m_noNullificationThisTime) {
		//if (trick_card->isKindOf("AOE") || trick_card->isKindOf("GlobalEffect")) {
			beginNullification(QString());
			onPlayerResponseCard(nullptr);
			return;
		//}
	}

	if (source) {
		prompt_doc->setHtml(tr("%1 used trick card %2 to %3 <br>Do you want to use nullification?")
			.arg(getPlayerName(source->objectName()))
			.arg(Sanguosha->translate(trick_name))
			.arg(getPlayerName(target_player->objectName())));
	} else {
		prompt_doc->setHtml(tr("Do you want to use nullification to trick card %1 from %2?")
			.arg(Sanguosha->translate(trick_name))
			.arg(getPlayerName(target_player->objectName())));
	}

	_m_roomState.setCurrentCardUsePattern("nullification");
	m_isDiscardActionRefusable = true;
	m_respondingUseFixedTarget = nullptr;
	beginNullification(prompt_doc->toHtml());
}

void Client::onPlayerChooseCard(int card_id)
{
	InteractionResponse response = card_id == -2
		? InteractionResponse::makeCancel(0)
		: InteractionResponse::makeCards(0, QList<int>() << card_id);
	if (!submitInteractionResponse(response))
		return;
	setStatus(NotActive);
}

void Client::onPlayerChoosePlayer(const QList<const Player *> &players)
{
	if (replayer) return;
	QStringList names;
	foreach (const Player *p, players)
		names << p->objectName();
	if (players.length() < choose_min_num && !m_isDiscardActionRefusable) {
		// UI 交上嚟嘅目標唔夠數(逾時／trust 嘅安全預設答案),就隨機補夠。
		// 補嘅時候優先揀 server 講明可揀嗰批:舊碼由 findChildren<Player *>()
		// 度隨機抽,抽得中一個唔喺可揀清單入面嘅玩家,server 一樣會當佢無效,
		// 所以先行合法池係同一個結果嘅較準版本,唔會改變可見行為。
		QList<const Player*> to_choose;
		QList<const Player*> fallback;
		foreach (const Player *p, findChildren<const Player *>()) {
			if (players.contains(p))
				continue;
			if (players_to_choose.contains(p->objectName()))
				to_choose.append(p);
			else
				fallback.append(p);
		}
		while (names.length() < choose_min_num) {
			QList<const Player*> &pool = to_choose.isEmpty() ? fallback : to_choose;
			if (pool.isEmpty()) break;
			names << pool.takeAt(UiRng::bounded(pool.length()))->objectName();
		}
	}

	if (!submitInteractionResponse(InteractionResponse::makePlayers(0, names)))
		return;
	setStatus(NotActive);
}

void Client::trust()
{
	ClientPlayer *trustPlayer = getControlRootPlayer(Self);
	if (trustPlayer != nullptr && trustPlayer != Self)
		setSelf(trustPlayer);
	TrustPayload payload;
	payload.trusted = trustPlayer == nullptr
		|| trustPlayer->getState() != QLatin1String("trust");
	notifyServer(S_COMMAND_TRUST, payload.toVariant());

	if (trustPlayer != nullptr && trustPlayer->getState() == "trust")
		Sanguosha->playSystemAudioEffect("untrust");
	else
		Sanguosha->playSystemAudioEffect("trust");

	setStatus(NotActive);
}

void Client::requestSurrender()
{
	requestServer(S_COMMAND_SURRENDER, SurrenderRequestPayload().toVariant());
	setStatus(NotActive);
}

void Client::speakToServer(const QString &text)
{
	if (text.isEmpty())
		return;

	ChatPayload payload;
	payload.text = text;
	notifyServer(S_COMMAND_SPEAK, payload.toVariant());
}

void Client::addHistory(const QVariant &history)
{
	const QVariantMap wire = history.toMap();
	ClientPlayer *target = wire.contains(QStringLiteral("player_name"))
		? getPlayer(wire.value(QStringLiteral("player_name")).toString()) : Self;
	if (target == nullptr)
		return;

	QString add_str = wire.value(QStringLiteral("history_name")).toString();
	if (add_str == "pushPile")
		emit card_used();
	else if (add_str == ".")
		target->clearHistory();
	else{
		int times = wire.value(QStringLiteral("times")).toInt();
		if (times == 0)
			target->clearHistory(add_str);
		else
			target->addHistory(add_str, times);
	}
}

void Client::playAudio(const QVariant &history)
{
	const QVariantMap wire = history.toMap();
	Sanguosha->playAudioEffect(wire.value(QStringLiteral("path")).toString(),
		wire.value(QStringLiteral("loop")).toBool());
}

void Client::updateCardDescription(const QVariant &arg)
{
    const QVariantMap req = arg.toMap();
    QString player_name = req.value(QStringLiteral("player_name")).toString();
    QString card_name = req.value(QStringLiteral("card_name")).toString();
    QString key = req.value(QStringLiteral("key")).toString();
    QString value = req.value(QStringLiteral("value")).toString();

    ClientPlayer *player = getPlayer(player_name);
    if (!player) return;

    player->setCardDescriptionSwap(card_name, key, value);

    emit card_description_updated(player_name, card_name);
}

int Client::alivePlayerCount() const
{
	int num = 0;
	foreach (const ClientPlayer*player, m_players) {
		if(player->isAlive()) num++;
	}
	return num;
}

ClientPlayer *Client::getPlayer(const QString &name)
{
	if (name == Self->objectName() || name == QSanProtocol::S_PLAYER_SELF_REFERENCE_ID)
		return Self;
	return findChild<ClientPlayer *>(name);
}

bool Client::save(const QString &filename) const
{
	if (recorder)
		return recorder->save(filename);
	return false;
}

QList<QByteArray> Client::getRecords() const
{
	if (recorder)
		return recorder->getRecords();
	return QList<QByteArray>();
}

QString Client::getReplayPath() const
{
	if (replayer)
		return replayer->getPath();
	return "";
}

QTextDocument *Client::getLinesDoc() const
{
	return lines_doc;
}

QTextDocument *Client::getPromptDoc() const
{
	return prompt_doc;
}

void Client::resetPiles(const QVariant &arg)
{
	discarded_list.clear();
	swap_pile = arg.toMap().value(QStringLiteral("swap_count")).toInt();
	updatePileNum();
	emit pile_reset();
}

void Client::setPileNumber(const QVariant &pile_str)
{
	pile_num = pile_str.toMap().value(QStringLiteral("count")).toInt();
	updatePileNum();
}

void Client::setTimeout(const QVariant &time)
{
	const int timeoutMs = time.toMap().value(QStringLiteral("timeout_ms")).toInt();
	Config.OperationTimeout = timeoutMs;
	ServerInfo.OperationTimeout = timeoutMs;
}

void Client::updateWeaponRange(const QVariant &arg)
{
	const QVariantMap req = arg.toMap();
	const QString weaponName = req.value(QStringLiteral("weapon_name")).toString();
	const int range = req.value(QStringLiteral("range")).toInt();
	Weapon*w = Sanguosha->findChild<Weapon*>(weaponName);
	if(w) w->setRange(range);
	QString translated = Sanguosha->translate(":"+weaponName+"1");
	translated.replace("%src", QString::number(range));
	Sanguosha->addTranslationEntry(":"+weaponName,translated);
}

void Client::synchronizeDiscardPile(const QVariant &discard_pile)
{
	if (JsonUtils::tryParse(discard_pile.toMap().value(QStringLiteral("card_ids")), discarded_list))
		updatePileNum();
}

void Client::syncPile(const QVariant &pile_info)
{
	SyncPileMessage message;
	if (!message.tryParse(pile_info))
		return;

	ClientPlayer *player = getPlayer(message.playerName);
	if (player)
		player->syncPileCards(message.pileName, message.cardIds);
}

void Client::setCardMark(const QVariant &pattern_str)
{
	const QVariantMap pattern = pattern_str.toMap();
	Card *card = Sanguosha->getCard(pattern.value(QStringLiteral("card_id")).toInt());
	if (card != nullptr) card->setMark(
		pattern.value(QStringLiteral("mark_name")).toString(),
		pattern.value(QStringLiteral("value")).toInt());
}

void Client::setCardFlag(const QVariant &pattern_str)
{
	const QVariantMap pattern = pattern_str.toMap();
	int id = pattern.value(QStringLiteral("card_id")).toInt();
	Card *card = Sanguosha->getCard(id);
	if (card != nullptr){
		QString flag = pattern.value(QStringLiteral("flag")).toString();
		card->setFlags(flag);
		if(flag.contains("-cardTip:")){
			emit card_tip();
		}else if(flag.endsWith("visible")){
			if(flag.startsWith("-")){
				QList<const Card *> cards;
				foreach (const Card *kc, Self->getKnownCards()) {
					if(kc->getId()!=id) cards << kc;
				}
				Self->setKnownCards(cards);
			}else
				Self->addKnownHandCard(card);
		}
	}
}

void Client::updatePileNum()
{
	QString pile_str = tr("Draw pile: <b>%1</b>, discard pile: <b>%2</b>, swap times: <b>%3</b>, round times: <b>%4</b>")
		.arg(pile_num).arg(discarded_list.length()).arg(swap_pile).arg(add_round);
	if (ServerInfo.GameMode == "04_boss")
		pile_str.prepend(tr("Level: <b>%1</b>,").arg(m_bossLevel + 1));

	lines_doc->setHtml(QString("<font color='%1'><p align = \"center\">%2</p></font>").arg(UiConfig.TextEditColor.name()).arg(pile_str));
}

void Client::askForDiscard(const QVariant &reqvar)
{
	const QVariantMap wire = reqvar.toMap();
	discard_num = wire.value(QStringLiteral("max_cards")).toInt();
	min_num = wire.value(QStringLiteral("min_cards")).toInt();
	m_isDiscardActionRefusable = wire.value(QStringLiteral("optional")).toBool();
	m_canDiscardEquip = wire.value(QStringLiteral("include_equip")).toBool();
	QString prompt = wire.value(QStringLiteral("prompt")).toString();
	QString pattern = wire.value(QStringLiteral("pattern")).toString();
	if (pattern.isEmpty()) pattern = ".";
	m_cardDiscardPattern = pattern;

	if (prompt.isEmpty()) {
		if (m_canDiscardEquip)
			prompt = tr("Please discard %1 card(s), include equip").arg(discard_num);
		else
			prompt = tr("Please discard %1 card(s), only hand cards is allowed").arg(discard_num);
		if (min_num < discard_num) {
			prompt.append("<br/>");
			prompt.append(tr("%1 %2 cards(s) are required at least").arg(min_num).arg(m_canDiscardEquip ? "" : tr("hand")));
		}
		prompt_doc->setHtml(prompt);
	} else {
		QStringList texts = prompt.split(":");
		if (texts.length() < 4) {
			while (texts.length() < 3)
				texts.append("");
			texts.append(QString::number(discard_num));
		}
		setPromptList(texts);
	}

	CardInteractionPayload payload;
	payload.selection.pattern = m_cardDiscardPattern;
	payload.selection.minSelection = min_num;
	payload.selection.maxSelection = discard_num;
	payload.selection.handlingMethod = Card::MethodDiscard;
	payload.includeEquip = m_canDiscardEquip;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::DiscardCard, payload, m_isDiscardActionRefusable);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForExchange(const QVariant &exchange)
{
	const QVariantMap wire = exchange.toMap();
	discard_num = wire.value(QStringLiteral("max_cards")).toInt();
	min_num = wire.value(QStringLiteral("min_cards")).toInt();
	m_canDiscardEquip = wire.value(QStringLiteral("include_equip")).toBool();
	QString prompt = wire.value(QStringLiteral("prompt")).toString();
	m_isDiscardActionRefusable = wire.value(QStringLiteral("optional")).toBool();

	QString pattern = wire.value(QStringLiteral("pattern")).toString();

	if (pattern.isEmpty()) pattern = ".";
	m_cardDiscardPattern = pattern;

	if (prompt.isEmpty()) {
		prompt = tr("Please give %1 cards to exchange").arg(discard_num);
		prompt_doc->setHtml(prompt);
	} else {
		QStringList texts = prompt.split(":");
		if (texts.length() < 4) {
			while (texts.length() < 3)
				texts.append("");
			texts.append(QString::number(discard_num));
		}
		setPromptList(texts);
	}
	CardInteractionPayload payload;
	payload.selection.pattern = m_cardDiscardPattern;
	payload.selection.minSelection = min_num;
	payload.selection.maxSelection = discard_num;
	payload.selection.handlingMethod = Card::MethodDiscard;
	payload.includeEquip = m_canDiscardEquip;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ExchangeCard, payload, m_isDiscardActionRefusable);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::gameOver(const QVariant &arg)
{
	disconnectFromHost();
	m_isGameOver = true;
	setStatus(Client::NotActive);

	const QVariantMap args = arg.toMap();
	QStringList roles, winners;
	if (!JsonUtils::tryParse(args.value(QStringLiteral("roles")), roles))
		return;
	if (!JsonUtils::tryParse(args.value(QStringLiteral("winner_tokens")), winners))
		return;

	//Q_ASSERT(roles.length() == m_players.length());
	for (int i = 0; i < roles.length(); i++){
		ClientPlayer *p = (ClientPlayer*)m_players.at(i);
		p->setProperty("win",winners.contains(p->objectName())||winners.contains(roles.at(i)));
		p->setRole(roles.at(i));
	}

	Sanguosha->unregisterRoom();
	if (args.value(QStringLiteral("standoff")).toBool()) {
		emit standoff();
	}else
		emit game_over();
}

void Client::killPlayer(const QVariant &player_name)
{
	const QString playerName = player_name.toMap()
		.value(QStringLiteral("player_name")).toString();
	ClientPlayer *player = getPlayer(playerName);
	if (!player) return;
	bool restoreOriginal = player == Self && m_original_self != nullptr && player != m_original_self;

	if (player == Self) {
		foreach (const Skill *skill, Self->getVisibleSkills())
			emit skill_detached(Self, skill->objectName());
	}
	player->detachAllSkills();
	if (restoreOriginal)
		setSelf(m_original_self);

	if (!Self->hasFlag("marshalling"))
		updatePileNum();

	emit player_killed(playerName);
}

void Client::revivePlayer(const QVariant &player_arg)
{
	const QString playerName = player_arg.toMap()
		.value(QStringLiteral("player_name")).toString();
	updatePileNum();
	emit player_revived(playerName);
}


void Client::warn(const QVariant &reason_var)
{
	DiagnosticPayload diagnostic;
	if (!DiagnosticPayload::parse(reason_var, &diagnostic))
		return;
	QString reason = diagnostic.code;
	QString msg;
	if (reason == "GAME_OVER")
		msg = tr("Game is over now");
	else if (reason == "INVALID_FORMAT")
		msg = tr("Invalid signup string");
	else if (reason == "LEVEL_LIMITATION")
		msg = tr("Your level is not enough");
	else
		msg = tr("Unknown warning: %1").arg(reason);

	disconnectFromHost();
	QMessageBox::warning(nullptr, tr("Warning"), msg);
}

void Client::askForGeneral(const QVariant &arg)
{
	QStringList generals;
	if (!JsonUtils::tryParse(arg.toMap().value(QStringLiteral("candidates")), generals)) return;

	OptionInteractionPayload payload;
	// 清單只係建議,唔係合法答案嘅完整集合:server 喺 FreeChoose 之下收清單
	// 以外嘅武將(player-decision-service.cpp:332),而 free-choose dialog 同
	// --test-general 自動選將(roomscene.cpp:2255)正正會咁答。ClientCore 唔可以
	// 攔一啲 server 本身收得起嘅答案。
	foreach (const QString &general, generals)
		payload.options << InteractionOption(general);
	payload.enumerated = false;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseGeneral, payload, false);
	beginInteraction(request);
}

void Client::askForSuit(const QVariant &)
{
	QStringList suits;
	suits << "spade" << "club" << "heart" << "diamond";
	OptionInteractionPayload payload;
	for (const QString &suit : suits)
		payload.options << InteractionOption(suit);
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseSuit, payload, false);
	beginInteraction(request);
}

void Client::askForKingdom(const QVariant &arg)
{
	QStringList kingdoms;
	if (!JsonUtils::tryParse(arg.toMap().value(QStringLiteral("kingdoms")), kingdoms)) return;
	OptionInteractionPayload payload;
	for (const QString &kingdom : kingdoms)
		payload.options << InteractionOption(kingdom);
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseKingdom, payload, false);
	beginInteraction(request);
}

void Client::askForChoice(const QVariant &ask_str)
{
	const QVariantMap wire = ask_str.toMap();
	QString skill_name = wire.value(QStringLiteral("skill_name")).toString();
	QStringList options;
	QStringList except_options;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("options")), options)
		|| !JsonUtils::tryParse(wire.value(QStringLiteral("disabled_options")), except_options))
		return;
	QString tip = wire.value(QStringLiteral("tip")).toString();

	OptionInteractionPayload payload;
	payload.tip = tip;
	auto hasOption = [&payload](const QString &value) {
		for (const InteractionOption &option : payload.options) {
			if (option.value == value)
				return true;
		}
		return false;
	};
	// 連空字串都照收:server 送嘅 option 串有可能有多餘嘅 "+",split 出嚟嗰個
	// 空項喺 dialog 度一樣會變成一個撳得嘅掣(objectName 就係空字串),而 server
	// 收得起。core 唔可以攔一個 desktop 產生得到嘅答案。
	foreach (const QString &option, options) {
		if (!hasOption(option))
			payload.options << InteractionOption(option);
	}
	// except_options 喺 dialog 度係「睇得到、撳唔到」嘅掣
	// (roomscene.cpp:2551 createOptionBox(..., false)),所以入 model 係
	// disabled option,而唔係唔存在。
	foreach (const QString &option, except_options) {
		if (option.isEmpty())
			continue;
		bool found = false;
		for (InteractionOption &entry : payload.options) {
			if (entry.value == option) {
				entry.enabled = false;
				found = true;
				break;
			}
		}
		if (!found)
			payload.options << InteractionOption(option, QString(), false);
	}
	// 揀項 dialog 自己嘅 objectName 就係 "cancel"(roomscene.cpp:2549):撳 Esc
	// 關窗會經同一個 slot 用 "cancel" 覆,BossModeExpStore 亦有一個永遠 enabled
	// 嘅 cancel 掣。server 收得起,所以 core 亦要收得起,否則關窗會變成冇覆。
	if (!hasOption(QStringLiteral("cancel"))) {
		InteractionOption cancel(QStringLiteral("cancel"));
		cancel.metadata.insert(QStringLiteral("synthetic_cancel"), true);
		payload.options << cancel;
	}
	// desktop chooseOption() 要嘅係 server 原本嗰三份資料,原值留喺 context。
	InteractionRequest request = makeInteractionRequest(
		InteractionType::Choice, payload, true);
	request.skillName = skill_name;
	beginInteraction(request);
}

void Client::askForTriggerOrder(const QVariant &ask_str)
{
	const QVariantMap wire = ask_str.toMap();
    QVariantList skillOptions = wire.value(QStringLiteral("options")).toList();
    bool optional = wire.value(QStringLiteral("optional")).toBool();
	TriggerOrderInteractionPayload payload;
    for (const QVariant &option : skillOptions) {
        const QVariantMap detail = option.toMap();
        QString skill = detail.value(QStringLiteral("skill")).toString();
        const int instanceId = detail.value(QStringLiteral("instanceID")).toInt();
        if (instanceId > 0)
            skill += QStringLiteral("#") + QString::number(instanceId);
        const QString invoker = detail.value(QStringLiteral("invoker")).toString();
        const QString owner = detail.value(QStringLiteral("owner"), invoker).toString();
        QStringList parts { skill, owner, invoker };
        const QString target = detail.value(QStringLiteral("preferredtarget")).toString();
        if (!target.isEmpty())
            parts << target << QString::number(
                detail.value(QStringLiteral("preferredtargetseat")).toInt());
        const QString value = parts.join(QStringLiteral(":"));
		TriggerOrderOption triggerOption;
		triggerOption.skillName = detail.value(QStringLiteral("skill")).toString();
		triggerOption.instanceId = instanceId;
		triggerOption.invoker = invoker;
		triggerOption.owner = owner;
		triggerOption.preferredTarget = target;
		triggerOption.preferredTargetSeat
			= detail.value(QStringLiteral("preferredtargetseat")).toInt();
		triggerOption.responseValue = value;
		payload.options << triggerOption;
    }
	InteractionRequest request = makeInteractionRequest(
		InteractionType::TriggerOrder, payload, optional);
    beginInteraction(request);
}

void Client::askForCardChosen(const QVariant &ask_str)
{
	const QVariantMap wire = ask_str.toMap();
	QString player_name = wire.value(QStringLiteral("player")).toString();
	QString flags = wire.value(QStringLiteral("zone_flags")).toString();
	QString reason = wire.value(QStringLiteral("reason")).toString();
	bool handcard_visible = wire.value(QStringLiteral("hand_cards_visible")).toBool();
	Card::HandlingMethod method = static_cast<Card::HandlingMethod>(
		wire.value(QStringLiteral("handling_method")).toInt());
	bool can_cancel = wire.value(QStringLiteral("can_cancel")).toBool();
	ClientPlayer *player = getPlayer(player_name);
	if (player == nullptr) return;
	QList<int> disabled_ids;
	JsonUtils::tryParse(wire.value(QStringLiteral("disabled_card_ids")), disabled_ids);
	CardInteractionPayload payload;
	payload.sourcePlayer = player_name;
	payload.zoneFlags = flags;
	payload.handCardsVisible = handcard_visible;
	payload.selection.disabledCards = disabled_ids;
	payload.selection.minSelection = can_cancel ? 0 : 1;
	payload.selection.maxSelection = 1;
	payload.selection.handlingMethod = static_cast<int>(method);
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseCard, payload, can_cancel);
	request.skillName = reason;
	beginInteraction(request);
}


void Client::askForOrder(const QVariant &arg)
{
	ChooseOrderRequestPayload parsed;
	if (!ChooseOrderRequestPayload::parseV2(arg, &parsed))
		return;
	Game3v3ChooseOrderCommand reason
		= static_cast<Game3v3ChooseOrderCommand>(parsed.reason);
	ChooseOrderInteractionPayload payload;
	payload.options << InteractionOption(QString::number(static_cast<int>(S_CAMP_COOL)))
		<< InteractionOption(QString::number(static_cast<int>(S_CAMP_WARM)));
	payload.reason = static_cast<int>(reason);
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseOrder, payload, false);
	beginInteraction(request);
}

void Client::askForRole3v3(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	QStringList roles;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("roles")), roles)) return;
	QString scheme = wire.value(QStringLiteral("scheme")).toString();
	OptionInteractionPayload payload;
	for (const QString &role : roles)
		payload.options << InteractionOption(role);
	payload.scheme = scheme;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseRole3v3, payload, false);
	beginInteraction(request);
}

void Client::askForDirection(const QVariant &)
{
	OptionInteractionPayload payload;
	payload.options << InteractionOption(QStringLiteral("cw"))
		<< InteractionOption(QStringLiteral("ccw"));
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseDirection, payload, false);
	beginInteraction(request);
}


void Client::setMark(const QVariant &mark_var)
{
	const QVariantMap markObject = mark_var.toMap();
	QString who = markObject.value(QStringLiteral("player_name")).toString();
	QString mark = markObject.value(QStringLiteral("mark_name")).toString();
	int value = markObject.value(QStringLiteral("value")).toInt();

	ClientPlayer *player = getPlayer(who);
	player->setMark(mark, value);

	// for all the skills has a ViewAsSkill Effect { RoomScene::detachSkill(const QString &) }
	// this is a DIRTY HACK!!! for we should prevent the ViewAsSkill button been removed temporily by duanchang
	if (player == Self && value < 1 && mark.startsWith("ViewAsSkill_") && mark.endsWith("Effect")) {
		QString skill_name = mark.mid(12);
		skill_name.chop(6);

		if (!Self->hasSkill(skill_name, true)) {
			emit skill_detached(Self, skill_name);
		}
	}
}

void Client::onPlayerChooseSuit()
{
	if (!submitInteractionResponse(
			InteractionResponse::makeOption(0, sender()->objectName())))
		return;
	setStatus(NotActive);
}

void Client::onPlayerChooseKingdom()
{
	if (!submitInteractionResponse(
			InteractionResponse::makeOption(0, sender()->objectName())))
		return;
	setStatus(NotActive);
}

void Client::onPlayerDiscardCards(const Card *cards)
{
	if (m_interactionCore == nullptr || !m_interactionCore->hasActiveRequest())
		return;
	const InteractionType type = m_interactionCore->activeRequest().type;
	if (type != InteractionType::DiscardCard && type != InteractionType::ExchangeCard)
		return;
	QList<int> selectedCards;
	if (cards != nullptr)
		selectedCards = cards->getSubcards();
	const InteractionResponse response = cards != nullptr
		? InteractionResponse::makeCards(0, selectedCards)
		: InteractionResponse::makeCancel(0);
	if (!submitInteractionResponse(response))
		return;
	if (cards) {
		if (cards->isVirtualCard() && !cards->parent())
			const_cast<Card *>(cards)->deleteLater();
	}

	setStatus(NotActive);
}

void Client::fillAG(const QVariant &cards_str)
{
	const QVariantMap cards = cards_str.toMap();
	QList<int> card_ids, disabled_ids;
	JsonUtils::tryParse(cards.value(QStringLiteral("card_ids")), card_ids);
	JsonUtils::tryParse(cards.value(QStringLiteral("disabled_card_ids")), disabled_ids);
	m_amazingGraceCards = card_ids;
	m_amazingGraceDisabledCards = disabled_ids;
	m_amazingGraceTakenCards.clear();
	emit ag_filled(card_ids, disabled_ids);
}

void Client::takeAG(const QVariant &take_var)
{
	const QVariantMap take = take_var.toMap();
	int card_id = take.value(QStringLiteral("card_id")).toInt();
	if (!m_amazingGraceTakenCards.contains(card_id))
		m_amazingGraceTakenCards << card_id;
	bool move_cards = take.value(QStringLiteral("move_cards")).toBool();

	if (!take.contains(QStringLiteral("taker"))) {
		if (move_cards) {
			discarded_list.prepend(card_id);
			updatePileNum();
		}
		emit ag_taken(nullptr, card_id, move_cards);
	} else {
		ClientPlayer *taker = getPlayer(take.value(QStringLiteral("taker")).toString());
		if (move_cards)
			taker->addCard(card_id, Player::PlaceHand);
		emit ag_taken(taker, card_id, move_cards);
	}
}

void Client::clearAG(const QVariant &)
{
	m_amazingGraceCards.clear();
	m_amazingGraceDisabledCards.clear();
	m_amazingGraceTakenCards.clear();
	emit ag_cleared();
}

void Client::askForSinglePeach(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	ClientPlayer *dying = getPlayer(wire.value(QStringLiteral("dying_player")).toString());
	if (dying == nullptr) return;
	int peaches = wire.value(QStringLiteral("peach_count")).toInt();
	// @todo: anti-cheating of askForSinglePeach is not done yet!!!
	QString pattern = "peach";
	if (dying == Self) {
		pattern += "+analeptic";
		prompt_doc->setHtml(tr("You are dying, please provide %1 peach(es)(or analeptic) to save yourself").arg(peaches));
	} else
		prompt_doc->setHtml(tr("%1 is dying, please provide %2 peach(es) to save him").arg(getPlayerName(dying->objectName())).arg(peaches));
	_m_roomState.setCurrentCardUsePattern(pattern);
	m_respondingUseFixedTarget = dying;
	m_isDiscardActionRefusable = true;
	CardInteractionPayload payload;
	payload.selection.pattern = pattern;
	payload.selection.minSelection = 1;
	payload.selection.maxSelection = 1;
	payload.fixedTargets << dying->objectName();
	InteractionRequest request = makeInteractionRequest(
		InteractionType::AskPeach, payload, true);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForCardShow(const QVariant &requestor)
{
	const QString requestorName = requestor.toMap().value(QStringLiteral("requestor")).toString();
	prompt_doc->setHtml(tr("%1 request you to show one hand card").arg(getPlayerName(requestorName)));

	_m_roomState.setCurrentCardUsePattern(".");
	CardInteractionPayload payload;
	payload.sourcePlayer = requestorName;
	payload.selection.pattern = QStringLiteral(".");
	payload.selection.minSelection = 1;
	payload.selection.maxSelection = 1;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ShowCard, payload, false);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForAG(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	bool refusable = wire.value(QStringLiteral("refusable")).toBool();
	m_isDiscardActionRefusable = refusable;

	QString reason = wire.value(QStringLiteral("reason")).toString();
	QString prompt = wire.value(QStringLiteral("prompt")).toString();
	QString source;

	if (!reason.isEmpty() && prompt.startsWith("@")) {
		const Skill *sk = Sanguosha->getSkill(reason);
		if (sk) {
			if (sk->isVisible())
				source = reason;
			else {
				sk = Sanguosha->getMainSkill(reason);
				if (sk) source = sk->objectName();
			}
		} else
			source = reason;
	}

	QString translate = source.isEmpty() ? "": Sanguosha->translate(source);
	if (source.isEmpty() || translate == source)
		translate = "";

	if (prompt.isEmpty()) {
		prompt = refusable ? tr("you can choose a card") : tr("please choose a card");
		if (!translate.isEmpty()) prompt.append(tr("<br/> <b>Source</b>: %1<br/>").arg(translate));
		prompt_doc->setHtml(prompt);
	} else {
		QStringList texts = prompt.split(":");
		QString text = setPromptList(texts);
		if (!translate.isEmpty()) text.append(tr("<br/> <b>Source</b>: %1<br/>").arg(translate));
		prompt_doc->setHtml(text);
	}
	AmazingGraceInteractionPayload payload;
	payload.selection.enumerated = !m_amazingGraceCards.isEmpty();
	payload.selection.selectableCards = m_amazingGraceCards;
	payload.selection.disabledCards = m_amazingGraceDisabledCards;
	for (int cardId : m_amazingGraceTakenCards) {
		if (!payload.selection.disabledCards.contains(cardId))
			payload.selection.disabledCards << cardId;
	}
	payload.selection.minSelection = refusable ? 0 : 1;
	payload.selection.maxSelection = 1;
	payload.takenCards = m_amazingGraceTakenCards;
	payload.selectable = true;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::AmazingGrace, payload, refusable);
	request.skillName = reason;
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::onPlayerChooseAG(int card_id)
{
	InteractionResponse response = card_id < 0
		? InteractionResponse::makeCancel(0)
		: InteractionResponse::makeCards(0, QList<int>() << card_id);
	if (!submitInteractionResponse(response))
		return;
	setStatus(NotActive);
}

QList<const ClientPlayer *> Client::getPlayers() const
{
    return m_players;
}

ClientPlayer *Client::getNextPlayer(ClientPlayer *player) const
{
    if (m_players.isEmpty() || player == nullptr)
        return nullptr;
    
    int index = m_players.indexOf(player);
    if (index == -1)
        return nullptr;
    
    index = (index + 1) % m_players.length();
    return const_cast<ClientPlayer *>(m_players.at(index));
}

ClientPlayer *Client::getLastPlayer(ClientPlayer *player) const
{
    if (m_players.isEmpty() || player == nullptr)
        return nullptr;
    
    int index = m_players.indexOf(player);
    if (index == -1)
        return nullptr;
    
    index = (index - 1 + m_players.length()) % m_players.length();
    return const_cast<ClientPlayer *>(m_players.at(index));
}

void Client::alertFocus()
{
	if (Self->getPhase() == Player::Play)
		QApplication::alert(QApplication::focusWidget());
}

void Client::showCard(const QVariant &show_str)
{
	const QVariantMap show = show_str.toMap();
	QString player_name = show.value(QStringLiteral("player_name")).toString();
	QList<int> card_ids;
	if (!JsonUtils::tryParse(show.value(QStringLiteral("card_ids")), card_ids))
		return;

	if (player_name != Self->objectName()) {
		ClientPlayer *player = getPlayer(player_name);
		foreach (int card_id, player->handCards()){
			if(card_ids.contains(card_id))
				player->addKnownHandCard(Sanguosha->getCard(card_id));
		}
	}

	emit card_shown(player_name, card_ids);
}

void Client::showVirtualCard(const QVariant &arg)
{
	const QVariantMap args = arg.toMap();
	QString player_name = args.value(QStringLiteral("player_name")).toString();
	QString card_name = args.value(QStringLiteral("card_name")).toString();
	QString suit = args.value(QStringLiteral("suit")).toString();
	int number = args.value(QStringLiteral("number")).toInt();
	QString skill_name = args.value(QStringLiteral("skill_name")).toString();
	QList<int> subcard_ids;
	JsonUtils::tryParse(args.value(QStringLiteral("subcard_ids")), subcard_ids);
	const QString target_name = args.value(QStringLiteral("target_player")).toString();
	emit virtual_card_shown(player_name, card_name, suit, number, skill_name, subcard_ids, target_name);
}

void Client::cardProvenance(const QVariant &arg)
{
	CardProvenanceMessage message;
	if (!message.tryParse(arg))
		return;
}

void Client::attachSkill(const QVariant &skill)
{
	const QVariantMap wire = skill.toMap();
	QString player_name = wire.value(QStringLiteral("player_name")).toString();
	QString skill_name = wire.value(QStringLiteral("skill_name")).toString();

	ClientPlayer *player = player_name.isEmpty() ? Self : getPlayer(player_name);
	if (player == nullptr || skill_name.isEmpty())
		return;

	player->acquireSkill(skill_name);
	emit skill_attached(player, skill_name);
}

void Client::syncSkillInstances(const QVariant &payload)
{
	SkillInstanceMessage message;
	if (!message.tryParse(payload))
		return;

	if (message.action == SkillInstanceMessage::Snapshot) {
		if (Self) Self->clearSkillInstances();
		foreach (const ClientPlayer *player, m_players)
			const_cast<ClientPlayer *>(player)->clearSkillInstances();

		foreach (const SkillInstanceEntryMessage &entry, message.entries) {
			ClientPlayer *owner = getPlayer(entry.ownerName);
			if (!owner) continue;
			owner->upsertSkillInstance(entry.instance);
			if (!entry.privateState.isEmpty())
				owner->setSkillInstanceState(entry.instance.skillName,
				                             entry.instance.instanceID,
				                             entry.privateState);
		}
		emit skill_instances_reset();
		return;
	}

	if (message.action == SkillInstanceMessage::Upsert) {
		const SkillInstanceEntryMessage &entry = message.entry;
		ClientPlayer *owner = getPlayer(entry.ownerName);
		if (!owner) return;
		owner->upsertSkillInstance(entry.instance);
		if (!entry.privateState.isEmpty())
			owner->setSkillInstanceState(entry.instance.skillName,
			                             entry.instance.instanceID,
			                             entry.privateState);
		emit skill_acquired(owner, SkillInstanceUtils::formatName(entry.instance.skillName,
		                                                       entry.instance.instanceID));
		return;
	}

	ClientPlayer *owner = getPlayer(message.ownerName);
	if (message.action == SkillInstanceMessage::Remove) {
		if (!owner || !owner->removeSkillInstance(message.skillName, message.instanceId)) return;
		emit skill_detached(owner, SkillInstanceUtils::formatName(message.skillName,
		                                                       message.instanceId));
		return;
	}

	if (message.action == SkillInstanceMessage::Amount) {
		if (!owner || !owner->hasSkillInstance(message.skillName, message.instanceId)) return;
		const bool applied = message.hasAmountOverride
			? owner->setSkillInstanceAmountOverride(message.skillName, message.instanceId,
			                                        message.amount)
			: owner->resetSkillInstanceAmountOverride(message.skillName, message.instanceId);
		if (applied)
			emit skill_instance_amount_changed(owner, message.skillName, message.instanceId);
		return;
	}

	if (message.action == SkillInstanceMessage::CorrectState) {
		if (!owner || !owner->hasSkillInstance(message.skillName, message.instanceId)) return;
		bool applied = false;
		if (message.operation == "set")
			applied = owner->setSkillInstanceCorrectStateValue(message.skillName,
			                                                    message.instanceId,
			                                                    message.key, message.value);
		else if (message.operation == "remove")
			applied = owner->removeSkillInstanceCorrectStateValue(message.skillName,
			                                                       message.instanceId,
			                                                       message.key);
		else if (message.operation == "clear")
			applied = owner->clearSkillInstanceCorrectState(message.skillName,
			                                                message.instanceId);
		if (applied)
			emit skill_instance_correct_state_changed(owner, message.skillName,
			                                          message.instanceId, message.key);
		return;
	}

	// Owner-only private state：set/remove/clear/replace
	if (message.action == SkillInstanceMessage::State) {
		if (!owner || !owner->hasSkillInstance(message.skillName, message.instanceId)) return;
		bool applied = false;
		if (message.operation == "set") {
			if (message.key.isEmpty()) return;
			owner->setSkillInstanceStateValue(message.skillName, message.instanceId,
			                                   message.key, message.value);
			applied = true;
		} else if (message.operation == "remove") {
			if (message.key.isEmpty()) return;
			owner->removeSkillInstanceStateValue(message.skillName, message.instanceId,
			                                      message.key);
			applied = true;
		} else if (message.operation == "clear") {
			owner->removeSkillInstanceState(message.skillName, message.instanceId);
			applied = true;
		} else if (message.operation == "replace") {
			owner->setSkillInstanceState(message.skillName, message.instanceId,
			                             message.value.toMap());
			applied = true;
		}
		if (applied)
			emit skill_instance_state_changed(owner, message.skillName,
			                                  message.instanceId, message.key);
	}
}

void Client::askForAssign(const QVariant &)
{
	RoleAssignmentInteractionPayload payload;
	for (const ClientPlayer *player : m_players) {
		if (player != nullptr)
			payload.playerNames << player->objectName();
	}
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChooseRole, payload, true);
	beginInteraction(request);
}

void Client::onPlayerAssignRole(const QList<QString> &names, const QList<QString> &roles)
{
	//Q_ASSERT(names.size() == roles.size());

	submitInteractionResponse(InteractionResponse::makeAssignment(0, names, roles));
}

void Client::onPlayerCancelAssignRole()
{
	submitInteractionResponse(InteractionResponse::makeCancel(0));
}

void Client::askForGuanxing(const QVariant &arg)
{
	const QVariantMap wire = arg.toMap();
	QList<int> card_ids;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("card_ids")), card_ids))
		return;
	RearrangeCardsInteractionPayload payload;
	payload.cardIds = card_ids;
	const QString mode = wire.value(
		QStringLiteral("mode"), QStringLiteral("both_sides")).toString();
	if (mode == QLatin1String("up_only")) {
		payload.mode = RearrangementMode::UpOnly;
		payload.minTop = payload.maxTop = card_ids.size();
	} else if (mode == QLatin1String("down_only")) {
		payload.mode = RearrangementMode::DownOnly;
		payload.minBottom = payload.maxBottom = card_ids.size();
	} else {
		payload.mode = RearrangementMode::BothSides;
		payload.maxTop = card_ids.size();
		payload.maxBottom = card_ids.size();
	}
	InteractionRequest request = makeInteractionRequest(
		InteractionType::SkillGuanxing, payload, false);
	beginInteraction(request);
}

void Client::showAllCards(const QVariant &arg)
{
	const QVariantMap args = arg.toMap();
	QList<int> card_ids;
	if (!JsonUtils::tryParse(args.value(QStringLiteral("card_ids")), card_ids)) return;
	ClientPlayer *who = getPlayer(args.value(QStringLiteral("player_name")).toString());

	if (who) who->setKnownCards(card_ids);

	emit gongxin(card_ids, false, QList<int>());
}

void Client::askForGongxin(const QVariant &args)
{
	const QVariantMap wire = args.toMap();
	QList<int> card_ids;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("card_ids")), card_ids)) return;
	QList<int> enabled_ids;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("enabled_card_ids")), enabled_ids)) return;
	ClientPlayer *who = getPlayer(wire.value(QStringLiteral("player")).toString());
	bool enable_heart = wire.value(QStringLiteral("enable_heart")).toBool();

	if (who == nullptr)
		return;
	who->setKnownCards(card_ids);

	GongxinInteractionPayload payload {
		who->objectName(), card_ids, enabled_ids, enable_heart };
	InteractionRequest request = makeInteractionRequest(
		InteractionType::SkillGongxin, payload, true);
	beginInteraction(request);
}

void Client::onPlayerReplyGongxin(int card_id)
{
	InteractionResponse response = card_id > -1
		? InteractionResponse::makeCards(0, QList<int>() << card_id)
		: InteractionResponse::makeCancel(0);
	if (!submitInteractionResponse(response))
		return;
	setStatus(NotActive);
}

void Client::askForPindian(const QVariant &ask_str)
{
	const QString from = ask_str.toMap().value(QStringLiteral("requestor")).toString();
	if (from == Self->objectName())
		prompt_doc->setHtml(tr("Please play a card for pindian"));
	else {
		prompt_doc->setHtml(tr("%1 ask for you to play a card to pindian").arg(getPlayerName(from)));
	}
	_m_roomState.setCurrentCardUsePattern(".");
	PindianInteractionPayload payload;
	payload.opponent = from;
	payload.selection.pattern = QStringLiteral(".");
	payload.selection.minSelection = 1;
	payload.selection.maxSelection = 1;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::Pindian, payload, false);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForYiji(const QVariant &ask_str)
{
	const QVariantMap wire = ask_str.toMap();
	const QVariantList card_list = wire.value(QStringLiteral("card_ids")).toList();
	m_isDiscardActionRefusable = wire.value(QStringLiteral("optional")).toBool();
	int count = wire.value(QStringLiteral("max_cards")).toInt();
	QString prompt = wire.value(QStringLiteral("prompt")).toString();

	if (prompt.isEmpty()) {
		prompt_doc->setHtml(tr("Please distribute %1 cards %2 as you wish")
			.arg(count).arg(m_isDiscardActionRefusable ? "" : tr("to another player")));
	} else {
		QStringList texts = prompt.split(":");
		if (texts.length() < 4) {
			while (texts.length() < 3)
				texts.append("");
			texts.append(QString::number(count));
		}
		setPromptList(texts);
}

	//@todo: use cards directly rather than the QString
	QStringList card_str,names;
	foreach (const QVariant &card, card_list)
		card_str << QString::number(card.toInt());

	JsonUtils::tryParse(wire.value(QStringLiteral("players")), names);

	_m_roomState.setCurrentCardUsePattern(QString("%1=%2=%3").arg(count).arg(card_str.join("+")).arg(names.join("+")));
	QList<int> cardIds;
	JsonUtils::tryParse(wire.value(QStringLiteral("card_ids")), cardIds);
	YijiInteractionPayload payload {
		cardIds, names, m_isDiscardActionRefusable ? 0 : 1, count, count };
	InteractionRequest request = makeInteractionRequest(
		InteractionType::SkillYiji, payload, m_isDiscardActionRefusable);
	request.prompt = prompt_doc->toHtml();
	beginInteraction(request);
}

void Client::askForPlayerChosen(const QVariant &players)
{
	const QVariantMap wire = players.toMap();
	QStringList choices;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("players")), choices)
		|| choices.isEmpty()) return;
	skill_name = wire.value(QStringLiteral("skill_name")).toString();
	players_to_choose.clear();
	players_to_choose = choices;
	m_isDiscardActionRefusable = (wire.value(QStringLiteral("min_players")).toInt() <= 0);

	choose_max_num = wire.value(QStringLiteral("max_players")).toInt();
	choose_min_num = wire.value(QStringLiteral("min_players")).toInt();

	QString text;
	QString description = Sanguosha->translate(ClientInstance->skill_name);
	QString prompt = wire.value(QStringLiteral("prompt")).toString();
	if (prompt.isEmpty()) {
		if (choose_max_num > 1 && choose_min_num > 0)
			text = tr("Please choose  %1  to  %2  players").arg(choose_min_num).arg(choose_max_num);
		else if (choose_max_num > 1 && choose_min_num <= 0)
			text = tr("Plsase choose  %1  players at most").arg(choose_max_num);
		else
			text = tr("Please choose a player");
		if (!description.isEmpty() && description != skill_name)
			text.append(tr("<br/> <b>Source</b>: %1<br/>").arg(description));
	} else {
		text = formatPromptList(prompt.split(":"));
		if (prompt.startsWith("@") && !description.isEmpty() && description != skill_name)
			text.append(tr("<br/> <b>Source</b>: %1<br/>").arg(description));
	}

	PlayerInteractionPayload payload;
	payload.selection.selectablePlayers = players_to_choose;
	payload.selection.minSelection = qMax(0, choose_min_num);
	payload.selection.maxSelection = choose_max_num;
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ChoosePlayer, payload, m_isDiscardActionRefusable);
	request.skillName = skill_name;
	request.prompt = text;
	// server 送 min <= 0 就即係「可以唔揀」,同 m_isDiscardActionRefusable 同一件事。
	beginInteraction(request);
}

void Client::onPlayerReplyYiji(const Card *card, const Player *to)
{
	InteractionResponse response = card != nullptr && to != nullptr
		? InteractionResponse::makeDistribution(0, card->getSubcards(), to->objectName())
		: InteractionResponse::makeCancel(0);
	if (!submitInteractionResponse(response))
		return;
	setStatus(NotActive);
}

void Client::onPlayerReplyGuanxing(const QList<int> &up_cards, const QList<int> &down_cards)
{
	if (!submitInteractionResponse(
			InteractionResponse::makeRearrangement(0, up_cards, down_cards)))
		return;
	setStatus(NotActive);
}

void Client::onPlayerDoGuanxingStep(int from, int to)
{
	QVariantMap args{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("move")},
		{QStringLiteral("from_index"), from},
		{QStringLiteral("to_index"), to}};
	notifyServer(S_COMMAND_MIRROR_GUANXING_STEP, args);

	if (recorder) {
		ProtocolMessage message;
		message.version = ProtocolVersion::V2;
		message.type = ProtocolMessageType::Notification;
		message.source = ProtocolEndpoint::Room;
		message.destination = ProtocolEndpoint::Client;
		message.command = S_COMMAND_MIRROR_GUANXING_STEP;
		message.hasPayload = true;
		message.payload = args;
		QString error;
		if (!recorder->recordMessage(message, &error))
			qWarning().noquote() << "Replay mirror recording failed:" << error;
	}
}

void Client::mirrorGuanxingStep(const QVariant &arg)
{
	const QVariantMap object = arg.toMap();
	const QString action = object.value(QStringLiteral("action")).toString();
	if (action == QLatin1String("move")) {
		emit mirror_guanxing_move(
			object.value(QStringLiteral("from_index")).toInt(),
			object.value(QStringLiteral("to_index")).toInt());
	} else if (action == QLatin1String("finish")) {
		emit mirror_guanxing_finish();
	} else if (action == QLatin1String("start")) {
		QList<int> cards;
		if (!JsonUtils::tryParse(object.value(QStringLiteral("card_ids")), cards))
			return;
		emit mirror_guanxing_start(
			object.value(QStringLiteral("player_name")).toString(),
			object.value(QStringLiteral("up_only")).toBool(), cards);
	}
}

void Client::preshow(const QVariant &arg)
{
	const QVariantMap object = arg.toMap();
	ClientPlayer *player = getPlayer(object.value(QStringLiteral("player_name")).toString());
	if (player == nullptr)
		return;
	const QVariantMap states = object.value(QStringLiteral("states")).toMap();
	for (auto it = states.constBegin(); it != states.constEnd(); ++it)
		player->setSkillPreshowed(it.key(), it.value().toBool());
}

void Client::log(const QVariant &log_str)
{
	if(recorder_eventsave){
		save(QSanRuntimePaths::recordDir()+"/debug.txt");
	}
	const QVariantMap object = log_str.toMap();
	QStringList recipients;
	QStringList arguments;
	if (JsonUtils::tryParse(object.value(QStringLiteral("to_players")), recipients)
		&& JsonUtils::tryParse(object.value(QStringLiteral("arguments")), arguments)
		&& arguments.size() == 5) {
		QStringList log{
			object.value(QStringLiteral("log_type")).toString(),
			object.value(QStringLiteral("from_player")).toString(),
			recipients.join(QStringLiteral("+")),
			object.value(QStringLiteral("card_string")).toString()
		};
		log.append(arguments);
		if (log.first().contains("#BasaraReveal"))
			Sanguosha->playSystemAudioEffect("choose-item");
		else if (log.first() == "#Zombify") {
			ClientPlayer *from = getPlayer(log.at(1));
			if (from) Sanguosha->playSystemAudioEffect(QString("zombify-%1").arg(from->isFemale() ? "female" : "male"));
		}/* else if (log.first() == "#UseLuckCard") {
			ClientPlayer *from = getPlayer(log.at(1));
			if (from && from != Self)
				from->setHandcardNum(0);
		}*/
		emit log_received(log);
	}
}

void Client::speak(const QVariant &speak)
{
	ChatMessagePayload payload;
	if (!ChatMessagePayload::parse(speak, &payload)) {
		qDebug() << speak;
		return;
	}

	QString text = payload.text;

	static const QString prefix("<img width=14  height=14 src='image/system/chatface/");
	static const QString suffix(".png'></img>");
	text = text.replace("<#", prefix).replace("#>", suffix);

	const ClientPlayer *from = getPlayer(payload.speaker);

	if (from) {
		emit player_speak(payload.speaker, QString("<p style=\"margin:3px 2px;\">%1</p>").arg(text));
		QString title = QString("<b>(%1)%2</b>").arg(from->screenName()).arg(Sanguosha->translate(from->getGeneralName()));
		text = tr("<font color='%1'>[%2] said: %3 </font>").arg(UiConfig.TextEditColor.name()).arg(title).arg(text);
	}else
		text = tr("<font color='red'>System: %1</font>").arg(text);

	emit line_spoken(QString("<p style=\"margin:3px 2px;\">%1</p>").arg(text));
}

void Client::moveFocus(const QVariant &focus)
{
	const QVariantMap wire = focus.toMap();
	QStringList players;
	if (!JsonUtils::tryParse(wire.value(QStringLiteral("player_names")), players)
		|| players.isEmpty()) {
		foreach (const ClientPlayer *player, m_players) {
			if (player->isAlive())
				players << player->objectName();
		}
    }

	int command = -1;
	Countdown countdown;
	if (!wire.contains(QStringLiteral("command"))) {
		countdown.current = 0;
		countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
		countdown.max = ServerInfo.getCommandTimeout(S_COMMAND_UNKNOWN, S_CLIENT_INSTANCE);
	} else{
		command = wire.value(QStringLiteral("command")).toInt();
		countdown.tryParse(wire.value(QStringLiteral("countdown")));
	}
	emit focus_moved(players, countdown, command);
}

void Client::setEmotion(const QVariant &set_str)
{
	const QVariantMap set = set_str.toMap();
	emit emotion_set(set.value(QStringLiteral("player_name")).toString(),
		set.value(QStringLiteral("emotion")).toString());
}

void Client::changeTableBg(const QVariant &set_str)
{
	emit change_table_bg(set_str.toMap().value(QStringLiteral("path")).toString());
}

void Client::skillInvoked(const QVariant &arg)
{
	const QVariantMap args = arg.toMap();
	emit skill_invoked(args.value(QStringLiteral("player_name")).toString(),
		args.value(QStringLiteral("skill_name")).toString());
}

void Client::animate(const QVariant &animate_str)
{
	const QVariantMap animate = animate_str.toMap();
	QString arg1 = animate.value(QStringLiteral("first_argument")).toString();
	if(arg1.contains(":@sgs")){
		foreach (QString ar, arg1.split(":")) {
			if(ar.contains("@sgs")){
				QString _ar = ar;
				_ar.remove("@");
				const ClientPlayer *who = getPlayer(_ar);
				if(who) arg1.replace(ar,who->screenName());
			}
		}
	}
	QStringList args;
	args << arg1 << animate.value(QStringLiteral("second_argument")).toString();
	int name = animate.value(QStringLiteral("animation")).toInt();
	emit animated(name, args);
}

void Client::setFixedDistance(const QVariant &set_str)
{
	const QVariantMap set = set_str.toMap();
	ClientPlayer *from = getPlayer(set.value(QStringLiteral("from_player")).toString());
	ClientPlayer *to = getPlayer(set.value(QStringLiteral("to_player")).toString());
	int distance = set.value(QStringLiteral("distance")).toInt();
	bool isSet = set.value(QStringLiteral("set")).toBool();

	if (from && to) {
		if (isSet)
			from->setFixedDistance(to, distance);
		else
			from->removeFixedDistance(to, distance);
	}
}

void Client::setAttackRangePair(const QVariant &set_arg)
{
	const QVariantMap set = set_arg.toMap();
	ClientPlayer *from = getPlayer(set.value(QStringLiteral("from_player")).toString());
	ClientPlayer *to = getPlayer(set.value(QStringLiteral("to_player")).toString());
	bool isSet = set.value(QStringLiteral("set")).toBool();

	if (from && to) {
		if (isSet)
			from->insertAttackRangePair(to);
		else
			from->removeAttackRangePair(to);
	}
}

void Client::fillGenerals(const QVariant &generals)
{
	QStringList filled;
	if (!JsonUtils::tryParse(generals.toMap().value(QStringLiteral("general_names")), filled))
		return;
	m_filledGenerals = filled;
	emit generals_filled(filled);
}

void Client::askForGeneral3v3(const QVariant &)
{
	OptionInteractionPayload payload;
	for (const QString &general : m_filledGenerals)
		payload.options << InteractionOption(general);
	payload.enumerated = !payload.options.isEmpty();
	InteractionRequest request = makeInteractionRequest(
		InteractionType::AskGeneral, payload, false);
	beginInteraction(request);
}

void Client::takeGeneral(const QVariant &take)
{
	const QVariantMap takeObject = take.toMap();
	QString who = takeObject.value(QStringLiteral("player_name")).toString();
	QString name = takeObject.value(QStringLiteral("general_name")).toString();
	QString rule = takeObject.value(QStringLiteral("rule")).toString();

	emit general_taken(who, name, rule);
}

void Client::startArrange(const QVariant &to_arrange)
{
	QStringList arrangeList;
	const QVariantMap wire = to_arrange.toMap();
	if (wire.contains(QStringLiteral("generals"))
		&& !JsonUtils::tryParse(wire.value(QStringLiteral("generals")), arrangeList))
		return;
	ArrangeGeneralsInteractionPayload payload;
	payload.generalNames = arrangeList.isEmpty() ? m_filledGenerals : arrangeList;
	payload.arrangement = arrangeList.join("+");
	payload.slotCount = payload.generalNames.size();
	InteractionRequest request = makeInteractionRequest(
		InteractionType::ArrangeGeneral, payload, false);
	beginInteraction(request);
}

void Client::onPlayerChooseDraftGeneral(const QString &name)
{
	if (!submitInteractionResponse(InteractionResponse::makeOption(0, name)))
		return;
	setStatus(NotActive);
}

void Client::onPlayerChooseTriggerOrder(const QString &choice)
{
	const InteractionResponse response = choice.isEmpty()
		? InteractionResponse::makeCancel(0)
		: InteractionResponse::makeOption(0, choice);
	if (!submitInteractionResponse(response))
		return;
	setStatus(NotActive);
}

void Client::onPlayerArrangeGenerals(const QStringList &names)
{
	if (!submitInteractionResponse(
			InteractionResponse::makeGeneralArrangement(0, names)))
		return;
	setStatus(NotActive);
}

void Client::onPlayerChooseRole3v3()
{
	const QString choice = sender()->objectName();
	if (!submitInteractionResponse(InteractionResponse::makeOption(0, choice)))
		return;
	setStatus(NotActive);
}

void Client::recoverGeneral(const QVariant &recover)
{
	const QVariantMap args = recover.toMap();
	int index = args.value(QStringLiteral("index")).toInt();
	QString name = args.value(QStringLiteral("general_name")).toString();

	emit general_recovered(index, name);
}

void Client::revealGeneral(const QVariant &reveal)
{
	const QVariantMap args = reveal.toMap();
	bool self = (args.value(QStringLiteral("player_name")).toString() == Self->objectName());
	QString general = args.value(QStringLiteral("general_name")).toString();

	emit general_revealed(self, general);
}

void Client::onPlayerChooseOrder()
{
	OptionButton *button = qobject_cast<OptionButton *>(sender());
	QString order = "cool";
	if (button) {
		order = button->objectName();
	} else {
		if (UiRng::bounded(2) == 0)
			order = "warm";
	}
	int req = (int)S_CAMP_COOL;
	if (order == "warm") req = (int)S_CAMP_WARM;
	if (!submitInteractionResponse(
			InteractionResponse::makeOption(0, QString::number(req))))
		return;
	setStatus(NotActive);
}

void Client::updateStateItem(const QVariant &state)
{
	emit role_state_changed(state.toMap().value(QStringLiteral("state")).toString());
}

void Client::updateBossLevel(const QVariant &arg)
{
	m_bossLevel = arg.toMap().value(QStringLiteral("level")).toInt();
}

void Client::setAvailableCards(const QVariant &pile)
{
	available_cards.clear();
	JsonUtils::tryParse(pile.toMap().value(QStringLiteral("card_ids")), available_cards);
}

void Client::updateSkill(const QVariant &skill_name)
{
	emit skill_updated(skill_name.toMap().value(QStringLiteral("skill_name")).toString());
}

void Client::addRound(const QVariant &)
{
	add_round++;
	updatePileNum();
	//emit round_add();
}

void Client::setSkillDescriptionSwap(const QVariant &reveal)
{
	const QVariantMap args = reveal.toMap();
	ClientPlayer *player = getPlayer(args.value(QStringLiteral("player_name")).toString());
	if(player){
		int instanceId = args.value(QStringLiteral("instance_id"), 0).toInt();
		player->setSkillDescriptionSwap(
			args.value(QStringLiteral("skill_name")).toString(),
			args.value(QStringLiteral("key")).toString(),
			args.value(QStringLiteral("value")).toString(), instanceId);
	}
}

void Client::addEquipArea(const QVariant &reveal)
{
	const QVariantMap args = reveal.toMap();
	const QString playerName = args.value(QStringLiteral("player_name")).toString();
	ClientPlayer *player = getPlayer(playerName);
	if(player) {
		player->addEquipArea(args.value(QStringLiteral("area")).toInt());
		emit update_areas(playerName);
	}
}

void Client::setEquipAreaCount(const QVariant &reveal)
{
	const QVariantMap args = reveal.toMap();
	const QString playerName = args.value(QStringLiteral("player_name")).toString();
	ClientPlayer *player = getPlayer(playerName);
	if (player == nullptr) return;
	player->setEquipAreaCount(args.value(QStringLiteral("area")).toInt(),
		args.value(QStringLiteral("count")).toInt());
	emit update_areas(playerName);
}



lua_State *Client::getLuaState() const
{
    return m_client_lua;
}

void Client::triggerAnytimeSkill(const QString &skill_name)
{
	if (m_anytimeSkillPending.contains(skill_name)) return;
	m_anytimeSkillPending.insert(skill_name);
	AnytimeSkillPayload payload;
	payload.skillName = skill_name;
	notifyServer(S_COMMAND_ANYTIME_SKILL, payload.toVariant());
}

bool Client::isAnytimeSkillPending(const QString &skill_name) const
{
	return m_anytimeSkillPending.contains(skill_name);
}

void Client::handleAnytimeSkillDone(const QVariant &arg)
{
	AnytimeSkillPayload payload;
	if (!AnytimeSkillPayload::parse(arg, &payload))
		return;
	const QString skill_name = payload.skillName;
	m_anytimeSkillPending.remove(skill_name);
	emit anytime_skill_done(skill_name);
}

void Client::askForQml(const QVariant &arg)
{
	if (arg.userType() != QMetaType::QVariantMap) {
		failProtocol(QStringLiteral("rejecting non-object Protocol V2 custom interaction"));
		return;
	}

	CustomInteractionPayload payload;
	const QVariantMap model = arg.toMap().value(QStringLiteral("interaction")).toMap();
	payload.schemaVersion = model.value(QStringLiteral("schema_version")).toInt();
	payload.typeName = model.value(QStringLiteral("type")).toString();
	payload.title = model.value(QStringLiteral("title")).toString();
	payload.payload = QJsonObject::fromVariantMap(
		model.value(QStringLiteral("payload")).toMap());
	payload.responseSchema = QJsonObject::fromVariantMap(
		model.value(QStringLiteral("response_schema")).toMap());

	if (!m_customInteractionRegistry.supports(payload.typeName, payload.schemaVersion)) {
		failProtocol(QStringLiteral("rejecting unsupported structured custom interaction %1 schema %2")
			.arg(payload.typeName).arg(payload.schemaVersion));
		return;
	}

	InteractionRequest request = makeInteractionRequest(
		InteractionType::QmlInteract, payload, true);
	request.prompt = payload.title;
	beginInteraction(request);
}

void Client::replyQml(const QVariant &result)
{
	if (m_interactionCore == nullptr
		|| !m_interactionCore->hasActiveRequest(InteractionType::QmlInteract))
		return;
	const CustomInteractionPayload *payload
		= m_interactionCore->activeRequest().payloadAs<CustomInteractionPayload>();
	if (payload == nullptr)
		return;
	if (!submitInteractionResponse(InteractionResponse::makeCustom(0,
			payload->schemaVersion, payload->typeName, result)))
		return;
	setStatus(NotActive);
}

void Client::setShownHandCards(const QVariant &card_var)
{
    const QVariantMap cardObject = card_var.toMap();
    QString who = cardObject.value(QStringLiteral("player_name")).toString();
    QList<int> card_ids;
    JsonUtils::tryParse(cardObject.value(QStringLiteral("card_ids")), card_ids);

    ClientPlayer *player = getPlayer(who);
    player->setShownHandcards(card_ids);
    player->changePile("shown_card", true, card_ids);
}

void Client::setBrokenEquips(const QVariant &card_var)
{
    const QVariantMap cardObject = card_var.toMap();
    QString who = cardObject.value(QStringLiteral("player_name")).toString();
    QList<int> card_ids;
    JsonUtils::tryParse(cardObject.value(QStringLiteral("card_ids")), card_ids);

    ClientPlayer *player = getPlayer(who);
    player->setBrokenEquips(card_ids);
}
