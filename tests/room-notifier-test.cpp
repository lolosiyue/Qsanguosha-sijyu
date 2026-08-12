#include "engine-bootstrap.h"
#include "json.h"
#include "protocol.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill-instance-types.h"

#include <QCoreApplication>
#include <QDebug>

using namespace QSanProtocol;

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.m_players << player;
        return player;
    }

    static void notifySkillInstanceState(Room &room, ServerPlayer *owner,
                                         const SkillInstance &instance,
                                         const QString &operation,
                                         const QString &key,
                                         const QVariant &value)
    {
        room.notifySkillInstanceState(owner, instance, operation, key, value);
    }

};

struct PacketRecord
{
    ServerPlayer *receiver;
    CommandType command;
    QVariant body;
};

class PacketRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this, player](const QString &message) {
            Packet packet;
            if (!packet.parse(message.toUtf8())) {
                parseFailed = true;
                return;
            }
            records << PacketRecord{player, packet.getCommandType(), packet.getMessageBody()};
        });
    }

    void clear()
    {
        records.clear();
        parseFailed = false;
    }

    int count(ServerPlayer *receiver, CommandType command) const
    {
        int result = 0;
        foreach (const PacketRecord &record, records) {
            if (record.receiver == receiver && record.command == command)
                ++result;
        }
        return result;
    }

    const PacketRecord *first(ServerPlayer *receiver, CommandType command) const
    {
        foreach (const PacketRecord &record, records) {
            if (record.receiver == receiver && record.command == command)
                return &record;
        }
        return nullptr;
    }

    QList<PacketRecord> records;
    bool parseFailed = false;
};

static bool expectCount(const PacketRecorder &recorder, ServerPlayer *receiver,
                        CommandType command, int expected, const char *context)
{
    const int actual = recorder.count(receiver, command);
    if (actual == expected)
        return true;
    qCritical() << context << "expected" << expected << "packets, got" << actual;
    return false;
}

static bool directNotificationArrivesOnce(Room &room, PacketRecorder &recorder,
                                          ServerPlayer *player)
{
    recorder.clear();
    const QVariant body = QStringLiteral("direct");
    room.doNotify(player, S_COMMAND_LOG_EVENT, body);

    const PacketRecord *record = recorder.first(player, S_COMMAND_LOG_EVENT);
    return !recorder.parseFailed
        && expectCount(recorder, player, S_COMMAND_LOG_EVENT, 1, "direct notify")
        && record != nullptr && record->body == body;
}

static bool controllerReceivesLogicalPlayerNotification(Room &room, PacketRecorder &recorder,
                                                        ServerPlayer *controller,
                                                        ServerPlayer *controlled,
                                                        ServerPlayer *other)
{
    room.setPlayerController(controlled, controller);
    recorder.clear();
    room.doNotify(controlled, S_COMMAND_LOG_EVENT, QStringLiteral("controlled"));

    return !recorder.parseFailed
        && expectCount(recorder, controlled, S_COMMAND_LOG_EVENT, 1, "controlled player notify")
        && expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1, "controller notify")
        && expectCount(recorder, other, S_COMMAND_LOG_EVENT, 0, "unrelated player notify");
}

static bool sharedControllerIsDeduplicated(Room &room, PacketRecorder &recorder,
                                           ServerPlayer *controller,
                                           ServerPlayer *firstControlled,
                                           ServerPlayer *secondControlled)
{
    room.setPlayerController(firstControlled, controller);
    room.setPlayerController(secondControlled, controller);

    recorder.clear();
    room.doBroadcastNotify(QList<ServerPlayer *>() << firstControlled << secondControlled,
                           S_COMMAND_LOG_EVENT, QStringLiteral("controlled-broadcast"));
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                        "shared controller broadcast"))
        return false;

    recorder.clear();
    room.doBroadcastNotify(QList<ServerPlayer *>() << controller << firstControlled
                                                   << secondControlled,
                           S_COMMAND_LOG_EVENT, QStringLiteral("full-broadcast"));
    return !recorder.parseFailed
        && expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                       "controller included in broadcast");
}

static bool ownerOnlySkillStateFollowsController(Room &room, PacketRecorder &recorder,
                                                 ServerPlayer *controller,
                                                 ServerPlayer *owner,
                                                 ServerPlayer *other)
{
    room.setPlayerController(owner, controller);
    recorder.clear();

    SkillInstance instance;
    instance.skillName = QStringLiteral("test_notifier_skill");
    instance.instanceID = 7;
    RoomTestAccess::notifySkillInstanceState(room, owner, instance,
                                             QStringLiteral("set"),
                                             QStringLiteral("counter"), 3);

    if (recorder.parseFailed
        || !expectCount(recorder, owner, S_COMMAND_SKILL_INSTANCE, 1,
                        "skill state owner")
        || !expectCount(recorder, controller, S_COMMAND_SKILL_INSTANCE, 1,
                        "skill state controller")
        || !expectCount(recorder, other, S_COMMAND_SKILL_INSTANCE, 0,
                        "skill state unrelated player"))
        return false;

    const PacketRecord *record = recorder.first(controller, S_COMMAND_SKILL_INSTANCE);
    if (record == nullptr)
        return false;
    SkillInstanceMessage message;
    return message.tryParse(record->body)
        && message.action == SkillInstanceMessage::State
        && message.ownerName == owner->objectName()
        && message.skillName == instance.skillName
        && message.instanceId == instance.instanceID
        && message.operation == QStringLiteral("set")
        && message.key == QStringLiteral("counter")
        && message.value.toInt() == 3;
}

