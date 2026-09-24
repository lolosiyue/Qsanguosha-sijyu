#include "serverplayer.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/protocol-payload-registry.h"
#include "protocol/protocol-v2-codec.h"
#include "protocol/session/session-payloads.h"
#include "player-ui-state-builder.h"
#include "skill-runtime-coordinator.h"
//#include "skill.h"
#include "engine.h"
//#include "standard.h"
#include "maneuvering.h"
#include "basicai.h"
#include "settings.h"
#include "record-buffer.h"
#include "banpair.h"
//#include "lua-wrapper.h"
#include "json.h"
#include "gamerule.h"
//#include "util.h"
#include "exppattern.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "socket.h"
#include "../core/resolution-history.h"

#include <QMutexLocker>
#include <QScopeGuard>
#include <QMetaMethod>
#include <QThread>

using namespace QSanProtocol;

namespace {

void updateHegemonyRole(ServerPlayer *player)
{
    Room *room = player->getRoom();
    const QString kingdom = player->getKingdom();
    if (player->getRole().startsWith("careerist_")) return;
    if (kingdom == "careerist" || kingdom == "ye") {
        room->setPlayerProperty(player, "role", "careerist");
        return;
    }
    bool livingLord = player->isAlive() && player->hasShownGeneral() && player->isHegemonyLord();
    bool deadLord = false;
    int shownAllies = 1;
    foreach (ServerPlayer *other, room->getPlayers()) {
        if (other == player || other->getKingdom() != kingdom) continue;
        // Concealed sovereigns do not affect publicly established allegiances.
        if (other->hasShownGeneral() && other->isHegemonyLord()) {
            livingLord = livingLord || other->isAlive();
            deadLord = deadLord || other->isDead();
        }
        if (other->hasShownOneGeneral() && !other->getRole().startsWith(QStringLiteral("careerist")))
            ++shownAllies;
    }
    QString role = HegemonyRule::getMappedRole(kingdom);
    if (deadLord || (!livingLord && shownAllies > room->getPlayers().size() / 2))
        role = QStringLiteral("careerist");
    room->setPlayerProperty(player, "role", role);
}

bool canSeeHegemonyLimitMark(const ServerPlayer *owner, const ServerPlayer *receiver,
                             const QString &mark, bool *isLimitMark = nullptr)
{
    bool found = false;
    bool visible = receiver == owner;
    for (const SkillInstance &instance : owner->getSkillInstances()) {
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        if (!skill || skill->getLimitMark().isEmpty() || skill->getLimitMark() != mark)
            continue;
        found = true;
        // A shared mark stays public while any owning instance is public.
        visible = visible || SkillRuntimeCoordinator::canReceiveSkillInstance(
            *owner->getRoom(), receiver, owner, instance);
    }
    if (isLimitMark) *isLimitMark = found;
    return visible;
}

bool canSeeHegemonySkill(const ServerPlayer *owner, const ServerPlayer *receiver,
                         const QString &name, int id)
{
    if (owner == receiver) return true;
    for (const SkillInstance &instance : owner->getSkillInstances()) {
        if (instance.skillName == name && (id <= 0 || instance.instanceID == id)
            && SkillRuntimeCoordinator::canReceiveSkillInstance(*owner->getRoom(), receiver, owner, instance))
            return true;
    }
    return false;
}

}

void ServerPlayer::syncHegemonyRevealState()
{
    ServerPlayer *player = this;
    Room *room = player->getRoom();
    QStringList hidden;
    if (!player->hasShownGeneral()) hidden << player->getActualGeneral1Name();
    if (player->getActualGeneral2() && !player->hasShownGeneral2()) hidden << player->getActualGeneral2Name();
    room->safeSetPlayerProperty(player, "hegemony_generals", hidden.join('+'));
    room->notifyProperty(player, player, "hegemony_generals");
    foreach (ServerPlayer *receiver, room->getPlayers())
        room->notifySkillInstanceSnapshot(receiver);
    const auto swaps = player->getAllSkillDescriptionSwaps();
    for (auto it = swaps.constBegin(); it != swaps.constEnd(); ++it) {
        QString name;
        const int id = SkillInstanceUtils::parseName(it.key(), name);
        foreach (ServerPlayer *receiver, room->getPlayers()) {
            if (!canSeeHegemonySkill(player, receiver, name, id)) continue;
            for (auto entry = it->constBegin(); entry != it->constEnd(); ++entry) {
                JsonArray args;
                args << player->objectName() << name << entry.key() << entry.value() << id;
                room->doNotify(receiver, S_COMMAND_SKILL_DESCRIPTION_SWAP, args);
            }
        }
    }
    QSet<QString> limitMarks;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        if (skill && !skill->getLimitMark().isEmpty())
            limitMarks.insert(skill->getLimitMark());
    }
    for (const QString &mark : limitMarks) {
        QList<ServerPlayer *> viewers;
        viewers << player;
        foreach (ServerPlayer *receiver, room->getAllPlayers(true)) {
            if (receiver != player && canSeeHegemonyLimitMark(player, receiver, mark))
                viewers << receiver;
        }
        // Refresh presentation and AI recipients without resetting the authority mark.
        room->refreshPlayerMarkVisibility(player, mark, viewers);
    }
}

const int ServerPlayer::S_NUM_SEMAPHORES = 6;

ServerPlayer::ServerPlayer(Room *room)
	: Player(room), m_isClientResponseReady(false), m_isWaitingReply(false),
	socket(nullptr), room(room), ai(nullptr), trust_ai(new BasicAI(this)),
	recordBuffer(nullptr), _m_phases_index(NotActive), next(nullptr)
{
	qRegisterMetaType<ProtocolMessage>("QSanProtocol::ProtocolMessage");
	semas = new QSemaphore *[S_NUM_SEMAPHORES];
	// SEMA_MUTEX guards member access and must behave like an unlocked mutex
	// from construction. Room::run() only hands out that first permit when the
	// game starts, so without it every pre-game acquireLock() -- trustCommand,
	// processResponse -- blocks the server main thread forever.
	for (int i = 0; i < S_NUM_SEMAPHORES; i++)
		semas[i] = new QSemaphore(i == SEMA_MUTEX ? 1 : 0);
	onsole_owner = this;

    const auto advanceProperty = [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
    };
    // Version changes must be visible to the next worker-side snapshot immediately.
    connect(this, &Player::general_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::general2_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::role_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::state_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::hp_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::kingdom_changed, this, advanceProperty, Qt::DirectConnection);
    connect(this, &Player::gameplay_property_changed, this, advanceProperty, Qt::DirectConnection);
    const auto dirtyDescriptions = [this]() {
        if (this->room && this->room->getThread())
            this->room->getThread()->markSkillDescriptionsDirty();
    };
    // Direct connection only flips an atomic flag. Evaluation is deferred to
    // the rule thread's event/request boundary, never the GUI QObject thread.
    connect(this, &Player::skill_set_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::skill_state_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::mark_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::mark_changed, this, [this]() {
        QVariantMap effects = getTag("SkillEffectDescriptions").toMap();
        bool changed = false;
        for (auto it = effects.begin(); it != effects.end();) {
            const QString mark = it.value().toMap().value("active_mark").toString();
            if (!mark.isEmpty() && getMark(mark) <= 0) { it = effects.erase(it); changed = true; }
            else ++it;
        }
        // Retire the descriptor with its real counter, so a later reuse of the
        // same mark cannot resurrect an old source or expiration condition.
        if (changed) setTag("SkillEffectDescriptions", effects);
    }, Qt::DirectConnection);
    connect(this, &Player::phase_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::card_limitation_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::general_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::general2_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::state_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::hp_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::gameplay_property_changed, this, dirtyDescriptions, Qt::DirectConnection);
    connect(this, &Player::phase_changed, this, [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::TurnStateChanged);
    }, Qt::DirectConnection);
    connect(this, &Player::mark_changed, this, [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::PlayerMarkChanged);
    }, Qt::DirectConnection);
    connect(this, &Player::skill_set_changed, this, [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::SkillSetChanged);
    }, Qt::DirectConnection);
    connect(this, &Player::skill_state_changed, this, [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::SkillInstanceStateChanged);
    }, Qt::DirectConnection);
    connect(this, &Player::card_limitation_changed, this, [this]() {
        if (this->room && this->room->roomRuntime())
            this->room->roomRuntime()->advanceStateRevision(RoomRuntime::CardLimitationChanged);
    }, Qt::DirectConnection);
}

ServerPlayer::~ServerPlayer()
{
	for (int i = 0; i < S_NUM_SEMAPHORES; i++)
		delete semas[i];
	delete[] semas;
	delete trust_ai;
	delete recordBuffer;
}

void ServerPlayer::setTag(const QString &key, const QVariant &value)
{
	if (!tag.contains(key) && !value.isValid()) return;
	if (tag.contains(key) && tag.value(key) == value) return;
	Player::setTag(key, value);
	if (!room) return;
    if (room->getThread()) room->getThread()->markSkillDescriptionsDirty();

	bool safe = false;
        switch (value.userType()) {
		case QMetaType::Int:
		case QMetaType::Bool:
		case QMetaType::QString:
		case QMetaType::QStringList:
		case QMetaType::UnknownType:
			safe = true;
			break;
		default:
			break;
	}

	if (safe) {
		room->broadcastTagProperty(this, key, value);
	}
}

void ServerPlayer::refreshUIState(bool force)
{
    if (!room || !getGeneral() || !isAlive()) return;
    if (!force && room->getThread() && room->getThread()->deferPlayerUiState(this)) return;

    const PlayerUIState state = PlayerUIStateBuilder::build(*this, *room);
    if (!force && state == m_uiState)
        return;

    m_uiState = state;
    room->notifyPlayerUIState(this, state);
}

void ServerPlayer::refreshSkillDescriptionState()
{
    if (!room) return;
    PlayerUIState state = m_uiState;
    PlayerUIStateBuilder::buildSkillDescriptions(state, *this, *room);
    if (state == m_uiState) return;
    m_uiState = state;
    room->notifyPlayerUIState(this, state);
}

QStringList ServerPlayer::getPendingAnytimeSkills() const
{
	return m_pendingAnytimeSkills;
}

void ServerPlayer::addPendingAnytimeSkill(const QString &skill_name)
{
	if (!m_pendingAnytimeSkills.contains(skill_name))
		m_pendingAnytimeSkills << skill_name;
}

void ServerPlayer::removePendingAnytimeSkill(const QString &skill_name)
{
	m_pendingAnytimeSkills.removeAll(skill_name);
}

void ServerPlayer::clearPendingAnytimeSkills()
{
	m_pendingAnytimeSkills.clear();
}

/*void ServerPlayer::drawCard(const Card *card)
{
	handcards << card;
}*/

Room *ServerPlayer::getRoom() const
{
	return room;
}

void ServerPlayer::setSkillInstanceState(const QString &skillName, int instanceID, const QVariantMap &state)
{
	if (!findSkillInstance(skillName, instanceID)) return;
	Player::setSkillInstanceState(skillName, instanceID, state);
	const SkillInstance *instance = findSkillInstance(skillName, instanceID);
	if (room && instance)
		room->notifySkillInstanceState(this, *instance, "replace", QString(), state);
}

void ServerPlayer::removeSkillInstanceState(const QString &skillName, int instanceID)
{
	if (!findSkillInstance(skillName, instanceID)) return;
	Player::removeSkillInstanceState(skillName, instanceID);
	const SkillInstance *instance = findSkillInstance(skillName, instanceID);
	if (room && instance)
		room->notifySkillInstanceState(this, *instance, "clear");
}

void ServerPlayer::setSkillInstanceStateValue(const QString &skillName, int instanceID, const QString &key, const QVariant &value)
{
	if (!findSkillInstance(skillName, instanceID) || key.isEmpty()) return;
	Player::setSkillInstanceStateValue(skillName, instanceID, key, value);
	const SkillInstance *instance = findSkillInstance(skillName, instanceID);
	if (room && instance)
		room->notifySkillInstanceState(this, *instance, "set", key, value);
}

void ServerPlayer::removeSkillInstanceStateValue(const QString &skillName, int instanceID, const QString &key)
{
	if (!findSkillInstance(skillName, instanceID) || key.isEmpty()) return;
	if (!getSkillInstanceState(skillName, instanceID).contains(key)) return;
	Player::removeSkillInstanceStateValue(skillName, instanceID, key);
	const SkillInstance *instance = findSkillInstance(skillName, instanceID);
	if (room && instance)
		room->notifySkillInstanceState(this, *instance, "remove", key);
}

void ServerPlayer::setOnsoleOwner(ServerPlayer *owner)
{
	if(owner){
		onsole_owner = owner;
		//room->setPlayerProperty(this,"onsole_owner",owner->objectName());
	}else{
		onsole_owner = this;
		//room->setPlayerProperty(this,"onsole_owner","");
	}
}

ServerPlayer *ServerPlayer::getOnsoleOwner() const
{
	return onsole_owner;
}

void ServerPlayer::broadcastSkillInvoke(const QString &card_name) const
{
	room->broadcastSkillInvoke(card_name, getGender()!=General::Female, -1);
}

void ServerPlayer::broadcastSkillInvoke(const Card *card) const
{
	if (card->isMute()) return;
	QString skill_name = card->getSkillName(false);
	if(skill_name.isEmpty()||skill_name.startsWith("_")){
		skill_name = card->getCommonEffectName();
		if (skill_name.isEmpty()) broadcastSkillInvoke(card->objectName());
		else room->broadcastSkillInvoke(skill_name, "common");
	}else{
		const Skill *skill = Sanguosha->getSkill(skill_name);
		if (skill) {
			int index = skill->getEffectIndex(this, card);
			if (index == 0) return;
			if ((index == -1 && skill->getSources().isEmpty()) || index == -2) {
				skill_name = card->getCommonEffectName();
				if (skill_name.isEmpty()) broadcastSkillInvoke(card->objectName());
				else room->broadcastSkillInvoke(skill_name, "common");
			} else
				room->broadcastSkillInvoke(skill_name, index, this);
		}else if(QFile::exists("audio/card/male/"+skill_name+".ogg"))
			broadcastSkillInvoke(skill_name);
		else
			broadcastSkillInvoke(card->objectName());
	}
}

void ServerPlayer::peiyin(const Skill *skill, int type)
{
	room->broadcastSkillInvoke(skill, type, this);
}

