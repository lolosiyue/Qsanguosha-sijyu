#ifndef LEGACY_V1_INTERACTION_REPLY_ADAPTER_H
#define LEGACY_V1_INTERACTION_REPLY_ADAPTER_H

#include "core/interaction-model.h"
#include "protocol.h"

struct LegacyV1InteractionReply
{
    QSanProtocol::CommandType command = QSanProtocol::S_COMMAND_UNKNOWN;
    QVariant argument;
};

class LegacyV1InteractionReplyAdapter
{
public:
    using Encoder = LegacyV1InteractionReply (*)(const InteractionRequest &,
        const InteractionResponse &);

    static LegacyV1InteractionReply optionString(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply optionBool(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply optionInt(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply playersJoined(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply cardIds(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply discardCards(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply cardId(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply amazingGraceCardId(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply cardResponse(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply assignment(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply rearrangement(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply distribution(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply generalArrangement(const InteractionRequest &request,
        const InteractionResponse &response);
    static LegacyV1InteractionReply custom(const InteractionRequest &request,
        const InteractionResponse &response);
};

#endif
