#include "interaction-reply-coordinator.h"

bool InteractionReplyCoordinator::submit(ClientCore *core,
    InteractionReplyEncoder::Encoder encoder,
    InteractionResponse response, const Sender &sender)
{
    if (core == nullptr || encoder == nullptr || !sender || !core->hasActiveRequest())
        return false;

    const InteractionRequest active = core->activeRequest();
    if (response.requestId == 0)
        response.requestId = active.requestId;
    if (response.command == 0)
        response.command = active.command;
    const InteractionValidation validation = core->submitResponse(response);
    if (!validation.accepted())
        return false;

    sender(encoder(active, response));
    return true;
}
