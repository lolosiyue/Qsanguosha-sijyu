#ifndef INTERACTION_COMMAND_REGISTRY_H
#define INTERACTION_COMMAND_REGISTRY_H

#include "core/interaction-model.h"
#include "interaction-reply-encoder.h"
#include "protocol.h"

#include <array>

struct InteractionCommandDescriptor
{
    QSanProtocol::CommandType command;
    InteractionType type;
    InteractionResponseShape responseShape;
    InteractionReplyEncoder::Encoder replyEncoder;
    const char *commandName;
    const char *encoderName;
    const char *testName;
};

class InteractionCommandRegistry
{
public:
    static const std::array<InteractionCommandDescriptor, 29> &descriptors();
    static const InteractionCommandDescriptor *find(QSanProtocol::CommandType command);
    static const InteractionCommandDescriptor *find(InteractionType type);
};

#endif