static bool presentationPayloadsStayStable(Room &room, PacketRecorder &recorder,
                                           ServerPlayer *controller,
                                           ServerPlayer *owner,
                                           ServerPlayer *target)
{
    room.setPlayerController(owner, controller);

    recorder.clear();
    room.broadcastTagProperty(owner, QStringLiteral("sample"), QStringLiteral("value"));
    const PacketRecord *tagRecord = recorder.first(controller, S_COMMAND_SET_PROPERTY);
    const QVariantList tagPayload = tagRecord ? tagRecord->body.toList() : QVariantList();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_SET_PROPERTY, 1, "tag property")
        || tagPayload != (QVariantList() << owner->objectName()
                                        << QStringLiteral("tag:sample")
                                        << QStringLiteral("value")))
        return false;

    recorder.clear();
    PlayerUIState state;
    state.handMax = 5;
    state.offensiveDistance = -1;
    room.notifyPlayerUIState(owner, owner, state);
    const PacketRecord *uiRecord = recorder.first(controller, S_COMMAND_UPDATE_PLAYER_UI_STATE);
    PlayerUIStateMessage uiMessage;
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_UPDATE_PLAYER_UI_STATE, 1,
                        "player UI state")
        || uiRecord == nullptr || !uiMessage.tryParse(uiRecord->body)
        || uiMessage.playerName != owner->objectName() || !(uiMessage.state == state))
        return false;

    recorder.clear();
    LogMessage log;
    log.type = QStringLiteral("#NotifierTest");
    log.from = owner;
    log.arg = QStringLiteral("arg");
    room.sendLog(log, QList<ServerPlayer *>() << owner);
    const PacketRecord *logRecord = recorder.first(controller, S_COMMAND_LOG_SKILL);
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_SKILL, 1, "targeted log")
        || logRecord == nullptr || logRecord->body != log.toVariant())
        return false;

    recorder.clear();
    room.broadcastSkillInvoke(QStringLiteral("test_effect"), false, 2);
    const PacketRecord *effectRecord = recorder.first(controller, S_COMMAND_LOG_EVENT);
    const QVariantList effectPayload = effectRecord ? effectRecord->body.toList() : QVariantList();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1, "skill effect")
        || effectPayload != (QVariantList() << S_GAME_EVENT_PLAY_EFFECT
                                           << QStringLiteral("test_effect") << false << 2
                                           << QString()))
        return false;

    recorder.clear();
    DummyCard virtualCard;
    virtualCard.setObjectName(QStringLiteral("test_virtual"));
    virtualCard.setSuit(Card::Heart);
    virtualCard.setNumber(9);
    virtualCard.setSkillName(QStringLiteral("test_skill"));
    virtualCard.addSubcard(12);
    room.showVirtualCard(owner, &virtualCard, target);
    const PacketRecord *virtualRecord = recorder.first(controller, S_COMMAND_SHOW_VIRTUAL_CARD);
    const QVariantList virtualPayload = virtualRecord ? virtualRecord->body.toList() : QVariantList();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_SHOW_VIRTUAL_CARD, 1,
                        "virtual card display")
        || virtualPayload != (QVariantList() << owner->objectName()
                                            << QStringLiteral("test_virtual")
                                            << QStringLiteral("heart") << 9
                                            << QStringLiteral("test_skill")
                                            << QStringLiteral("12") << target->objectName()))
        return false;

    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *controller = RoomTestAccess::addPlayer(room, QStringLiteral("controller"));
    ServerPlayer *firstControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_b"));
    ServerPlayer *secondControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_c"));

    PacketRecorder recorder;
    recorder.watch(controller);
    recorder.watch(firstControlled);
    recorder.watch(secondControlled);

    qInfo() << "room notifier test: direct";
    if (!directNotificationArrivesOnce(room, recorder, controller))
        return 2;
    qInfo() << "room notifier test: controller";
    if (!controllerReceivesLogicalPlayerNotification(room, recorder, controller,
                                                     firstControlled, secondControlled))
        return 3;
    qInfo() << "room notifier test: deduplication";
    if (!sharedControllerIsDeduplicated(room, recorder, controller,
                                        firstControlled, secondControlled))
        return 4;
    qInfo() << "room notifier test: skill state";
    if (!ownerOnlySkillStateFollowsController(room, recorder, controller,
                                              firstControlled, secondControlled))
        return 5;
    qInfo() << "room notifier test: presentation";
    if (!presentationPayloadsStayStable(room, recorder, controller,
                                        firstControlled, secondControlled))
        return 6;

    qInfo() << "room notifier behavior passed";
    return 0;
}
