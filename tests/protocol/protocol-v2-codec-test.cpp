#include "protocol/protocol-v2-codec.h"

#include <QByteArray>
#include <QDateTime>
#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

#include <limits>

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
    message.version = ProtocolVersion::V2;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = 42;
    message.command = 37;
    return message;
}

bool semanticallyEqual(const ProtocolMessage &left, const ProtocolMessage &right)
{
    return left.version == right.version
        && left.type == right.type
        && left.source == right.source
        && left.destination == right.destination
        && left.messageId == right.messageId
        && left.replyTo == right.replyTo
        && left.command == right.command
        && left.hasPayload == right.hasPayload
        && (!left.hasPayload || left.payload == right.payload);
}

bool expectWire(const ProtocolV2Codec &codec, const ProtocolMessage &message,
                const QByteArray &expected, const QString &label)
{
    QString error = QStringLiteral("stale");
    const QByteArray actual = codec.encode(message, &error);
    if (actual == expected && error.isEmpty()) {
        ++testCaseCount;
        return true;
    }
    ++testCaseCount;
    QTextStream(stderr) << label << " failed\nexpected: " << expected
                        << "\nactual:   " << actual
                        << "\nerror:    " << error << "\n";
    return false;
}

bool expectEncodeFailure(const ProtocolV2Codec &codec,
                         const ProtocolMessage &message, const QString &label)
{
    QString error = QStringLiteral("stale");
    const QByteArray encoded = codec.encode(message, &error);
    return expect(encoded.isEmpty() && !error.isEmpty(), label);
}

ProtocolMessage failureSentinel()
{
    ProtocolMessage message = requestMessage();
    message.messageId = 91;
    message.command = -17;
    message.hasPayload = true;
    message.payload = QStringLiteral("unchanged");
    return message;
}

bool expectDecodeFailure(const ProtocolV2Codec &codec, const QByteArray &raw,
                         ProtocolDecodeError error, const QString &label)
{
    ProtocolMessage output = failureSentinel();
    const ProtocolMessage original = output;
    const ProtocolDecodeResult result = codec.decode(raw, &output);
    return expect(!result.success && result.error == error
                      && !result.detail.isEmpty()
                      && semanticallyEqual(output, original),
                  label);
}

