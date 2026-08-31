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
