#ifndef PLAYER_STATE_SERVICE_H
#define PLAYER_STATE_SERVICE_H

#include <QList>
#include <QString>
#include <QVariant>

class AiDecisionCoordinator;
class CardMovementService;
class EventDispatcher;
class Room;
class RoomNotifier;
class RoomRuntime;
class ServerPlayer;

class PlayerStateService
{
public:
	PlayerStateService(Room &room, RoomRuntime &runtime, RoomNotifier &notifier,
		AiDecisionCoordinator &aiDecisions, CardMovementService &cardMovement,
		EventDispatcher &eventDispatcher);

	void setPlayerFlag(ServerPlayer *player, const QString &flag);
	void setPlayerProperty(ServerPlayer *player, const char *propertyName,
		const QVariant &value);
	void safeSetPlayerProperty(ServerPlayer *player, const char *propertyName,
		const QVariant &value);

	void setPlayerMark(ServerPlayer *player, const QString &mark, int value,
		QList<ServerPlayer *> onlyViewers);
	void addPlayerMark(ServerPlayer *player, const QString &mark, int addNum,
		QList<ServerPlayer *> onlyViewers);
	void removePlayerMark(ServerPlayer *player, const QString &mark, int removeNum);

	void setPlayerCardLimitation(ServerPlayer *player, const QString &limitList,
		const QString &pattern, bool singleTurn, const QString &reason);
	void removePlayerCardLimitation(ServerPlayer *player, const QString &limitList,
		const QString &pattern, const QString &reason);
	void removePlayerCardLimitationByReason(ServerPlayer *player, const QString &reason);
	void clearPlayerCardLimitation(ServerPlayer *player, bool singleTurn);
	void setPlayerEquipsNullified(ServerPlayer *player, const QString &pattern,
		const QString &reason, bool singleTurn);
	void removePlayerEquipsNullified(ServerPlayer *player, const QString &pattern,
		const QString &reason);

	bool notifyProperty(ServerPlayer *player, const ServerPlayer *owner,
		const char *propertyName, const QString &value);
	bool broadcastProperty(ServerPlayer *owner, const char *propertyName,
		const QString &value);

private:
	Room &m_room;
	RoomRuntime &m_runtime;
	RoomNotifier &m_notifier;
	AiDecisionCoordinator &m_aiDecisions;
	CardMovementService &m_cardMovement;
	EventDispatcher &m_eventDispatcher;
};

#endif
