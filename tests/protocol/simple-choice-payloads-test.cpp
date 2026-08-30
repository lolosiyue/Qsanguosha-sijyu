#include "json.h"
#include "protocol.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/gameplay/simple-choice-payloads.h"
#include "protocol/protocol-runtime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <functional>

using namespace QSanProtocol;

namespace
{
int testCaseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++testCaseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolMessage requestMessage(int command, quint64 messageId,
                               const QVariant &payload = QVariant(),
                               bool hasPayload = true)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = messageId;
    message.command = command;
    message.hasPayload = hasPayload;
    message.payload = payload;
    return message;
}

ProtocolMessage replyMessage(int command, quint64 messageId, quint64 replyTo,
                             const QVariant &payload)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.messageId = messageId;
    message.replyTo = replyTo;
    message.command = command;
    message.hasPayload = true;
    message.payload = payload;
    return message;
}

struct GoldenInteraction
{
    QString name;
    ProtocolMessage request;
    ProtocolMessage reply;
    QByteArray v1Request;
    QByteArray v2Request;
    QByteArray v1Reply;
    QByteArray v2Reply;
};

QList<GoldenInteraction> goldenInteractions()
{
    return {
        {
            QStringLiteral("choose general"),
            requestMessage(S_COMMAND_CHOOSE_GENERAL, 101,
                           QVariantList{QStringLiteral("caocao"), QStringLiteral("liubei")}),
            replyMessage(S_COMMAND_CHOOSE_GENERAL, 201, 101, QStringLiteral("caocao")),
            "[101,0,1041,10,[\"caocao\",\"liubei\"]]",
            "{\"command\":10,\"destination\":\"client\",\"message_id\":\"101\","
            "\"payload\":{\"candidates\":[\"caocao\",\"liubei\"],\"schema_version\":1},"
            "\"source\":\"room\",\"type\":\"request\",\"v\":2}",
            "[201,101,322,10,\"caocao\"]",
            "{\"command\":10,\"destination\":\"room\",\"message_id\":\"201\","
            "\"payload\":{\"general\":\"caocao\",\"schema_version\":1},"
            "\"reply_to\":\"101\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        },
        {
            QStringLiteral("choose suit"),
            requestMessage(S_COMMAND_CHOOSE_SUIT, 102, QVariant(), false),
            replyMessage(S_COMMAND_CHOOSE_SUIT, 202, 102, QStringLiteral("spade")),
            "[102,0,1041,12]",
            "{\"command\":12,\"destination\":\"client\",\"message_id\":\"102\","
            "\"payload\":{\"schema_version\":1},\"source\":\"room\","
            "\"type\":\"request\",\"v\":2}",
            "[202,102,322,12,\"spade\"]",
            "{\"command\":12,\"destination\":\"room\",\"message_id\":\"202\","
            "\"payload\":{\"schema_version\":1,\"suit\":\"spade\"},"
            "\"reply_to\":\"102\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        },
        {
            QStringLiteral("choose kingdom"),
            requestMessage(S_COMMAND_CHOOSE_KINGDOM, 103,
                           QVariantList{QStringLiteral("wei+shu")}),
            replyMessage(S_COMMAND_CHOOSE_KINGDOM, 203, 103, QStringLiteral("wei")),
            "[103,0,1041,11,[\"wei+shu\"]]",
            "{\"command\":11,\"destination\":\"client\",\"message_id\":\"103\","
            "\"payload\":{\"kingdoms\":[\"wei\",\"shu\"],\"schema_version\":1},"
            "\"source\":\"room\",\"type\":\"request\",\"v\":2}",
            "[203,103,322,11,\"wei\"]",
            "{\"command\":11,\"destination\":\"room\",\"message_id\":\"203\","
            "\"payload\":{\"kingdom\":\"wei\",\"schema_version\":1},"
            "\"reply_to\":\"103\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        },
        {
            QStringLiteral("choose order"),
            requestMessage(S_COMMAND_CHOOSE_ORDER, 104,
                           static_cast<int>(S_REASON_CHOOSE_ORDER_TURN)),
            replyMessage(S_COMMAND_CHOOSE_ORDER, 204, 104,
                         static_cast<int>(S_CAMP_WARM)),
            "[104,0,1041,17,0]",
            "{\"command\":17,\"destination\":\"client\",\"message_id\":\"104\","
            "\"payload\":{\"reason\":\"turn\",\"schema_version\":1},"
            "\"source\":\"room\",\"type\":\"request\",\"v\":2}",
            "[204,104,322,17,0]",
            "{\"command\":17,\"destination\":\"room\",\"message_id\":\"204\","
            "\"payload\":{\"camp\":\"warm\",\"schema_version\":1},"
            "\"reply_to\":\"104\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        },
        {
            QStringLiteral("invoke skill"),
            requestMessage(S_COMMAND_INVOKE_SKILL, 105,
                           QVariantList{QStringLiteral("test_skill"),
                                        QStringLiteral("playerdata:sgs1")}),
            replyMessage(S_COMMAND_INVOKE_SKILL, 205, 105, true),
            "[105,0,1041,8,[\"test_skill\",\"playerdata:sgs1\"]]",
            "{\"command\":8,\"destination\":\"client\",\"message_id\":\"105\","
            "\"payload\":{\"data\":\"playerdata:sgs1\",\"schema_version\":1,"
            "\"skill_name\":\"test_skill\"},\"source\":\"room\","
            "\"type\":\"request\",\"v\":2}",
            "[205,105,322,8,true]",
            "{\"command\":8,\"destination\":\"room\",\"message_id\":\"205\","
            "\"payload\":{\"invoke\":true,\"schema_version\":1},"
            "\"reply_to\":\"105\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        },
        {
            QStringLiteral("surrender vote"),
            requestMessage(S_COMMAND_SURRENDER, 106, QStringLiteral("caocao")),
            replyMessage(S_COMMAND_SURRENDER, 206, 106, false),
            "[106,0,1041,34,\"caocao\"]",
            "{\"command\":34,\"destination\":\"client\",\"message_id\":\"106\","
            "\"payload\":{\"initiator_general\":\"caocao\",\"schema_version\":1},"
            "\"source\":\"room\",\"type\":\"request\",\"v\":2}",
            "[206,106,322,34,false]",
            "{\"command\":34,\"destination\":\"room\",\"message_id\":\"206\","
            "\"payload\":{\"schema_version\":1,\"surrender\":false},"
            "\"reply_to\":\"106\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
        }
    };
}

bool goldenWireReplayAndLogicalParity()
{
    const ProtocolCodecRouter router;
    for (const GoldenInteraction &interaction : goldenInteractions()) {
        QString error;
        if (!expect(router.encode(ProtocolVersion::V1, interaction.request, &error)
                        == interaction.v1Request,
                    interaction.name + QStringLiteral(" exact V1 request"))
            || !expect(router.encode(ProtocolVersion::V2, interaction.request, &error)
                           == interaction.v2Request,
                       interaction.name + QStringLiteral(" exact V2 request"))
            || !expect(router.encode(ProtocolVersion::V1, interaction.reply, &error)
                           == interaction.v1Reply,
                       interaction.name + QStringLiteral(" exact V1 reply"))
            || !expect(router.encode(ProtocolVersion::V2, interaction.reply, &error)
                           == interaction.v2Reply,
                       interaction.name + QStringLiteral(" exact V2 reply"))) {
            return false;
        }

        ProtocolMessage decodedRequest;
        ProtocolMessage decodedReply;
        if (!expect(router.decode(ProtocolVersion::V2, interaction.v2Request,
                                  &decodedRequest).success
                        && decodedRequest.hasPayload == interaction.request.hasPayload
                        && decodedRequest.payload == interaction.request.payload,
                    interaction.name + QStringLiteral(" request logical parity"))
            || !expect(router.decode(ProtocolVersion::V2, interaction.v2Reply,
                                     &decodedReply).success
                           && decodedReply.hasPayload == interaction.reply.hasPayload
                           && decodedReply.payload == interaction.reply.payload,
                       interaction.name + QStringLiteral(" reply logical parity"))
            || !expect(router.encodeReplayV1(decodedRequest, &error)
                           == interaction.v1Request,
                       interaction.name + QStringLiteral(" request replay V1"))
            || !expect(router.encodeReplayV1(decodedReply, &error)
                           == interaction.v1Reply,
                       interaction.name + QStringLiteral(" reply replay V1"))) {
            return false;
        }
    }
    return true;
}

QByteArray mutatePayload(const QByteArray &wire,
                         const std::function<void(QJsonObject &)> &mutation)
{
    QJsonObject root = QJsonDocument::fromJson(wire).object();
    QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
    mutation(payload);
    root.insert(QStringLiteral("payload"), payload);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool strictSchemasAndTransactionality()
{
    const ProtocolCodecRouter router;
    const QList<QVariant> invalidSchemas{
        QVariant(), 0, 2, QStringLiteral("1"), true, 1.5
    };
    for (const GoldenInteraction &interaction : goldenInteractions()) {
        for (const QByteArray &wire : {interaction.v2Request, interaction.v2Reply}) {
            const QJsonObject originalPayload = QJsonDocument::fromJson(wire).object()
                .value(QStringLiteral("payload")).toObject();
            for (const QVariant &schema : invalidSchemas) {
                const QByteArray invalid = mutatePayload(wire, [&schema](QJsonObject &payload) {
                    if (schema.isValid())
                        payload.insert(QStringLiteral("schema_version"),
                                       QJsonValue::fromVariant(schema));
                    else
                        payload.remove(QStringLiteral("schema_version"));
                });
                ProtocolMessage sentinel = replyMessage(
                    S_COMMAND_MULTIPLE_CHOICE, 999, 998, QStringLiteral("unchanged"));
                const ProtocolDecodeResult result = router.decode(
                    ProtocolVersion::V2, invalid, &sentinel);
                if (!expect(!result.success
                                && result.error == ProtocolDecodeError::InvalidPayload
                                && !result.detail.isEmpty()
                                && sentinel.payload == QStringLiteral("unchanged"),
                            interaction.name
                                + QStringLiteral(" rejects schema transactionally"))) {
                    return false;
                }
            }

            const QByteArray nullSchema = mutatePayload(wire, [](QJsonObject &payload) {
                payload.insert(QStringLiteral("schema_version"), QJsonValue::Null);
            });
            ProtocolMessage nullOutput;
            const ProtocolDecodeResult nullResult = router.decode(
                ProtocolVersion::V2, nullSchema, &nullOutput);
            if (!expect(!nullResult.success
                            && nullResult.error == ProtocolDecodeError::InvalidPayload,
                        interaction.name + QStringLiteral(" rejects null schema"))) {
                return false;
            }

            for (auto it = originalPayload.constBegin(); it != originalPayload.constEnd(); ++it) {
                if (it.key() == QStringLiteral("schema_version"))
                    continue;
                const QString requiredKey = it.key();
                const QByteArray missing = mutatePayload(
                    wire, [&requiredKey](QJsonObject &payload) { payload.remove(requiredKey); });
                ProtocolMessage output;
                const ProtocolDecodeResult result = router.decode(
                    ProtocolVersion::V2, missing, &output);
                if (!expect(!result.success
                                && result.error == ProtocolDecodeError::InvalidPayload,
                            interaction.name + QStringLiteral(" requires ") + requiredKey)) {
                    return false;
                }

                const QByteArray wrongType = mutatePayload(
                    wire, [&requiredKey](QJsonObject &payload) {
                        payload.insert(requiredKey,
                                       QJsonObject{{QStringLiteral("wrong"), true}});
                    });
                ProtocolMessage wrongTypeOutput;
                const ProtocolDecodeResult wrongTypeResult = router.decode(
                    ProtocolVersion::V2, wrongType, &wrongTypeOutput);
                if (!expect(!wrongTypeResult.success
                                && wrongTypeResult.error
                                    == ProtocolDecodeError::InvalidPayload,
                            interaction.name + QStringLiteral(" rejects wrong type for ")
                                + requiredKey)) {
                    return false;
                }
            }

            const QByteArray extended = mutatePayload(wire, [](QJsonObject &payload) {
                payload.insert(QStringLiteral("future_field"), QStringLiteral("ignored"));
            });
            ProtocolMessage decoded;
            if (!expect(router.decode(ProtocolVersion::V2, extended, &decoded).success,
                        interaction.name + QStringLiteral(" ignores unknown fields"))) {
                return false;
            }
        }
    }

    const QList<QByteArray> legacyShapes{
        "{\"command\":10,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":[\"caocao\"],\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":12,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":\"spade\",\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":11,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":[\"wei+shu\"],\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":17,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":0,\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":8,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":[\"skill\",\"data\"],\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":34,\"destination\":\"client\",\"message_id\":\"1\","
        "\"payload\":\"caocao\",\"source\":\"room\",\"type\":\"request\",\"v\":2}",
        "{\"command\":10,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":\"caocao\",\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}",
        "{\"command\":12,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":\"spade\",\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}",
        "{\"command\":11,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":\"wei\",\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}",
        "{\"command\":17,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":0,\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}",
        "{\"command\":8,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":true,\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}",
        "{\"command\":34,\"destination\":\"room\",\"message_id\":\"2\","
        "\"payload\":false,\"reply_to\":\"1\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}"
    };
    for (qsizetype index = 0; index < legacyShapes.size(); ++index) {
        ProtocolMessage output;
        const ProtocolDecodeResult result = router.decode(
            ProtocolVersion::V2, legacyShapes.at(index), &output);
        if (!expect(!result.success && result.error == ProtocolDecodeError::InvalidPayload,
                    QStringLiteral("legacy V2 shape %1 rejected").arg(index))) {
            return false;
        }
    }
    return true;
}

bool edgeSemantics()
{
    QString error;
    ChooseGeneralRequestPayload general;
    QVariantMap generalObject{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("candidates"),
         QVariantList{QStringLiteral("曹操"), QStringLiteral("custom_general")}},
        {QStringLiteral("future"), true}
    };
    ChooseGeneralReplyPayload generalReply;
    if (!expect(ChooseGeneralRequestPayload::parseV2(
                    generalObject, &general, &error)
                    && general.candidates
                        == QStringList{QStringLiteral("曹操"),
                                       QStringLiteral("custom_general")},
                QStringLiteral("general Unicode candidates"))
        || !expect(ChooseGeneralRequestPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("candidates"), QVariantList{}}},
                       &general, &error)
                       && general.candidates.isEmpty(),
                   QStringLiteral("general empty candidates"))
        || !expect(ChooseGeneralReplyPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("general"),
                                    QStringLiteral("outside_candidates")}},
                       &generalReply, &error),
                   QStringLiteral("general reply remains structural"))) {
        return false;
    }

    ChooseSuitReplyPayload suit;
    if (!expect(ChooseSuitReplyPayload::parseV2(
                    QVariantMap{{QStringLiteral("schema_version"), 1},
                                {QStringLiteral("suit"), QStringLiteral("future_suit")}},
                    &suit, &error),
                QStringLiteral("suit reply remains structural"))) {
        return false;
    }

    ChooseKingdomRequestPayload kingdom;
    if (!expect(ChooseKingdomRequestPayload::parseLegacy(
                    QVariantList{QString()}, &kingdom, &error)
                    && kingdom.kingdoms == QStringList{QString()},
                QStringLiteral("kingdom empty legacy token preserved"))
        || !expect(ChooseKingdomRequestPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("kingdoms"),
                                    QVariantList{QStringLiteral("魏"),
                                                 QStringLiteral("神")}}},
                       &kingdom, &error)
                       && kingdom.kingdoms
                           == QStringList{QStringLiteral("魏"), QStringLiteral("神")},
                   QStringLiteral("kingdom Unicode order preserved"))
        || !expect(ChooseKingdomRequestPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("kingdoms"), QVariantList{}}},
                       &kingdom, &error)
                       && kingdom.kingdoms.isEmpty()
                       && kingdom.toLegacyVariant()
                           == QVariant(QVariantList{QString()}),
                   QStringLiteral("kingdom empty typed edge normalizes like legacy"))) {
        return false;
    }

    ChooseOrderRequestPayload orderRequest;
    ChooseOrderReplyPayload orderReply;
    if (!expect(ChooseOrderRequestPayload::parseV2(
                    QVariantMap{{QStringLiteral("schema_version"), 1},
                                {QStringLiteral("reason"), QStringLiteral("select")}},
                    &orderRequest, &error)
                    && orderRequest.reason == S_REASON_CHOOSE_ORDER_SELECT,
                QStringLiteral("order select mapping"))
        || !expect(ChooseOrderReplyPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("camp"), QStringLiteral("cool")}},
                       &orderReply, &error)
                       && orderReply.camp == S_CAMP_COOL,
                   QStringLiteral("order cool mapping"))
        || !expect(!ChooseOrderRequestPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("reason"), QStringLiteral("unknown")}},
                       &orderRequest, &error),
                   QStringLiteral("unknown order reason rejected"))
        || !expect(!ChooseOrderReplyPayload::parseV2(
                       QVariantMap{{QStringLiteral("schema_version"), 1},
                                   {QStringLiteral("camp"), QStringLiteral("unknown")}},
                       &orderReply, &error),
                   QStringLiteral("unknown order camp rejected"))) {
        return false;
    }

    InvokeSkillRequestPayload invoke;
    SurrenderVoteRequestPayload surrender;
    return expect(InvokeSkillRequestPayload::parseV2(
                      QVariantMap{{QStringLiteral("schema_version"), 1},
                                  {QStringLiteral("skill_name"), QStringLiteral("技能")},
                                  {QStringLiteral("data"), QString()}},
                      &invoke, &error)
                      && invoke.skillName == QStringLiteral("技能")
                      && invoke.data.isEmpty(),
                  QStringLiteral("invoke opaque empty data and Unicode"))
        && expect(SurrenderVoteRequestPayload::parseV2(
                      QVariantMap{{QStringLiteral("schema_version"), 1},
                                  {QStringLiteral("initiator_general"),
                                   QStringLiteral("曹操")}},
                      &surrender, &error)
                      && surrender.initiatorGeneral == QStringLiteral("曹操"),
                  QStringLiteral("surrender Unicode general"));
}

