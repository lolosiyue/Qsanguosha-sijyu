#ifndef PROTOCOL_V1_MESSAGE_ADAPTER_H
#define PROTOCOL_V1_MESSAGE_ADAPTER_H

#include "protocol-message.h"

namespace QSanProtocol {

class Packet;

ProtocolMessage protocolMessageFromV1Packet(const Packet &packet);
void applyProtocolMessageToV1Packet(const ProtocolMessage &message, Packet &packet);

qint32 protocolV1Description(const ProtocolMessage &message);
void applyProtocolV1Description(qint32 description, ProtocolMessage &message);

}

#endif