void ServerPlayer::peiyin(const QString &skillName, int type)
{
	room->broadcastSkillInvoke(skillName, type, this);
}

/*int ServerPlayer::getRandomHandCardId() const
{
	const Card * c = getRandomHandCard();
	if (c) return c->getEffectiveId();
	return -1;
}

const Card *ServerPlayer::getRandomHandCard() const
{
	if (handcards.isEmpty()) return nullptr;
	return handcards.at(qsanRandomBounded(handcards.length()));
}*/

void ServerPlayer::obtainCard(const Card *card, bool visible)
{
	room->obtainCard(this, card, CardMoveReason(CardMoveReason::S_REASON_GOTCARD, objectName()), visible);
}

void ServerPlayer::throwAllEquips(const QString &reason)
{
	QList<int>ids;
	foreach (const Card *equip, getEquips())
		if (!isJilei(equip)) ids << equip->getId();
	room->throwCard(ids, reason, this);
}

void ServerPlayer::throwAllHandCards(const QString &reason)
{
	int card_length = getHandcardNum();
	room->askForDiscard(this, reason, card_length, card_length);
}

void ServerPlayer::throwAllHandCardsAndEquips(const QString &reason)
{
	int card_length = getCardCount();
	room->askForDiscard(this, reason, card_length, card_length, false, true);
}

void ServerPlayer::throwAllMarks(bool visible_only)
{
	// throw all marks
	foreach (QString m, marks.keys()) {
		if(m=="@bossExp"||m.endsWith("-Keep")) continue;
		if(visible_only){
			if(!(m.startsWith("@") || m.startsWith("&") || m.contains("sys_"))) 
				continue;
		}
		room->setPlayerMark(this, m, 0);
	}
}

void ServerPlayer::clearOnePrivatePile(const QString &pile_name)
{
	if (piles.contains(pile_name))
		room->throwCard(piles[pile_name], CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, objectName()), nullptr);
}

void ServerPlayer::clearPrivatePiles()
{
	foreach(QString pile_name, piles.keys())
		clearOnePrivatePile(pile_name);
}

void ServerPlayer::bury()
{
    // Marks are cleared below; retire command invalidity before losing its owner marker.
    if (getMark("command4_effect") > 0)
        room->removeSkillInvalidity(this, "all", objectName(), "heg_command4");
	room->setPlayerFlag(this,".");
	room->addPlayerHistory(this,".");
	throwAllCards("bury");
	throwAllMarks();
	clearPrivatePiles();

	room->clearPlayerCardLimitation(this, false);
}

void ServerPlayer::throwAllCards(const QString &reason)
{
	room->throwCard(handCards()+getEquipsId(), reason, this);
	room->throwCard(getJudgingAreaID(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName(), reason, ""), nullptr);
}

void ServerPlayer::fillHandCards(int n, const QString &reason)
{
    // Fill only the current deficit; ordinary draw events remain authoritative.
    if (isAlive() && n > getHandcardNum())
        drawCards(n - getHandcardNum(), reason);
}

void ServerPlayer::drawCards(int n, const QString &reason, bool isTop, bool visible)
{
	room->drawCards(this, n, reason, isTop, visible);
}

QList<int> ServerPlayer::drawCardsList(int n, const QString &reason, bool isTop, bool visible)
{
	return room->drawCardsList(this, n, reason, isTop, visible);
}

bool ServerPlayer::askForSkillInvoke(const QString &skill_name, const QVariant &data, bool notify)
{
	return room->askForSkillInvoke(this, skill_name, data, notify);
}

bool ServerPlayer::askForSkillInvoke(const Skill *skill, const QVariant &data, bool notify)
{
	//Q_ASSERT(skill != nullptr);
	return skill&&askForSkillInvoke(skill->objectName(), data, notify);
}

bool ServerPlayer::askForSkillInvoke(const QString &skill_name, ServerPlayer *player, bool notify)
{
	return askForSkillInvoke(skill_name, QVariant::fromValue(player), notify);
}

bool ServerPlayer::askForSkillInvoke(const Skill *skill, ServerPlayer *player, bool notify)
{
	//Q_ASSERT(skill != nullptr);
	return skill&&askForSkillInvoke(skill->objectName(), player, notify);
}

QList<int> ServerPlayer::forceToDiscard(int discard_num, bool include_equip, bool is_discard, const QString &pattern)
{
	QList<const Card *> all_cards = getHandcards();
	if (include_equip) all_cards << getEquips();
	qsanShuffle(all_cards);

	QList<int> to_discard;
	ExpPattern exp_pattern(pattern);
	foreach (const Card *c, all_cards){
		if(exp_pattern.match(this, c)){
			if(is_discard&&isJilei(c)) continue;
			to_discard << c->getId();
			if (to_discard.length() >= discard_num)
				break;
		}
	}
	return to_discard;
}

int ServerPlayer::aliveCount(bool includeRemoved) const
{
    if (includeRemoved)
        return room->alivePlayerCount();
    int count = 0;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (!p->isRemoved())
            ++count;
    }
    return count;
}

/*int ServerPlayer::getHandcardNum() const
{
	return handcards.size();
}*/

void ServerPlayer::setSocket(ClientSocket *socket)
{
	if (socket) {
		connect(socket, SIGNAL(disconnected()), this, SIGNAL(disconnected()));
		connect(socket, SIGNAL(message_got(QByteArray)), this, SLOT(getMessage(QByteArray)));
		connect(this, SIGNAL(message_ready(QByteArray)), this, SLOT(sendMessage(QByteArray)));
	} else {
		if (this->socket) {
			this->disconnect(this->socket);
			this->socket->disconnect(this);
			disconnectSocketFromOwnerThread();
			this->socket->deleteLater();
		}

		disconnect(this, SLOT(sendMessage(QByteArray)));
	}

	this->socket = socket;
}

void ServerPlayer::adoptProtocolConnectionState(
	const ProtocolConnectionState &state)
{
	m_connectionGeneration = state.generation;
	m_lastIncomingMessageId = state.lastIncomingMessageId;
	QMutexLocker outboundLocker(&m_outboundMutex);
	if (!m_protocolMessageIds.setNextValue(state.nextOutgoingMessageId))
		m_protocolMessageIds.reset();
}

void ServerPlayer::kick()
{
	room->notifyProperty(this, this, "flags", "is_kicked");
	disconnectSocketFromOwnerThread();
	setSocket(nullptr);
}

void ServerPlayer::getMessage(const QByteArray &message)
{
	ProtocolMessage decoded;
	const ProtocolDecodeResult result = m_protocolRouter.decode(message, &decoded);
	if (!result.success || decoded.messageId == 0
		|| decoded.messageId <= m_lastIncomingMessageId) {
		qWarning().noquote() << reportHeader()
			<< (result.success ? QStringLiteral("Non-monotonic Protocol V2 message_id")
				: QStringLiteral("Protocol decode failed: %1").arg(result.detail));
		disconnectSocketFromOwnerThread();
		return;
	}
	m_lastIncomingMessageId = decoded.messageId;

	emit request_got(QString::fromUtf8(message), decoded);
}

void ServerPlayer::startNetworkDelayTest()
{
	test_time = QDateTime::currentDateTime();
	ProtocolMessage message;
	message.type = ProtocolMessageType::Notification;
	message.source = ProtocolEndpoint::Room;
	message.destination = ProtocolEndpoint::Client;
	message.command = S_COMMAND_NETWORK_DELAY_TEST;
	message.hasPayload = true;
	NetworkDelayPayload payload;
	payload.nonce = QString::number(test_time.toMSecsSinceEpoch());
	message.payload = payload.toVariant();
	sendProtocolMessage(message);
}

qint64 ServerPlayer::endNetworkDelayTest()
{
	return test_time.msecsTo(QDateTime::currentDateTime());
}

void ServerPlayer::startRecord()
{
	delete recordBuffer;
	recordBuffer = new RecordBuffer(
		Sanguosha ? Sanguosha->getVersion() : QStringLiteral("unknown"),
		Sanguosha ? Sanguosha->getMODName() : QStringLiteral("unknown"));
}

void ServerPlayer::saveRecord(const QString &filename)
{
	if (recordBuffer)
		recordBuffer->saveText(filename);
}

void ServerPlayer::addToSelected(const QString &general)
{
	selected.append(general);
}

QStringList ServerPlayer::getSelected() const
{
	return selected;
}

QString ServerPlayer::findReasonable(const QStringList &generals, bool no_unreasonable)
{
	QStringList ban_list;
	if (Config.GameMode.mode_id == "zombie_mode")
		ban_list << Config.value("Banlist/Zombie").toStringList();
	if (Config.GameMode.mode_id.endsWith("p")
		|| Config.GameMode.mode_id.endsWith("pd")
		|| Config.GameMode.mode_id.endsWith("pz")
		|| Config.GameMode.mode_id.contains("_mini_")
		|| Config.GameMode.mode_id == "custom_scenario") {
		ban_list << Config.value("Banlist/Roles").toStringList();
	}
	foreach (QString name, generals) {
		if (Config.Enable2ndGeneral) {
			if (getGeneral()) {
				if (!BanPair::isBanned(getGeneralName()) && BanPair::isBanned(getGeneralName(), name)) continue;
				if (Config.EnableHegemony && getGeneral()->getKingdom() != Sanguosha->getGeneral(name)->getKingdom())
					continue;
			} else if(BanPair::isBanned(name)) continue;
		}
		if (ban_list.contains(name)) continue;
		return name;
	}

	if (no_unreasonable)
		return "";

	return generals.first();
}

void ServerPlayer::clearSelected()
{
	selected.clear();
}

void ServerPlayer::sendMessage(const QByteArray &message)
{
	if (socket) {
#ifdef LOGNETWORK
		emit Sanguosha->logNetworkMessage("send "+this->objectName()+":"+QString::fromUtf8(message));
#endif
		socket->send(message);
	}
}

quint64 ServerPlayer::sendProtocolMessage(ProtocolMessage message,
	const std::function<void(quint64)> &onNumbered)
{
	// Numbering, encoding and queueing happen together: a frame numbered here
	// must also be queued here, or a send from another thread could overtake it.
	QMutexLocker outboundLocker(&m_outboundMutex);
	// Bots without a wire observer or replay recorder consume no notifications.
	// Keep requests, numbering callbacks and replay-only recipients on the full
	// path; controller forwarding has already happened in RoomNotifier.
	static const QMetaMethod readySignal = QMetaMethod::fromSignal(&ServerPlayer::message_ready);
	if (message.type == ProtocolMessageType::Notification && !onNumbered
		&& !recordBuffer && !isSignalConnected(readySignal))
		return 0;
	if (message.messageId == 0)
		message.messageId = m_protocolMessageIds.next();

	QString error;
	ProtocolMessage canonical;
	if (ProtocolGameplayPayloadRegistry::isMigratedFlow(message)) {
		if (!ProtocolGameplayPayloadRegistry::encodeForWire(
				message, &canonical, &error)) {
			qWarning().noquote() << reportHeader() << "Protocol payload encode failed: command"
				<< message.command << ":" << error;
			disconnectSocketFromOwnerThread();
			return 0;
		}
	} else {
		canonical = message;
	}
	if (!ProtocolPayloadRegistry::encodeObjectPayload(
			canonical, &canonical, &error)
		|| !ProtocolPayloadRegistry::validateObjectPayload(canonical, &error)) {
		qWarning().noquote() << reportHeader() << "Protocol payload encode failed: command"
			<< message.command << ":" << error;
		disconnectSocketFromOwnerThread();
		return 0;
	}
	const QByteArray encoded = ProtocolV2Codec().encode(canonical, &error);
	if (encoded.isEmpty()) {
		qWarning().noquote() << reportHeader() << "Protocol encode failed: command"
			<< message.command << ":" << error;
		disconnectSocketFromOwnerThread();
		return 0;
	}

	if (recordBuffer && !recordBuffer->recordMessage(canonical, &error)) {
		qWarning().noquote() << reportHeader() << "Replay recording failed: command"
			<< message.command << ":" << error;
		disconnectSocketFromOwnerThread();
		return 0;
	}
	// Still inside the outbound lock: the id is published before the frame can
	// reach the socket, so a reply can never overtake the state that accepts it.
	if (onNumbered)
		onNumbered(message.messageId);
	m_outboundFrames.append(encoded);
	outboundLocker.unlock();

	// message_ready is always emitted on this object's thread, so the socket is
	// only ever written from the thread that owns it.
	if (QThread::currentThread() == thread())
		flushOutbound();
	else
		QMetaObject::invokeMethod(this, "flushOutbound", Qt::QueuedConnection);
	return message.messageId;
}

void ServerPlayer::disconnectSocketFromOwnerThread()
{
	if (socket == nullptr)
		return;
	// A QAbstractSocket may only be touched from the thread that owns it, and
	// that owner is the main thread, not this player's thread. Encode, validate
	// and replay failures are reported from the room thread, so the teardown has
	// to hop the same way flushOutbound() makes the writes hop.
	if (QThread::currentThread() == socket->thread())
		socket->disconnectFromHost();
	else
		QMetaObject::invokeMethod(socket, "disconnectFromHost", Qt::QueuedConnection);
}

void ServerPlayer::flushOutbound()
{
	forever {
		QByteArray frame;
		{
			QMutexLocker locker(&m_outboundMutex);
			if (m_outboundFrames.isEmpty())
				return;
			frame = m_outboundFrames.takeFirst();
		}
		emit message_ready(frame);
	}
}

QString ServerPlayer::reportHeader() const
{
	return QString("%1 ").arg(objectName().isEmpty()?tr("Anonymous"):objectName());
}

void ServerPlayer::removeCard(int id, Place place)
{
	if(place==PlaceEquip)
		qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard())->onUninstall(this);
	Player::removeCard(id, place);
	/*switch (place) {
	case PlaceHand: {
		handcards.removeAll(card);
		break;
	}case PlaceEquip: {
		WrappedCard *wrapped = Sanguosha->getWrappedCard(card->getEffectiveId());
		const EquipCard *equip = qobject_cast<const EquipCard *>(wrapped->getRealCard());
		if (!equip) equip = qobject_cast<const EquipCard *>(wrapped);
			//equip = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(card->getEffectiveId()));
		//Q_ASSERT(equip != nullptr);
		equip->onUninstall(this);
		removeEquip(wrapped);

		LogMessage log;
		log.type = "#Uninstall";
		log.card_str = wrapped->toString();
		log.from = this;
		room->sendLog(log);
		break;
	}case PlaceDelayedTrick: {
		removeDelayedTrick(card);
		break;
	}case PlaceSpecial: {
		int card_id = card->getEffectiveId();
		QString pile_name = getPileName(card_id);

		//@todo: sanity check required
		if (!pile_name.isEmpty())
			piles[pile_name].removeOne(card_id);

		break;
	}default:
		break;
	}*/
}

