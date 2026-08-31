#include "player-state-service.h"

#include "ai-decision-coordinator.h"
#include "card-movement-service.h"
#include "event-dispatcher.h"
#include "json.h"
#include "protocol.h"
#include "room.h"
#include "room-notifier.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"

#include <QJsonDocument>
#include <QThread>

#include <cstring>

using namespace QSanProtocol;

PlayerStateService::PlayerStateService(Room &room, RoomRuntime &runtime,
	RoomNotifier &notifier, AiDecisionCoordinator &aiDecisions,
	CardMovementService &cardMovement, EventDispatcher &eventDispatcher)
	: m_room(room), m_runtime(runtime), m_notifier(notifier),
	  m_aiDecisions(aiDecisions), m_cardMovement(cardMovement),
	  m_eventDispatcher(eventDispatcher)
{
}

void PlayerStateService::setPlayerFlag(ServerPlayer *player, const QString &flag)
{
	if (flag.startsWith("-") && !player->hasFlag(flag.mid(1)))
		return;
	player->setFlags(flag);
	broadcastProperty(player, "flags", flag);
}

void PlayerStateService::setPlayerProperty(ServerPlayer *player,
	const char *propertyName, const QVariant &value)
{
	if (!player) return; // 防禦空檢查

	const quint64 revisionBefore = m_runtime.stateRevision();
	int old = player->getMaxHp();
	bool same = player->property(propertyName).toString() == value.toString();

	// 關鍵邏輯：保持 Room 原有的 BlockingQueuedConnection 跨執行緒寫入路徑。
	if (QThread::currentThread() == player->thread()) {
		player->setProperty(propertyName, value);
	}
	else {
		emit m_room.signalSetProperty(player, propertyName, value);
	}
	if (propertyName == "hp" ||
		propertyName == "maxhp" ||
		propertyName == "weapon_area" ||
		propertyName == "armor_area" ||
		propertyName == "defensive_horse_area" ||
		propertyName == "offensive_horse_area" ||
		propertyName == "kingdom" ||
		propertyName == "chained")
	{
		player->refreshUIState();
	}

	broadcastProperty(player, propertyName, QString());

	if (same) return;
	if (m_runtime.stateRevision() == revisionBefore)
		m_runtime.advanceStateRevision(RoomRuntime::PlayerPropertyChanged);

	QString property = QString(propertyName);
	if (property == QStringLiteral("hp")) {
		QVariant data = m_room.getTag("HpChangedData");
		m_eventDispatcher.dispatch(HpChanged, player, data);
	}
	else if (property == QStringLiteral("maxhp")) {
		MaxHpStruct maxhp(player, player->getMaxHp() - old);
		QVariant data = QVariant::fromValue(maxhp);
		m_eventDispatcher.dispatch(MaxHpChanged, player, data);
	}
	else if (property == QStringLiteral("chained")) {
		QVariant data;
		m_eventDispatcher.dispatch(ChainStateChanged, player, data);
	}
	else if (property == QStringLiteral("kingdom")) {
		QVariant data = value;
		m_eventDispatcher.dispatch(KingdomChanged, player, data);
	}
	else if (property == QStringLiteral("weapon_area")) {
		QVariantList list;
		list << 0;
		QVariant data = list;
		if (value.toBool()) {
			m_eventDispatcher.dispatch(ObtainEquipArea, player, data);
		}
		else {
			if (player->getEquip(0))
				m_cardMovement.throwCard(player->getEquip(0),
					CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
					nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowEquipArea, player, data);
		}
	}
	else if (property == QStringLiteral("armor_area")) {
		QVariantList list;
		list << 1;
		QVariant data = list;
		if (value.toBool()) {
			m_eventDispatcher.dispatch(ObtainEquipArea, player, data);
		}
		else {
			if (player->getEquip(1))
				m_cardMovement.throwCard(player->getEquip(1),
					CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
					nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowEquipArea, player, data);
		}
	}
	else if (property == QStringLiteral("defensive_horse_area")) {
		QVariantList list;
		list << 2;
		QVariant data = list;
		if (value.toBool()) {
			m_eventDispatcher.dispatch(ObtainEquipArea, player, data);
		}
		else {
			// 保持既有行為；裝備索引的相鄰問題不在本次重構範圍。
			if (player->getEquip(1))
				m_cardMovement.throwCard(player->getEquip(1),
					CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
					nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowEquipArea, player, data);
		}
	}
	else if (property == QStringLiteral("offensive_horse_area")) {
		QVariantList list;
		list << 3;
		QVariant data = list;
		if (value.toBool()) {
			m_eventDispatcher.dispatch(ObtainEquipArea, player, data);
		}
		else {
			if (player->getEquip(3))
				m_cardMovement.throwCard(player->getEquip(3),
					CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
					nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowEquipArea, player, data);
		}
	}
	else if (property == QStringLiteral("treasure_area")) {
		QVariantList list;
		list << 4;
		QVariant data = list;
		if (value.toBool()) {
			m_eventDispatcher.dispatch(ObtainEquipArea, player, data);
		}
		else {
			if (player->getEquip(4))
				m_cardMovement.throwCard(player->getEquip(4),
					CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
					nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowEquipArea, player, data);
		}
	}
	else if (property == QStringLiteral("hasjudgearea")) {
		QVariant data;
		if (player->hasJudgeArea()) {
			m_eventDispatcher.dispatch(ObtainJudgeArea, player, data);
		}
		else {
			m_cardMovement.throwCard(player->getJudgingAreaID(),
				CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName()),
				nullptr, nullptr);
			m_eventDispatcher.dispatch(ThrowJudgeArea, player, data);
		}
	}
}

