#include "json.h"
#include "protocol.h"
#include "protocol/gameplay/multiple-choice-payload.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/protocol-runtime.h"

#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

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

ProtocolMessage requestMessage()
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = 42;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = QVariantList{
        QStringLiteral("test_skill"),
        QStringLiteral("alpha+beta+gamma"),
        QStringLiteral("beta"),
        QStringLiteral("choose")
    };
    return message;
}

ProtocolMessage replyMessage(const QString &choice = QStringLiteral("cancel"))
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.messageId = 81;
    message.replyTo = 42;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = choice;
    return message;
}

bool payloadValueObjects()
{
    MultipleChoiceRequestPayload legacy;
    const QVariantList legacyValue{
        QStringLiteral("skill"),
        QStringLiteral("first++first"),
        QStringLiteral("+disabled++foreign+"),
        QStringLiteral("tip")
    };
    QString error = QStringLiteral("stale");
    if (!expect(MultipleChoiceRequestPayload::parseLegacy(
                    legacyValue, &legacy, &error),
                QStringLiteral("legacy request parse"))
        || !expect(error.isEmpty(), QStringLiteral("legacy parse clears error"))
        || !expect(legacy.skillName == QStringLiteral("skill")
                       && legacy.options == QStringList({QStringLiteral("first"),
                                                        QString(),
                                                        QStringLiteral("first")})
                       && legacy.disabledOptions
                           == QStringList({QStringLiteral("disabled"),
                                           QStringLiteral("foreign")})
                       && legacy.tip == QStringLiteral("tip"),
                   QStringLiteral("legacy delimiter semantics"))) {
        return false;
    }

    QVariantMap typed;
    typed.insert(QStringLiteral("schema_version"), 1);
    typed.insert(QStringLiteral("skill_name"), QStringLiteral("skill"));
    typed.insert(QStringLiteral("options"), QVariantList{
        QStringLiteral("same"), QStringLiteral("same"), QString(),
        QStringLiteral("contains+delimiter")
    });
    typed.insert(QStringLiteral("disabled_options"), QVariantList{
        QStringLiteral("not-an-option"), QString()
    });
    typed.insert(QStringLiteral("tip"), QStringLiteral("tip"));
    typed.insert(QStringLiteral("future_field"), true);

    MultipleChoiceRequestPayload parsed;
    if (!expect(MultipleChoiceRequestPayload::parseV2(typed, &parsed, &error),
                QStringLiteral("typed request accepts unknown field"))
        || !expect(parsed.options == QStringList({QStringLiteral("same"),
                                                  QStringLiteral("same"), QString(),
                                                  QStringLiteral("contains+delimiter")})
                       && parsed.disabledOptions
                           == QStringList({QStringLiteral("not-an-option"), QString()}),
                   QStringLiteral("typed request performs structural validation only"))
        || !expect(parsed.toV2Variant().value(QStringLiteral("future_field")).isNull(),
                   QStringLiteral("typed request canonical encoder omits unknown field"))) {
        return false;
    }

    MultipleChoiceReplyPayload reply;
    QVariantMap typedReply{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("choice"), QStringLiteral("cancel")},
        {QStringLiteral("future_field"), 7}
    };
    return expect(MultipleChoiceReplyPayload::parseV2(typedReply, &reply, &error)
                      && reply.choice == QStringLiteral("cancel")
                      && reply.toLegacyVariant() == QStringLiteral("cancel"),
                  QStringLiteral("typed cancel reply"));
}

