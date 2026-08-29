#include "protocol-v1-codec.h"

#include "json.h"
#include "protocol-v1-message-adapter.h"

#include <limits>

using namespace QSanProtocol;

namespace
{
ProtocolDecodeResult decodeFailure(ProtocolDecodeError error, const QString &detail)
{
    ProtocolDecodeResult result;
    result.error = error;
    result.detail = detail;
    return result;
}
}

ProtocolVersion ProtocolV1Codec::version() const
{
    return ProtocolVersion::V1;
}

QByteArray ProtocolV1Codec::encode(const ProtocolMessage &message, QString *error) const
{
    if (error != nullptr)
        error->clear();

    if (message.version != ProtocolVersion::V1) {
        if (error != nullptr)
            *error = QStringLiteral("Protocol V1 codec cannot encode another version");
        return QByteArray();
    }
    if (message.messageId > std::numeric_limits<unsigned int>::max()
        || message.replyTo > std::numeric_limits<unsigned int>::max()) {
        if (error != nullptr)
            *error = QStringLiteral("Protocol V1 serial exceeds the 32-bit range");
        return QByteArray();
    }

    JsonArray result;
    result << static_cast<unsigned int>(message.messageId);
    result << static_cast<unsigned int>(message.replyTo);
    result << protocolV1Description(message);
    result << message.command;
    if (message.hasPayload)
        result << message.payload;

    const QByteArray encoded = JsonDocument(result).toJson();
    if (encoded.length() > MaxPacketSize) {
        if (error != nullptr)
            *error = QStringLiteral("Protocol V1 packet exceeds 65535 bytes");
        return QByteArray();
    }
    return encoded;
}

ProtocolDecodeResult ProtocolV1Codec::decode(
    QByteArrayView raw, ProtocolMessage *message) const
{
    if (message == nullptr)
        return decodeFailure(ProtocolDecodeError::NullOutput,
                             QStringLiteral("Protocol V1 output message is null"));
    if (raw.isEmpty())
        return decodeFailure(ProtocolDecodeError::EmptyInput,
                             QStringLiteral("Protocol V1 input is empty"));
    if (raw.size() > MaxPacketSize)
        return decodeFailure(ProtocolDecodeError::PacketTooLarge,
                             QStringLiteral("Protocol V1 packet exceeds 65535 bytes"));

    const JsonDocument document = JsonDocument::fromJson(
        QByteArray(raw.data(), raw.size()));
    if (!document.isValid())
        return decodeFailure(ProtocolDecodeError::InvalidJson, document.errorString());
    if (!document.isArray())
        return decodeFailure(ProtocolDecodeError::InvalidEnvelope,
                             QStringLiteral("Protocol V1 root must be an array"));

    const JsonArray values = document.array();
    if (values.size() < 4 || values.size() > 5)
        return decodeFailure(ProtocolDecodeError::InvalidEnvelope,
                             QStringLiteral("Protocol V1 envelope must contain 4 or 5 fields"));
    if (!JsonUtils::isNumberArray(values, 0, 3))
        return decodeFailure(ProtocolDecodeError::InvalidHeader,
                             QStringLiteral("Protocol V1 header fields must be numeric"));

    ProtocolMessage decoded;
    decoded.version = ProtocolVersion::V1;
    decoded.messageId = values[0].toUInt();
    decoded.replyTo = values[1].toUInt();
    applyProtocolV1Description(values[2].toInt(), decoded);
    decoded.command = values[3].toInt();
    decoded.hasPayload = values.size() == 5;
    if (decoded.hasPayload)
        decoded.payload = values[4];
    *message = decoded;

    ProtocolDecodeResult result;
    result.success = true;
    return result;
}
