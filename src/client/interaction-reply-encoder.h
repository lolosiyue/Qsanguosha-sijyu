#ifndef INTERACTION_REPLY_ENCODER_H
#define INTERACTION_REPLY_ENCODER_H

#include "core/interaction-model.h"
#include "protocol.h"

struct InteractionWireReply
{
    QSanProtocol::CommandType command = QSanProtocol::S_COMMAND_UNKNOWN;
    QVariant payload;
    quint64 replyTo = 0;
};

class InteractionReplyEncoder
{
public:
    using Encoder = InteractionWireReply (*)(const InteractionRequest &,
        const InteractionResponse &);

    static InteractionWireReply optionString(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply optionBool(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply optionInt(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply playersJoined(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply cardIds(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply discardCards(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply cardId(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply amazingGraceCardId(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply cardResponse(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply assignment(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply rearrangement(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply distribution(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply generalArrangement(const InteractionRequest &request,
        const InteractionResponse &response);
    static InteractionWireReply custom(const InteractionRequest &request,
        const InteractionResponse &response);
};

#endif
