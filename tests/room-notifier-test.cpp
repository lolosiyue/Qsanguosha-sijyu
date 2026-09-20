#include "engine-bootstrap.h"
#include "engine.h"
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
#include <cstdio>

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
            const auto decoded = ProtocolCodecRouter().decode(message, &packet);
            if (!decoded.success) {
                std::fprintf(stderr,
                             "[room-notifier privacy] wire decode failed receiver=%s bytes=%d\n",
                             player->objectName().toUtf8().constData(),
                             static_cast<int>(message.size()));
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

static QVariantMap firstMovePayload(const MessageRecorder &recorder,
                                    ServerPlayer *receiver, CommandType command)
{
    const PacketRecord *record = recorder.first(receiver, command);
    if (!record) {
        std::fprintf(stderr, "[room-notifier privacy] missing packet receiver=%s command=%d count=%d\n",
                     receiver ? receiver->objectName().toUtf8().constData() : "<null>",
                     static_cast<int>(command), recorder.count(receiver, command));
        return QVariantMap();
    }
    if (record->body.userType() != QMetaType::QVariantMap) {
        std::fprintf(stderr,
                     "[room-notifier privacy] packet body is not map receiver=%s command=%d userType=%d\n",
                     receiver ? receiver->objectName().toUtf8().constData() : "<null>",
                     static_cast<int>(command), record->body.userType());
        return QVariantMap();
    }
    const QVariantMap payload = record->body.toMap();
    const QVariantList moves = payload.value(QStringLiteral("moves")).toList();
    if (moves.isEmpty() || moves.first().userType() != QMetaType::QVariantMap) {
        std::fprintf(stderr,
                     "[room-notifier privacy] moves shape invalid receiver=%s command=%d hasMoves=%d movesType=%d count=%d keys=%s\n",
                     receiver ? receiver->objectName().toUtf8().constData() : "<null>",
                     static_cast<int>(command), payload.contains(QStringLiteral("moves")),
                     payload.value(QStringLiteral("moves")).userType(),
                     static_cast<int>(moves.size()),
                     payload.keys().join(QLatin1Char(',')).toUtf8().constData());
        return QVariantMap();
    }
    return moves.first().toMap();
}

static bool firstCardIdIs(const QVariantMap &move, int expected)
{
    const QVariantList cardIds = move.value(QStringLiteral("card_ids")).toList();
    return !cardIds.isEmpty() && cardIds.first().toInt() == expected;
}

static QString moveIdsText(const QVariantMap &move)
{
    if (!move.contains(QStringLiteral("card_ids")))
        return QStringLiteral("<missing>");
    const QVariantList ids = move.value(QStringLiteral("card_ids")).toList();
    QStringList values;
    for (const QVariant &id : ids)
        values << (id.isValid() ? id.toString() : QStringLiteral("<invalid>"));
    return values.join(QLatin1Char(','));
}

static bool expectMove(const char *stage, const char *recipient, const QVariantMap &move,
                       bool expectedOpen, int expectedCardId)
{
    const bool hasOpen = move.contains(QStringLiteral("open"));
    const bool actualOpen = move.value(QStringLiteral("open")).toBool();
    const bool cardMatches = firstCardIdIs(move, expectedCardId);
    if (hasOpen && actualOpen == expectedOpen && cardMatches)
        return true;

    const QByteArray stageBytes(stage);
    const QByteArray recipientBytes(recipient);
    const QByteArray idsBytes(moveIdsText(move).toUtf8());
    const QByteArray keysBytes(move.keys().join(QLatin1Char(',')).toUtf8());
    std::fprintf(stderr,
                 "[room-notifier privacy] mismatch stage=%s recipient=%s expectedOpen=%d actualOpen=%s expectedCardId=%d actualCardIds=[%s] keys=[%s]\n",
                 stageBytes.constData(), recipientBytes.constData(), expectedOpen,
                 hasOpen ? (actualOpen ? "true" : "false") : "<missing>",
                 expectedCardId, idsBytes.constData(), keysBytes.constData());
    return false;
}

static bool expectMovePacketCount(const MessageRecorder &recorder, ServerPlayer *receiver,
                                  CommandType command, int expected, const char *stage)
{
    const int actual = recorder.count(receiver, command);
    if (actual == expected)
        return true;
    std::fprintf(stderr,
                 "[room-notifier privacy] packet count mismatch stage=%s receiver=%s command=%d expected=%d actual=%d parseFailed=%d\n",
                 stage, receiver ? receiver->objectName().toUtf8().constData() : "<null>",
                 static_cast<int>(command), expected, actual, recorder.parseFailed);
    return false;
}

static void printRoomNotifierStage(const char *stage)
{
    std::fprintf(stderr, "[room-notifier stage] %s\n", stage);
    std::fflush(stderr);
}

static bool movePayloadHidesUnauthorizedIds(Room &room, MessageRecorder &recorder,
                                            ServerPlayer *owner, ServerPlayer *destination,
                                            ServerPlayer *observer,
                                            ServerPlayer *controlledPeer)
{
    const int privatePileId = 12;
    const int hiddenHandId = 13;
    const int visibleFlagId = 14;
    const int openPileId = 15;
    if (!room.getCard(privatePileId) || !room.getCard(hiddenHandId)
        || !room.getCard(visibleFlagId) || !room.getCard(openPileId)) {
        std::fprintf(stderr,
                     "[room-notifier privacy] fixture card missing roomCards id12=%d id13=%d id14=%d id15=%d\n",
                     room.getCard(privatePileId) != nullptr,
                     room.getCard(hiddenHandId) != nullptr,
                     room.getCard(visibleFlagId) != nullptr,
                     room.getCard(openPileId) != nullptr);
        return false;
    }

    printRoomNotifierStage("privacy.private-pile");
    owner->setPileOpen(QStringLiteral("privacy_test"), owner->objectName());
    recorder.clear();

    CardsMoveStruct privatePileMove(privatePileId, owner, destination, Player::PlaceSpecial,
                                    Player::PlaceHand,
                                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                   owner->objectName()));
    privatePileMove.open = true;
    privatePileMove.from_pile_name = QStringLiteral("privacy_test");
    QList<CardsMoveStruct> privatePileMoves;
    privatePileMoves << privatePileMove;
    room.notifyMoveCards(true, privatePileMoves, false,
                         QList<ServerPlayer *>() << owner << observer);
    room.notifyMoveCards(false, privatePileMoves, false,
                         QList<ServerPlayer *>() << owner << observer);

    const QVariantMap ownerLose = firstMovePayload(recorder, owner, S_COMMAND_LOSE_CARD);
    const QVariantMap observerLose = firstMovePayload(recorder, observer, S_COMMAND_LOSE_CARD);
    const QVariantMap ownerGet = firstMovePayload(recorder, owner, S_COMMAND_GET_CARD);
    const QVariantMap observerGet = firstMovePayload(recorder, observer, S_COMMAND_GET_CARD);
    const QVariantList privateIds = observerLose.value(QStringLiteral("card_ids")).toList();
    CardsMoveStruct parsedHiddenPileMove;
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during private pile GET/LOSE\n");
        return false;
    }
    if (!expectMove("private-pile-LOSE-owner", owner->objectName().toUtf8().constData(),
                    ownerLose, true, privatePileId)
        || !expectMove("private-pile-LOSE-observer", observer->objectName().toUtf8().constData(),
                       observerLose, false, Card::S_UNKNOWN_CARD_ID)
        || !expectMove("private-pile-GET-owner", owner->objectName().toUtf8().constData(),
                       ownerGet, true, privatePileId)
        || !expectMove("private-pile-GET-observer", observer->objectName().toUtf8().constData(),
                       observerGet, false, Card::S_UNKNOWN_CARD_ID))
        return false;
    if (privateIds.size() != 1 || privateIds.first().toInt() != Card::S_UNKNOWN_CARD_ID) {
        std::fprintf(stderr,
                     "[room-notifier privacy] parsed observer LOSE ids count=%d actualFirst=%s expected=%d\n",
                     static_cast<int>(privateIds.size()), privateIds.isEmpty() ? "<empty>"
                         : privateIds.first().toString().toUtf8().constData(),
                     Card::S_UNKNOWN_CARD_ID);
        return false;
    }
    if (!parsedHiddenPileMove.tryParse(observerLose)) {
        std::fprintf(stderr, "[room-notifier privacy] CardsMoveStruct::tryParse rejected observer private-pile LOSE payload\n");
        return false;
    }
    if (parsedHiddenPileMove.card_ids != (QList<int>() << Card::S_UNKNOWN_CARD_ID)) {
        std::fprintf(stderr,
                     "[room-notifier privacy] parsed observer private-pile ids differ, count=%d first=%d expected=%d\n",
                     static_cast<int>(parsedHiddenPileMove.card_ids.size()),
                     parsedHiddenPileMove.card_ids.isEmpty() ? 0
                         : parsedHiddenPileMove.card_ids.first(),
                     Card::S_UNKNOWN_CARD_ID);
        return false;
    }
    if (!expectMovePacketCount(recorder, owner, S_COMMAND_LOSE_CARD, 1,
                               "private-pile-owner-LOSE")
        || !expectMovePacketCount(recorder, observer, S_COMMAND_LOSE_CARD, 1,
                                  "private-pile-observer-LOSE")
        || !expectMovePacketCount(recorder, owner, S_COMMAND_GET_CARD, 1,
                                  "private-pile-owner-GET")
        || !expectMovePacketCount(recorder, observer, S_COMMAND_GET_CARD, 1,
                                  "private-pile-observer-GET")
        || !expectMovePacketCount(recorder, destination, S_COMMAND_LOSE_CARD, 0,
                                  "controlled-destination-LOSE")
        || !expectMovePacketCount(recorder, destination, S_COMMAND_GET_CARD, 0,
                                  "controlled-destination-GET")
        || !expectMovePacketCount(recorder, controlledPeer, S_COMMAND_LOSE_CARD, 0,
                                  "controlled-peer-LOSE")
        || !expectMovePacketCount(recorder, controlledPeer, S_COMMAND_GET_CARD, 0,
                                  "controlled-peer-GET"))
        return false;
    if (privatePileMoves.first().card_ids.first() != privatePileId
        || !privatePileMoves.first().open) {
        std::fprintf(stderr,
                     "[room-notifier privacy] shared move mutated ids=%d open=%d expectedId=%d open=1\n",
                     privatePileMoves.first().card_ids.first(), privatePileMoves.first().open,
                     privatePileId);
        return false;
    }

    printRoomNotifierStage("privacy.authorized-pile-viewer");
    owner->setPileOpen(QStringLiteral("authorized_viewer_test"), observer->objectName());
    recorder.clear();
    CardsMoveStruct authorizedPileMove(openPileId, owner, destination,
                                       Player::PlaceSpecial, Player::PlaceHand,
                                       CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                      owner->objectName()));
    authorizedPileMove.from_pile_name = QStringLiteral("authorized_viewer_test");
    QList<CardsMoveStruct> authorizedPileMoves;
    authorizedPileMoves << authorizedPileMove;
    room.notifyMoveCards(true, authorizedPileMoves, false,
                         QList<ServerPlayer *>() << observer);
    room.notifyMoveCards(false, authorizedPileMoves, false,
                         QList<ServerPlayer *>() << observer);
    const QVariantMap authorizedLose = firstMovePayload(
        recorder, observer, S_COMMAND_LOSE_CARD);
    const QVariantMap authorizedGet = firstMovePayload(
        recorder, observer, S_COMMAND_GET_CARD);
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during authorized-pile GET/LOSE\n");
        return false;
    }
    if (!expectMove("authorized-pile-LOSE", observer->objectName().toUtf8().constData(),
                    authorizedLose, true, openPileId)
        || !expectMove("authorized-pile-GET", observer->objectName().toUtf8().constData(),
                       authorizedGet, true, openPileId)
        || !expectMovePacketCount(recorder, observer, S_COMMAND_LOSE_CARD, 1,
                                  "authorized-pile-LOSE")
        || !expectMovePacketCount(recorder, observer, S_COMMAND_GET_CARD, 1,
                                  "authorized-pile-GET"))
        return false;

    printRoomNotifierStage("privacy.hidden-hand");
    recorder.clear();
    CardsMoveStruct hiddenHandMove(hiddenHandId, owner, destination, Player::PlaceHand,
                                   Player::PlaceHand,
                                   CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                  owner->objectName()));
    QList<CardsMoveStruct> hiddenHandMoves;
    hiddenHandMoves << hiddenHandMove;
    room.notifyMoveCards(true, hiddenHandMoves, false,
                         QList<ServerPlayer *>() << owner << observer);
    const QVariantMap ownerHand = firstMovePayload(recorder, owner, S_COMMAND_LOSE_CARD);
    const QVariantMap observerHand = firstMovePayload(recorder, observer, S_COMMAND_LOSE_CARD);
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during hidden-hand LOSE\n");
        return false;
    }
    if (!expectMove("hidden-hand-LOSE-owner", owner->objectName().toUtf8().constData(),
                    ownerHand, true, hiddenHandId)
        || !expectMove("hidden-hand-LOSE-observer", observer->objectName().toUtf8().constData(),
                       observerHand, false, Card::S_UNKNOWN_CARD_ID))
        return false;
    if (hiddenHandMoves.first().card_ids.first() != hiddenHandId) {
        std::fprintf(stderr,
                     "[room-notifier privacy] shared hidden-hand move id mutated actual=%d expected=%d\n",
                     hiddenHandMoves.first().card_ids.first(), hiddenHandId);
        return false;
    }

    printRoomNotifierStage("privacy.visible-special");
    room.setCardFlag(visibleFlagId, QStringLiteral("visible"));
    CardsMoveStruct visibleFlagMove(visibleFlagId, owner, destination, Player::PlaceSpecial,
                                   Player::PlaceHand,
                                   CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                  owner->objectName()));
    visibleFlagMove.from_pile_name = QStringLiteral("privacy_test");
    QList<CardsMoveStruct> visibleFlagMoves;
    visibleFlagMoves << visibleFlagMove;
    recorder.clear();
    room.notifyMoveCards(true, visibleFlagMoves, false,
                         QList<ServerPlayer *>() << observer);
    room.notifyMoveCards(false, visibleFlagMoves, false,
                         QList<ServerPlayer *>() << observer);
    const QVariantMap visibleFlagPayload = firstMovePayload(
        recorder, observer, S_COMMAND_LOSE_CARD);
    const QVariantMap visibleFlagGetPayload = firstMovePayload(
        recorder, observer, S_COMMAND_GET_CARD);
    room.clearCardFlag(visibleFlagId, nullptr);
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during visible-special GET/LOSE\n");
        return false;
    }
    if (!expectMove("visible-special-LOSE", observer->objectName().toUtf8().constData(),
                    visibleFlagPayload, false, visibleFlagId)
        || !expectMove("visible-special-GET", observer->objectName().toUtf8().constData(),
                       visibleFlagGetPayload, false, visibleFlagId))
        return false;

    printRoomNotifierStage("privacy.visible-draw-pile");
    room.setCardFlag(visibleFlagId, QStringLiteral("visible"));
    CardsMoveStruct visibleDrawPileMove(visibleFlagId, owner, destination,
                                        Player::DrawPile, Player::PlaceHand,
                                        CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                       owner->objectName()));
    QList<CardsMoveStruct> visibleDrawPileMoves;
    visibleDrawPileMoves << visibleDrawPileMove;
    recorder.clear();
    room.notifyMoveCards(true, visibleDrawPileMoves, false,
                         QList<ServerPlayer *>() << observer);
    room.notifyMoveCards(false, visibleDrawPileMoves, false,
                         QList<ServerPlayer *>() << observer);
    const QVariantMap visibleDrawLose = firstMovePayload(
        recorder, observer, S_COMMAND_LOSE_CARD);
    const QVariantMap visibleDrawGet = firstMovePayload(
        recorder, observer, S_COMMAND_GET_CARD);
    room.clearCardFlag(visibleFlagId, nullptr);
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during visible-draw-pile GET/LOSE\n");
        return false;
    }
    if (!expectMove("visible-draw-pile-LOSE", observer->objectName().toUtf8().constData(),
                    visibleDrawLose, false, visibleFlagId)
        || !expectMove("visible-draw-pile-GET", observer->objectName().toUtf8().constData(),
                       visibleDrawGet, false, visibleFlagId))
        return false;

    printRoomNotifierStage("privacy.public-origin");
    recorder.clear();
    CardsMoveStruct publicOriginMove(hiddenHandId, owner, destination, Player::DiscardPile,
                                     Player::PlaceHand,
                                     CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                                                    owner->objectName()));
    QList<CardsMoveStruct> publicOriginMoves;
    publicOriginMoves << publicOriginMove;
    room.notifyMoveCards(true, publicOriginMoves, false,
                         QList<ServerPlayer *>() << observer);
    const QVariantMap publicOriginPayload = firstMovePayload(
        recorder, observer, S_COMMAND_LOSE_CARD);
    if (recorder.parseFailed) {
        std::fprintf(stderr, "[room-notifier privacy] decode failed during public-origin LOSE\n");
        return false;
    }
    return expectMove("public-discard-origin-LOSE",
                      observer->objectName().toUtf8().constData(),
                      publicOriginPayload, true, hiddenHandId);
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
    // Exercise the owner-scoped broadcast overload so the controller receives
    // the same private state through the normal recipient resolution path.
    room.notifyPlayerUIState(owner, state);
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

