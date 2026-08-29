#include "interaction-request-factory.h"

#include <utility>

InteractionRequest InteractionRequestFactory::create(InteractionType type, int command,
    InteractionResponseShape responseShape, InteractionPayload payload, bool cancelable)
{
    InteractionRequest request;
    request.type = type;
    request.command = command;
    request.responseSchema = responseShape;
    request.payload = std::move(payload);
    request.cancelable = cancelable;
    return request;
}
