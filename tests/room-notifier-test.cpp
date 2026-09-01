#include "engine-bootstrap.h"
#include "json.h"
#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill-instance-types.h"

#include <QCoreApplication>
#include <QDebug>

using namespace QSanProtocol;

namespace {

struct PacketRecord
{
    ServerPlayer *receiver;
    CommandType command;
    QVariant body;
};

class MessageRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this, player](const QByteArray &message) {
            ProtocolMessage packet;
            if (!ProtocolCodecRouter().decode(message, &packet).success) {
                parseFailed = true;
                return;
            }
            records << PacketRecord{player,
                static_cast<CommandType>(packet.command), packet.payload};
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

// S_COMMAND_LOG_EVENT carries a typed GameEventPayload since the Protocol V2
// cutover, so the routing cases below need a real domain payload rather than a
// placeholder string.  The wire form is the object the encoder builds from it.
static QVariant playEffectBody(const QString &skillName)
{
    return QVariantList() << S_GAME_EVENT_PLAY_EFFECT << skillName << false << 1
                          << QString();
}

static bool bodyIsPlayEffect(const QVariant &body, const QString &skillName)
{
    const QVariantMap object = body.toMap();
    return object.value(QStringLiteral("event")).toInt() == S_GAME_EVENT_PLAY_EFFECT
        && object.value(QStringLiteral("skill_name")).toString() == skillName;
}

static bool expectCount(const MessageRecorder &recorder, ServerPlayer *receiver,
                        CommandType command, int expected, const char *context)
{
    const int actual = recorder.count(receiver, command);
    if (actual == expected)
        return true;
    qCritical() << context << "expected" << expected << "packets, got" << actual;
    return false;
}

static bool directNotificationArrivesOnce(Room &room, MessageRecorder &recorder,
                                          ServerPlayer *player)
{
    recorder.clear();
    room.doNotify(player, S_COMMAND_LOG_EVENT, playEffectBody(QStringLiteral("direct")));

    const PacketRecord *record = recorder.first(player, S_COMMAND_LOG_EVENT);
    return !recorder.parseFailed
        && expectCount(recorder, player, S_COMMAND_LOG_EVENT, 1, "direct notify")
        && record != nullptr
        && bodyIsPlayEffect(record->body, QStringLiteral("direct"));
}

static bool controllerReceivesLogicalPlayerNotification(Room &room, MessageRecorder &recorder,
                                                        ServerPlayer *controller,
                                                        ServerPlayer *controlled,
                                                        ServerPlayer *other)
{
    room.setPlayerController(controlled, controller);
    recorder.clear();
    room.doNotify(controlled, S_COMMAND_LOG_EVENT, playEffectBody(QStringLiteral("controlled")));

    return !recorder.parseFailed
        && expectCount(recorder, controlled, S_COMMAND_LOG_EVENT, 1, "controlled player notify")
        && expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1, "controller notify")
        && expectCount(recorder, other, S_COMMAND_LOG_EVENT, 0, "unrelated player notify");
}

static bool sharedControllerIsDeduplicated(Room &room, MessageRecorder &recorder,
                                           ServerPlayer *controller,
                                           ServerPlayer *firstControlled,
                                           ServerPlayer *secondControlled)
{
    room.setPlayerController(firstControlled, controller);
    room.setPlayerController(secondControlled, controller);

    recorder.clear();
    room.doBroadcastNotify(QList<ServerPlayer *>() << firstControlled << secondControlled,
                           S_COMMAND_LOG_EVENT, playEffectBody(QStringLiteral("controlled-broadcast")));
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                        "shared controller broadcast"))
        return false;

    recorder.clear();
    room.doBroadcastNotify(QList<ServerPlayer *>() << controller << firstControlled
                                                   << secondControlled,
                           S_COMMAND_LOG_EVENT, playEffectBody(QStringLiteral("full-broadcast")));
    return !recorder.parseFailed
        && expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                       "controller included in broadcast");
}

static bool ownerOnlySkillStateFollowsController(Room &room, MessageRecorder &recorder,
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

static bool presentationPayloadsStayStable(Room &room, MessageRecorder &recorder,
                                           ServerPlayer *controller,
                                           ServerPlayer *owner,
                                           ServerPlayer *target)
{
    room.setPlayerController(owner, controller);

    recorder.clear();
    room.broadcastTagProperty(owner, QStringLiteral("sample"), QStringLiteral("value"));
    const PacketRecord *tagRecord = recorder.first(controller, S_COMMAND_SET_PROPERTY);
    const QVariantMap tagPayload = tagRecord ? tagRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_SET_PROPERTY, 1, "tag property")
        || tagPayload.value(QStringLiteral("action")).toString() != QStringLiteral("tag")
        || tagPayload.value(QStringLiteral("player_name")).toString() != owner->objectName()
        || tagPayload.value(QStringLiteral("tag_name")).toString() != QStringLiteral("sample")
        || tagPayload.value(QStringLiteral("value_kind")).toString() != QStringLiteral("scalar")
        || tagPayload.value(QStringLiteral("value")).toString() != QStringLiteral("value"))
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
    // The wire carries JSON arrays, so the QStringList members of the logical
    // payload come back as QVariantList; compare field by field instead of
    // against the unsent QVariant.
    const QVariantMap logPayload = logRecord ? logRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_SKILL, 1, "targeted log")
        || logRecord == nullptr
        || logPayload.value(QStringLiteral("log_type")).toString() != log.type
        || logPayload.value(QStringLiteral("from_player")).toString() != owner->objectName()
        || logPayload.value(QStringLiteral("to_players")).toStringList() != QStringList()
        || logPayload.value(QStringLiteral("arguments")).toStringList()
               != (QStringList() << log.arg << QString() << QString() << QString() << QString()))
        return false;

    recorder.clear();
    room.broadcastSkillInvoke(QStringLiteral("test_effect"), false, 2);
    const PacketRecord *effectRecord = recorder.first(controller, S_COMMAND_LOG_EVENT);
    const QVariantMap effectPayload = effectRecord ? effectRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1, "skill effect")
        || effectPayload.value(QStringLiteral("event")).toInt() != S_GAME_EVENT_PLAY_EFFECT
        || effectPayload.value(QStringLiteral("skill_name")).toString()
               != QStringLiteral("test_effect")
        || effectPayload.value(QStringLiteral("category")).toString()
               != QStringLiteral("female")
        || effectPayload.value(QStringLiteral("audio_type")).toInt() != 2)
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
    const QVariantMap virtualPayload = virtualRecord ? virtualRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_SHOW_VIRTUAL_CARD, 1,
                        "virtual card display")
        || virtualPayload.value(QStringLiteral("player_name")).toString() != owner->objectName()
        || virtualPayload.value(QStringLiteral("card_name")).toString()
               != QStringLiteral("test_virtual")
        || virtualPayload.value(QStringLiteral("suit")).toString() != QStringLiteral("heart")
        || virtualPayload.value(QStringLiteral("number")).toInt() != 9
        || virtualPayload.value(QStringLiteral("skill_name")).toString()
               != QStringLiteral("test_skill")
        || virtualPayload.value(QStringLiteral("subcard_ids")).toList() != (QVariantList() << 12)
        || virtualPayload.value(QStringLiteral("target_player")).toString()
               != target->objectName())
        return false;

    return true;
}

}

int runRoomNotifierTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *controller = RoomTestAccess::addPlayer(room, QStringLiteral("controller"));
    ServerPlayer *firstControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_b"));
    ServerPlayer *secondControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_c"));

        MessageRecorder recorder;
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
