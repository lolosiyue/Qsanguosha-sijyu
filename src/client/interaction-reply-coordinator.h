#ifndef INTERACTION_REPLY_COORDINATOR_H
#define INTERACTION_REPLY_COORDINATOR_H

#include "core/client-core.h"
#include "legacy-v1-interaction-reply-adapter.h"

#include <functional>

class InteractionReplyCoordinator
{
public:
    using Sender = std::function<void(const LegacyV1InteractionReply &)>;

    static bool submit(ClientCore *core,
        LegacyV1InteractionReplyAdapter::Encoder encoder,
        InteractionResponse response, const Sender &sender);
};

#endif
