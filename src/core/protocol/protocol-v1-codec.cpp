#include "protocol-v1-codec.h"

#include "json.h"
#include "protocol.h"

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

QByteArray ProtocolV1Codec::encode(const Packet &packet, QString *error) const
{
    if (error != nullptr)
        error->clear();

    JsonArray result;
    result << packet.globalSerial;
    result << packet.localSerial;
    result << packet.getPacketDescription();
    result << packet.getCommandType();
    if (!packet.getMessageBody().isNull())
        result << packet.getMessageBody();

    const QByteArray message = JsonDocument(result).toJson();
    if (message.length() > MaxPacketSize) {
        if (error != nullptr)
            *error = QStringLiteral("Protocol V1 packet exceeds 65535 bytes");
        return QByteArray();
    }
    return message;
}

ProtocolDecodeResult ProtocolV1Codec::decode(QByteArrayView raw, Packet *packet) const
{
    if (packet == nullptr)
        return decodeFailure(ProtocolDecodeError::NullOutput,
                             QStringLiteral("Protocol V1 output packet is null"));
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

    Packet decoded(static_cast<PacketDescription>(values[2].toInt()),
                   static_cast<CommandType>(values[3].toInt()));
    decoded.globalSerial = values[0].toUInt();
    decoded.localSerial = values[1].toUInt();
    if (values.size() == 5)
        decoded.setMessageBody(values[4]);
    else
        decoded.setMessageBody(packet->getMessageBody());
    *packet = decoded;

    ProtocolDecodeResult result;
    result.success = true;
    return result;
}
