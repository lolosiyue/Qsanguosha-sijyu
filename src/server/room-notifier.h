#ifndef ROOM_NOTIFIER_H
#define ROOM_NOTIFIER_H

#include <QList>
#include <QString>
#include <QVariant>

class Room;
class ServerPlayer;

struct LogMessage;
struct PlayerUIState;
struct SkillInstance;

class RoomNotifier
{
public:
    explicit RoomNotifier(Room &room);

    bool doNotify(ServerPlayer *player, int command, const char *arg);
    bool doNotify(ServerPlayer *player, int command, const QVariant &arg);
    bool doBroadcastNotify(int command, const char *arg);
    bool doBroadcastNotify(const QList<ServerPlayer *> &players, int command, const char *arg);
    bool doBroadcastNotify(int command, const QVariant &arg);
    bool doBroadcastNotify(const QList<ServerPlayer *> &players, int command, const QVariant &arg);

    void sendLog(const LogMessage &log, const QList<ServerPlayer *> &players);
    void showCard(const QString &playerName, const QString &cardIds,
                  const QList<ServerPlayer *> &players);
    void showVirtualCard(const QString &playerName, const QString &cardName,
                         const QString &suit, int number, const QString &skillName,
                         const QString &subcardIds, const QString &targetName);
    void broadcastSkillInvoke(const QString &skillName, const QString &category);
    void broadcastSkillInvoke(const QString &skillName, bool isMale, int type,
                              const QString &playerName);
    void broadcastTagProperty(ServerPlayer *owner, const QString &tagKey,
                              const QVariant &value);
    void notifyPlayerUIState(ServerPlayer *owner, const PlayerUIState &state);
    void notifyPlayerUIState(ServerPlayer *receiver, const ServerPlayer *owner,
                             const PlayerUIState &state);
    void notifySkillInstanceState(ServerPlayer *owner, const SkillInstance &instance,
                                  const QString &operation, const QString &key,
                                  const QVariant &value);

private:
    QList<ServerPlayer *> resolveRecipients(const QList<ServerPlayer *> &players) const;
    bool broadcast(const QList<ServerPlayer *> &players, int command, const QVariant &body);
    bool sendPacket(const QList<ServerPlayer *> &receivers, int command, const QVariant &body);

    Room &m_room;
};

#endif