void ServerPlayer::addCard(int id, Place place)
{
	addCard(id, place, std::function<void()>());
}

void ServerPlayer::addCard(int id, Place place, const std::function<void()> &afterMutation)
{
	Player::addCard(id, place);
	if (afterMutation)
		afterMutation();
	if(place==PlaceEquip)
		qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard())->onInstall(this);
	/*switch (place) {
	case PlaceHand: {
		handcards << card;
		break;
	}case PlaceEquip: {
		WrappedCard *wrapped = Sanguosha->getWrappedCard(card->getEffectiveId());
		const EquipCard *equip = qobject_cast<const EquipCard *>(wrapped->getRealCard());
		setEquip(wrapped);
		equip->onInstall(this);
		break;
	}case PlaceDelayedTrick: {
		addDelayedTrick(card);
		break;
	}default:
		break;
	}*/
}

/*bool ServerPlayer::isLastHandCard(const Card *card, bool contain) const
{
	if(card->isVirtualCard()){
		QList<int> ids = card->getSubcards();
		if(ids.length()>0){
			if (contain) {
				foreach (const Card *h, handcards) {
					if (!ids.contains(h->getId()))
						return false;
				}
				return true;
			} else if(ids.length()>=handcards.length()){
				foreach (int id, ids) {
					if (!handcards.contains(Sanguosha->getCard(id)))
						return false;
				}
				return true;
			}
		}
	}else if(handcards.length() == 1)
		return handcards.contains(card);
	return false;
}

QList<const Card *> ServerPlayer::getHandcards() const
{
	return handcards;
}*/

QList<const Card *> ServerPlayer::getCards(const QString &flags) const
{
	QList<const Card *> cards;
	if (flags.contains("h"))
		cards << getHandcards();
	if (flags.contains("e"))
		cards << getEquips();
	if (flags.contains("j"))
		cards << getJudgingArea();

	return cards;
}

DummyCard *ServerPlayer::wholeHandCards() const
{
	return dummyCard(handCards());
}

QList<int> ServerPlayer::getHandPile() const
{
	QList<int> handpile = Player::getHandPile();
	if (getTag("TaoxiHere").toBool()) {
		bool ok = false;
		int id = getTag("TaoxiId").toInt(&ok);
		if (ok) handpile << id;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p!=this&&getMark("&bshaoshi+#"+p->objectName())>0)
			handpile << p->handCards();
	}
	return handpile;
}

bool ServerPlayer::hasNullification() const
{
	// Faction nullification is a distinct physical name with the same response type.
	foreach (const Card *card, getHandcards()) {
		if (card->isKindOf("Nullification"))
			return true;
	}
	foreach (int id, getHandPile()) {
		if (Sanguosha->getCard(id)->isKindOf("Nullification"))
			return true;
	}
	foreach (const Skill *skill, getVisibleSkillList(true)) {
		if (skill->inherits("ViewAsSkill")) {
			const ViewAsSkill *vsskill = qobject_cast<const ViewAsSkill *>(skill);
			if(hasSkill(skill->objectName())&&vsskill->isEnabledAtResponse(this,"nullification")) return true;
			//if (vsskill->isEnabledAtNullification(this)) return true;
		} else if (skill->inherits("TriggerSkill")) {
			const ViewAsSkill *vsskill = qobject_cast<const TriggerSkill *>(skill)->getViewAsSkill();
			if(vsskill&&hasSkill(skill->objectName())&&vsskill->isEnabledAtResponse(this,"nullification")) return true;
			//if (vsskill && vsskill->isEnabledAtNullification(this)) return true;
		}
	}
	return false;
}

bool ServerPlayer::pindian(ServerPlayer *target, const QString &reason, const Card *card1)
{
	PindianStruct *pindian_struct = PinDian(target, reason, card1);
	if (pindian_struct) return pindian_struct->success;
	return false;
}

int ServerPlayer::pindianInt(ServerPlayer *target, const QString &reason, const Card *card1)
{
	PindianStruct *pindian_struct = PinDian(target, reason, card1);
	if (pindian_struct->success) return 1;
	else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
	else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
	return -2;
}

PindianStruct *ServerPlayer::PinDian(ServerPlayer *target, const QString &reason, const Card *card1)
{
    return finishPindian(pindianSelect(target, reason, card1));
}

bool ServerPlayer::pindian(PindianStruct *selection)
{
    // Original Quhu/Tianyi pay selection before revealing, then resolve the same cards.
    std::unique_ptr<PindianStruct> owned(selection);
    const PindianStruct *result = finishPindian(selection);
    return result && result->success;
}

PindianStruct *ServerPlayer::pindianSelect(ServerPlayer *target, const QString &reason, const Card *card1)
{
	//Q_ASSERT(canPindian(target, false));

	LogMessage log;
	log.type = "#Pindian";
	log.from = this;
	log.to << target;
	room->sendLog(log);

	PindianStruct *pindian_struct = new PindianStruct;
	pindian_struct->from = this;
	pindian_struct->to = target;
	pindian_struct->from_card = card1;
	pindian_struct->to_card = nullptr;
	pindian_struct->reason = reason;

	QVariant data = QVariant::fromValue(pindian_struct);
	room->getThread()->trigger(AskforPindianCard, room, this, data);
	pindian_struct = data.value<PindianStruct *>();

	if (!pindian_struct->from_card && !pindian_struct->to_card) {
		QList<const Card *> cards = room->askForPindianRace(this, target, reason);
		pindian_struct->from_card = cards.first();
		pindian_struct->to_card = cards.last();
	} else if (!pindian_struct->to_card)
		pindian_struct->to_card = room->askForPindian(target, this, reason);
	else if (!pindian_struct->from_card)
		pindian_struct->from_card = room->askForPindian(this, this, reason);

	if (!pindian_struct->from_card || !pindian_struct->to_card) {
        delete pindian_struct;
        return nullptr;
    }

	pindian_struct->from_number = pindian_struct->from_card->getNumber();
	pindian_struct->to_number = pindian_struct->to_card->getNumber();

	CardsMoveStruct move1;
	move1.card_ids << pindian_struct->from_card->getEffectiveId();
	move1.from = room->getCardOwner(pindian_struct->from_card->getEffectiveId());
	move1.to = nullptr;
	move1.to_place = PlaceTable;
	move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(), pindian_struct->to->objectName(), reason, "");

	CardsMoveStruct move2;
	move2.card_ids << pindian_struct->to_card->getEffectiveId();
	move2.from = room->getCardOwner(pindian_struct->to_card->getEffectiveId());
	move2.to = nullptr;
	move2.to_place = PlaceTable;
	move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), reason, "");

	log.type = "$PindianResult";
	log.from = pindian_struct->from;
	log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
	room->sendLog(log);

	log.from = pindian_struct->to;
	log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
	room->sendLog(log);

	QList<CardsMoveStruct> moves;
	moves << move1 << move2;
	room->moveCardsAtomic(moves, true);

	return pindian_struct;
}

PindianStruct *ServerPlayer::finishPindian(PindianStruct *pindian_struct)
{
    if (!pindian_struct) return nullptr;
    ServerPlayer *target = pindian_struct->to;
    const QString reason = pindian_struct->reason;
    QVariant data;
    LogMessage log;
    QList<CardsMoveStruct> moves;
	data.setValue(pindian_struct);
	room->getThread()->trigger(PindianVerifying, room, this, data);
	pindian_struct = data.value<PindianStruct *>();

	pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

	log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
	log.from = this;
	room->sendLog(log);

	JsonArray arg;
	arg << S_GAME_EVENT_REVEAL_PINDIAN << objectName() << pindian_struct->from_card->getEffectiveId()
		<< target->objectName() << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << reason;
	room->doBroadcastNotify(S_COMMAND_LOG_EVENT, arg);

	data.setValue(pindian_struct);
	room->getThread()->trigger(Pindian, room, this, data);

	moves.clear();
	if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == PlaceTable) {
		CardsMoveStruct move1;
		move1.card_ids << pindian_struct->from_card->getEffectiveId();
		move1.from = pindian_struct->from;
		move1.to = nullptr;
		move1.to_place = DiscardPile;
		move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(), pindian_struct->to->objectName(), reason, "");
		moves.append(move1);
	}

	if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == PlaceTable) {
		if (pindian_struct->to_card->getEffectiveId() != pindian_struct->from_card->getEffectiveId()) {
			CardsMoveStruct move2;
			move2.card_ids << pindian_struct->to_card->getEffectiveId();
			move2.from = pindian_struct->to;
			move2.to = nullptr;
			move2.to_place = DiscardPile;
			move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), reason, "");
			moves.append(move2);
		}
	}
	room->moveCardsAtomic(moves, true);

	data = QString("pindian:%1:%2:%3:%4:%5").arg(reason).arg(objectName()).arg(pindian_struct->from_card->getEffectiveId())
		.arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId());
	room->getThread()->trigger(ChoiceMade, room, this, data);

	return pindian_struct;
}

void ServerPlayer::turnOver()
{
	if (room->getThread()->trigger(TurnOver, room, this)) return;
	setFaceUp(!faceUp());
	room->broadcastProperty(this, "faceup");

	LogMessage log;
	log.type = "#TurnOver";
	log.from = this;
	log.arg = faceUp() ? "face_up" : "face_down";
	room->sendLog(log);

	room->getThread()->trigger(TurnedOver, room, this);
}

bool ServerPlayer::changePhase(Phase from, Phase to)
{
	setPhase(PhaseNone);

	PhaseChangeStruct phase_change;
	phase_change.from = from;
	phase_change.to = to;
	QVariant data = QVariant::fromValue(phase_change);
	ResolutionHistoryEventGuard phaseHistory(
		room->resolutionHistory(), QStringLiteral("phase"),
		QVariantMap{{QStringLiteral("player"), objectName()},
			{QStringLiteral("from_phase"), static_cast<int>(from)},
			{QStringLiteral("phase"), static_cast<int>(to)}},
		room->historyRecordingEnabled());

	try {
	bool skip = room->getThread()->trigger(EventPhaseChanging, room, this, data);
	room->flushHegemonyReveals();
	phase_change = data.value<PhaseChangeStruct>();
	phaseHistory.update(QVariantMap{{QStringLiteral("from_phase"), static_cast<int>(phase_change.from)},
		{QStringLiteral("phase"), static_cast<int>(phase_change.to)}});

	setPhase(phase_change.to);
	if(phase_change.to == NotActive){
		room->broadcastProperty(this, "phase");
		room->getThread()->trigger(EventPhaseStart, room, this);
		room->processScheduledExtraTurns();
		phaseHistory.finish(QStringLiteral("completed"));
		return false;
	}
	//if (!phases.isEmpty()) phases.removeFirst();
	if (skip) {
		setPhase(from);
		phaseHistory.finish(QStringLiteral("skipped"));
		return true;
	}
	room->broadcastProperty(this, "phase");

	// Match XXY's freeChain boundaries without flushing inside a skill effect.
	const bool phaseEnded = room->getThread()->trigger(EventPhaseStart, room, this, data);
	room->flushHegemonyReveals();
	if (!phaseEnded) {
		room->getThread()->trigger(EventPhaseProceeding, room, this, data);
		room->flushHegemonyReveals();
	}
	room->getThread()->trigger(EventPhaseEnd, room, this, data);
	room->flushHegemonyReveals();
	phaseHistory.finish(QStringLiteral("completed"));
	return false;
	} catch (TriggerEvent) {
		if (phaseHistory.id() != 0)
			room->getThread()->rememberInterruptedPhase(phaseHistory.id());
		throw;
	}
}

