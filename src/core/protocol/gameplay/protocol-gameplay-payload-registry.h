#ifndef PROTOCOL_GAMEPLAY_PAYLOAD_REGISTRY_H
#define PROTOCOL_GAMEPLAY_PAYLOAD_REGISTRY_H

#include "protocol/protocol-message.h"

namespace QSanProtocol {

class ProtocolGameplayPayloadRegistry
{
public:
    static bool encodeForWire(ProtocolVersion activeVersion,
                              const ProtocolMessage &logicalMessage,
                              ProtocolMessage *wireMessage,
                              QString *error = nullptr);
    static bool decodeFromWire(ProtocolVersion activeVersion,
                               const ProtocolMessage &wireMessage,
                               ProtocolMessage *logicalMessage,
                               QString *error = nullptr);

    static bool isMigratedFlow(const ProtocolMessage &message);
    static bool isMigratedCommand(int command);
    static int migratedCommandCount();
};

}

#endif
