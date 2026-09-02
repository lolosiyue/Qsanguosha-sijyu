#include "protocol/protocol-payload-registry.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

using namespace QSanProtocol;

namespace
{
int fail(const QString &detail)
{
    QTextStream(stderr) << "PROTOCOL_FLOW_MATRIX_FAIL " << detail << '\n';
    return 1;
}

ProtocolMessage roomNotification(int command, const QVariantMap &payload)
{
    ProtocolMessage message;
    message.version = ProtocolVersion::V2;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = 1;
    message.command = command;
    message.hasPayload = true;
    message.payload = payload;
    return message;
}

bool strictPayloadContracts(QString *error)
{
    const QVariantMap reason {
        {QStringLiteral("reason"), 0x06},
        {QStringLiteral("player_id"), QStringLiteral("p1")},
        {QStringLiteral("skill_name"), QStringLiteral("draw")},
        {QStringLiteral("event_name"), QString()},
        {QStringLiteral("target_id"), QString()}
    };
    QVariantMap move {
        {QStringLiteral("card_ids"), QVariantList{1, 2}},
        {QStringLiteral("from_place"), 6},
        {QStringLiteral("to_place"), 0},
        {QStringLiteral("from_player"), QString()},
        {QStringLiteral("to_player"), QStringLiteral("p1")},
        {QStringLiteral("from_pile"), QString()},
        {QStringLiteral("to_pile"), QString()},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("open"), true}
    };
    QVariantMap movement {
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("move_id"), 1},
        {QStringLiteral("moves"), QVariantList{move}}
    };
    ProtocolMessage message = roomNotification(S_COMMAND_GET_CARD, movement);
    if (!ProtocolPayloadRegistry::validateObjectPayload(message, error))
        return false;

    QVariantMap invalidMove = move;
    invalidMove.insert(QStringLiteral("from_place"), 99);
    movement.insert(QStringLiteral("moves"), QVariantList{invalidMove});
    message.payload = movement;
    if (ProtocolPayloadRegistry::validateObjectPayload(message, error)) {
        *error = QStringLiteral("unknown card place was accepted");
        return false;
    }

    QVariantMap invalidReason = reason;
    invalidReason.insert(QStringLiteral("reason"), 0x7F);
    invalidMove = move;
    invalidMove.insert(QStringLiteral("reason"), invalidReason);
    movement.insert(QStringLiteral("moves"), QVariantList{invalidMove});
    message.payload = movement;
    if (ProtocolPayloadRegistry::validateObjectPayload(message, error)) {
        *error = QStringLiteral("unknown card movement reason was accepted");
        return false;
    }

    // The Judge phase flips a delayed trick onto the table with
    // CardMoveReason::S_MASK_BASIC_REASON (0x0F), a sentinel that deliberately
    // matches no basic reason. It is a real reason on the wire: rejecting it
    // drops the move and tears the connection down on every game where somebody
    // holds a delayed trick.
    QVariantMap sentinelReason = reason;
    sentinelReason.insert(QStringLiteral("reason"), 0x0F);
    sentinelReason.insert(QStringLiteral("skill_name"), QString());
    sentinelReason.insert(QStringLiteral("event_name"), QStringLiteral("delayed_effect"));
    QVariantMap sentinelMove = move;
    sentinelMove.insert(QStringLiteral("from_place"), 2);  // Player::PlaceDelayedTrick
    sentinelMove.insert(QStringLiteral("to_place"), 7);    // Player::PlaceTable
    sentinelMove.insert(QStringLiteral("reason"), sentinelReason);
    movement.insert(QStringLiteral("moves"), QVariantList{sentinelMove});
    message.payload = movement;
    if (!ProtocolPayloadRegistry::validateObjectPayload(message, error))
        return false;

    QVariantMap gameEvent {
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), S_GAME_EVENT_PLAY_EFFECT},
        {QStringLiteral("skill_name"), QStringLiteral("hujia")},
        {QStringLiteral("category"), QStringLiteral("male")},
        {QStringLiteral("audio_type"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")}
    };
    message = roomNotification(S_COMMAND_LOG_EVENT, gameEvent);
    if (!ProtocolPayloadRegistry::validateObjectPayload(message, error))
        return false;
    gameEvent.insert(QStringLiteral("category"), true);
    message.payload = gameEvent;
    if (ProtocolPayloadRegistry::validateObjectPayload(message, error)) {
        *error = QStringLiteral("wrongly typed game-event category was accepted");
        return false;
    }
    error->clear();

    ProtocolMessage showAll;
    showAll.version = ProtocolVersion::V2;
    showAll.type = ProtocolMessageType::Notification;
    showAll.source = ProtocolEndpoint::Room;
    showAll.destination = ProtocolEndpoint::Client;
    showAll.messageId = 1;
    showAll.command = S_COMMAND_SHOW_ALL_CARDS;
    showAll.hasPayload = true;
    showAll.payload = QVariantList{
        QStringLiteral("sgs1"),
        false,
        QVariantList{1, 2}
    };
    ProtocolMessage encoded;
    if (!ProtocolPayloadRegistry::encodeObjectPayload(showAll, &encoded, error))
        return false;
    const QVariantMap showAllObject = encoded.payload.toMap();
    if (showAllObject.value(QStringLiteral("player_name")).toString()
            != QLatin1String("sgs1")
        || showAllObject.value(QStringLiteral("card_ids")).toList()
            != QVariantList{1, 2}) {
        *error = QStringLiteral("Gongxin-shaped SHOW_ALL_CARDS did not map card_ids");
        return false;
    }

    showAll.payload = QVariantList{QStringLiteral("sgs1"), QVariantList{3}};
    if (!ProtocolPayloadRegistry::encodeObjectPayload(showAll, &encoded, error))
        return false;
    if (encoded.payload.toMap().value(QStringLiteral("card_ids")).toList()
        != QVariantList{3}) {
        *error = QStringLiteral("two-field SHOW_ALL_CARDS did not map card_ids");
        return false;
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!ProtocolPayloadRegistry::validateInventory(&error))
        return fail(error);
    if (!strictPayloadContracts(&error))
        return fail(error);

    const QByteArray expected = ProtocolPayloadRegistry::inventoryBytes();
    const QStringList arguments = application.arguments();
    const int writeIndex = arguments.indexOf(QStringLiteral("--write"));
    if (writeIndex >= 0) {
        if (writeIndex + 1 >= arguments.size())
            return fail(QStringLiteral("--write requires a file path"));
        QFile output(arguments.at(writeIndex + 1));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(QStringLiteral("cannot write %1").arg(output.fileName()));
        if (output.write(expected) != expected.size())
            return fail(QStringLiteral("short write for %1").arg(output.fileName()));
        output.close();
    }

    const int checkIndex = arguments.indexOf(QStringLiteral("--check"));
    if (checkIndex >= 0) {
        if (checkIndex + 1 >= arguments.size())
            return fail(QStringLiteral("--check requires a file path"));
        QFile input(arguments.at(checkIndex + 1));
        if (!input.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot read %1").arg(input.fileName()));
        const QByteArray actual = input.readAll();
        if (actual != expected)
            return fail(QStringLiteral("artifact differs from production registry"));
    }

    const QJsonObject summary = ProtocolPayloadRegistry::inventoryJson()
        .value(QStringLiteral("summary")).toObject();
    if (summary.value(QStringLiteral("production_flow_count")).toInt() != 145
        || summary.value(QStringLiteral("typed_registry_flow_count")).toInt() != 145
        || summary.value(QStringLiteral("typed_complete")).toInt() != 145
        || summary.value(QStringLiteral("implicit_passthrough")).toInt() != 0
        || summary.value(QStringLiteral("unclassified_production_flow")).toInt() != 0) {
        return fail(QStringLiteral("inventory summary is not 145/145 typed-complete"));
    }
    QTextStream(stdout) << "PROTOCOL_FLOW_MATRIX_OK flows="
                        << summary.value(QStringLiteral("production_flow_count")).toInt()
                        << " implicit_passthrough="
                        << summary.value(QStringLiteral("implicit_passthrough")).toInt()
                        << " unclassified="
                        << summary.value(QStringLiteral("unclassified_production_flow")).toInt()
                        << '\n';
    return 0;
}