void ServerPlayer::play(QList<Phase> set_phases)
{
	static QList<Phase> all_phases;
	if(all_phases.isEmpty()) all_phases << RoundStart << Start << Judge << Draw << Play << Discard << Finish;
	if (set_phases.isEmpty()) {
		if(getMark("@extra_turn")>0){
			foreach (QVariant v, getTag("extraTurnPhases").toList())
				set_phases << Phase(v.toInt());
		}else
			set_phases = all_phases;
		room->setTag("Global_AllTurnsNum",room->getTag("Global_AllTurnsNum").toInt()+1);
	}
	if (!set_phases.contains(NotActive))
		set_phases << NotActive;

	room->setPlayerFlag(this,"CurrentPlayer");

	_m_phases_state.clear();
	static QStringList phase_names;
	if(phase_names.isEmpty()) phase_names << "roundstart" << "start" << "judge" << "draw" << "play" << "discard" << "finish";
	foreach (Phase pha, set_phases) {
		int n = all_phases.indexOf(pha);
		if (n>=0&&getMark("LostPlayerPhase_"+phase_names.at(n))>0)
			set_phases.removeOne(pha);
		else{
			PhaseStruct _phase;
			_phase.phase = pha;
			_m_phases_state << _phase;
		}
	}
	phases = set_phases;

	PhaseChangeStruct phase_change;
	for (int i = 0; i < _m_phases_state.length(); i++) {
		if (isDead()) {
			changePhase(getPhase(), NotActive);
			break;
		}
		phase_change.from = getPhase();
		phase_change.to = phases[i];
		_m_phases_index = i;
		setPhase(PhaseNone);
		QVariant data = QVariant::fromValue(phase_change);
		ResolutionHistoryEventGuard phaseHistory(
			room->resolutionHistory(), QStringLiteral("phase"),
			QVariantMap{{QStringLiteral("player"), objectName()},
				{QStringLiteral("from_phase"), static_cast<int>(phase_change.from)},
				{QStringLiteral("phase"), static_cast<int>(phase_change.to)}},
			room->historyRecordingEnabled());
		try {
		bool skip = room->getThread()->trigger(EventPhaseChanging, room, this, data);
		room->flushHegemonyReveals();
		_m_phases_state[i].phase = phases[i] = data.value<PhaseChangeStruct>().to;
		phaseHistory.update(QVariantMap{{QStringLiteral("from_phase"), static_cast<int>(phase_change.from)},
			{QStringLiteral("phase"), static_cast<int>(phases[i])}});
		setPhase(phases[i]);
		room->broadcastProperty(this, "phase");
		if(phases[i] == NotActive){
			room->getThread()->trigger(EventPhaseStart, room, this, data);
			phaseHistory.finish(QStringLiteral("completed"));
			break;
		}
		if (skip || _m_phases_state[i].skipped != 0) {
			data = _m_phases_state[i].skipped < 0;
			if (!room->getThread()->trigger(EventPhaseSkipping, room, this, data)) {
				room->getThread()->trigger(EventPhaseSkipped, room, this, data);
				phaseHistory.finish(QStringLiteral("skipped"));
				//Sanguosha->playSystemAudioEffect("skip");
				continue;
			}
			data = QVariant::fromValue(phase_change);
		}
		// A TurnStart frame encloses all phases; it must not delay reveal rewards.
		const bool phaseEnded = room->getThread()->trigger(EventPhaseStart, room, this, data);
		room->flushHegemonyReveals();
		if (!phaseEnded) {
			room->getThread()->trigger(EventPhaseProceeding, room, this, data);
			room->flushHegemonyReveals();
		}
		room->getThread()->trigger(EventPhaseEnd, room, this, data);
		room->flushHegemonyReveals();
		phaseHistory.finish(QStringLiteral("completed"));/*
		if (phases[i] != NotActive && (skip || _m_phases_state[i].skipped != 0)) {
			data = QVariant::fromValue(_m_phases_state[i].skipped < 0);
			if (!room->getThread()->trigger(EventPhaseSkipping, room, this, data)) {
				room->getThread()->trigger(EventPhaseSkipped, room, this);
				continue;
			}
		}
		skip = room->getThread()->trigger(EventPhaseStart, room, this);
		if (getPhase() == NotActive) break;
		if (!skip) room->getThread()->trigger(EventPhaseProceeding, room, this);
		room->getThread()->trigger(EventPhaseEnd, room, this);*/
		} catch (TriggerEvent) {
			if (phaseHistory.id() != 0)
				room->getThread()->rememberInterruptedPhase(phaseHistory.id());
			throw;
		}
	}
}

QList<Player::Phase> &ServerPlayer::getPhases()
{
	return phases;
}

void ServerPlayer::skip(Phase phase, bool isCost)
{
	static QStringList phase_strings;
	if (phase_strings.isEmpty())
		phase_strings << "round_start" << "start" << "judge" << "draw" << "play" << "discard" << "finish" << "not_active";
	for (int i = _m_phases_index; i < _m_phases_state.size(); i++) {
		if (_m_phases_state[i].phase == phase) {
			if (_m_phases_state[i].skipped != 0) {
				if (isCost && _m_phases_state[i].skipped == 1)
					_m_phases_state[i].skipped = -1;
				return;
			}
			_m_phases_state[i].skipped = (isCost ? -1 : 1);
			LogMessage log;
			log.type = "#SkipPhase";
			log.from = this;
			log.arg = phase_strings.at(phase);
			room->sendLog(log);
			break;
		}
	}
}

void ServerPlayer::insertPhase(Phase phase)
{
	PhaseStruct _phase;
	_phase.phase = phase;
	_m_phases_state.insert(_m_phases_index+1, _phase);
	phases.insert(_m_phases_index+1, phase);
}

bool ServerPlayer::isSkipped(Phase phase) const
{
	for (int i = _m_phases_index; i < _m_phases_state.size(); i++) {
		if (_m_phases_state[i].phase == phase)
			return _m_phases_state[i].skipped != 0;
	}
	return false;
}

void ServerPlayer::gainMark(const QString &mark, int n)
{
	if (n == 0) return;
	int value = getMark(mark) + n;

	LogMessage log;
	log.type = "#GetMark";
	log.from = this;
	log.arg = mark;
	if (mark.startsWith("&"))
		log.arg = log.arg.mid(1);
	if (log.arg.contains("+"))
		log.arg = log.arg.split("+").first();
	log.arg2 = QString::number(n);
	room->sendLog(log);
	room->setPlayerMark(this, mark, value);
}

void ServerPlayer::loseMark(const QString &mark, int n)
{
	if (n == 0 || getMark(mark) == 0) return;
	int value = getMark(mark) - n;
	if (value < 0) {
		value = 0;
		n = getMark(mark);
	}

	QString new_mark = mark;
	if (mark.startsWith("&"))
		new_mark = new_mark.mid(1);
	if (new_mark.contains("+"))
		new_mark = new_mark.split("+").at(0);

	LogMessage log;
	log.type = "#LoseMark";
	log.from = this;
	log.arg = new_mark;
	log.arg2 = QString::number(n);
	room->sendLog(log);
	room->setPlayerMark(this, mark, value);
}

void ServerPlayer::loseAllMarks(const QString &mark_name)
{
	loseMark(mark_name, getMark(mark_name));
}

void ServerPlayer::removeCurrentClub(){
    if (hasClub()){
        QString club_name = getClubName();
        LogMessage log;
        log.type = "$quit_club";
        log.from = this;
        log.arg = club_name;
        room->sendLog(log);
        loseAllMarks("@amclub_" + club_name);
    }
}

void ServerPlayer::addClub(const QString &club_name){
    removeCurrentClub();
    LogMessage log;
    log.type = "$join_club";
    log.from = this;
    log.arg = club_name;
    room->sendLog(log);
    gainMark("@amclub_" + club_name);
}

void ServerPlayer::gainHujia(int n, int max_num)
{
	if (n <= 0) return;

	if (max_num > 0) {
		int hujia = getHujia();
		if (hujia >= max_num) return;
		n = qMin(n,max_num-hujia);
	}

	QVariant data = n;
	if (room->getThread()->trigger(GainHujia, room, this, data)) return;

	n = data.toInt();
	if (n <= 0) return;

	LogMessage log;
	log.type = "#GetHujia";
	log.from = this;
	log.arg = QString::number(n);

	room->sendLog(log);
	room->setPlayerMark(this, "@HuJia", getHujia()+n);

	room->getThread()->trigger(GainedHujia, room, this, data);
}

void ServerPlayer::loseHujia(int n)
{
	loseHujia(n, std::function<void(int)>());
}

void ServerPlayer::loseHujia(int n, const std::function<void(int)> &afterMutation)
{
	if (n<1||getHujia()<1) return;

	QVariant data = n;
	if (room->getThread()->trigger(LoseHujia, room, this, data)) return;

	n = data.toInt();
	if (n <= 0) return;

	n = qMin(n,getHujia());

	LogMessage log;
	log.type = "#LoseHuJia";
	log.from = this;
	log.arg = QString::number(n);

	room->sendLog(log);
	room->setPlayerMark(this, "@HuJia", getHujia()-n);
	if (afterMutation)
		afterMutation(n);

	room->getThread()->trigger(LostHujia, room, this, data);
}

void ServerPlayer::loseAllHujias()
{
	loseHujia(getHujia());
}

void ServerPlayer::addSkill(const QString &skill_name)
{
    if(room->getMode() == "03_1v1"){
        const Skill *skill = Sanguosha->getMainSkill(skill_name);
        if (skill && skill->isLordSkill()) return;
    }

    Player::addSkill(skill_name);
    JsonArray args;
    args << (int)QSanProtocol::S_GAME_EVENT_ADD_SKILL << objectName() << skill_name;
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    refreshUIState();
}

void ServerPlayer::addSkill(const QString &skill_name, bool head_skill)
{
    if(room->getMode() == "03_1v1"){
        const Skill *skill = Sanguosha->getMainSkill(skill_name);
        if (skill && skill->isLordSkill()) return;
    }

    Player::addSkill(skill_name, head_skill);
    JsonArray args;
    args << (int)QSanProtocol::S_GAME_EVENT_ADD_SKILL << objectName() << skill_name << head_skill;
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    refreshUIState();
}

void ServerPlayer::loseSkill(const QString &skill_name)
{
    Player::loseSkill(skill_name);
    JsonArray args;
    args << (int)QSanProtocol::S_GAME_EVENT_LOSE_SKILL << objectName() << skill_name;
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    refreshUIState();
}

void ServerPlayer::loseSkill(const QString &skill_name, bool head)
{
    Player::loseSkill(skill_name, head);
    JsonArray args;
    args << (int)QSanProtocol::S_GAME_EVENT_LOSE_SKILL << objectName() << skill_name << head;
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    refreshUIState();
}

void ServerPlayer::setGender(General::Gender gender)
{
	if (gender == getGender())
		return;
	Player::setGender(gender);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_CHANGE_GENDER << objectName() << (int)gender;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

bool ServerPlayer::isOnline() const
{
	return getState() == "online";
}

void ServerPlayer::setAI(AI *ai)
{
	this->ai = ai;
}

AI *ServerPlayer::getAI() const
{
	if (room != nullptr) {
		ServerPlayer *actualController = room->getActualController(const_cast<ServerPlayer *>(this));
		if (actualController != nullptr && actualController != this && actualController->isOnline())
			return nullptr;
	}

	if (getState() == "online")
		return nullptr;

	if (onsole_owner == nullptr || onsole_owner->getState() == "online")
		return nullptr;

	if (ai != nullptr)
		return ai;
	return trust_ai;
}

AI *ServerPlayer::getSmartAI() const
{
	return ai;
}

void ServerPlayer::addVictim(ServerPlayer *victim)
{
	victims.append(victim);
}

QList<ServerPlayer *> ServerPlayer::getVictims() const
{
	return victims;
}

void ServerPlayer::setNext(ServerPlayer *next)
{
	this->next = next;
}

ServerPlayer *ServerPlayer::getNext() const
{
	return next;
}

ServerPlayer *ServerPlayer::getNextAlive(int n) const
{
    ServerPlayer *next = const_cast<ServerPlayer *>(this);
    if (room->getAlivePlayers().length()<2) return next;
    for (int i = 0; i < n; i++) {
        do next = next->next; while (next->isDead() || (next != this && next->isRemoved()));
    }
    return next;
}

ServerPlayer *ServerPlayer::getNextGamePlayer(int n) const
{
	ServerPlayer *next = const_cast<ServerPlayer *>(this);
	if (room->getAlivePlayers().length()<2) {
		do {
			next = next->next;
		} while (next->isDead());
		return next;
	}
	for (int i = 0; i < n; i++) {
		do next = next->next; while (next->isDead());
	}
	return next;
}

ServerPlayer *ServerPlayer::getPreviousAlive(int n) const
{
    int count = aliveCount();
    if (count < 2) return const_cast<ServerPlayer *>(this);
    return getNextAlive(count - n);
}

ServerPlayer *ServerPlayer::getLastAlive(int n) const
{
    ServerPlayer *p = const_cast<ServerPlayer *>(this);
    for (int i = 0; i < n; ++i) {
        p = getPreviousAlive(1);
    }
    return p;
}


int ServerPlayer::getGeneralMaxHp() const
{
	if (Config.EnableHegemony && getActualGeneral1() && getActualGeneral2())
		return qMax((getActualGeneral1()->getMaxHpHead() + getActualGeneral2()->getMaxHpDeputy()) / 2, 1);

	int max_hp = getGeneral()->getMaxHp();

	if (getGeneral2()){
		int plan = Config.MaxHpScheme;
		if (Config.GameMode.mode_id.contains("_mini_") || Config.GameMode.mode_id == "custom_scenario") plan = 1;
		int second = getGeneral2()->getMaxHp();

		switch (plan) {
		case 3: max_hp = (max_hp + second) / 2; break;
		case 2: max_hp = qMax(max_hp, second); break;
		case 1: max_hp = qMin(max_hp, second); break;
		default:
			max_hp += second - Config.Scheme0Subtraction; break;
		}
		max_hp = qMax(max_hp, 1);
	}

	if (room->hasWelfare(this))
		max_hp++;

	return max_hp;
}

int ServerPlayer::getGeneralStartHp() const
{
	if (Config.EnableHegemony && getActualGeneral1() && getActualGeneral2())
		return getGeneralMaxHp();

	int start_hp = getGeneral()->getStartHp();

	if (getGeneral2()){
		int plan = Config.MaxHpScheme;
		if (Config.GameMode.mode_id.contains("_mini_") || Config.GameMode.mode_id == "custom_scenario") plan = 1;
		int second = getGeneral2()->getStartHp();

		switch (plan) {
		case 3: start_hp = (start_hp + second) / 2; break;
		case 2: start_hp = qMax(start_hp, second); break;
		case 1: start_hp = qMin(start_hp, second); break;
		default:
			start_hp += second - Config.Scheme0Subtraction; break;
		}

		start_hp = qMax(start_hp, 1);
	}

	if (room->hasWelfare(this))
		start_hp++;

	return start_hp;
}

int ServerPlayer::getGeneralStartHujia() const
{
	if (Config.EnableHegemony && getActualGeneral1() && getActualGeneral2())
		return getActualGeneral1()->getStartHujia() + getActualGeneral2()->getStartHujia();
	int start_hujia = getGeneral()->getStartHujia();
	if (getGeneral2())
		start_hujia += getGeneral2()->getStartHujia();
	return start_hujia;
}

QString ServerPlayer::getGameMode() const
{
	return room->getMode();
}

QString ServerPlayer::getIp() const
{
	if (socket)
		return socket->peerAddress();
	return "";
}

void ServerPlayer::introduceTo(ServerPlayer *player)
{
	QVariantMap introduce_str{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("player_name"), objectName()},
		{QStringLiteral("screen_name"), screenName()},
		{QStringLiteral("avatar"), property("avatar").toString()}};

	if (player)
		room->doNotify(player, S_COMMAND_ADD_PLAYER, introduce_str);
	else {
		QList<ServerPlayer *> players = room->getPlayers();
		players.removeOne(this);
		room->doBroadcastNotify(players, S_COMMAND_ADD_PLAYER, introduce_str);
	}
}

