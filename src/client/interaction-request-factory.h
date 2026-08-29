#ifndef INTERACTION_REQUEST_FACTORY_H
#define INTERACTION_REQUEST_FACTORY_H

#include "core/interaction-model.h"

class InteractionRequestFactory
{
public:
    static InteractionRequest create(InteractionType type, int command,
        InteractionResponseShape responseShape, InteractionPayload payload,
        bool cancelable);
};

#endif