bool goldenEncode()
{
    const ProtocolV2Codec codec;
    ProtocolMessage request = requestMessage();
    if (!expect(codec.version() == ProtocolVersion::V2, QStringLiteral("codec version"))
        || !expectWire(codec, request,
                       "{\"command\":37,\"destination\":\"client\",\"message_id\":\"42\","
                       "\"source\":\"room\",\"type\":\"request\",\"v\":2}",
                       QStringLiteral("request without payload"))) {
        return false;
    }

    request.hasPayload = true;
    request.payload = true;
    if (!expectWire(codec, request,
                    "{\"command\":37,\"destination\":\"client\",\"message_id\":\"42\","
                    "\"payload\":true,\"source\":\"room\",\"type\":\"request\",\"v\":2}",
                    QStringLiteral("request scalar payload"))) {
        return false;
    }

    QVariantMap object;
    object.insert(QStringLiteral("choice"), QStringLiteral("中"));
    request.payload = object;
    if (!expectWire(codec, request,
                    QStringLiteral("{\"command\":37,\"destination\":\"client\","
                                   "\"message_id\":\"42\",\"payload\":{\"choice\":\"中\"},"
                                   "\"source\":\"room\",\"type\":\"request\",\"v\":2}")
                        .toUtf8(),
                    QStringLiteral("request object payload"))) {
        return false;
    }

    ProtocolMessage reply = requestMessage();
    reply.type = ProtocolMessageType::Reply;
    reply.source = ProtocolEndpoint::Client;
    reply.destination = ProtocolEndpoint::Room;
    reply.messageId = 81;
    reply.replyTo = 42;
    reply.hasPayload = true;
    reply.payload = object;
    if (!expectWire(codec, reply,
                    QStringLiteral("{\"command\":37,\"destination\":\"room\","
                                   "\"message_id\":\"81\",\"payload\":{\"choice\":\"中\"},"
                                   "\"reply_to\":\"42\",\"source\":\"client\","
                                   "\"type\":\"reply\",\"v\":2}")
                        .toUtf8(),
                    QStringLiteral("reply golden"))) {
        return false;
    }

    ProtocolMessage notification = requestMessage();
    notification.type = ProtocolMessageType::Notification;
    notification.messageId = 43;
    notification.command = 90;
    if (!expectWire(codec, notification,
                    "{\"command\":90,\"destination\":\"client\",\"message_id\":\"43\","
                    "\"source\":\"room\",\"type\":\"notification\",\"v\":2}",
                    QStringLiteral("notification golden"))) {
        return false;
    }

    notification.hasPayload = true;
    notification.payload = QVariant();
    if (!expectWire(codec, notification,
                    "{\"command\":90,\"destination\":\"client\",\"message_id\":\"43\","
                    "\"payload\":null,\"source\":\"room\","
                    "\"type\":\"notification\",\"v\":2}",
                    QStringLiteral("explicit null payload"))) {
        return false;
    }

    notification.messageId = 44;
    notification.command = 64;
    notification.payload = QStringLiteral("中文 日本語 emoji 😀 café");
    const QByteArray unicode = codec.encode(notification);
    if (!expect(unicode.contains(QStringLiteral("中文 日本語 emoji 😀 café").toUtf8()),
                QStringLiteral("UTF-8 payload"))
        || !expect(!unicode.contains('\n'), QStringLiteral("codec excludes newline framing"))) {
        return false;
    }

    QVariantMap innerObject;
    innerObject.insert(QStringLiteral("inner"), QStringLiteral("值"));
    QVariantList nestedArray;
    nestedArray << 1 << QVariant::fromValue(innerObject);
    QVariantMap nestedObject;
    nestedObject.insert(QStringLiteral("outer"), QVariant::fromValue(nestedArray));
    request.payload = nestedObject;
    const QByteArray nestedWire = codec.encode(request);
    ProtocolMessage nestedDecoded;
    if (!expect(!nestedWire.isEmpty(), QStringLiteral("nested arrays and maps encode"))
        || !expect(codec.decode(nestedWire, &nestedDecoded).success
                       && nestedDecoded.payload == request.payload,
                   QStringLiteral("nested arrays and maps round trip"))) {
        return false;
    }

    QVariant reasonablyDeep = 1;
    for (int depth = 0; depth < 64; ++depth)
        reasonablyDeep = QVariant::fromValue(QVariantList() << reasonablyDeep);
    request.payload = reasonablyDeep;
    ProtocolMessage deepDecoded;
    const QByteArray deepWire = codec.encode(request);
    if (!expect(!deepWire.isEmpty() && codec.decode(deepWire, &deepDecoded).success,
                QStringLiteral("reasonable payload nesting"))) {
        return false;
    }

    request.hasPayload = false;
    request.command = 4096;
    if (!expect(codec.encode(request).contains("\"command\":4096"),
                QStringLiteral("unknown numeric command encode"))) {
        return false;
    }

    request.messageId = std::numeric_limits<quint64>::max();
    const QByteArray maximumId = codec.encode(request);
    if (!expect(maximumId.contains("\"message_id\":\"18446744073709551615\""),
                QStringLiteral("maximum message ID encode"))
        || !expect(codec.encode(request) == maximumId,
                   QStringLiteral("deterministic repeat encoding"))) {
        return false;
    }

    ProtocolMessage atLimit = requestMessage();
    atLimit.hasPayload = true;
    atLimit.payload = QString();
    const qsizetype emptyStringSize = codec.encode(atLimit).size();
    atLimit.payload = QString(ProtocolV2Codec::MaxPacketSize - emptyStringSize,
                              QLatin1Char('x'));
    const QByteArray limitWire = codec.encode(atLimit);
    if (!expect(limitWire.size() == ProtocolV2Codec::MaxPacketSize,
                QStringLiteral("packet at size limit"))) {
        return false;
    }
    atLimit.payload = QString(ProtocolV2Codec::MaxPacketSize - emptyStringSize + 1,
                              QLatin1Char('x'));
    return expectEncodeFailure(codec, atLimit, QStringLiteral("oversized encode rejection"));
}

