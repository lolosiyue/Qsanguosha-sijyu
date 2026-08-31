#ifndef PROTOCOL_INTERACTION_REQUEST_BUILDER_H
#define PROTOCOL_INTERACTION_REQUEST_BUILDER_H

#include "core/interaction-model.h"
#include "protocol/protocol-message.h"

class ClientGameState;

class ProtocolInteractionRequestBuilder
{
public:
    static bool build(const QSanProtocol::ProtocolMessage &message,
                      const ClientGameState &state, InteractionRequest *request,
                      QString *error = nullptr);
};

#endif