static bool akarinVisibilityFollowsRecipients(Room &room, MessageRecorder &recorder,
                                              ServerPlayer *controller,
                                              ServerPlayer *subject,
                                              ServerPlayer *viewer)
{
    room.setPlayerController(viewer, controller);
    recorder.clear();
    room.akarinPlayer(subject, viewer);

    const PacketRecord *hiddenRecord = recorder.first(viewer, S_COMMAND_LOG_EVENT);
    const QVariantMap hiddenPayload = hiddenRecord
        ? hiddenRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 1, "Akarin viewer")
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                        "Akarin controller")
        || !expectCount(recorder, subject, S_COMMAND_LOG_EVENT, 0,
                        "Akarin subject")
        || hiddenPayload.value(QStringLiteral("event")).toInt() != S_GAME_EVENT_AKARIN
        || hiddenPayload.value(QStringLiteral("player_name")).toString()
               != subject->objectName()
        || !hiddenPayload.value(QStringLiteral("hidden")).toBool()
        || !room.isAkarin(subject, viewer)) {
        return false;
    }

    recorder.clear();
    room.akarinPlayer(subject, viewer);
    if (!expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 0,
                     "duplicate Akarin apply")) {
        return false;
    }

    recorder.clear();
    room.removeAkarinEffect(subject, viewer);
    const PacketRecord *shownRecord = recorder.first(viewer, S_COMMAND_LOG_EVENT);
    const QVariantMap shownPayload = shownRecord
        ? shownRecord->body.toMap() : QVariantMap();
    if (recorder.parseFailed
        || !expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 1, "Akarin restore")
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                        "Akarin restore controller")
        || shownPayload.value(QStringLiteral("event")).toInt() != S_GAME_EVENT_AKARIN
        || shownPayload.value(QStringLiteral("player_name")).toString()
               != subject->objectName()
        || shownPayload.value(QStringLiteral("hidden")).toBool()
        || room.isAkarin(subject, viewer)) {
        return false;
    }

    recorder.clear();
    room.removeAkarinEffect(subject, viewer);
    if (!expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 0,
                     "duplicate Akarin removal")) {
        return false;
    }

    recorder.clear();
    room.akarinPlayer(subject);
    if (recorder.parseFailed
        || !expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                        "global Akarin controller")
        || !expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 1,
                        "global Akarin viewer")
        || !expectCount(recorder, subject, S_COMMAND_LOG_EVENT, 0,
                        "global Akarin subject")
        || !room.isAkarin(subject, controller)
        || !room.isAkarin(subject, viewer)) {
        return false;
    }

    recorder.clear();
    room.removeAkarinEffect(subject);
    return !recorder.parseFailed
        && expectCount(recorder, controller, S_COMMAND_LOG_EVENT, 1,
                       "global Akarin restore controller")
        && expectCount(recorder, viewer, S_COMMAND_LOG_EVENT, 1,
                       "global Akarin restore viewer")
        && !room.isAkarin(subject, controller)
        && !room.isAkarin(subject, viewer);
}

}