bool goldenDecode()
{
    const ProtocolV2Codec codec;
    ProtocolMessage request;
    const QByteArray reordered =
        "{\"command\":37,\"message_id\":\"42\",\"destination\":\"client\","
        "\"source\":\"room\",\"type\":\"request\",\"v\":2}";
    if (!expect(codec.decode(reordered, &request).success
                    && request.version == ProtocolVersion::V2
                    && request.type == ProtocolMessageType::Request
                    && request.source == ProtocolEndpoint::Room
                    && request.destination == ProtocolEndpoint::Client
                    && request.messageId == 42
                    && request.replyTo == 0
                    && request.command == 37
                    && !request.hasPayload,
                QStringLiteral("reordered request decode"))) {
        return false;
    }

    ProtocolMessage withUnknown;
    const QByteArray unknownField =
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"9\",\"command\":5,"
        "\"future_hint\":true}";
    if (!expect(codec.decode(unknownField, &withUnknown).success
                    && withUnknown.messageId == 9 && withUnknown.command == 5,
                QStringLiteral("unknown top-level field ignored"))) {
        return false;
    }

    ProtocolMessage reply;
    const QByteArray replyWire =
        "{\"v\":2,\"type\":\"reply\",\"source\":\"client\","
        "\"destination\":\"room\",\"message_id\":\"81\",\"reply_to\":\"42\","
        "\"command\":37,\"payload\":{}}";
    if (!expect(codec.decode(replyWire, &reply).success
                    && reply.type == ProtocolMessageType::Reply
                    && reply.messageId == 81 && reply.replyTo == 42
                    && reply.hasPayload && reply.payload.toMap().isEmpty(),
                QStringLiteral("reply decode"))) {
        return false;
    }

    ProtocolMessage absent;
    ProtocolMessage explicitNull;
    const QByteArray absentWire =
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"10\",\"command\":7}";
    const QByteArray nullWire =
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"10\",\"command\":7,"
        "\"payload\":null}";
    if (!expect(codec.decode(absentWire, &absent).success && !absent.hasPayload,
                QStringLiteral("absent payload decode"))
        || !expect(codec.decode(nullWire, &explicitNull).success
                       && explicitNull.hasPayload && explicitNull.payload.isNull(),
                   QStringLiteral("explicit null payload decode"))) {
        return false;
    }

    ProtocolMessage unicode;
    const QByteArray unicodeWire = QStringLiteral(
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"11\",\"command\":64,"
        "\"payload\":\"中文 日本語 😀 café\"}").toUtf8();
    if (!expect(codec.decode(unicodeWire, &unicode).success
                    && unicode.payload.toString() == QStringLiteral("中文 日本語 😀 café"),
                QStringLiteral("Unicode decode"))) {
        return false;
    }

    ProtocolMessage unknownCommand;
    const QByteArray unknownCommandWire =
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"12\",\"command\":4096}";
    if (!expect(codec.decode(unknownCommandWire, &unknownCommand).success
                    && unknownCommand.command == 4096,
                QStringLiteral("unknown numeric command decode"))) {
        return false;
    }

    ProtocolMessage maximumId;
    const QByteArray maximumIdWire =
        "{\"v\":2,\"type\":\"notification\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"18446744073709551615\","
        "\"command\":7}";
    if (!expect(codec.decode(maximumIdWire, &maximumId).success
                    && maximumId.messageId == std::numeric_limits<quint64>::max(),
                QStringLiteral("maximum message ID decode"))) {
        return false;
    }

    ProtocolMessage source = requestMessage();
    source.hasPayload = true;
    QVariantMap payload;
    payload.insert(QStringLiteral("array"),
                   QVariant::fromValue(QVariantList() << true << 1.5 << QStringLiteral("值")));
    source.payload = payload;
    ProtocolMessage roundTrip;
    const QByteArray encoded = codec.encode(source);
    return expect(!encoded.isEmpty() && codec.decode(encoded, &roundTrip).success
                      && semanticallyEqual(source, roundTrip),
                  QStringLiteral("semantic round trip"));
}

