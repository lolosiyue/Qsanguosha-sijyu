#include "protocol-v2-codec.h"

#include "protocol-message-utils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaType>

#include <cstddef>
#include <cmath>
#include <limits>

using namespace QSanProtocol;

namespace
{
constexpr qint64 JsonSafeInteger = 9007199254740991LL;
constexpr int MaxPayloadDepth = 128;

template <typename Enum>
struct EnumName
{
    Enum value;
    const char *name;
};

const EnumName<ProtocolMessageType> MessageTypeNames[] = {
    {ProtocolMessageType::Request, "request"},
    {ProtocolMessageType::Reply, "reply"},
    {ProtocolMessageType::Notification, "notification"}
};

const EnumName<ProtocolEndpoint> EndpointNames[] = {
    {ProtocolEndpoint::Room, "room"},
    {ProtocolEndpoint::Lobby, "lobby"},
    {ProtocolEndpoint::Client, "client"}
};

ProtocolDecodeResult decodeFailure(ProtocolDecodeError error, const QString &detail)
{
    ProtocolDecodeResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

bool encodeFailure(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

template <typename Enum, std::size_t Size>
QString wireName(Enum value, const EnumName<Enum> (&names)[Size])
{
    for (const EnumName<Enum> &entry : names) {
        if (entry.value == value)
            return QString::fromLatin1(entry.name);
    }
    return QString();
}

template <typename Enum, std::size_t Size>
bool parseWireName(const QJsonValue &value,
                   const EnumName<Enum> (&names)[Size], Enum *output)
{
    if (!value.isString() || output == nullptr)
        return false;

    const QString name = value.toString();
    for (const EnumName<Enum> &entry : names) {
        if (name == QLatin1String(entry.name)) {
            *output = entry.value;
            return true;
        }
    }
    return false;
}

bool parseMessageId(const QJsonValue &value, quint64 *output)
{
    if (!value.isString() || output == nullptr)
        return false;

    const QString text = value.toString();
    if (text.isEmpty() || text == QLatin1String("0")
        || (text.size() > 1 && text.startsWith(QLatin1Char('0')))) {
        return false;
    }

    quint64 parsed = 0;
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if (code < '0' || code > '9')
            return false;
        const quint64 digit = code - '0';
        if (parsed > (std::numeric_limits<quint64>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }

    if (parsed == 0)
        return false;
    *output = parsed;
    return true;
}

bool jsonPayload(const QVariant &value, QJsonValue *output,
                 QString *error, int depth = 0)
{
    if (output == nullptr)
        return encodeFailure(error, QStringLiteral("Protocol V2 payload output is null"));
    if (depth > MaxPayloadDepth)
        return encodeFailure(error, QStringLiteral("Protocol V2 payload nesting is too deep"));

    switch (value.userType()) {
    case QMetaType::UnknownType:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    case QMetaType::Nullptr:
#endif
        *output = QJsonValue(QJsonValue::Null);
        return true;
    case QMetaType::Bool:
        *output = QJsonValue(value.toBool());
        return true;
    case QMetaType::QString:
        *output = QJsonValue(value.toString());
        return true;
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong: {
        const qint64 number = value.toLongLong();
        if (number < -JsonSafeInteger || number > JsonSafeInteger) {
            return encodeFailure(
                error, QStringLiteral("Protocol V2 integer payload exceeds the JSON safe range"));
        }
        *output = QJsonValue(number);
        return true;
    }
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong: {
        const quint64 number = value.toULongLong();
        if (number > static_cast<quint64>(JsonSafeInteger)) {
            return encodeFailure(
                error, QStringLiteral("Protocol V2 integer payload exceeds the JSON safe range"));
        }
        *output = QJsonValue(static_cast<qint64>(number));
        return true;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number)) {
            return encodeFailure(
                error, QStringLiteral("Protocol V2 floating-point payload must be finite"));
        }
        *output = QJsonValue(number);
        return true;
    }
    case QMetaType::QVariantList: {
        QJsonArray array;
        const QVariantList values = value.toList();
        for (const QVariant &entry : values) {
            QJsonValue encoded;
            if (!jsonPayload(entry, &encoded, error, depth + 1))
                return false;
            array.append(encoded);
        }
        *output = array;
        return true;
    }
    case QMetaType::QStringList: {
        QJsonArray array;
        const QStringList values = value.toStringList();
        for (const QString &entry : values)
            array.append(entry);
        *output = array;
        return true;
    }
    case QMetaType::QVariantMap: {
        QJsonObject object;
        const QVariantMap values = value.toMap();
        for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator) {
            QJsonValue encoded;
            if (!jsonPayload(iterator.value(), &encoded, error, depth + 1))
                return false;
            object.insert(iterator.key(), encoded);
        }
        *output = object;
        return true;
    }
    default:
        return encodeFailure(
            error, QStringLiteral("Protocol V2 payload contains unsupported QVariant type %1")
                       .arg(QString::fromLatin1(value.typeName())));
    }
}

bool validatePayloadFromWire(const QJsonValue &value, QVariant *output,
                             QString *error)
{
    if (!value.isObject())
        return encodeFailure(error, QStringLiteral("Protocol V2 payload must be an object"));
    const QVariant decoded = value.toVariant();
    QJsonValue checked;
    if (!jsonPayload(decoded, &checked, error))
        return false;
    const QVariantMap object = decoded.toMap();
    int schemaVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion <= 0) {
        return encodeFailure(error,
            QStringLiteral("Protocol V2 payload schema_version must be a positive integer"));
    }
    *output = decoded;
    return true;
}
}