void PlayerStateService::safeSetPlayerProperty(ServerPlayer *player,
	const char *propertyName, const QVariant &value)
{
	if (!player) return;
	const bool same = player->property(propertyName).toString() == value.toString();
	const quint64 revisionBefore = m_runtime.stateRevision();
	if (QThread::currentThread() == player->thread())
		player->setProperty(propertyName, value);
	else
		emit m_room.signalSetProperty(player, propertyName, value);
	if (!same && m_runtime.stateRevision() == revisionBefore)
		m_runtime.advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void PlayerStateService::setPlayerMark(ServerPlayer *player, const QString &mark,
	int value, QList<ServerPlayer *> onlyViewers)
{
	if (value == player->getMark(mark)) return;

	if (mark.endsWith("Clear") && value != 0 && !m_room.current) return;

	bool trigger = m_room.game_state > 0 && !(mark.endsWith("Clear") ||
		mark.endsWith("_lun") || mark.endsWith("-Keep") || mark == "@HuJia" ||
		mark.contains("Global_") || mark.contains("sys_") || mark.contains("ExtraBf") ||
		mark.contains("damage_point_") || (mark.startsWith("&") && mark.endsWith("_num")));

	MarkStruct markStruct;
	markStruct.who = player;
	markStruct.name = mark;
	markStruct.count = value;
	markStruct.gain = value - player->getMark(mark);
	QVariant data = QVariant::fromValue(markStruct);
	if (trigger) {
		if (m_eventDispatcher.dispatch(MarkChange, player, data))
			return;
		markStruct = data.value<MarkStruct>();
		if (markStruct.count == player->getMark(mark)) return;
	}
	m_aiDecisions.setMarkVisibility(player, markStruct.name, markStruct.count, onlyViewers);
	player->setMark(markStruct.name, markStruct.count);

	player->refreshUIState();

	JsonArray arg;
	arg << player->objectName() << markStruct.name << markStruct.count;
	if (onlyViewers.isEmpty())
		m_notifier.doBroadcastNotify(S_COMMAND_SET_MARK, arg);
	else
		m_notifier.doBroadcastNotify(onlyViewers, S_COMMAND_SET_MARK, arg);

	if (trigger)
		m_eventDispatcher.dispatch(MarkChanged, player, data);
}

void PlayerStateService::addPlayerMark(ServerPlayer *player, const QString &mark,
	int addNum, QList<ServerPlayer *> onlyViewers)
{
	setPlayerMark(player, mark, player->getMark(mark) + addNum, onlyViewers);
}

void PlayerStateService::removePlayerMark(ServerPlayer *player, const QString &mark,
	int removeNum)
{
	setPlayerMark(player, mark, qMax(0, player->getMark(mark) - removeNum),
		QList<ServerPlayer *>());
}

void PlayerStateService::setPlayerCardLimitation(ServerPlayer *player,
	const QString &limitList, const QString &pattern, bool singleTurn,
	const QString &reason)
{
	player->setCardLimitation(limitList, pattern,
		reason.isEmpty() ? m_room.objectName() : reason, singleTurn);

	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("set")},
		{QStringLiteral("methods"), limitList.split(QLatin1Char(','), Qt::SkipEmptyParts)},
		{QStringLiteral("pattern"), pattern},
		{QStringLiteral("reason"), reason},
		{QStringLiteral("single_turn"), singleTurn}};
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void PlayerStateService::removePlayerCardLimitation(ServerPlayer *player,
	const QString &limitList, const QString &pattern, const QString &reason)
{
	player->removeCardLimitation(limitList, pattern,
		reason.isEmpty() ? m_room.objectName() : reason);

	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("remove")},
		{QStringLiteral("methods"), limitList.split(QLatin1Char(','), Qt::SkipEmptyParts)},
		{QStringLiteral("pattern"), pattern},
		{QStringLiteral("reason"), reason}};
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void PlayerStateService::removePlayerCardLimitationByReason(ServerPlayer *player,
	const QString &reason)
{
	player->removeCardLimitationByReason(reason);

	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("remove_by_reason")},
		{QStringLiteral("reason"), reason}};
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void PlayerStateService::clearPlayerCardLimitation(ServerPlayer *player, bool singleTurn)
{
	player->clearCardLimitation(singleTurn);

	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("clear")},
		{QStringLiteral("single_turn"), singleTurn}};
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void PlayerStateService::setPlayerEquipsNullified(ServerPlayer *player,
	const QString &pattern, const QString &reason, bool singleTurn)
{
	player->addEquipsNullified(pattern, reason, singleTurn);

	QString fullPattern = pattern;
	if (!pattern.contains("|.|.|"))
		fullPattern = pattern + "|.|.|";

	JsonArray arg;
	arg << true << "effect" << fullPattern << reason << singleTurn;
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

void PlayerStateService::removePlayerEquipsNullified(ServerPlayer *player,
	const QString &pattern, const QString &reason)
{
	player->removeEquipsNullified(pattern, reason);

	QString fullPattern = pattern;
	if (!pattern.contains("|.|.|"))
		fullPattern = pattern + "|.|.|";
	if (!fullPattern.endsWith("$1") && !fullPattern.endsWith("$0"))
		fullPattern = fullPattern + "$0";

	JsonArray arg;
	arg << false << "effect" << fullPattern << reason << false;
	m_notifier.doNotify(player, S_COMMAND_CARD_LIMITATION, arg);
}

bool PlayerStateService::notifyProperty(ServerPlayer *player,
	const ServerPlayer *owner, const char *propertyName, const QString &value)
{
	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("property")},
		{QStringLiteral("player_name"), owner == player
			? QSanProtocol::S_PLAYER_SELF_REFERENCE_ID : owner->objectName()},
		{QStringLiteral("property_name"), QString::fromLatin1(propertyName)},
		{QStringLiteral("string_value"), value.isEmpty()
			? owner->property(propertyName).toString() : value}};
	return m_notifier.doNotify(player, S_COMMAND_SET_PROPERTY, arg);
}

