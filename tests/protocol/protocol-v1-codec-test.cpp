#include "json.h"
#include "protocol.h"
#include "protocol/protocol-v1-codec.h"

#include <QTextStream>
#include <QVariantMap>

using namespace QSanProtocol;

namespace
{
bool expect(bool condition, const QString &label)
{
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

bool expectWire(const Packet &packet, const QByteArray &expected, const QString &label)
{
    const QByteArray actual = packet.toJson();
    if (actual == expected)
        return true;
    QTextStream(stderr) << label << " failed\nexpected: " << expected
                        << "\nactual:   " << actual << "\n";
    return false;
}

bool goldenEncode()
{
    Packet request(S_SRC_ROOM | S_TYPE_REQUEST | S_DEST_CLIENT,
                   S_COMMAND_INVOKE_SKILL);
    if (!expectWire(request, "[0,0,1041,8]", "request without body"))
        return false;

    request.setMessageBody(true);
    if (!expectWire(request, "[0,0,1041,8,true]", "request scalar body"))
        return false;

    Packet arrayRequest(S_SRC_ROOM | S_TYPE_REQUEST | S_DEST_CLIENT,
                        S_COMMAND_MULTIPLE_CHOICE);
    arrayRequest.setMessageBody(QVariant::fromValue(JsonArray() << 1 << "two"));
    if (!expectWire(arrayRequest, "[0,0,1041,24,[1,\"two\"]]",
                    "request array body"))
        return false;

    Packet notification(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                        S_COMMAND_GAME_START);
    if (!expectWire(notification, "[0,0,1044,37]", "notification"))
        return false;

    Packet reply(S_SRC_CLIENT | S_TYPE_REPLY | S_DEST_ROOM,
                 S_COMMAND_RESPONSE_CARD);
    reply.localSerial = 77;
    reply.setMessageBody(QStringLiteral("slash"));
    if (!expectWire(reply, "[0,77,322,3,\"slash\"]", "client reply serial"))
        return false;

    Packet serials(S_SRC_CLIENT | S_TYPE_REQUEST | S_DEST_ROOM,
                   S_COMMAND_SIGNUP);
    serials.globalSerial = 23;
    serials.localSerial = 17;
    if (!expectWire(serials, "[23,17,321,87]", "serial fields"))
        return false;

    Packet unicode(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                   S_COMMAND_SPEAK);
    unicode.setMessageBody(QStringLiteral("中文測試"));
    if (!expectWire(unicode, QStringLiteral("[0,0,1044,64,\"中文測試\"]").toUtf8(),
                    "Unicode body"))
        return false;

    QVariantMap nestedObject;
    nestedObject.insert(QStringLiteral("inner"), QStringLiteral("值"));
    JsonArray nestedArray;
    nestedArray << 1 << QVariant::fromValue(nestedObject);
    QVariantMap nestedBody;
    nestedBody.insert(QStringLiteral("outer"), QVariant::fromValue(nestedArray));
    Packet nested(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                  S_COMMAND_QML_INTERACT);
    nested.setMessageBody(nestedBody);
    if (!expectWire(nested,
                    QStringLiteral("[0,0,1044,103,{\"outer\":[1,{\"inner\":\"值\"}]}]")
                        .toUtf8(),
                    "nested object and array"))
        return false;

    Packet generated;
    if (!expect(generated.createGlobalSerial() == 1
                    && generated.globalSerial == 1,
                "explicit global serial generation"))
        return false;

    Packet oversized(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                     S_COMMAND_SPEAK);
    oversized.setMessageBody(QString(65536, QLatin1Char('x')));
    return expect(oversized.toJson().isEmpty(), "oversized encode rejection")
        && expect(oversized.toString().isEmpty(), "oversized toString rejection");
}

bool goldenDecode()
{
    Packet fourFields;
    if (!expect(fourFields.parse("[23,17,1041,8]"), "four-field decode")
        || !expect(fourFields.globalSerial == 23 && fourFields.localSerial == 17,
                   "four-field serials")
        || !expect(fourFields.getPacketDescription() == 1041,
                   "four-field description")
        || !expect(fourFields.getCommandType() == S_COMMAND_INVOKE_SKILL,
                   "four-field command")
        || !expect(fourFields.getMessageBody().isNull(), "four-field null body"))
        return false;

    Packet fiveFields;
    if (!expect(fiveFields.parse("[5,4,322,3,[1,\"two\"]]"),
                "five-field decode")
        || !expect(fiveFields.getPacketType() == S_TYPE_REPLY,
                   "reply packet type")
        || !expect(fiveFields.getPacketSource() == S_SRC_CLIENT,
                   "reply packet source")
        || !expect(fiveFields.getPacketDestination() == S_DEST_ROOM,
                   "reply packet destination")
        || !expect(fiveFields.getMessageBody().value<JsonArray>()
                       == (JsonArray() << 1 << "two"),
                   "array body type"))
        return false;

    Packet notification;
    if (!expect(notification.parse("[0,0,1044,37]"), "notification decode")
        || !expect(notification.getPacketType() == S_TYPE_NOTIFICATION,
                   "notification packet type"))
        return false;

    Packet roundTrip;
    const QByteArray canonical = "[9,8,1041,24,{\"choice\":\"中\"}]";
    if (!expect(roundTrip.parse(canonical), "canonical round-trip decode")
        || !expect(roundTrip.toJson() == canonical,
                   "canonical round-trip bytes"))
        return false;

    Packet normalized;
    if (!expect(normalized.parse("[ 9, 8, 1041, 24, { \"choice\" : \"中\" } ]"),
                "non-canonical decode")
        || !expect(normalized.toJson() == canonical,
                   "non-canonical decode canonical encode"))
        return false;

    Packet unknown;
    if (!expect(unknown.parse("[1,2,32767,4096]"),
                "unknown numeric values remain accepted")
        || !expect(static_cast<int>(unknown.getPacketDescription()) == 32767,
                   "unknown description preserved")
        || !expect(static_cast<int>(unknown.getCommandType()) == 4096,
                   "unknown command preserved"))
        return false;

    Packet reused;
    if (!expect(reused.parse("[1,2,1041,8,\"legacy-body\"]"),
                "reused packet five-field setup")
        || !expect(reused.parse("[3,4,1041,8]"),
                   "reused packet four-field decode")
        || !expect(reused.getMessageBody() == QStringLiteral("legacy-body"),
                   "legacy four-field body retention"))
        return false;

    return expect(static_cast<int>(S_COMMAND_INVOKE_SKILL) == 8,
                  "command ID remains unchanged")
        && expect((S_SRC_ROOM | S_TYPE_REQUEST | S_DEST_CLIENT) == 1041,
                  "packet description bits remain unchanged");
}

bool invalidInput()
{
    const QList<QPair<QByteArray, QString>> invalidCases = {
        {QByteArray(), QStringLiteral("empty input")},
        {QByteArray("not-json"), QStringLiteral("invalid JSON")},
        {QByteArray("{}"), QStringLiteral("non-array root")},
        {QByteArray("[0,0,1041]"), QStringLiteral("short header")},
        {QByteArray("[0,0,{},8]"), QStringLiteral("non-convertible header")},
        {QByteArray("[0,0,1041,8,null,6]"), QStringLiteral("too many fields")},
        {QByteArray("[0,0,1041,8"), QStringLiteral("truncated input")},
        {QByteArray(65536, ' '), QStringLiteral("oversized decode")}
    };

    foreach (const auto &testCase, invalidCases) {
        Packet packet;
        packet.globalSerial = 91;
        packet.localSerial = 92;
        packet.setMessageBody(QStringLiteral("unchanged"));
        if (!expect(!packet.parse(testCase.first), testCase.second)
            || !expect(packet.globalSerial == 91 && packet.localSerial == 92
                           && packet.getMessageBody() == QStringLiteral("unchanged"),
                       testCase.second + QStringLiteral(" leaves packet unchanged")))
            return false;
    }
    return true;
}

bool expectDecodeError(const ProtocolV1Codec &codec, QByteArrayView raw,
                       Packet *packet, ProtocolDecodeError expected,
                       const QString &label)
{
    const ProtocolDecodeResult result = codec.decode(raw, packet);
    return expect(!result.success && result.error == expected && !result.detail.isEmpty(),
                  label);
}

bool codecBoundary()
{
    const ProtocolV1Codec codec;
    if (!expect(codec.version() == ProtocolVersion::V1, "V1 codec version"))
        return false;

    Packet source(S_SRC_CLIENT | S_TYPE_REPLY | S_DEST_ROOM,
                  S_COMMAND_RESPONSE_CARD);
    source.globalSerial = 12;
    source.localSerial = 34;
    source.setMessageBody(QStringLiteral("reply"));
    QString encodeError = QStringLiteral("stale");
    const QByteArray encoded = codec.encode(source, &encodeError);
    if (!expect(encoded == QByteArray("[12,34,322,3,\"reply\"]"),
                "codec golden encode")
        || !expect(encodeError.isEmpty(), "codec successful encode clears error")
        || !expect(encoded == source.toJson(), "facade and codec encode match")
        || !expect(source.toString() == QString::fromUtf8(encoded),
                   "facade toString matches codec"))
        return false;

    Packet decoded;
    const ProtocolDecodeResult decodedResult = codec.decode(encoded, &decoded);
    if (!expect(decodedResult.success
                    && decodedResult.error == ProtocolDecodeError::None
                    && decodedResult.detail.isEmpty(),
                "codec successful decode result")
        || !expect(decoded.globalSerial == source.globalSerial
                       && decoded.localSerial == source.localSerial
                       && decoded.getPacketDescription() == source.getPacketDescription()
                       && decoded.getCommandType() == source.getCommandType()
                       && decoded.getMessageBody() == source.getMessageBody(),
                   "codec decoded fields")
        || !expect(codec.encode(decoded) == encoded, "codec round trip bytes"))
        return false;

    const QByteArray valid = "[0,0,1041,8]";
    if (!expectDecodeError(codec, valid, nullptr, ProtocolDecodeError::NullOutput,
                           "null output rejection"))
        return false;

    Packet failureTarget;
    failureTarget.globalSerial = 91;
    failureTarget.setMessageBody(QStringLiteral("unchanged"));
    if (!expectDecodeError(codec, QByteArrayView(), &failureTarget,
                           ProtocolDecodeError::EmptyInput, "empty input diagnostic")
        || !expectDecodeError(codec, QByteArray(65536, ' '), &failureTarget,
                              ProtocolDecodeError::PacketTooLarge,
                              "oversized input diagnostic")
        || !expectDecodeError(codec, QByteArray("not-json"), &failureTarget,
                              ProtocolDecodeError::InvalidJson,
                              "invalid JSON diagnostic")
        || !expectDecodeError(codec, QByteArray("{}"), &failureTarget,
                              ProtocolDecodeError::InvalidEnvelope,
                              "non-array diagnostic")
        || !expectDecodeError(codec, QByteArray("[0,0,1041]"), &failureTarget,
                              ProtocolDecodeError::InvalidEnvelope,
                              "short envelope diagnostic")
        || !expectDecodeError(codec, QByteArray("[0,0,{},8]"), &failureTarget,
                              ProtocolDecodeError::InvalidHeader,
                              "invalid header diagnostic")
        || !expectDecodeError(codec, QByteArray("[0,0,1041,8,null,6]"), &failureTarget,
                              ProtocolDecodeError::InvalidEnvelope,
                              "long envelope diagnostic")
        || !expect(failureTarget.globalSerial == 91
                       && failureTarget.getMessageBody() == QStringLiteral("unchanged"),
                   "codec failures leave output unchanged"))
        return false;

    Packet unknown;
    const QByteArray unknownWire = "[1,2,32767,4096]";
    if (!expect(codec.decode(unknownWire, &unknown).success,
                "codec accepts unknown numeric values")
        || !expect(codec.encode(unknown) == unknownWire,
                   "codec preserves unknown numeric values"))
        return false;

    Packet reused;
    reused.setMessageBody(QStringLiteral("legacy-body"));
    if (!expect(codec.decode(valid, &reused).success,
                "codec four-field reuse decode")
        || !expect(reused.getMessageBody() == QStringLiteral("legacy-body"),
                   "codec preserves legacy four-field body retention"))
        return false;

    Packet oversizedOutput(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                           S_COMMAND_SPEAK);
    oversizedOutput.setMessageBody(QString(65536, QLatin1Char('x')));
    encodeError.clear();
    return expect(codec.encode(oversizedOutput, &encodeError).isEmpty()
                      && !encodeError.isEmpty(),
                  "oversized encode diagnostic");
}
}

int main()
{
    return goldenEncode() && goldenDecode() && invalidInput() && codecBoundary() ? 0 : 1;
}