bool inventoryAndIdentityExceptions()
{
    const QList<int> migrated{
        S_COMMAND_MULTIPLE_CHOICE,
        S_COMMAND_CHOOSE_GENERAL,
        S_COMMAND_CHOOSE_SUIT,
        S_COMMAND_CHOOSE_KINGDOM,
        S_COMMAND_CHOOSE_ORDER,
        S_COMMAND_INVOKE_SKILL,
        S_COMMAND_SURRENDER
    };
    if (!expect(ProtocolGameplayPayloadRegistry::migratedCommandCount() == 29,
                QStringLiteral("migration count is 29"))) {
        return false;
    }
    for (int command : migrated) {
        ProtocolMessage request = requestMessage(command, 1, QStringLiteral("shape"));
        ProtocolMessage reply = replyMessage(command, 2, 1, QStringLiteral("shape"));
        if (!expect(ProtocolGameplayPayloadRegistry::isMigratedCommand(command),
                    QStringLiteral("command %1 is registered").arg(command))
            || !expect(ProtocolGameplayPayloadRegistry::isMigratedFlow(request),
                       QStringLiteral("command %1 request flow is registered").arg(command))
            || !expect(ProtocolGameplayPayloadRegistry::isMigratedFlow(reply),
                       QStringLiteral("command %1 reply flow is registered").arg(command))) {
            return false;
        }
    }

    const ProtocolCodecRouter router;
    QString error;
    ProtocolMessage invokeNotification;
    invokeNotification.type = ProtocolMessageType::Notification;
    invokeNotification.source = ProtocolEndpoint::Room;
    invokeNotification.destination = ProtocolEndpoint::Client;
    invokeNotification.messageId = 7;
    invokeNotification.command = S_COMMAND_INVOKE_SKILL;
    invokeNotification.hasPayload = true;
    invokeNotification.payload = QVariantList{
        QStringLiteral("skill_name"), QStringLiteral("player_name")};

    ProtocolMessage surrenderInitiation;
    surrenderInitiation.type = ProtocolMessageType::Request;
    surrenderInitiation.source = ProtocolEndpoint::Client;
    surrenderInitiation.destination = ProtocolEndpoint::Room;
    surrenderInitiation.messageId = 8;
    surrenderInitiation.command = S_COMMAND_SURRENDER;
    surrenderInitiation.hasPayload = false;

    const QList<ProtocolMessage> exceptions{
        invokeNotification, surrenderInitiation};
    for (qsizetype index = 0; index < exceptions.size(); ++index) {
        ProtocolMessage wire;
        if (!expect(!ProtocolGameplayPayloadRegistry::isMigratedFlow(exceptions.at(index)),
                    QStringLiteral("exception flow %1 is identity").arg(index))
            || !expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                           ProtocolVersion::V2, exceptions.at(index), &wire, &error)
                           && wire.hasPayload == exceptions.at(index).hasPayload
                           && wire.payload == exceptions.at(index).payload,
                       QStringLiteral("exception flow %1 wire unchanged").arg(index))) {
            return false;
        }
    }

    return expect(ProtocolGameplayPayloadRegistry::isMigratedCommand(S_COMMAND_LUCK_CARD),
                  QStringLiteral("luck card migrated"))
        && expect(ProtocolGameplayPayloadRegistry::isMigratedCommand(
                      S_COMMAND_CHOOSE_DIRECTION),
                  QStringLiteral("choose direction migrated"));
}
}

int main()
{
    const bool success = goldenWireReplayAndLogicalParity()
        && strictSchemasAndTransactionality()
        && edgeSemantics()
        && inventoryAndIdentityExceptions();
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_SIMPLE_CHOICE_PAYLOAD_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