// docs/large-room-ui-protocol-audit.md D4: the play direction is
// state, so every seat ring carries it -- the opening arrange included, because
// a replay seek can only rewind a value that is re-asserted from index 0.  The
// battle log under the flip is narration a reconnecting client never sees.
// adjustSeats, swapSeat and reversePlayOrder all publish through one
// Room::broadcastSeatRing(), so one of them is enough to pin that read; the
// roster-level direction bookkeeping is covered in room-roster-test.
static bool seatRingCarriesPlayDirection(Room &room, MessageRecorder &recorder,
                                         ServerPlayer *player)
{
    QStringList expectedNames;
    foreach (ServerPlayer *seatPlayer, room.getPlayers())
        expectedNames << seatPlayer->objectName();

    recorder.clear();
    room.adjustSeats();
    const PacketRecord *opened = recorder.first(player, S_COMMAND_ARRANGE_SEATS);
    if (!expectCount(recorder, player, S_COMMAND_ARRANGE_SEATS, 1,
                     "opening arrange publishes seats")
        || opened == nullptr)
        return false;
    const QVariantMap openedBody = opened->body.toMap();
    if (openedBody.value(QStringLiteral("schema_version")).toInt() != 2
        || openedBody.value(QStringLiteral("player_names")).toStringList() != expectedNames
        || !openedBody.contains(QStringLiteral("play_order_reversed"))
        || openedBody.value(QStringLiteral("play_order_reversed")).toBool()) {
        qCritical() << "opening seat ring did not carry a cleared direction" << openedBody;
        return false;
    }

    recorder.clear();
    room.reversePlayOrder();
    const PacketRecord *reversed = recorder.first(player, S_COMMAND_ARRANGE_SEATS);
    if (!expectCount(recorder, player, S_COMMAND_ARRANGE_SEATS, 1, "reverse republishes seats")
        || reversed == nullptr)
        return false;
    if (!reversed->body.toMap().value(QStringLiteral("play_order_reversed")).toBool()) {
        qCritical() << "reversed seat ring lost its direction" << reversed->body;
        return false;
    }

    recorder.clear();
    room.reversePlayOrder();
    const PacketRecord *restored = recorder.first(player, S_COMMAND_ARRANGE_SEATS);
    if (restored == nullptr
        || restored->body.toMap().value(QStringLiteral("play_order_reversed")).toBool()) {
        qCritical() << "restoring the original order did not republish the direction";
        return false;
    }
    return !recorder.parseFailed;
}

