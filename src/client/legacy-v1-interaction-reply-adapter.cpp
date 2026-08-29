#include "legacy-v1-interaction-reply-adapter.h"

namespace {

QVariantList toVariantList(const QList<int> &values)
{
    QVariantList result;
    for (int value : values)
        result << value;
    return result;
}

QVariantList toVariantList(const QStringList &values)
{
    QVariantList result;
    for (const QString &value : values)
        result << value;
    return result;
}

LegacyV1InteractionReply replyFor(const InteractionRequest &request, const QVariant &argument)
{
    return { static_cast<QSanProtocol::CommandType>(request.command), argument };
}

} // namespace

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::optionString(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr ? QVariant(answer->value) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::optionBool(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr
        ? QVariant(answer->value == QLatin1String("yes")) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::optionInt(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr ? QVariant(answer->value.toInt()) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::playersJoined(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::PlayerSelectionData *answer
        = response.payloadAs<InteractionResponse::PlayerSelectionData>();
    return replyFor(request, answer != nullptr && !answer->names.isEmpty()
        ? QVariant(answer->names.join(QLatin1Char('+'))) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::cardIds(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(toVariantList(answer->cardIds)) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::discardCards(
    const InteractionRequest &, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return { QSanProtocol::S_COMMAND_DISCARD_CARD,
        answer != nullptr && !answer->cardIds.isEmpty()
            ? QVariant(toVariantList(answer->cardIds)) : QVariant() };
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::cardId(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(answer->cardIds.first()) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::amazingGraceCardId(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(answer->cardIds.first()) : QVariant(-1));
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::cardResponse(
    const InteractionRequest &, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    if (answer == nullptr || answer->cardText.isEmpty())
        return { QSanProtocol::S_COMMAND_RESPONSE_CARD, QVariant() };
    QVariantList wire;
    wire << answer->cardText << QVariant(toVariantList(answer->targets))
         << answer->activationSkillName << answer->activationSkillInstanceId;
    return { QSanProtocol::S_COMMAND_RESPONSE_CARD, wire };
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::assignment(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::AssignmentData *answer
        = response.payloadAs<InteractionResponse::AssignmentData>();
    if (answer == nullptr)
        return replyFor(request, QVariant());
    QVariantList wire;
    wire << QVariant(toVariantList(answer->names)) << QVariant(toVariantList(answer->values));
    return replyFor(request, wire);
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::rearrangement(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::RearrangementData *answer
        = response.payloadAs<InteractionResponse::RearrangementData>();
    if (answer == nullptr)
        return replyFor(request, QVariant());
    QVariantList wire;
    wire << QVariant(toVariantList(answer->first)) << QVariant(toVariantList(answer->second));
    return replyFor(request, wire);
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::distribution(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::DistributionData *answer
        = response.payloadAs<InteractionResponse::DistributionData>();
    if (answer == nullptr)
        return replyFor(request, QVariant());
    QVariantList wire;
    wire << QVariant(toVariantList(answer->cards)) << answer->target;
    return replyFor(request, wire);
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::generalArrangement(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::GeneralArrangementData *answer
        = response.payloadAs<InteractionResponse::GeneralArrangementData>();
    return replyFor(request, answer != nullptr
        ? QVariant(toVariantList(answer->generalNames)) : QVariant());
}

LegacyV1InteractionReply LegacyV1InteractionReplyAdapter::custom(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CustomData *answer
        = response.payloadAs<InteractionResponse::CustomData>();
    return replyFor(request, answer != nullptr ? answer->value : QVariant());
}