ProtocolVersion ProtocolV2Codec::version() const
{
    return ProtocolVersion::V2;
}

QByteArray ProtocolV2Codec::encode(
    const ProtocolMessage &message, QString *error) const
{
    if (error != nullptr)
        error->clear();

    if (message.version != ProtocolVersion::V2) {
        encodeFailure(error, QStringLiteral("Protocol V2 codec cannot encode another version"));
        return QByteArray();
    }

    const QString typeName = wireName(message.type, MessageTypeNames);
    const QString sourceName = wireName(message.source, EndpointNames);
    const QString destinationName = wireName(message.destination, EndpointNames);
    if (typeName.isEmpty()) {
        encodeFailure(error, QStringLiteral("Protocol V2 message type is invalid"));
        return QByteArray();
    }
    if (sourceName.isEmpty() || destinationName.isEmpty()) {
        encodeFailure(error, QStringLiteral("Protocol V2 endpoint is invalid"));
        return QByteArray();
    }
    if (message.messageId == 0) {
        encodeFailure(error, QStringLiteral("Protocol V2 message_id must be positive"));
        return QByteArray();
    }
    if (message.type == ProtocolMessageType::Reply && message.replyTo == 0) {
        encodeFailure(error, QStringLiteral("Protocol V2 reply requires reply_to"));
        return QByteArray();
    }
    if (message.type != ProtocolMessageType::Reply && message.replyTo != 0) {
        encodeFailure(error, QStringLiteral("Protocol V2 non-reply must not carry reply_to"));
        return QByteArray();
    }
    if (!message.hasPayload || message.payload.userType() != QMetaType::QVariantMap) {
        encodeFailure(error, QStringLiteral("Protocol V2 payload is required and must be an object"));
        return QByteArray();
    }
    int schemaVersion = 0;
    const QVariantMap payloadObject = message.payload.toMap();
    if (!ProtocolMessageUtils::tryParseInt(
            payloadObject.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion <= 0) {
        encodeFailure(error,
            QStringLiteral("Protocol V2 payload schema_version must be a positive integer"));
        return QByteArray();
    }

    QJsonObject object;
    object.insert(QStringLiteral("v"), 2);
    object.insert(QStringLiteral("type"), typeName);
    object.insert(QStringLiteral("source"), sourceName);
    object.insert(QStringLiteral("destination"), destinationName);
    object.insert(QStringLiteral("message_id"), QString::number(message.messageId));
    if (message.type == ProtocolMessageType::Reply)
        object.insert(QStringLiteral("reply_to"), QString::number(message.replyTo));
    object.insert(QStringLiteral("command"), message.command);

    QJsonValue payload;
    if (!jsonPayload(message.payload, &payload, error))
        return QByteArray();
    object.insert(QStringLiteral("payload"), payload);

    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (encoded.size() > MaxPacketSize) {
        encodeFailure(error, QStringLiteral("Protocol V2 packet exceeds 65535 bytes"));
        return QByteArray();
    }
    return encoded;
}

ProtocolDecodeResult ProtocolV2Codec::decode(
    QByteArrayView raw, ProtocolMessage *message) const
{
    if (message == nullptr)
        return decodeFailure(ProtocolDecodeError::NullOutput,
                             QStringLiteral("Protocol V2 output message is null"));
    if (raw.isEmpty())
        return decodeFailure(ProtocolDecodeError::EmptyInput,
                             QStringLiteral("Protocol V2 input is empty"));
    if (raw.size() > MaxPacketSize)
        return decodeFailure(ProtocolDecodeError::PacketTooLarge,
                             QStringLiteral("Protocol V2 packet exceeds 65535 bytes"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(raw.data(), raw.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // QJsonDocument accepts only object/array roots even though JSON also
        // permits scalars. Distinguish a valid scalar envelope from malformed
        // JSON without depending on newer Qt scalar-parser APIs.
        QJsonParseError wrappedError;
        const QByteArray wrapped = QByteArray("[")
            + QByteArray(raw.data(), raw.size()) + QByteArray("]");
        const QJsonDocument wrappedDocument = QJsonDocument::fromJson(
            wrapped, &wrappedError);
        if (wrappedError.error == QJsonParseError::NoError
            && wrappedDocument.isArray() && wrappedDocument.array().size() == 1) {
            return decodeFailure(ProtocolDecodeError::InvalidEnvelope,
                                 QStringLiteral("Protocol V2 root must be an object"));
        }
        return decodeFailure(ProtocolDecodeError::InvalidJson, parseError.errorString());
    }
    if (!document.isObject())
        return decodeFailure(ProtocolDecodeError::InvalidEnvelope,
                             QStringLiteral("Protocol V2 root must be an object"));

    const QJsonObject object = document.object();
    if (!object.contains(QStringLiteral("v")))
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 v field is required"));
    const QJsonValue versionValue = object.value(QStringLiteral("v"));
    if (!versionValue.isDouble() || !std::isfinite(versionValue.toDouble())
        || std::trunc(versionValue.toDouble()) != versionValue.toDouble()
        || versionValue.toDouble() != 2.0) {
        return decodeFailure(ProtocolDecodeError::UnsupportedVersion,
                             QStringLiteral("Protocol V2 requires v equal to 2"));
    }

    ProtocolMessage decoded;
    decoded.version = ProtocolVersion::V2;
    if (!parseWireName(object.value(QStringLiteral("type")),
                       MessageTypeNames, &decoded.type)) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 type field is invalid"));
    }
    if (!parseWireName(object.value(QStringLiteral("source")),
                       EndpointNames, &decoded.source)) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 source field is invalid"));
    }
    if (!parseWireName(object.value(QStringLiteral("destination")),
                       EndpointNames, &decoded.destination)) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 destination field is invalid"));
    }
    if (!parseMessageId(object.value(QStringLiteral("message_id")),
                        &decoded.messageId)) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 message_id field is invalid"));
    }

    const QJsonValue commandValue = object.value(QStringLiteral("command"));
    const double command = commandValue.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!commandValue.isDouble() || !std::isfinite(command)
        || std::trunc(command) != command
        || command < std::numeric_limits<int>::min()
        || command > std::numeric_limits<int>::max()) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 command field is invalid"));
    }
    decoded.command = static_cast<int>(command);

    const bool hasReplyTo = object.contains(QStringLiteral("reply_to"));
    if (decoded.type == ProtocolMessageType::Reply) {
        if (!hasReplyTo
            || !parseMessageId(object.value(QStringLiteral("reply_to")),
                               &decoded.replyTo)) {
            return decodeFailure(ProtocolDecodeError::InvalidHeader,
                                 QStringLiteral("Protocol V2 reply_to field is invalid"));
        }
    } else if (hasReplyTo) {
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V2 non-reply must not carry reply_to"));
    }

    if (!object.contains(QStringLiteral("payload"))) {
        return decodeFailure(ProtocolDecodeError::InvalidPayload,
                             QStringLiteral("Protocol V2 payload field is required"));
    }
    decoded.hasPayload = true;
    QString payloadError;
    if (!validatePayloadFromWire(object.value(QStringLiteral("payload")),
                                 &decoded.payload, &payloadError)) {
        return decodeFailure(ProtocolDecodeError::InvalidPayload, payloadError);
    }

    *message = decoded;
    ProtocolDecodeResult result;
    result.success = true;
    return result;
}
