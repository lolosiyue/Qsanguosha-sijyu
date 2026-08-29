#include "interaction-reply-coordinator.h"

bool InteractionReplyCoordinator::submit(ClientCore *core,
    LegacyV1InteractionReplyAdapter::Encoder encoder,
    InteractionResponse response, const Sender &sender)
{
    if (core == nullptr || encoder == nullptr || !sender || !core->hasActiveRequest())
        return false;

    const InteractionRequest active = core->activeRequest();
    if (response.requestId == 0)
        response.requestId = active.requestId;
    if (response.serverSerial == 0)
        response.serverSerial = active.serverSerial;
    if (response.command == 0)
        response.command = active.command;
    const InteractionValidation validation = core->submitResponse(response);
    if (!validation.accepted())
        return false;

    sender(encoder(active, response));
    return true;
}