bool malformedPayloads()
{
    QVariantMap validRequest{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("skill_name"), QStringLiteral("skill")},
        {QStringLiteral("options"), QVariantList{QStringLiteral("a")}},
        {QStringLiteral("disabled_options"), QVariantList{}},
        {QStringLiteral("tip"), QStringLiteral("tip")}
    };

    QList<QPair<QVariant, QString>> invalidRequests;
    invalidRequests.append({QStringLiteral("scalar"), QStringLiteral("request root")});
    for (const QString &field : {QStringLiteral("schema_version"),
                                 QStringLiteral("skill_name"),
                                 QStringLiteral("options"),
                                 QStringLiteral("disabled_options"),
                                 QStringLiteral("tip")}) {
        QVariantMap missing = validRequest;
        missing.remove(field);
        invalidRequests.append({missing, QStringLiteral("missing %1").arg(field)});
    }
    for (const QVariant &schema : {QVariant(0), QVariant(2),
                                   QVariant(QStringLiteral("1")), QVariant(true),
                                   QVariant(1.5)}) {
        QVariantMap invalid = validRequest;
        invalid.insert(QStringLiteral("schema_version"), schema);
        invalidRequests.append({invalid, QStringLiteral("invalid schema")});
    }
    QVariantMap stringOptions = validRequest;
    stringOptions.insert(QStringLiteral("options"), QStringLiteral("a+b"));
    invalidRequests.append({stringOptions, QStringLiteral("options scalar")});
    QVariantMap mixedOptions = validRequest;
    mixedOptions.insert(QStringLiteral("options"), QVariantList{QStringLiteral("a"), 2});
    invalidRequests.append({mixedOptions, QStringLiteral("options mixed type")});
    QVariantMap mixedDisabled = validRequest;
    mixedDisabled.insert(QStringLiteral("disabled_options"), QVariantList{false});
    invalidRequests.append({mixedDisabled, QStringLiteral("disabled mixed type")});
    QVariantMap numericSkill = validRequest;
    numericSkill.insert(QStringLiteral("skill_name"), 7);
    invalidRequests.append({numericSkill, QStringLiteral("skill type")});
    QVariantMap numericTip = validRequest;
    numericTip.insert(QStringLiteral("tip"), 7);
    invalidRequests.append({numericTip, QStringLiteral("tip type")});

    for (const auto &testCase : invalidRequests) {
        MultipleChoiceRequestPayload sentinel;
        sentinel.skillName = QStringLiteral("unchanged");
        QString error;
        if (!expect(!MultipleChoiceRequestPayload::parseV2(
                        testCase.first, &sentinel, &error)
                        && !error.isEmpty()
                        && sentinel.skillName == QStringLiteral("unchanged"),
                    testCase.second + QStringLiteral(" rejected transactionally"))) {
            return false;
        }
    }

    const QList<QVariant> invalidLegacyRequests = {
        QStringLiteral("not-array"),
        QVariantList{QStringLiteral("skill"), QStringLiteral("a"),
                     QStringLiteral("disabled")},
        QVariantList{QStringLiteral("skill"), QStringLiteral("a"), 7,
                     QStringLiteral("tip")}
    };
    for (qsizetype index = 0; index < invalidLegacyRequests.size(); ++index) {
        MultipleChoiceRequestPayload output;
        QString error;
        if (!expect(!MultipleChoiceRequestPayload::parseLegacy(
                        invalidLegacyRequests.at(index), &output, &error)
                        && !error.isEmpty(),
                    QStringLiteral("invalid legacy request %1").arg(index))) {
            return false;
        }
    }

    QVariantMap validReply{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("choice"), QStringLiteral("cancel")}
    };
    const QList<QVariant> invalidReplies = {
        QStringLiteral("cancel"),
        QVariantMap{{QStringLiteral("choice"), QStringLiteral("cancel")}},
        QVariantMap{{QStringLiteral("schema_version"), 2},
                    {QStringLiteral("choice"), QStringLiteral("cancel")}},
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("choice"), 7}}
    };
    for (qsizetype index = 0; index < invalidReplies.size(); ++index) {
        MultipleChoiceReplyPayload output;
        QString error;
        if (!expect(!MultipleChoiceReplyPayload::parseV2(
                        invalidReplies.at(index), &output, &error)
                        && !error.isEmpty(),
                    QStringLiteral("invalid typed reply %1").arg(index))) {
            return false;
        }
    }

    MultipleChoiceReplyPayload reply;
    QString error;
    return expect(MultipleChoiceReplyPayload::parseV2(validReply, &reply, &error),
                  QStringLiteral("valid reply control"))
        && expect(!MultipleChoiceReplyPayload::parseLegacy(7, &reply, &error),
                  QStringLiteral("numeric legacy reply rejected"))
        && expect(!MultipleChoiceRequestPayload::parseV2(validRequest, nullptr, &error),
                  QStringLiteral("null request output rejected"))
        && expect(!MultipleChoiceReplyPayload::parseV2(validReply, nullptr, &error),
                  QStringLiteral("null reply output rejected"));
}

