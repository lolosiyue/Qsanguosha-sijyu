#include "room-notifier.h"

#include "json.h"
#include "protocol.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "room.h"
#include "roomthread.h"
#include "server.h"
#include "serverplayer.h"
#include "skill-instance-types.h"

#include <QSet>

using namespace QSanProtocol;

RoomNotifier::RoomNotifier(Room &room)
    : m_room(room)
{
}

QList<ServerPlayer *> RoomNotifier::resolveRecipients(const QList<ServerPlayer *> &players) const
{
    QList<ServerPlayer *> recipients;
    QSet<ServerPlayer *> delivered;
    foreach (ServerPlayer *player, players) {
        if (player == nullptr)
            continue;
        if (!delivered.contains(player)) {
            delivered.insert(player);
            recipients << player;
        }

        ServerPlayer *controller = m_room.getActualController(player);
        if (controller != nullptr && controller != player && controller->isOnline()
            && !delivered.contains(controller)) {
            delivered.insert(controller);
            recipients << controller;
        }
    }
    return recipients;
}

bool RoomNotifier::sendPacket(const QList<ServerPlayer *> &receivers, int command,
                              const QVariant &body)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = command;
    message.hasPayload = !body.isNull();
    message.payload = body;
    foreach (ServerPlayer *receiver, receivers) {
        ProtocolMessage perConnection = message;
        receiver->sendProtocolMessage(perConnection);
    }
    return true;
}

bool RoomNotifier::broadcast(const QList<ServerPlayer *> &players, int command,
                             const QVariant &body)
{
    return sendPacket(resolveRecipients(players), command, body);
}

bool RoomNotifier::doNotify(ServerPlayer *player, int command, const char *arg)
{
    JsonDocument doc = JsonDocument::fromJson(arg);
    if (!doc.isValid()) {
        m_room.output(QString("Fail to parse the Json Value %1").arg(arg));
        return true;
    }
    return doNotify(player, command, doc.toVariant());
}

bool RoomNotifier::doNotify(ServerPlayer *player, int command, const QVariant &arg)
{
    return broadcast(QList<ServerPlayer *>() << player, command, arg);
}

bool RoomNotifier::doBroadcastNotify(const QList<ServerPlayer *> &players, int command,
                                     const char *arg)
{
    JsonDocument doc = JsonDocument::fromJson(arg);
    if (!doc.isValid()) {
        m_room.output(QString("Fail to parse the Json Value %1").arg(arg));
        return true;
    }
    return doBroadcastNotify(players, command, doc.toVariant());
}

bool RoomNotifier::doBroadcastNotify(int command, const char *arg)
{
    return doBroadcastNotify(m_room.getPlayers(), command, arg);
}

bool RoomNotifier::doBroadcastNotify(const QList<ServerPlayer *> &players, int command,
                                     const QVariant &arg)
{
    return broadcast(players, command, arg);
}

bool RoomNotifier::doBroadcastNotify(int command, const QVariant &arg)
{
    return doBroadcastNotify(m_room.getPlayers(), command, arg);
}

void RoomNotifier::sendLog(const LogMessage &log, const QList<ServerPlayer *> &players)
{
    if (log.type.isEmpty()) return;
    if (Server::isHeadlessMode) {
        QString msg = QString("[LOG] %1").arg(log.type);
        if (!log.arg.isEmpty()) msg += QString(" | %1").arg(log.arg);
        if (!log.arg2.isEmpty()) msg += QString(" | %1").arg(log.arg2);
        if (log.from) msg += QString(" | from: %1").arg(log.from->objectName());
        Server::writeHeadlessLog(msg);
    }
    if (players.isEmpty())
        doBroadcastNotify(S_COMMAND_LOG_SKILL, log.toVariant());
    else
        doBroadcastNotify(players, S_COMMAND_LOG_SKILL, log.toVariant());
}

void RoomNotifier::showCard(const QString &playerName, const QString &cardIds,
                            const QList<ServerPlayer *> &players)
{
    JsonArray args;
    args << playerName << cardIds;
    doBroadcastNotify(players, S_COMMAND_SHOW_CARD, args);
}

void RoomNotifier::showVirtualCard(const QString &playerName, const QString &cardName,
                                   const QString &suit, int number, const QString &skillName,
                                   const QString &subcardIds, const QString &targetName)
{
    JsonArray args;
    args << playerName << cardName << suit << number << skillName << subcardIds << targetName;
    doBroadcastNotify(S_COMMAND_SHOW_VIRTUAL_CARD, args);
}

void RoomNotifier::broadcastSkillInvoke(const QString &skillName, const QString &category)
{
    JsonArray args;
    args << S_GAME_EVENT_PLAY_EFFECT << skillName << category << -1 << "";
    doBroadcastNotify(S_COMMAND_LOG_EVENT, args);
}

void RoomNotifier::broadcastSkillInvoke(const QString &skillName, bool isMale, int type,
                                        const QString &playerName)
{
    JsonArray args;
    args << S_GAME_EVENT_PLAY_EFFECT << skillName << isMale << type << playerName;
    doBroadcastNotify(S_COMMAND_LOG_EVENT, args);
}

void RoomNotifier::broadcastTagProperty(ServerPlayer *owner, const QString &tagKey,
                                        const QVariant &value)
{
    QVariantMap args{{QStringLiteral("schema_version"), 1},
                     {QStringLiteral("action"), QStringLiteral("tag")},
                     {QStringLiteral("player_name"), owner->objectName()},
                     {QStringLiteral("tag_name"), tagKey}};
    if (!value.isValid()) {
        args.insert(QStringLiteral("value_kind"), QStringLiteral("removed"));
    } else if (value.userType() == QMetaType::QStringList) {
        args.insert(QStringLiteral("value_kind"), QStringLiteral("string_list"));
        args.insert(QStringLiteral("value"), value.toStringList());
    } else {
        args.insert(QStringLiteral("value_kind"), QStringLiteral("scalar"));
        args.insert(QStringLiteral("value"), value);
    }
    doBroadcastNotify(S_COMMAND_SET_PROPERTY, args);
}

void RoomNotifier::notifyPlayerUIState(ServerPlayer *owner, const PlayerUIState &state)
{
    if (!owner) return;
    PlayerUIStateMessage message;
    message.playerName = owner->objectName();
    message.state = state;
    doBroadcastNotify(S_COMMAND_UPDATE_PLAYER_UI_STATE, message.toVariant());
}

void RoomNotifier::notifyPlayerUIState(ServerPlayer *receiver, const ServerPlayer *owner,
                                       const PlayerUIState &state)
{
    if (!receiver || !owner) return;
    PlayerUIStateMessage message;
    message.playerName = owner->objectName();
    message.state = state;
    doNotify(receiver, S_COMMAND_UPDATE_PLAYER_UI_STATE, message.toVariant());
}

void RoomNotifier::notifySkillInstanceState(ServerPlayer *owner, const SkillInstance &instance,
                                            const QString &operation, const QString &key,
                                            const QVariant &value)
{
    if (!owner) return;
    const SkillInstanceMessage message = SkillInstanceMessage::makeState(
        owner->objectName(), instance.skillName, instance.instanceID,
        operation, key, value);
    doNotify(owner, S_COMMAND_SKILL_INSTANCE, message.toVariant());
}
