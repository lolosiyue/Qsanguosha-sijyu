#ifndef PROTOCOL_GAMEPLAY_PAYLOAD_REGISTRY_H
#define PROTOCOL_GAMEPLAY_PAYLOAD_REGISTRY_H

#include "protocol/protocol-message.h"

namespace QSanProtocol {

class ProtocolGameplayPayloadRegistry
{
public:
    static bool encodeForWire(const ProtocolMessage &logicalMessage,
                              ProtocolMessage *wireMessage,
                              QString *error = nullptr);
    static bool decodeFromWire(const ProtocolMessage &wireMessage,
                               ProtocolMessage *logicalMessage,
                               QString *error = nullptr);
    static bool decodeReplyDomainValue(const ProtocolMessage &wireMessage,
                                       QVariant *domainValue,
                                       QString *error = nullptr);

    static bool isMigratedFlow(const ProtocolMessage &message);
    static bool isMigratedCommand(int command);
    static int migratedCommandCount();
};

}

#endif