int runRoomNotifierTests()
{
    QString error;
    printRoomNotifierStage("engine-bootstrap.begin");
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    printRoomNotifierStage("engine-bootstrap.complete");

    printRoomNotifierStage("room.construct.begin");
    Room room(nullptr, QStringLiteral("02_1v1"));
    printRoomNotifierStage("room.construct.complete");
    EngineRuntimeContextScope runtimeScope(*Sanguosha, &room);
    printRoomNotifierStage("room-state.reset.begin");
    room.roomRuntime()->state().reset();
    printRoomNotifierStage("room-state.reset.complete");
    ServerPlayer *controller = RoomTestAccess::addPlayer(room, QStringLiteral("controller"));
    ServerPlayer *firstControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_b"));
    ServerPlayer *secondControlled = RoomTestAccess::addPlayer(room, QStringLiteral("controlled_c"));
    ServerPlayer *privateObserver = RoomTestAccess::addPlayer(room, QStringLiteral("private_observer"));

        MessageRecorder recorder;
    recorder.watch(controller);
    recorder.watch(firstControlled);
    recorder.watch(secondControlled);
    recorder.watch(privateObserver);

    printRoomNotifierStage("suite.direct-notify");
    qInfo() << "room notifier test: direct";
    if (!directNotificationArrivesOnce(room, recorder, controller))
        return 2;
    printRoomNotifierStage("suite.controller-route");
    qInfo() << "room notifier test: controller";
    if (!controllerReceivesLogicalPlayerNotification(room, recorder, controller,
                                                     firstControlled, secondControlled))
        return 3;
    printRoomNotifierStage("suite.controller-deduplication");
    qInfo() << "room notifier test: deduplication";
    if (!sharedControllerIsDeduplicated(room, recorder, controller,
                                        firstControlled, secondControlled))
        return 4;
    printRoomNotifierStage("suite.skill-state");
    qInfo() << "room notifier test: skill state";
    if (!ownerOnlySkillStateFollowsController(room, recorder, controller,
                                              firstControlled, secondControlled))
        return 5;
    printRoomNotifierStage("suite.presentation");
    qInfo() << "room notifier test: presentation";
    if (!presentationPayloadsStayStable(room, recorder, controller,
                                        firstControlled, secondControlled))
        return 6;
    printRoomNotifierStage("suite.akarin-visibility");
    qInfo() << "room notifier test: Akarin visibility";
    if (!akarinVisibilityFollowsRecipients(room, recorder, controller,
                                           firstControlled, secondControlled))
        return 7;
    printRoomNotifierStage("suite.card-movement-privacy");
    qInfo() << "room notifier test: card movement privacy";
    if (!movePayloadHidesUnauthorizedIds(room, recorder, controller, firstControlled,
                                         privateObserver, secondControlled))
        return 8;
    printRoomNotifierStage("suite.seat-ring-direction");
    qInfo() << "room notifier test: seat ring direction";
    if (!seatRingCarriesPlayDirection(room, recorder, controller))
        return 9;

    qInfo() << "room notifier behavior passed";
    printRoomNotifierStage("suite.complete");
    return 0;
}