bool PlayerStateService::broadcastProperty(ServerPlayer *owner,
	const char *propertyName, const QString &value)
{
	if (std::strcmp(propertyName, "role") == 0)
		owner->setShownRole(true);
	owner->addProperty(propertyName);

	const QString property = QString::fromLatin1(propertyName);
	const QString propertyValue = value.isEmpty()
		? owner->property(propertyName).toString() : value;
	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("action"), QStringLiteral("property")},
		{QStringLiteral("player_name"), owner->objectName()},
		{QStringLiteral("property_name"), property},
		{QStringLiteral("string_value"), propertyValue}};
	if (property == QLatin1String("general_pile_changed")) {
		const QVariantMap pile = QJsonDocument::fromJson(propertyValue.toUtf8()).toVariant().toMap();
		arg = QVariantMap{{QStringLiteral("schema_version"), 1},
			{QStringLiteral("action"), QStringLiteral("general_pile")},
			{QStringLiteral("player_name"), owner->objectName()},
			{QStringLiteral("pile_name"), pile.value(QStringLiteral("pile_name"))},
			{QStringLiteral("general_names"), pile.value(QStringLiteral("general_names"))},
			{QStringLiteral("add"), pile.value(QStringLiteral("add"))}};
	}
	m_notifier.doBroadcastNotify(S_COMMAND_SET_PROPERTY, arg);

	if (std::strcmp(propertyName, "hp") == 0 && owner->getHp() > 0 &&
		owner->hasFlag("Global_Dying")) {
		setPlayerFlag(owner, "-Global_Dying");
		QStringList currentDying = m_room.getTag("CurrentDying").toStringList();
		currentDying.removeOne(owner->objectName());
		m_room.setTag("CurrentDying", currentDying);
	}
	return true;
}