void ServerPlayer::marshal(ServerPlayer *player) const
{
	room->notifyProperty(player, this, "maxhp");
	room->notifyProperty(player, this, "hp");
	room->notifyProperty(player, this, "gender");
	room->notifyProperty(player, this, "player_seat");
	room->syncRole(player, this);
	room->notifyProperty(player, this, "general_showed");
	room->notifyProperty(player, this, "general2_showed");
    room->notifyProperty(player, this, "disable_show");
	if (player == this) {
		room->notifyProperty(player, this, "actual_general1");
		room->notifyProperty(player, this, "actual_general2");
		room->notifyProperty(player, this, "hegemony_generals");
        room->notifyProperty(player, this, "hegemony_kingdom");
	}

	//if (getKingdom() != getGeneral()->getKingdom())
		room->notifyProperty(player, this, "kingdom");

	if (isAlive()) {
		room->notifyProperty(player, this, "seat");
		if (getPhase() != NotActive)
			room->notifyProperty(player, this, "phase");
	} else if (!isRest()) {
		room->notifyProperty(player, this, "alive");
		room->doNotify(player, S_COMMAND_KILL_PLAYER, objectName());
	}

	if (!faceUp())
		room->notifyProperty(player, this, "faceup");

	if (isChained())
		room->notifyProperty(player, this, "chained");

	foreach(const QByteArray &property_name, propertys) {
		if (property_name == "role" || property_name == "role_shown"
			|| property_name == "general_pile_changed") continue;
		room->notifyProperty(player, this, property_name.constData());
	}

	// Restore each viewer's general-pile projection, never the private soul names.
	for (auto it = general_piles.cbegin(); it != general_piles.cend(); ++it) {
		QStringList names = it.value();
		if (player != this && !generalPileOpen(it.key(), player->objectName()))
			for (QString &name : names) name = QStringLiteral("unknown");
		const QVariantMap state{{"schema_version", 1}, {"action", "general_pile"},
			{"player_name", objectName()}, {"pile_name", it.key()},
			{"general_names", names}, {"add", true}};
		room->doNotify(player, S_COMMAND_SET_PROPERTY, state);
	}

	room->notifyPlayerUIState(player, this, m_uiState);

	QList<CardsMoveStruct> moves;

	CardsMoveStruct move;
	move.to = (Player*)this;
	move.to_player_name = objectName();
	move.from_place = DrawPile;
	move.card_ids = handCards();
	if (move.card_ids.length()>0) {
		if (player == this) {
			foreach (int id, move.card_ids) {
				WrappedCard *wrapped = qobject_cast<WrappedCard *>(Sanguosha->getCard(id));
				if (wrapped&&wrapped->isModified())
					room->notifyUpdateCard(player, id, wrapped);
			}
		}
		move.to_place = PlaceHand;
		moves << move;
	}

	move.card_ids = getEquipsId();
	if (move.card_ids.length()>0) {
		foreach (int id, move.card_ids) {
			WrappedCard *wrapped = qobject_cast<WrappedCard *>(Sanguosha->getCard(id));
			if (wrapped&&wrapped->isModified())
				room->notifyUpdateCard(player, id, wrapped);
		}
		move.to_place = PlaceEquip;
		moves << move;
	}

	move.card_ids = getJudgingAreaID();
	if (move.card_ids.length()>0) {
		move.to_place = PlaceDelayedTrick;
		moves << move;
	}

	if (piles.keys().length()>0) {
		move.to_place = PlaceSpecial;
		foreach (QString pile, piles.keys()) {
			move.card_ids = piles[pile];
			move.to_pile_name = pile;
			moves << move;
		}
	}

	for (int i = 0; i < 5; i++) {
		JsonArray arg;
		arg << objectName() << i << getEquipArea(i);
		room->doNotify(player, S_COMMAND_SET_EQUIP_AREA_COUNT, arg);
	}

	room->notifyProperty(player, this, "hasjudgearea");

	if (moves.length()>0) {
		QList<ServerPlayer*> players;
		players << player;
		room->notifyMoveCards(true, moves, false, players);
		room->notifyMoveCards(false, moves, false, players);
	}

	foreach (QString mark_name, marks.keys()) {
		if (mark_name.startsWith("@") || mark_name.startsWith("&")) {
            if (Config.EnableHegemony && player != this) {
                bool limited = false;
                const bool visible = canSeeHegemonyLimitMark(this, player, mark_name, &limited);
                if (limited ? !visible : !room->isAIMarkVisibleTo(this, mark_name, player))
                    continue;
            }
			JsonArray arg;
			arg << objectName() << mark_name << getMark(mark_name);
			room->doNotify(player, S_COMMAND_SET_MARK, arg);
		}
	}

	foreach (const Skill *skill, getVisibleSkillList(true)) {
        if (Config.EnableHegemony && player != this) {
            bool visible = false;
            for (const SkillInstance &instance : getSkillInstances()) {
                if (instance.skillName == skill->objectName()
                    && SkillRuntimeCoordinator::canReceiveSkillInstance(*room, player, this, instance)) {
                    visible = true;
                    break;
                }
            }
            if (!visible) continue;
        }
		JsonArray args1;
		args1 << S_GAME_EVENT_ACQUIRE_SKILL << objectName() << skill->objectName();
		room->doNotify(player, S_COMMAND_LOG_EVENT, args1);
	}

	foreach(QString flag, flags)
		room->notifyProperty(player, this, "flags", flag);

	foreach (QString item, history.keys()) {
		JsonArray arg;
		arg << objectName() << item << history.value(item);
		room->doNotify(player, S_COMMAND_ADD_HISTORY, arg);
	}

	/*if (hasShownRole())
		room->notifyProperty(player, this, "role");*/
}

void ServerPlayer::addToPile(const QString &pile_name, const Card *card, bool open, QList<ServerPlayer *> open_players)
{
	QList<int> card_ids;
	if (card->isVirtualCard())
		card_ids = card->getSubcards();
	else card_ids << card->getId();
	return addToPile(pile_name, card_ids, open, open_players);
}

void ServerPlayer::addToPile(const QString &pile_name, int card_id, bool open, QList<ServerPlayer *> open_players)
{
	return addToPile(pile_name, QList<int>()<<card_id, open, open_players);
}

void ServerPlayer::addToPile(const QString &pile_name, QList<int> card_ids, bool open, QList<ServerPlayer *> open_players)
{
	return addToPile(pile_name, card_ids, open, open_players, CardMoveReason());
}

void ServerPlayer::addToPile(const QString &pile_name, QList<int> card_ids,
	bool open, QList<ServerPlayer *> open_players, CardMoveReason reason)
{
	if(card_ids.isEmpty()) return;
	if(open)
		open_players = room->getAlivePlayers();
	else{
		setPileOpen(pile_name, ".");
		if (open_players.isEmpty()) {
			foreach (int id, card_ids) {
				ServerPlayer *owner = room->getCardOwner(id);
				if (owner && !open_players.contains(owner))
					open_players << owner;
			}
		}
	}
	foreach(ServerPlayer *p, open_players)
		setPileOpen(pile_name, p->objectName());
	piles[pile_name].append(card_ids);

	CardsMoveStruct move;
	move.card_ids = card_ids;
	move.to = this;
	move.to_place = PlaceSpecial;
	move.reason = reason;
	move.to_pile_name = pile_name;
	room->moveCardsAtomic(move, open);
}

void ServerPlayer::addToRenPile(const Card *card, const QString &skill_name)
{
	QList<int> card_ids;
	if (card->isVirtualCard())
		card_ids = card->getSubcards();
	else card_ids << card->getId();
	return addToRenPile(card_ids, skill_name);
}

void ServerPlayer::addToRenPile(int card_id, const QString &skill_name)
{
	return addToRenPile(QList<int>()<<card_id, skill_name);
}

void ServerPlayer::addToRenPile(QList<int> card_ids, const QString &skill_name)
{
	if(card_ids.isEmpty()) return;

	CardsMoveStruct move1;
	move1.to_place = DiscardPile;
	move1.from_pile_name = "ren_pile";
	move1.reason = CardMoveReason(CardMoveReason::S_REASON_RULEDISCARD, objectName(),skill_name,"removeRenPile");
	QVariantList ren = room->getTag("ren_pile").toList();
	foreach (int id, card_ids) {
		if(ren.length()>=6){
			move1.card_ids << ren.takeFirst().toInt();
		}
		ren << id;
	}
	room->setTag("ren_pile",ren);
	CardsMoveStruct move2;
	move2.card_ids = card_ids;
	move2.to_place = PlaceTable;
	move2.reason = CardMoveReason(CardMoveReason::S_REASON_RECYCLE, objectName(),skill_name,"addRenPile");
	move2.to_pile_name = "ren_pile";
	QList<CardsMoveStruct> moves;
	moves << move1 << move2;
	room->moveCardsAtomic(moves, true);
}

void ServerPlayer::exchangeFreelyFromPrivatePile(const QString &skill_name, const QString &pile_name, int upperlimit, bool include_equip)
{
	QList<int> pile = getPile(pile_name);
	if (pile.isEmpty()) return;

	QString tempMovingFlag = QString("%1_InTempMoving").arg(skill_name);
	room->setPlayerFlag(this, tempMovingFlag);

	int ai_delay = Config.AIDelay;
	Config.AIDelay = 0;

	QList<int> will_to_handcard;
	room->fillAG(pile, this);
	while (!pile.isEmpty()) {
		int card_id = room->askForAG(this, pile, true, skill_name);
		if (card_id == -1) break;
		room->takeAG(this,card_id,false,QList<ServerPlayer*>()<<this);

		pile.removeOne(card_id);
		will_to_handcard << card_id;
		if (pile.length() >= upperlimit) break;
	}
	room->clearAG(this);
	Config.AIDelay = ai_delay;

	int n = will_to_handcard.length();
	if (n == 0) return;
	room->obtainCard(this, dummyCard(will_to_handcard), CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, objectName()), false);
	const Card *exchange_card = room->askForExchange(this, skill_name, n, n, include_equip);

	QList<int> duplicate, will_to_handcard_x = will_to_handcard, will_to_pile_x = exchange_card->getSubcards();
	foreach (int id, exchange_card->getSubcards()) {
		if (will_to_handcard_x.contains(id)) {
			will_to_handcard_x.removeOne(id);
			will_to_pile_x.removeOne(id);
			duplicate << id;
			n--;
		}
	}

	if (n == 0) {
		addToPile(pile_name, exchange_card->getSubcards(), false);
		room->setPlayerFlag(this, "-" + tempMovingFlag);
		return;
	}

	LogMessage log;
	log.type = "#QixingExchange";
	log.from = this;
	log.arg = QString::number(n);
	log.arg2 = skill_name;
	room->sendLog(log);

	addToPile(pile_name, duplicate, false);
	room->setPlayerFlag(this, "-" + tempMovingFlag);
	addToPile(pile_name, will_to_pile_x, false);

	room->setPlayerFlag(this, tempMovingFlag);
	addToPile(pile_name, will_to_handcard_x, false);
	room->setPlayerFlag(this, "-" + tempMovingFlag);

	DummyCard *dummy = new DummyCard(will_to_handcard_x);
	room->obtainCard(this, dummy, CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, objectName()), false);
	dummy->deleteLater();
}

void ServerPlayer::clearOneGeneralPile(const QString &pile_name)
{
	if (!general_piles.contains(pile_name)) return;
	const QStringList removedNames = general_piles.value(pile_name);
	const QStringList viewers = general_pile_open.value(pile_name);

	general_piles.remove(pile_name);
	general_pile_open.remove(pile_name);

	QVariantMap data;
	data["pile_name"] = pile_name;
	data["general_names"] = removedNames;
	data["open_players"] = viewers;
	data["add"] = false;

	QJsonDocument doc = QJsonDocument::fromVariant(data);
	room->broadcastProperty(this, "general_pile_changed", doc.toJson(QJsonDocument::Compact));
}

void ServerPlayer::clearGeneralPiles()
{
	foreach(QString pile_name, general_piles.keys())
		clearOneGeneralPile(pile_name);
	general_piles.clear();
	general_pile_open.clear();
}

void ServerPlayer::addGeneralToPile(const QString &pile_name, const QString &general_name, bool open, QList<ServerPlayer *> open_players)
{
	QStringList general_names;
	general_names << general_name;
	return addGeneralToPile(pile_name, general_names, open, open_players);
}

void ServerPlayer::addGeneralToPile(const QString &pile_name, const QStringList &general_names, bool open, QList<ServerPlayer *> open_players)
{
	if (general_names.isEmpty()) return;

	if (open)
		open_players = room->getAlivePlayers();
	else {
		setGeneralPileOpen(pile_name, ".");
		if (open_players.isEmpty()) {
			open_players << this;
		}
	}

	foreach(ServerPlayer *p, open_players)
		setGeneralPileOpen(pile_name, p->objectName());

	general_piles[pile_name].append(general_names);

	QVariantMap data;
	data["pile_name"] = pile_name;
	data["general_names"] = general_names;
	data["add"] = true;

	QStringList open_player_names;
	foreach(ServerPlayer *p, open_players)
		open_player_names << p->objectName();
	data["open_players"] = open_player_names;

	QJsonDocument doc = QJsonDocument::fromVariant(data);
	room->broadcastProperty(this, "general_pile_changed", doc.toJson(QJsonDocument::Compact));
}

void ServerPlayer::removeGeneralFromPile(const QString &pile_name, const QString &general_name)
{
	if (!general_piles.value(pile_name).contains(general_name)) return;
	const QStringList viewers = general_pile_open.value(pile_name);

	general_piles[pile_name].removeOne(general_name);

	if (general_piles[pile_name].isEmpty()) {
		general_piles.remove(pile_name);
		general_pile_open.remove(pile_name);
	}

	QVariantMap data;
	data["pile_name"] = pile_name;
	data["general_names"] = QStringList() << general_name;
	data["open_players"] = viewers;
	data["add"] = false;

	QJsonDocument doc = QJsonDocument::fromVariant(data);
	room->broadcastProperty(this, "general_pile_changed", doc.toJson(QJsonDocument::Compact));
}

void ServerPlayer::gainAnExtraTurn(QList<Phase> phases)
{
	room->executeExtraTurn(this, phases, QString(), SkillInstanceRef());
}

void ServerPlayer::copyFrom(ServerPlayer *sp)
{
	Player::copyFrom(sp);
	//handcards = QList<const Card *>(sp->handcards);
	phases = QList<ServerPlayer::Phase>(sp->phases);
	selected = QStringList(sp->selected);
}

