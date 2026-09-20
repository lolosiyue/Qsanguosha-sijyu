#ifndef PLAYER_STATE_SERVICE_H
#define PLAYER_STATE_SERVICE_H

#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

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
	void akarinPlayer(ServerPlayer *player, ServerPlayer *to);
	void removeAkarinEffect(ServerPlayer *player, ServerPlayer *to);
	bool isAkarin(ServerPlayer *player, ServerPlayer *to) const;
	void setPlayerProperty(ServerPlayer *player, const char *propertyName,
		const QVariant &value);
	void safeSetPlayerProperty(ServerPlayer *player, const char *propertyName,
		const QVariant &value);

	bool isRoleRevealed(const ServerPlayer *player) const;
	bool canSeeRole(const ServerPlayer *viewer, const ServerPlayer *target) const;
	void revealRole(ServerPlayer *player, const QString &value = QString());
	void revealRoleTo(ServerPlayer *viewer, ServerPlayer *target);
	void grantRoleVisibility(ServerPlayer *viewer, const ServerPlayer *target);
	void syncRole(ServerPlayer *viewer, const ServerPlayer *target);

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
	// Room::applyDamageHp uses this hook to commit damage history at the
	// property mutation point, before HpChanged observers can run.
	void setPlayerProperty(ServerPlayer *player, const char *propertyName,
		const QVariant &value, const std::function<void()> &beforeEventDispatch);

	friend class Room;

	Room &m_room;
	RoomRuntime &m_runtime;
	RoomNotifier &m_notifier;
	AiDecisionCoordinator &m_aiDecisions;
	CardMovementService &m_cardMovement;
	EventDispatcher &m_eventDispatcher;
};

#endif