bool invalidDecode()
{
    const ProtocolV2Codec codec;
    const QByteArray valid =
        "{\"v\":2,\"type\":\"request\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}";
    if (!expectDecodeFailure(codec, QByteArray(), ProtocolDecodeError::EmptyInput,
                             QStringLiteral("empty input"))
        || !expectDecodeFailure(codec, "not-json", ProtocolDecodeError::InvalidJson,
                                QStringLiteral("malformed JSON"))
        || !expectDecodeFailure(codec, "[]", ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("root array"))
        || !expectDecodeFailure(codec, "[1,0,1041,37,{}]",
                                ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("Protocol V1 array"))
        || !expectDecodeFailure(codec, "42", ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("root number"))
        || !expectDecodeFailure(codec, "\"hello\"", ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("root string"))
        || !expectDecodeFailure(codec, "null", ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("root null"))
        || !expectDecodeFailure(codec, "true", ProtocolDecodeError::InvalidEnvelope,
                                QStringLiteral("root boolean"))
        || !expectDecodeFailure(codec,
                                "{\"type\":\"request\",\"source\":\"room\","
                                "\"destination\":\"client\",\"message_id\":\"1\","
                                "\"command\":7}",
                                ProtocolDecodeError::InvalidHeader,
                                QStringLiteral("missing version"))) {
        return false;
    }

    const QList<QByteArray> unsupportedVersions = {
        "{\"v\":1,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}",
        "{\"v\":3,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}",
        "{\"v\":\"2\",\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}",
        "{\"v\":2.1,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}",
        "{\"v\":null,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}"
    };
    for (qsizetype index = 0; index < unsupportedVersions.size(); ++index) {
        if (!expectDecodeFailure(codec, unsupportedVersions.at(index),
                                 ProtocolDecodeError::UnsupportedVersion,
                                 QStringLiteral("unsupported version %1").arg(index))) {
            return false;
        }
    }

    struct InvalidHeaderCase
    {
        const char *wire;
        const char *label;
    };
    const InvalidHeaderCase invalidHeaders[] = {
        {"{\"v\":2,\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}", "missing type"},
        {"{\"v\":2,\"type\":\"Request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}", "invalid type"},
        {"{\"v\":2,\"type\":\"request\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}", "missing source"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"server\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7}", "invalid source"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"message_id\":\"1\",\"command\":7}", "missing destination"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"server\",\"message_id\":\"1\",\"command\":7}", "invalid destination"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"command\":7}", "missing message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":1,\"command\":7}", "numeric message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"\",\"command\":7}", "empty message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"0\",\"command\":7}", "zero message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"-1\",\"command\":7}", "negative message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"+1\",\"command\":7}", "signed message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1.0\",\"command\":7}", "decimal message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\" 1\",\"command\":7}", "whitespace message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"01\",\"command\":7}", "leading-zero message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"18446744073709551616\",\"command\":7}", "overflow message ID"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\"}", "missing command"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":\"7\"}", "string command"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":7.5}", "fractional command"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":2147483648}", "overflow command"},
        {"{\"v\":2,\"type\":\"reply\",\"source\":\"client\",\"destination\":\"room\",\"message_id\":\"2\",\"command\":7}", "reply missing reply_to"},
        {"{\"v\":2,\"type\":\"reply\",\"source\":\"client\",\"destination\":\"room\",\"message_id\":\"2\",\"reply_to\":\"0\",\"command\":7}", "zero reply_to"},
        {"{\"v\":2,\"type\":\"reply\",\"source\":\"client\",\"destination\":\"room\",\"message_id\":\"2\",\"reply_to\":\"01\",\"command\":7}", "invalid reply_to"},
        {"{\"v\":2,\"type\":\"reply\",\"source\":\"client\",\"destination\":\"room\",\"message_id\":\"2\",\"reply_to\":\"18446744073709551616\",\"command\":7}", "overflow reply_to"},
        {"{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"reply_to\":\"2\",\"command\":7}", "request carrying reply_to"},
        {"{\"v\":2,\"type\":\"notification\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"reply_to\":\"2\",\"command\":7}", "notification carrying reply_to"}
    };
    for (const InvalidHeaderCase &testCase : invalidHeaders) {
        if (!expectDecodeFailure(codec, QByteArray(testCase.wire),
                                 ProtocolDecodeError::InvalidHeader,
                                 QString::fromLatin1(testCase.label))) {
            return false;
        }
    }

    ProtocolMessage *nullOutput = nullptr;
    const ProtocolDecodeResult nullResult = codec.decode(valid, nullOutput);
    if (!expect(!nullResult.success
                    && nullResult.error == ProtocolDecodeError::NullOutput
                    && !nullResult.detail.isEmpty(),
                QStringLiteral("null output"))
        || !expectDecodeFailure(codec, QByteArray(ProtocolV2Codec::MaxPacketSize + 1, ' '),
                                ProtocolDecodeError::PacketTooLarge,
                                QStringLiteral("oversized input"))) {
        return false;
    }

    return expectDecodeFailure(
        codec,
        "{\"v\":2,\"type\":\"request\",\"source\":\"room\","
        "\"destination\":\"client\",\"message_id\":\"1\",\"command\":7,"
        "\"payload\":9007199254740992}",
        ProtocolDecodeError::InvalidPayload,
        QStringLiteral("unsafe integer payload decode"));
}