bool ServerPlayer::CompareByActionOrder(ServerPlayer *a, ServerPlayer *b)
{
	return a->getRoom()->getFront(a, b) == a;
}

void ServerPlayer::syncEquipAreaCount(int i)
{
	if (i < 0 || i > 4)
		return;
	JsonArray arg;
	arg << objectName() << i << getEquipArea(i);
	room->doBroadcastNotify(S_COMMAND_SET_EQUIP_AREA_COUNT, arg);
}

void ServerPlayer::throwEquipArea(int i)
{
	throwEquipArea(QList<int>() << i);
}

void ServerPlayer::throwEquipArea(QList<int> list)
{
	QList<int>ids;
	QVariantList newlist;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	foreach (int i, list) {
		if (i<0||i>4) continue;
		if (hasEquipArea(i)) {
			setEquipArea(i, false);
			if (getEquip(i)) ids << getEquip(i)->getId();
			syncEquipAreaCount(i);
			room->broadcastProperty(this, areas[i]);
			newlist << i;

			LogMessage log;
			log.type = "#ThrowArea";
			log.from = this;
			log.arg = areas[i];
			room->sendLog(log);
		}
	}
	if (newlist.isEmpty()) return;
	room->throwCard(ids, CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
	QVariant data = newlist;
	room->getThread()->trigger(ThrowEquipArea, room, this, data);
}

void ServerPlayer::throwEquipArea()
{
	QVariantList list;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	for (int i = 0; i < 5; i++) {
		for (int n = 0; n < getEquipArea(i); n++) {
			setEquipArea(i, false);
			list << i;
		}
		syncEquipAreaCount(i);
		room->broadcastProperty(this, areas[i]);
	}
	if (list.isEmpty()) return;
	LogMessage log;
	log.type = "#ThrowArea";
	log.from = this;
	log.arg = "equip_area";
	room->sendLog(log);
	room->throwCard(getEquipsId(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
	QVariant data = list;
	room->getThread()->trigger(ThrowEquipArea, room, this, data);
}


void ServerPlayer::addEquipArea(int i)
{
	Player::addEquipArea(i);
	syncEquipAreaCount(i);
}

void ServerPlayer::obtainEquipArea(int i)
{
	obtainEquipArea(QList<int>() << i);
}

void ServerPlayer::obtainEquipArea(QList<int> list)
{
	QVariantList newlist;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	foreach (int i, list) {
		if (i<0||i>4||hasEquipArea(i)) continue;
		setEquipArea(i, true);
		syncEquipAreaCount(i);
		room->broadcastProperty(this, areas[i]);
		newlist << i;

		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = areas[i];
		room->sendLog(log);
	}
	if (newlist.isEmpty()) return;
	QVariant data = newlist;
	room->getThread()->trigger(ObtainEquipArea, room, this, data);
}

void ServerPlayer::obtainEquipArea()
{
	QVariantList list;
	static QList<const char*> areas;
	if(areas.isEmpty())
		areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	for (int i = 0; i < 5; i++) {
		if (hasEquipArea(i)) continue;
		setEquipArea(i, true);
		syncEquipAreaCount(i);
		room->broadcastProperty(this, areas[i]);
		list << i;
	}
	if (!list.isEmpty()) {
		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = "equip_area";
		room->sendLog(log);
		QVariant data = list;
		room->getThread()->trigger(ObtainEquipArea, room, this, data);
	}
}

void ServerPlayer::throwJudgeArea()
{
	if (hasJudgeArea()) {
		setJudgeArea(false);
		room->broadcastProperty(this, "hasjudgearea");

		LogMessage log;
		log.type = "#ThrowArea";
		log.from = this;
		log.arg = "judge_area";
		room->sendLog(log);

		room->throwCard(getJudgingAreaID(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
		room->getThread()->trigger(ThrowJudgeArea, room, this);
	}
}

void ServerPlayer::obtainJudgeArea()
{
	if (!hasJudgeArea()) {
		setJudgeArea(true);
		room->broadcastProperty(this, "hasjudgearea");
		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = "judge_area";
		room->sendLog(log);
		room->getThread()->trigger(ObtainJudgeArea, room, this);
	}
}

ServerPlayer *ServerPlayer::getSaver() const
{
	QStringList list = property("MyDyingSaver").toStringList();
	if (list.isEmpty()) return nullptr;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->objectName() == list.first())
			return p;
	}
	return nullptr;
}

bool ServerPlayer::isLowestHpPlayer(bool only)
{
	int hp = getHp();
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getHp() < hp || (only && p->getHp() <= hp))
			return false;
	}
	return true;
}

void ServerPlayer::ViewAsEquip(const QString &equip_name, bool can_duplication)
{
	if (equip_name.isEmpty()) return;
	QStringList equips = property("View_As_Equips_List").toString().split("+");
	if (!can_duplication && equips.contains(equip_name)) return;
	equips << equip_name;
	room->setPlayerProperty(this, "View_As_Equips_List", equips.join("+"));
	const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(equip_name);
	if (vsSkill) room->attachSkillToPlayer(this,equip_name);
	//else room->acquireSkill(this,equip_name,true,true,false);
}

void ServerPlayer::removeViewAsEquip(const QString &equip_name, bool all_duplication)
{
	if (equip_name.length()<2)
		room->setPlayerProperty(this, "View_As_Equips_List", "");
	else{
		QStringList equips = property("View_As_Equips_List").toString().split("+");
		if (!equips.contains(equip_name)) return;
		if (all_duplication) equips.removeAll(equip_name);
		else equips.removeOne(equip_name);
		room->setPlayerProperty(this, "View_As_Equips_List", equips.join("+"));
		if (!equips.contains(equip_name)){
			const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(equip_name);
			if (vsSkill) room->detachSkillFromPlayer(this,equip_name,true,true);
		}
	}
}

bool ServerPlayer::canUse(const Card *card, QList<ServerPlayer *> players, bool)
{
	if(isCardLimited(card,Card::MethodUse)) return false;
	if(players.isEmpty()) players = room->getAlivePlayers();
	addMark("&xiyan1-Clear");
	foreach (ServerPlayer *p, players) {
		int maxVotes = 0;
		if (card->targetFilter(QList<const Player *>(),p,this,maxVotes)||maxVotes>0){
			removeMark("&xiyan1-Clear");
			return true;
		}
	}
	removeMark("&xiyan1-Clear");
	return false;
}

bool ServerPlayer::canUse(const Card *card, ServerPlayer *player, bool player_must_be_target)
{
	if (player==nullptr) return canUse(card);
	return canUse(card, QList<ServerPlayer *>() << player, player_must_be_target);
}

void ServerPlayer::endPlayPhase(bool sendLog)
{
	if (getPhase() != Play || isDead()) return;
	if (hasFlag("Global_PlayPhaseTerminated")) return;
	if (sendLog) {
		LogMessage log;
		log.type = "#EndPlayPhase";
		log.from = this;
		log.arg = "play";
		room->sendLog(log);
	}
	room->setPlayerFlag(this, "Global_PlayPhaseTerminated");
}

void ServerPlayer::breakYinniState()
{
	QStringList names;
	if (getGeneralName() == "yinni_hide") {
		QString generalname = property("yinni_general").toString();
		if (!generalname.isEmpty()) names << generalname;
	}
	QString general2name = property("yinni_general2").toString();
	if (!general2name.isEmpty()&&getGeneral2Name() == "yinni_hide")
		names << general2name;
	if (names.isEmpty()) return;
	room->setPlayerProperty(this, "yinni_general", "");
	room->setPlayerProperty(this, "yinni_general2", "");

	LogMessage log;
	log.from = this;
	log.arg = names.first();
	log.type = "#BreakYinniState";
	if (names.length()>1) {
		log.type = "#BreakYinniState2";
		log.arg2 = names.last();
		room->sendLog(log);
		room->changeHero(this, log.arg, false, false, false, false);
		room->changeHero(this, log.arg2, false, false, true, false);
	} else{
		room->sendLog(log);
		room->changeHero(this, log.arg, false, false, log.arg==general2name, false);
	}

	Player::setMaxHp(getGeneralMaxHp());
	Player::setHp(getGeneralStartHp());
	room->broadcastProperty(this, "maxhp");
	room->broadcastProperty(this, "hp");

	room->getThread()->trigger(Appear, room, this);
}

void ServerPlayer::enterYinniState(int type)
{
	if(type>=0){
		room->setPlayerProperty(this, "yinni_general", getGeneralName());
		room->setPlayerProperty(this, "yinni_general_kingdom", getKingdom());
	}
	if (type > 0) {  //只变主将
		room->changeHero(this, "yinni_hide", false, false, false, false);
		return;
	} else if (type == 0)  //主将、副将都变
		room->changeHero(this, "yinni_hide", true, false, false, false);
	if(getGeneral2()){  //只变副将
		room->setPlayerProperty(this, "yinni_general2", getGeneral2Name());
		room->changeHero(this, "yinni_hide", false, false, true, false);
	}
}

	int ServerPlayer::getDerivativeCard(const QString &card_name, Place place, bool visible) const
{
	foreach (int id, Sanguosha->getRandomCards(true)) {
		const Card *card = Sanguosha->getEngineCard(id);
		if (card->objectName() != card_name || room->getCardOwner(id)) continue;
		if (place == PlaceTable) return id;
		CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, objectName());
		QList<CardsMoveStruct> moves;
		if(place == PlaceEquip){
			if(card->isKindOf("EquipCard")){
				const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
				QList<int> occupy_slots = equip->getOccupyLocations();
				foreach(int slot, occupy_slots){
					if(!hasEquipArea(slot)) return id;
				}
				foreach(int slot, occupy_slots){
					const Card *exEquip = getEquip(slot);
					if(exEquip){
						reason.m_reason = CardMoveReason::S_REASON_PUT;
						moves << CardsMoveStruct(exEquip->getId(), nullptr, DiscardPile, reason);
					}
				}
			}else
				return id;
		}
		moves << CardsMoveStruct(id, (Player *)this, place, reason);
		room->moveCardsAtomic(moves, visible);
		return id;
	}
	return -1;
}

void ServerPlayer::setCanWake(const QString &skill_name, const QString &waked_skill_name)
{
	QStringList names = getTag(waked_skill_name + "_SKILLCANWAKE").toStringList();
	if (names.contains(skill_name)) return;
	names << skill_name;
	setTag(waked_skill_name + "_SKILLCANWAKE", QVariant(names));
	room->setPlayerMark(this, "&" + skill_name + "+:+" + waked_skill_name, 1);
}

bool ServerPlayer::canWake(const QString &waked_skill_name)
{
	QStringList names = getTag(waked_skill_name + "_SKILLCANWAKE").toStringList();
	if (names.isEmpty()) return false;
	removeTag(waked_skill_name + "_SKILLCANWAKE");

	LogMessage log;
	log.type = "#WakeSkillCanWake";
	log.from = this;
	log.arg = names.first();
	log.arg2 = waked_skill_name;
	room->sendLog(log);
	//room->notifySkillInvoked(this, waked_skill_name);
	//room->broadcastSkillInvoke(waked_skill_name);

	foreach (QString skill_name, names)
		room->setPlayerMark(this, "&" + skill_name + "+:+" + waked_skill_name, 0);
	return true;
}

const Card *ServerPlayer::askForUseCard(const QString &pattern, const QString &prompt, ServerPlayer *who, const Card *whocard, QString flag)
{
	CardUseStruct card_use = room->askForUseCardStruct(this, pattern, prompt, -1, Card::MethodUse, true, who, whocard, flag);
	if (card_use.card) return card_use.card;
	return nullptr;
}

const Card *ServerPlayer::askForUseCard(const QString &pattern, const QString &prompt, bool addHistory, ServerPlayer *who, const Card *whocard, QString flag)
{
	CardUseStruct card_use = room->askForUseCardStruct(this, pattern, prompt, -1, Card::MethodUse, addHistory, who, whocard, flag);
	if (card_use.card) return card_use.card;
	return nullptr;
}

const Card *ServerPlayer::askForResponseCard(const QString &pattern, const QString &prompt, const QVariant &data, ServerPlayer *who, const Card *m_toCard)
{
	return room->askForCard(this, pattern, prompt, data, Card::MethodResponse, who, false, "", false, m_toCard);
}

const Card *ServerPlayer::askForResponseCard(const QString &pattern, const QString &prompt, const QVariant &data, ServerPlayer *who, bool isProvision, const Card *m_toCard)
{
	return room->askForCard(this, pattern, prompt, data, Card::MethodResponse, who, false, "", isProvision, m_toCard);
}