bool registryInventoryAndDirections()
{
    if (!expect(ProtocolGameplayPayloadRegistry::migratedCommandCount() == 7,
                QStringLiteral("seven migrated commands"))
        || !expect(ProtocolGameplayPayloadRegistry::isMigratedCommand(
                       S_COMMAND_MULTIPLE_CHOICE),
                   QStringLiteral("multiple choice registered"))
        || !expect(!ProtocolGameplayPayloadRegistry::isMigratedCommand(
                       S_COMMAND_CHOOSE_PLAYER),
                   QStringLiteral("non-migrated command absent"))) {
        return false;
    }

    ProtocolMessage logical = requestMessage();
    ProtocolMessage wire;
    QString error;
    if (!expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                    ProtocolVersion::V2, logical, &wire, &error)
                    && wire.payload.userType() == QMetaType::QVariantMap,
                QStringLiteral("formal request transforms"))) {
        return false;
    }

    ProtocolMessage otherDirection = logical;
    otherDirection.type = ProtocolMessageType::Notification;
    if (!expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                    ProtocolVersion::V2, otherDirection, &wire, &error)
                    && wire.payload == otherDirection.payload,
                QStringLiteral("other direction remains identity"))) {
        return false;
    }

    ProtocolMessage noPayload = logical;
    noPayload.hasPayload = false;
    ProtocolMessage sentinel = replyMessage(QStringLiteral("unchanged"));
    if (!expect(!ProtocolGameplayPayloadRegistry::encodeForWire(
                    ProtocolVersion::V2, noPayload, &sentinel, &error)
                    && !error.isEmpty()
                    && sentinel.payload == QStringLiteral("unchanged"),
                QStringLiteral("registry failure is transactional"))
        || !expect(!ProtocolGameplayPayloadRegistry::encodeForWire(
                       ProtocolVersion::V2, logical, nullptr, &error),
                   QStringLiteral("null wire output rejected"))
        || !expect(!ProtocolGameplayPayloadRegistry::decodeFromWire(
                       ProtocolVersion::V2, wire, nullptr, &error),
                   QStringLiteral("null logical output rejected"))) {
        return false;
    }

    return expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                      ProtocolVersion::V1, logical, &wire, &error)
                      && wire.payload == logical.payload,
                  QStringLiteral("V1 registry identity"));
}

