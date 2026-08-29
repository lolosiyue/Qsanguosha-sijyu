#include "protocol-v1-message-adapter.h"

#include "protocol.h"

using namespace QSanProtocol;

ProtocolMessage QSanProtocol::protocolMessageFromV1Packet(const Packet &packet)
{
    ProtocolMessage message;
    message.version = ProtocolVersion::V1;
    applyProtocolV1Description(static_cast<qint32>(packet.getPacketDescription()), message);
    message.messageId = packet.globalSerial;
    message.replyTo = packet.localSerial;
    message.command = static_cast<int>(packet.getCommandType());
    message.hasPayload = !packet.getMessageBody().isNull();
    if (message.hasPayload)
        message.payload = packet.getMessageBody();
    return message;
}

void QSanProtocol::applyProtocolMessageToV1Packet(
    const ProtocolMessage &message, Packet &packet)
{
    Packet converted(protocolV1Description(message),
                     static_cast<CommandType>(message.command));
    converted.globalSerial = static_cast<unsigned int>(message.messageId);
    converted.localSerial = static_cast<unsigned int>(message.replyTo);

    // Four-field V1 decode historically retained the target Packet's old body.
    if (message.hasPayload)
        converted.setMessageBody(message.payload);
    else
        converted.setMessageBody(packet.getMessageBody());
    packet = converted;
}

qint32 QSanProtocol::protocolV1Description(const ProtocolMessage &message)
{
    const quint32 type = static_cast<quint32>(message.type) & 0x0f;
    const quint32 source = (static_cast<quint32>(message.source) & 0x0f) << 4;
    const quint32 destination = static_cast<quint32>(message.destination) << 8;
    return static_cast<qint32>(type | source | destination);
}

void QSanProtocol::applyProtocolV1Description(
    qint32 description, ProtocolMessage &message)
{
    const quint32 raw = static_cast<quint32>(description);
    message.type = static_cast<ProtocolMessageType>(raw & 0x0f);
    message.source = static_cast<ProtocolEndpoint>((raw >> 4) & 0x0f);
    message.destination = static_cast<ProtocolEndpoint>(raw >> 8);
}
