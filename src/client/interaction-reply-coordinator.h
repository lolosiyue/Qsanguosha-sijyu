#ifndef INTERACTION_REPLY_COORDINATOR_H
#define INTERACTION_REPLY_COORDINATOR_H

#include "core/client-core.h"
#include "interaction-reply-encoder.h"

#include <functional>

class InteractionReplyCoordinator
{
public:
    using Sender = std::function<void(const InteractionWireReply &)>;

    static bool submit(ClientCore *core,
        InteractionReplyEncoder::Encoder encoder,
        InteractionResponse response, const Sender &sender);
};

#endif
