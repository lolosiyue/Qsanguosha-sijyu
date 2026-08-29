#include "json.h"
#include "protocol.h"

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
}

int main()
{
    return goldenEncode() && goldenDecode() && invalidInput() ? 0 : 1;
}
