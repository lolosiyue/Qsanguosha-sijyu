#ifndef TYPED_INTERACTION_PAYLOADS_H
#define TYPED_INTERACTION_PAYLOADS_H

#include <QVariant>

namespace QSanProtocol {

enum class TypedInteractionPayloadKind
{
    ChooseRoleRequest,
    ChooseRoleReply,
    ChooseDirectionRequest,
    ChooseDirectionReply,
    ExchangeCardRequest,
    AskPeachRequest,
    GuanxingRequest,
    GuanxingReply,
    GongxinRequest,
    OptionalCardIdReply,
    YijiRequest,
    YijiReply,
    PlayCardRequest,
    ResponseCardRequest,
    ResponseCardReply,
    DiscardCardRequest,
    CardIdsReply,
    ChoosePlayerRequest,
    ChoosePlayerReply,
    TriggerOrderRequest,
    TriggerOrderReply,
    NullificationRequest,
    ShowCardRequest,
    AmazingGraceRequest,
    AmazingGraceReply,
    PindianRequest,
    ChooseCardRequest,
    ChooseRole3v3Request,
    ChooseRole3v3Reply,
    LuckCardRequest,
    LuckCardReply,
    AskGeneralRequest,
    AskGeneralReply,
    ArrangeGeneralRequest,
    ArrangeGeneralReply,
    QmlInteractRequest,
    QmlInteractReply
};

class TypedInteractionPayloads
{
public:
    static bool encode(TypedInteractionPayloadKind kind,
                       bool hasLegacyPayload, const QVariant &legacyPayload,
                       QVariant *v2Payload, QString *error = nullptr);
    static bool decode(TypedInteractionPayloadKind kind,
                       const QVariant &v2Payload,
                       bool *hasLegacyPayload, QVariant *legacyPayload,
                       QString *error = nullptr);
};

}

#endif