bool goldenWireAndReplay()
{
    const ProtocolCodecRouter router;
    const ProtocolMessage request = requestMessage();
    const QByteArray v1Request =
        "[42,0,1041,24,[\"test_skill\",\"alpha+beta+gamma\",\"beta\",\"choose\"]]";
    const QByteArray v2Request =
        "{\"command\":24,\"destination\":\"client\",\"message_id\":\"42\","
        "\"payload\":{\"disabled_options\":[\"beta\"],"
        "\"options\":[\"alpha\",\"beta\",\"gamma\"],\"schema_version\":1,"
        "\"skill_name\":\"test_skill\",\"tip\":\"choose\"},"
        "\"source\":\"room\",\"type\":\"request\",\"v\":2}";
    QString error;
    if (!expect(router.encode(ProtocolVersion::V1, request, &error) == v1Request
                    && error.isEmpty(),
                QStringLiteral("exact V1 request golden"))
        || !expect(router.encode(ProtocolVersion::V2, request, &error) == v2Request
                       && error.isEmpty(),
                   QStringLiteral("exact V2 request golden"))) {
        return false;
    }

    const ProtocolMessage reply = replyMessage();
    const QByteArray v1Reply = "[81,42,322,24,\"cancel\"]";
    const QByteArray v2Reply =
        "{\"command\":24,\"destination\":\"room\",\"message_id\":\"81\","
        "\"payload\":{\"choice\":\"cancel\",\"schema_version\":1},"
        "\"reply_to\":\"42\",\"source\":\"client\",\"type\":\"reply\",\"v\":2}";
    if (!expect(router.encode(ProtocolVersion::V1, reply, &error) == v1Reply,
                QStringLiteral("exact V1 reply golden"))
        || !expect(router.encode(ProtocolVersion::V2, reply, &error) == v2Reply,
                   QStringLiteral("exact V2 reply golden"))) {
        return false;
    }

    ProtocolMessage decodedRequest;
    ProtocolMessage decodedReply;
    if (!expect(router.decode(ProtocolVersion::V2, v2Request, &decodedRequest).success
                    && decodedRequest.payload == request.payload,
                QStringLiteral("typed request normalizes to legacy logical payload"))
        || !expect(router.decode(ProtocolVersion::V2, v2Reply, &decodedReply).success
                       && decodedReply.payload == reply.payload,
                   QStringLiteral("typed reply normalizes to legacy logical payload"))
        || !expect(router.encodeReplayV1(decodedRequest, &error) == v1Request,
                   QStringLiteral("V2 request replay normalizes to exact V1"))
        || !expect(router.encodeReplayV1(decodedReply, &error) == v1Reply,
                   QStringLiteral("V2 reply replay normalizes to exact V1"))) {
        return false;
    }

    ProtocolMessage sentinel = replyMessage(QStringLiteral("unchanged"));
    const QByteArray legacyRequestInsideV2 =
        "{\"command\":24,\"destination\":\"client\",\"message_id\":\"42\","
        "\"payload\":[\"test_skill\",\"a+b\",\"\",\"tip\"],"
        "\"source\":\"room\",\"type\":\"request\",\"v\":2}";
    const ProtocolDecodeResult invalidRequest = router.decode(
        ProtocolVersion::V2, legacyRequestInsideV2, &sentinel);
    const QByteArray legacyReplyInsideV2 =
        "{\"command\":24,\"destination\":\"room\",\"message_id\":\"81\","
        "\"payload\":\"cancel\",\"reply_to\":\"42\",\"source\":\"client\","
        "\"type\":\"reply\",\"v\":2}";
    const ProtocolDecodeResult invalidReply = router.decode(
        ProtocolVersion::V2, legacyReplyInsideV2, &sentinel);
    return expect(!invalidRequest.success
                      && invalidRequest.error == ProtocolDecodeError::InvalidPayload
                      && !invalidRequest.detail.isEmpty(),
                  QStringLiteral("V2 request rejects legacy shape"))
        && expect(!invalidReply.success
                      && invalidReply.error == ProtocolDecodeError::InvalidPayload
                      && !invalidReply.detail.isEmpty(),
                  QStringLiteral("V2 reply rejects legacy shape"))
        && expect(sentinel.payload == QStringLiteral("unchanged"),
                  QStringLiteral("router payload failures leave output unchanged"));
}

bool nonMigratedIdentity()
{
    ProtocolMessage logical;
    logical.type = ProtocolMessageType::Request;
    logical.source = ProtocolEndpoint::Room;
    logical.destination = ProtocolEndpoint::Client;
    logical.messageId = 7;
    logical.command = S_COMMAND_CHOOSE_PLAYER;
    logical.hasPayload = true;
    logical.payload = QVariantList{QStringLiteral("caocao"), QStringLiteral("liubei")};

    const ProtocolCodecRouter router;
    QString error;
    const QByteArray wire = router.encode(ProtocolVersion::V2, logical, &error);
    const QByteArray expected =
        "{\"command\":16,\"destination\":\"client\",\"message_id\":\"7\","
        "\"payload\":[\"caocao\",\"liubei\"],\"source\":\"room\","
        "\"type\":\"request\",\"v\":2}";
    ProtocolMessage decoded;
    return expect(wire == expected, QStringLiteral("non-migrated V2 wire identity"))
        && expect(router.decode(ProtocolVersion::V2, wire, &decoded).success
                      && decoded.payload == logical.payload,
                  QStringLiteral("non-migrated V2 decode identity"));
}
}

int main()
{
    const bool success = payloadValueObjects()
        && malformedPayloads()
        && registryInventoryAndDirections()
        && goldenWireAndReplay()
        && nonMigratedIdentity();
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_GAMEPLAY_PAYLOAD_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