bool invalidEncode()
{
    const ProtocolV2Codec codec;
    ProtocolMessage message = requestMessage();

    message.version = ProtocolVersion::V1;
    if (!expectEncodeFailure(codec, message, QStringLiteral("wrong version encode")))
        return false;

    message = requestMessage();
    message.messageId = 0;
    if (!expectEncodeFailure(codec, message, QStringLiteral("zero message ID encode")))
        return false;

    message = requestMessage();
    message.type = ProtocolMessageType::Reply;
    if (!expectEncodeFailure(codec, message, QStringLiteral("reply missing reply_to encode")))
        return false;

    message = requestMessage();
    message.replyTo = 9;
    if (!expectEncodeFailure(codec, message, QStringLiteral("request reply_to encode")))
        return false;

    message.type = ProtocolMessageType::Notification;
    if (!expectEncodeFailure(codec, message, QStringLiteral("notification reply_to encode")))
        return false;

    message = requestMessage();
    message.type = ProtocolMessageType::Unknown;
    if (!expectEncodeFailure(codec, message, QStringLiteral("unknown type encode")))
        return false;

    message = requestMessage();
    message.source = ProtocolEndpoint::Unknown;
    if (!expectEncodeFailure(codec, message, QStringLiteral("unknown source encode")))
        return false;

    message = requestMessage();
    message.destination = ProtocolEndpoint::Unknown;
    if (!expectEncodeFailure(codec, message, QStringLiteral("unknown destination encode")))
        return false;

    message = requestMessage();
    message.hasPayload = true;
    message.payload = QByteArray("binary");
    if (!expectEncodeFailure(codec, message, QStringLiteral("QByteArray payload encode")))
        return false;

    message.payload = QDateTime::currentDateTimeUtc();
    if (!expectEncodeFailure(codec, message, QStringLiteral("QDateTime payload encode")))
        return false;

    message.payload = std::numeric_limits<double>::quiet_NaN();
    if (!expectEncodeFailure(codec, message, QStringLiteral("NaN payload encode")))
        return false;

    message.payload = std::numeric_limits<double>::infinity();
    if (!expectEncodeFailure(codec, message, QStringLiteral("Infinity payload encode")))
        return false;

    message.payload = QVariant::fromValue<qint64>(9007199254740992LL);
    if (!expectEncodeFailure(codec, message, QStringLiteral("unsafe signed integer encode")))
        return false;

    message.payload = QVariant::fromValue<quint64>(9007199254740992ULL);
    if (!expectEncodeFailure(codec, message, QStringLiteral("unsafe unsigned integer encode")))
        return false;

    QVariant deeplyNested = 1;
    for (int depth = 0; depth < 130; ++depth)
        deeplyNested = QVariant::fromValue(QVariantList() << deeplyNested);
    message.payload = deeplyNested;
    return expectEncodeFailure(codec, message, QStringLiteral("excessive payload depth encode"));
}

bool payloadDomain()
{
    const ProtocolV2Codec codec;
    ProtocolMessage message = requestMessage();
    message.hasPayload = true;

    const QList<QVariant> values = {
        QVariant(),
        true,
        -7,
        QVariant::fromValue<qint64>(-9007199254740991LL),
        QVariant::fromValue<quint64>(9007199254740991ULL),
        1.5,
        1.0e20,
        QStringLiteral("text"),
        QVariant::fromValue(QVariantList() << 1 << QStringLiteral("two")),
        QVariant::fromValue(QVariantMap{{QStringLiteral("key"), true}})
    };
    for (qsizetype index = 0; index < values.size(); ++index) {
        message.payload = values.at(index);
        const QByteArray encoded = codec.encode(message);
        ProtocolMessage decoded;
        if (!expect(!encoded.isEmpty() && codec.decode(encoded, &decoded).success
                        && decoded.hasPayload,
                    QStringLiteral("JSON-domain payload %1").arg(index))) {
            return false;
        }
    }
    return true;
}
}

int main()
{
    const bool success = goldenEncode()
        && goldenDecode()
        && invalidDecode()
        && invalidEncode()
        && payloadDomain();
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_V2_CONTRACT_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
