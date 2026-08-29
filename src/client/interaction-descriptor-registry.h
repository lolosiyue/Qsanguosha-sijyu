#ifndef CLIENT_INTERACTION_DESCRIPTOR_REGISTRY_H
#define CLIENT_INTERACTION_DESCRIPTOR_REGISTRY_H

#include "client.h"
#include "legacy-v1-interaction-reply-adapter.h"

#include <QJsonArray>
#include <QJsonObject>

#include <array>

enum class InteractionSupport
{
    CanonicalTyped,
    LegacyAdapter
};

QString interactionSupportName(InteractionSupport support);

struct ClientInteractionDescriptor
{
    QSanProtocol::CommandType command;
    InteractionType type;
    Client::Callback builder;
    void (IClientInteractionPresenter::*presenter)(const InteractionRequest &);
    InteractionResponseShape responseShape;
    LegacyV1InteractionReplyAdapter::Encoder replyEncoder;
    InteractionSupport support;
    const char *commandName;
    const char *builderName;
    const char *presenterName;
    const char *encoderName;
    const char *testName;
};

class InteractionDescriptorRegistry
{
public:
    static const std::array<ClientInteractionDescriptor, 29> &descriptors();
    static const ClientInteractionDescriptor *find(QSanProtocol::CommandType command);
    static const ClientInteractionDescriptor *find(InteractionType type);
    static QJsonArray inventory();
    static QJsonObject inventoryDocument();
};

#endif