QList<ServerPlayer *> ServerPlayer::assignmentCards(QList<int> &cards, const QString &prompt, QList<ServerPlayer *> players, int max_num, int min_num, bool visible)
{
	if (max_num<0) max_num = cards.length();
	if (players.isEmpty()) players = room->getAlivePlayers();
	QList<int> ids,ids2;
	foreach(int id, cards){
		if (!hasCard(id))
			ids << id;
	}
	QList<CardsMoveStruct> moves,_moves;
	QList<ServerPlayer *> _guojia,tos;
	_guojia.append(this);
	if (!ids.isEmpty()){
		foreach (int id, ids)
			_moves << CardsMoveStruct(id,room->getCardOwner(id),this,PlaceTable,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	int n = 0;
	QString prompt1 = prompt, prompt2;
	if (prompt.contains("|")){
		prompt1 = prompt.split("|").first();
		prompt2 = prompt.split("|").last();
	}
	while (isAlive()&&n<max_num) {
		bool optional = n>=min_num;
		if (min_num<0&&n>0) optional = false;
		CardsMoveStruct yiji = room->askForYijiStruct(this, cards, prompt1,
			true, visible, optional||players.contains(this), max_num-n, players, CardMoveReason(), prompt2, false, false);
		if (yiji.card_ids.isEmpty()) break;
		ServerPlayer *to = (ServerPlayer *)yiji.to;
		if(!tos.contains(to)) tos << to;
		moves.append(yiji);
		QList<int> ids3;
		foreach (int id, yiji.card_ids) {
			if (hasCard(id)) ids2.append(id);
			ids.removeOne(id);
			ids3.append(id);
			n++;
		}
		if (ids3.isEmpty()) continue;
		_moves.clear();
		_moves << CardsMoveStruct(ids3,this,nullptr,PlaceHand,PlaceTable,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	while (min_num>n&&cards.length()>0) {
		int id = cards.at(qsanRandomBounded(cards.length()));
		ServerPlayer *to = players.at(qsanRandomBounded(players.length()));
		if(players.contains(this)) to = this;
		if(!tos.contains(to)) tos << to;
		moves << CardsMoveStruct(id,to,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_GIVE,objectName(),to->objectName(),prompt1.split("=").first(),""));
		cards.removeOne(id);
		n++;
	}
	_moves.clear();
	if (!ids2.isEmpty())
		_moves << CardsMoveStruct(ids2,nullptr,this,PlaceTable,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
	if (!ids.isEmpty())
		_moves << CardsMoveStruct(ids,this,nullptr,PlaceHand,PlaceTable,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
	if (!_moves.isEmpty()){
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	room->moveCardsAtomic(moves, visible);
	return tos;
}

void ServerPlayer::skillInvoked(const QString &skill_name, int type, ServerPlayer *owner)
{
	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = this;
	log.arg = skill_name;
	if(owner){
		log.type = "#InvokeOthersSkill";
		log.to << owner;
	}else
		owner = this;
	room->sendLog(log);
	room->broadcastSkillInvoke(skill_name,type,owner);
	room->notifySkillInvoked(owner,skill_name);
}

void ServerPlayer::skillInvoked(const Skill* skill, int type, ServerPlayer *owner)
{
	skillInvoked(skill->objectName(),type,owner);
}

QList<ServerPlayer *> ServerPlayer::getRandomTargets(const Card *card, QList<ServerPlayer *> players)
{
	if (players.isEmpty()) players = room->getAlivePlayers();
	qsanShuffle(players);
	QList<const Player *> tos;
	for (int i = 0; i < players.length(); i++) {
		int x = 0;
		if(card->targetFilter(tos,players.at(i),this,x)||x>0){
			tos << players.at(i);
			if(card->targetsFeasible(tos,this))
				break;
			i = 0;
		}
	}
	QList<ServerPlayer *> targets;
	foreach (const Player *p, tos)
		targets << (ServerPlayer *)p;
	return targets;
}

void ServerPlayer::setSkillDescriptionSwap(const QString &skill_name, const QString &key, const QString &value, int instanceId)
{
	JsonArray arg;
	arg << objectName();
	arg << skill_name;
	arg << key;
	arg << value;
	arg << instanceId;
	if (Config.EnableHegemony) {
		foreach (ServerPlayer *receiver, room->getPlayers()) {
			if (canSeeHegemonySkill(this, receiver, skill_name, instanceId))
				room->doNotify(receiver, S_COMMAND_SKILL_DESCRIPTION_SWAP, arg);
		}
	} else {
		room->doBroadcastNotify(S_COMMAND_SKILL_DESCRIPTION_SWAP, arg);
	}
	Player::setSkillDescriptionSwap(skill_name, key, value, instanceId);
}

void ServerPlayer::setCardDescriptionSwap(const QString &card_name, const QString &key, const QString &value)
{
	JsonArray arg;
	arg << objectName();
	arg << card_name;
	arg << key;
	arg << value;
	room->doBroadcastNotify(S_COMMAND_UPDATE_CARD_DESC, arg);
	Player::setCardDescriptionSwap(card_name, key, value);
}

void ServerPlayer::setAvatarIcon(const QString &avatar_name, bool isSmall)
{
	if(isSmall) room->setPlayerProperty(this,"avatarIcon2",avatar_name);
	else room->setPlayerProperty(this,"avatarIcon",avatar_name);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_AVATAR_ICON << objectName() << isSmall << avatar_name;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

bool ServerPlayer::damageRevises(QVariant &data, int n)
{
	DamageStruct damage = data.value<DamageStruct>();
	int x = qMax(0,damage.damage+n);
	LogMessage log;
	log.type = "$DamageRevises1";
	if(this!=damage.to) log.type = "$DamageRevises2";
	if(x<1) log.type = "$DamageRevises0";
	log.from = this;
	log.arg = QString::number(damage.damage);
	log.arg2 = QString::number(x);
	log.arg3 = "Damage+";
	if(n<1) log.arg3 = "Damage-";
	room->sendLog(log);
	damage.damage = x;
	damage.prevented = x<1;
	data.setValue(damage);
	return x<1;
}

bool ServerPlayer::inSiegeRelation(const ServerPlayer *skill_owner, const ServerPlayer *victim) const
{
	if (isFriendWith(victim) || !isFriendWith(skill_owner) || !victim->hasShownOneGeneral())
		return false;
	if (this == skill_owner)
		return (getNextAlive() == victim && getNextAlive(2)->isFriendWith(this)) || (getPreviousAlive() == victim && getPreviousAlive(2)->isFriendWith(this));
	else
		return (getNextAlive() == victim && getNextAlive(2) == skill_owner) || (getPreviousAlive() == victim && getPreviousAlive(2) == skill_owner);
}

bool ServerPlayer::inFormationRalation(ServerPlayer *teammate) const
{
	QList<const Player *> teammates = getFormation();
	return teammates.length() > 1 && teammates.contains(teammate);
}

void ServerPlayer::summonFriends(const QString &type)
{
	room->tryPause();

	if (aliveCount() < 4)
		return;
	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = this;
	log.arg = "GameRule_AskForArraySummon";
	room->sendLog(log);
	LogMessage log2;
	log2.type = "#SummonType";
	log2.arg = (type == "Siege") ? "summon_type_siege" : "summon_type_formation";
	room->sendLog(log2);

	if (type == "Siege") {
		if (isFriendWith(getNextAlive()) && isFriendWith(getPreviousAlive()))
			return;
		bool failed = true;
		if (!isFriendWith(getNextAlive()) && getNextAlive()->hasShownOneGeneral()) {
			ServerPlayer *target = getNextAlive(2);
			if (!target->hasShownOneGeneral()) {
				QString prompt = target->willBeFriendWith(this) ? "SiegeSummon" : "SiegeSummon!";
				bool success = room->askForSkillInvoke(target, prompt);
				LogMessage log;
				log.type = "#SummonResult";
				log.from = target;
				log.arg = success ? "summon_success" : "summon_failed";
				room->sendLog(log);
				if (success) {
					target->askForGeneralShow();
					failed = false;
				}
			}
		}
		if (!isFriendWith(getPreviousAlive()) && getPreviousAlive()->hasShownOneGeneral()) {
			ServerPlayer *target = getPreviousAlive(2);
			if (!target->hasShownOneGeneral()) {
				QString prompt = target->willBeFriendWith(this) ? "SiegeSummon" : "SiegeSummon!";
				bool success = room->askForSkillInvoke(target, prompt);
				LogMessage log;
				log.type = "#SummonResult";
				log.from = target;
				log.arg = success ? "summon_success" : "summon_failed";
				room->sendLog(log);
				if (success) {
					target->askForGeneralShow();
					failed = false;
				}
			}
		}
		if (failed)
			room->setPlayerFlag(this, "Global_SummonFailed");

	} else if (type == "Formation") {
		int n = aliveCount(false);
		int asked = n;
		bool failed = true;
		for (int i = 1; i < n; ++i) {
			ServerPlayer *target = getNextAlive(i);
			if (isFriendWith(target))
				continue;
			else if (!target->hasShownOneGeneral()) {
				QString prompt = target->willBeFriendWith(this) ? "FormationSummon" : "FormationSummon!";
				bool success = room->askForSkillInvoke(target, prompt);
				LogMessage log;
				log.type = "#SummonResult";
				log.from = target;
				log.arg = success ? "summon_success" : "summon_failed";
				room->sendLog(log);

				if (success) {
					target->askForGeneralShow();
					room->doBattleArrayAnimate(target);
					failed = false;
					break;
				}
			} else {
				asked = i;
				break;
			}
		}

		if (failed) {
			n -= asked;
			for (int i = 1; i < n; ++i) {
				ServerPlayer *target = getPreviousAlive(i);
				if (isFriendWith(target))
					continue;
				else if (!target->hasShownOneGeneral()) {
					QString prompt = target->willBeFriendWith(this) ? "FormationSummon" : "FormationSummon!";
					bool success = room->askForSkillInvoke(target, prompt);
					LogMessage log;
					log.type = "#SummonResult";
					log.from = target;
					log.arg = success ? "summon_success" : "summon_failed";
					room->sendLog(log);

					if (success) {
						target->askForGeneralShow();
						room->doBattleArrayAnimate(target);
						break;
					}
				} else
					break;
			}
		}
	}
}

bool ServerPlayer::askCommandto(const QString &reason, ServerPlayer *target)
{
    if (!target) return false;
    // The issuer chooses the command; the target decides whether to execute it.
    const int index = startCommand(reason, target);
    return target->doCommand(reason, index, this);
}

int ServerPlayer::startCommand(const QString &reason, ServerPlayer *target)
{
    QStringList commands{"command1", "command2", "command3", "command4", "command5", "command6"};
    qsanShuffle(commands);
    commands = commands.mid(0, 2);
    const QString prompt = target
        ? QString("@startcommandto::%1:%2:#%3:#%4").arg(target->objectName(), reason, commands[0], commands[1])
        : QString("@startcommand:::%1:#%2:#%3").arg(reason, commands[0], commands[1]);
    QString choice = room->askForChoice(this, "startcommand_" + reason, commands.join('+'),
                                       QVariant::fromValue(target), QString(), prompt);
    if (!commands.contains(choice)) choice = commands.first();
    LogMessage log;
    log.type = "#CommandChoice";
    log.from = this;
    log.arg = "#" + choice;
    room->sendLog(log);
    return choice.right(1).toInt() - 1;
}

bool ServerPlayer::doCommand(const QString &reason, int index, ServerPlayer *source)
{
    if (!source || isDead() || source->isDead() || index < 0 || index >= 6) return false;
    const QString command = QString("command%1").arg(index + 1);
    const QString prompt = index < 2
        ? QString("@docommand%1:%2::%3").arg(index + 1).arg(source->objectName(), reason)
        : QString("@docommand:%1::%2:#%3").arg(source->objectName(), reason, command);
    QString choice;
    {
        // Nested prompts and interrupted decisions must not leave an AI hint behind.
        const int previous = getMark("command_index");
        room->setPlayerMark(this, "command_index", index + 1);
        auto restore = qScopeGuard([&]() { room->setPlayerMark(this, "command_index", previous); });
        choice = room->askForChoice(this, "docommand_" + reason, "yes+no",
                                    QVariant::fromValue(source), QString(), prompt);
    }
    LogMessage log;
    log.type = "#CommandChoice";
    log.from = this;
    log.arg = "#commandselect_" + choice;
    room->sendLog(log);
    if (choice != "yes" || isDead() || source->isDead()) return false;

    QVariant data = QStringList{reason, command, source->objectName() + "->" + objectName()}.join(':');
    room->getThread()->trigger(CommandVerifying, room, this, data);
    const QStringList changed = data.toString().split(':');
    if (changed.size() == 3 && changed[1].startsWith("command")) {
        bool ok = false;
        const int replacement = changed[1].mid(7).toInt(&ok);
        if (ok && replacement >= 1 && replacement <= 6) index = replacement - 1;
    }
    if (isDead() || source->isDead()) return true;
    switch (index) {
    case 0: {
        ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(),
                                                        "command_" + reason, "@command-damage");
        if (target && target->isAlive() && isAlive()) {
            log.type = "#CommandDamage";
            log.from = source;
            log.to << target;
            room->sendLog(log);
            room->damage(DamageStruct("command", this, target));
        }
        break;
    }
    case 1: {
        drawCards(1, "command");
        if (isDead() || source->isDead() || this == source || isNude()) break;
        const int count = qMin(2, int(getCards("he").size()));
        room->setPlayerFlag(source, "CommandSource");
        auto clearSource = qScopeGuard([&]() { room->setPlayerFlag(source, "-CommandSource"); });
        Card *give = room->askForExchange(this, "command", count, count, true,
                                          "@command-give:" + source->objectName());
        if (give && source->isAlive()) {
            CardMoveReason move(CardMoveReason::S_REASON_GIVE, objectName(), source->objectName(), "command", QString());
            room->obtainCard(source, give, move, false);
        }
        break;
    }
    case 2:
        room->loseHp(this);
        break;
    case 3:
        room->addPlayerMark(this, "command4_effect");
        // Reuse source-scoped invalidity rather than overriding another skill's mark.
        room->addSkillInvalidity(this, "all", objectName(), "heg_command4");
        room->setPlayerCardLimitation(this, "use,response", ".|.|.|hand", true, "heg_command4");
        for (ServerPlayer *player : room->getAlivePlayers()) {
            room->filterCards(player, player->getCards("he"), true);
            player->refreshUIState(true);
        }
        break;
    case 4:
        turnOver();
        room->addPlayerMark(this, "command5_effect");
        break;
    case 5: {
        if (getHandcardNum() < 2 && getEquips().size() < 2) break;
        QList<int> retained;
        if (!isKongcheng()) retained << handCards().first();
        if (hasEquip()) retained << getEquips().first()->getEffectiveId();
        const Card *selection = room->askForCard(this, "@@heg_commandefect!", "@command-select",
                                               QVariant(), Card::MethodNone);
        if (selection) {
            QList<int> validated;
            bool hand = false, equip = false, valid = true;
            const QList<int> selectedIds = selection->isVirtualCard()
                ? selection->getSubcards() : QList<int>{selection->getEffectiveId()};
            for (int id : selectedIds) {
                const bool inHand = handCards().contains(id);
                const bool inEquip = room->getCardOwner(id) == this && room->getCardPlace(id) == Player::PlaceEquip;
                if ((inHand && hand) || (inEquip && equip) || (!inHand && !inEquip)) { valid = false; break; }
                hand |= inHand;
                equip |= inEquip;
                validated << id;
            }
            if (valid && (isKongcheng() || hand) && (!hasEquip() || equip)) retained = validated;
        }
        DummyCard discarded;
        for (const Card *card : getCards("he"))
            if (!isJilei(card) && !retained.contains(card->getEffectiveId())) discarded.addSubcard(card);
        if (discarded.subcardsLength() > 0) room->throwCard(&discarded, "command", this);
        break;
    }
    }
    return true;
}

void ServerPlayer::changeToLord()
{
    if (!Config.EnableHegemony || !getActualGeneral1() || !getActualGeneral2()) return;
    const QString oldName = getActualGeneral1Name();
    const QString name = oldName.startsWith("heg_")
        ? "heg_lord_" + oldName.mid(4) : "heg_lord_" + oldName;
    const General *lord = Sanguosha->getGeneral(name);
    if (!lord || !lord->isLord() || Sanguosha->getBanPackages().contains(lord->getPackage())) return;
    for (ServerPlayer *other : room->getOtherPlayers(this, true))
        if (other->getActualGeneral1Name() == name || other->getActualGeneral2Name() == name) return;
    const int oldMaximum = getGeneralMaxHp();
    const bool shown = hasShownGeneral();
    if (!room->replaceHegemonyGeneral(this, name, true, shown)) return;
    const int difference = getGeneralMaxHp() - oldMaximum;
    room->setPlayerProperty(this, "maxhp", qMax(1, getMaxHp() + difference));
    room->setPlayerProperty(this, "hp", qMin(getMaxHp(), getHp() + qMax(0, difference)));
    setMark("CompanionEffect", 1);
    setMark("HalfMaxHpLeft", (lord->getMaxHpHead() + getActualGeneral2()->getMaxHpDeputy()) % 2);
}

void ServerPlayer::askForGeneralShow()
{
	if (hasShownGeneral() && hasShownGeneral2())
		return;
	QStringList choices;
	if (!hasShownGeneral())
		choices << "showhead";
	if (!hasShownGeneral2() && getGeneral2())
		choices << "showdeputy";
	if (choices.isEmpty())
		return;
	QString choice = room->askForChoice(this, "GameRule_AskForGeneralShow", choices.join("+"));
	if (choice == "showhead")
		room->showGeneral(this, "h");
	else if (choice == "showdeputy")
		room->showGeneral(this, "d");
}

void ServerPlayer::showHiddenSkill(const QString &skill_name)
{
    if (inHeadSkills(skill_name))
        room->showGeneral(this, "h");
    else
        room->showGeneral(this, "d");
}

void ServerPlayer::showGeneral(bool head_general, bool trigger_event, bool sendLog)
{
    const QStringList names = room->getTag(objectName()).toStringList();
    const int slot = head_general ? 0 : 1;
    if (names.size() <= slot || names.at(slot).isEmpty()) return;
    if (head_general ? hasShownGeneral() : hasShownGeneral2()) return;
    if (trigger_event && !canShowGeneral(head_general ? "h" : "d")) return;
    const General *revealed = Sanguosha->getGeneral(names.at(slot));
    if (!revealed) return;
    const bool wasShown = hasShownOneGeneral();
    const QString establishedKingdom = wasShown ? getKingdom() : getHegemonyKingdom();
    room->safeSetPlayerProperty(this, head_general ? "actual_general1" : "actual_general2", names.at(slot));
    room->notifyProperty(this, this, head_general ? "actual_general1" : "actual_general2");
    setSkillsPreshowed(head_general ? "h" : "d");
    room->setPlayerProperty(this, head_general ? "general_showed" : "general2_showed", true);
    // Revealing is presentation only: changeHero/changePlayerGeneral would
    // destroy the paid instance and reset its limited marks and private state.
    room->setPlayerProperty(this, head_general ? "general" : "general2", names.at(slot));
    room->setPlayerProperty(this, "gender", head_general || !hasShownGeneral()
        ? revealed->getGender() : getActualGeneral1()->getGender());
    if (Config.EnableHegemony && (head_general && revealed->getKingdom() == "careerist")) {
        room->setPlayerProperty(this, "kingdom", "careerist");
        updateHegemonyRole(this);
    } else if (Config.EnableHegemony && !wasShown) {
        room->setPlayerProperty(this, "kingdom", establishedKingdom);
        updateHegemonyRole(this);
    }
    if (Config.EnableHegemony && head_general && isAlive() && isHegemonyLord()) {
        foreach (ServerPlayer *p, room->getPlayers()) {
            if (p->hasShownOneGeneral() && p->getKingdom() == getKingdom()
                && !p->getRole().startsWith("careerist_"))
                room->setPlayerProperty(p, "role", HegemonyRule::getMappedRole(getKingdom()));
        }
    }
    if (Config.EnableHegemony) room->revealRole(this);
    syncHegemonyRevealState();
    if (sendLog) {
        LogMessage log;
        log.type = "#HegemonyReveal";
        log.from = this;
        log.arg = getGeneralName();
        log.arg2 = getGeneral2Name();
        room->sendLog(log);
    }
    if (trigger_event && room->getThread()) {
        const QString pendingKey = "HegemonyPendingReveals:" + objectName();
        QStringList revealSlots = room->getTag(pendingKey).toStringList();
        const QString slotName = head_general ? "head" : "deputy";
        if (!revealSlots.contains(slotName)) revealSlots << slotName;
        room->setTag(pendingKey, revealSlots);
        QVariant shown = head_general;
        room->getThread()->trigger(GeneralShown, room, this, shown);
    }
    room->filterCards(this, getCards("he"), true);
    // Visibility changed even if the authority-side numeric state is identical.
    refreshUIState(true);
}

void ServerPlayer::hideGeneral(bool head_general)
{
    if (!Config.EnableHegemony || (head_general ? !hasShownGeneral() : !hasShownGeneral2())) return;
    setSkillsPreshowed(head_general ? "h" : "d", false);
    room->setPlayerProperty(this, head_general ? "general_showed" : "general2_showed", false);
    room->setPlayerProperty(this, head_general ? "general" : "general2", QStringLiteral("anjiang"));
    if (!hasShownOneGeneral()) {
        room->setPlayerProperty(this, "kingdom", QStringLiteral("god"));
        room->setPlayerProperty(this, "role_shown", false);
        room->setPlayerProperty(this, "gender", General::Sexless);
    } else {
        const General *visible = hasShownGeneral() ? getActualGeneral1() : getActualGeneral2();
        if (visible) room->setPlayerProperty(this, "gender", visible->getGender());
    }
    syncHegemonyRevealState();
    if (room->getThread()) {
        QVariant hidden = head_general;
        room->getThread()->trigger(GeneralHidden, room, this, hidden);
    }
    room->filterCards(this, getCards("he"), true);
    refreshUIState(true);
}

void ServerPlayer::removeGeneral(bool head_general)
{
    if (!Config.EnableHegemony) return;
    const General *removed = head_general ? getActualGeneral1() : getActualGeneral2();
    if (!removed || removed->objectName().startsWith(QStringLiteral("sujiang"))) return;
    showGeneral(head_general);
    if (head_general ? !hasShownGeneral() : !hasShownGeneral2()) return;
    const QString replacement = removed->isMale() ? QStringLiteral("sujiang") : QStringLiteral("sujiangf");
    if (!Sanguosha->getGeneral(replacement)) return;
    // Remove precisely the selected general's roots, preserving same-named
    // deputy/acquired instances and using normal attached-child teardown.
    const QList<SkillInstance> instances = getSkillInstances();
    QSet<QString> removedMarks;
    foreach (const SkillInstance &instance, instances) {
        if (instance.source == SourceInnate && instance.bindHead == (head_general ? 1 : 2)) {
            const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (skill && !skill->getLimitMark().isEmpty()) removedMarks.insert(skill->getLimitMark());
            room->detachSkillFromPlayer(this, SkillInstanceUtils::formatName(instance.skillName, instance.instanceID),
                                        false, false, true);
        }
    }
    for (const QString &mark : removedMarks) {
        bool stillOwned = false;
        canSeeHegemonyLimitMark(this, this, mark, &stillOwned);
        if (!stillOwned) room->setPlayerMark(this, mark, 0);
    }
    QStringList names = room->getTag(objectName()).toStringList();
    if (names.size() < 2) return;
    names[head_general ? 0 : 1] = replacement;
    room->setTag(objectName(), names);
    room->safeSetPlayerProperty(this, head_general ? "actual_general1" : "actual_general2", replacement);
    room->notifyProperty(this, this, head_general ? "actual_general1" : "actual_general2");
    room->setPlayerProperty(this, head_general ? "general" : "general2", replacement);
    syncHegemonyRevealState();
    LogMessage log;
    log.type = "#HegemonyRemove";
    log.from = this;
    log.arg = head_general ? "head_general" : "deputy_general";
    log.arg2 = removed->objectName();
    room->sendLog(log);
    if (room->getThread()) {
        QVariant data = removed->objectName();
        room->getThread()->trigger(GeneralRemoved, room, this, data);
    }
    room->filterCards(this, getCards("he"), true);
    refreshUIState();
}
void ServerPlayer::notifyPreshow()
{
    QVariantMap preshowMap;
    for (const SkillInstance &instance : getSkillInstances()) {
        if (instance.source != SourceInnate) continue;
        const QString name = SkillInstanceUtils::formatName(instance.skillName, instance.instanceID);
        preshowMap[name] = hasPreshowedSkill(name);
    }
    QVariantMap args{{QStringLiteral("schema_version"), 1},
                     {QStringLiteral("player_name"), objectName()},
                     {QStringLiteral("states"), preshowMap}};
    room->doNotify(this, QSanProtocol::S_COMMAND_PRESHOW, args);
}

void ServerPlayer::addToShownHandCards(const QList<int> &card_ids)
{
	QList<int> add_ids;
	foreach (int id, card_ids) {
		if (!shown_handcards.contains(id) && room->getCardOwner(id) == this)
			add_ids.append(id);
	}

	if (add_ids.isEmpty())
		return;

	shown_handcards.append(add_ids);

	JsonArray arg;
	arg << objectName();
	arg << JsonUtils::toJsonArray(shown_handcards);

	foreach (ServerPlayer *player, room->getAllPlayers(true))
		room->doNotify(player, S_COMMAND_SET_SHOWN_HANDCARD, arg);

	LogMessage log;
	log.type = "$AddShownHand";
	log.from = this;
	log.card_str = ListI2S(add_ids).join("+");
	room->sendLog(log);
	foreach (int id, add_ids)
		room->showCard(this, id);
	room->getThread()->delay();

	ShownCardChangedStruct s;
	s.ids = add_ids;
	s.player = this;
	s.shown = true;
	QVariant v = QVariant::fromValue(s);
	room->getThread()->trigger(ShownCardChanged, room, this, v);

	room->filterCards(this, this->getCards("hs"), true);
}

void ServerPlayer::removeShownHandCards(const QList<int> &card_ids, bool sendLog, bool moveFromHand)
{
	QList<int> removed_ids;
	foreach (int id, card_ids) {
		if (shown_handcards.removeAll(id) > 0)
			removed_ids << id;
	}

	if (removed_ids.isEmpty())
		return;

	JsonArray arg;
	arg << objectName();
	arg << JsonUtils::toJsonArray(shown_handcards);

	foreach (ServerPlayer *player, room->getAllPlayers(true))
		room->doNotify(player, S_COMMAND_SET_SHOWN_HANDCARD, arg);

	if (sendLog) {
		LogMessage log;
		log.type = "$RemoveShownHand";
		log.from = this;
		log.card_str = ListI2S(removed_ids).join("+");
		room->sendLog(log);
		room->getThread()->delay();
	}

	ShownCardChangedStruct s;
	s.ids = removed_ids;
	s.player = this;
	s.shown = false;
	s.moveFromHand = moveFromHand;
	QVariant v = QVariant::fromValue(s);
	room->getThread()->trigger(ShownCardChanged, room, this, v);
}

void ServerPlayer::addBrokenEquips(const QList<int> &card_ids)
{
	QList<int> add_ids;
	foreach (int id, card_ids) {
		if (!broken_equips.contains(id) && room->getCardOwner(id) == this)
			add_ids.append(id);
	}

	if (add_ids.isEmpty())
		return;

	broken_equips.append(add_ids);

	JsonArray arg;
	arg << objectName();
	arg << JsonUtils::toJsonArray(broken_equips);

	foreach (ServerPlayer *player, room->getAllPlayers(true))
		room->doNotify(player, S_COMMAND_SET_BROKEN_EQUIP, arg);

	if (!hasFlag("GameRule_brokenEquips"))
		setFlags("GameRule_brokenEquips");

	LogMessage log;
	log.type = "$AddBrokenEquip";
	log.from = this;
	log.card_str = ListI2S(add_ids).join("+");
	room->sendLog(log);
	room->getThread()->delay();

	BrokenEquipChangedStruct b;
	b.ids = add_ids;
	b.player = this;
	b.broken = true;
	QVariant bv = QVariant::fromValue(b);
	room->getThread()->trigger(BrokenEquipChanged, room, this, bv);
}

void ServerPlayer::removeBrokenEquips(const QList<int> &card_ids, bool sendLog, bool moveFromEquip)
{
	QList<int> removed_ids;
	foreach (int id, card_ids) {
		if (broken_equips.removeAll(id) > 0)
			removed_ids << id;
	}

	if (removed_ids.isEmpty())
		return;

	JsonArray arg;
	arg << objectName();
	arg << JsonUtils::toJsonArray(broken_equips);

	foreach (ServerPlayer *player, room->getAllPlayers(true))
		room->doNotify(player, S_COMMAND_SET_BROKEN_EQUIP, arg);

	if (sendLog) {
		LogMessage log;
		log.type = "$RemoveBrokenEquip";
		log.from = this;
		log.card_str = ListI2S(removed_ids).join("+");
		room->sendLog(log);
		room->getThread()->delay();
	}

	BrokenEquipChangedStruct b;
	b.ids = removed_ids;
	b.player = this;
	b.broken = false;
	b.moveFromEquip = moveFromEquip;
	QVariant bv = QVariant::fromValue(b);
	room->getThread()->trigger(BrokenEquipChanged, room, this, bv);
}

int ServerPlayer::getPlayerNumWithSameKingdom(const QString &reason, const QString &kingdom,
                                              MaxCardsType::MaxCardsCount type) const
{
    const QString faction = kingdom.isEmpty() ? getSeemingKingdom() : kingdom;
    int count = Player::getPlayerNumWithSameKingdom(QStringLiteral("AI"), faction, type);
    if (reason != "AI") {
        QVariant data = QVariant::fromValue(PlayerNumStruct(count, faction, type, reason));
        room->getThread()->trigger(ConfirmPlayerNum, room, const_cast<ServerPlayer *>(this), data);
        count = data.value<PlayerNumStruct>().m_num;
    }
    return qMax(count, 0);
}
