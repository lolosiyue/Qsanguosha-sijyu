#include "interaction-reply-encoder.h"

#include "protocol/gameplay/protocol-gameplay-payload-registry.h"

#include <QDebug>

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

InteractionWireReply replyForCommand(const InteractionRequest &request,
    QSanProtocol::CommandType command, const QVariant &domainValue)
{
    QSanProtocol::ProtocolMessage logical;
    logical.type = QSanProtocol::ProtocolMessageType::Reply;
    logical.source = QSanProtocol::ProtocolEndpoint::Client;
    logical.destination = QSanProtocol::ProtocolEndpoint::Room;
    logical.command = static_cast<int>(command);
    logical.replyTo = request.requestId;
    logical.hasPayload = domainValue.isValid() && !domainValue.isNull();
    logical.payload = domainValue;

    QSanProtocol::ProtocolMessage wire;
    QString error;
    if (!QSanProtocol::ProtocolGameplayPayloadRegistry::encodeForWire(
            logical, &wire, &error)) {
        qWarning().noquote() << "Interaction reply encoding failed:" << error;
        return {};
    }
    return {command, wire.payload, request.requestId};
}

InteractionWireReply replyFor(const InteractionRequest &request, const QVariant &domainValue)
{
    return replyForCommand(request,
        static_cast<QSanProtocol::CommandType>(request.command), domainValue);
}

} // namespace

InteractionWireReply InteractionReplyEncoder::optionString(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr ? QVariant(answer->value) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::optionBool(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr
        ? QVariant(answer->value == QLatin1String("yes")) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::optionInt(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    return replyFor(request, answer != nullptr ? QVariant(answer->value.toInt()) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::playersJoined(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::PlayerSelectionData *answer
        = response.payloadAs<InteractionResponse::PlayerSelectionData>();
    return replyFor(request, answer != nullptr && !answer->names.isEmpty()
        ? QVariant(answer->names.join(QLatin1Char('+'))) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::cardIds(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(toVariantList(answer->cardIds)) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::discardCards(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyForCommand(request, QSanProtocol::S_COMMAND_DISCARD_CARD,
        answer != nullptr && !answer->cardIds.isEmpty()
            ? QVariant(toVariantList(answer->cardIds)) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::cardId(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(answer->cardIds.first()) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::amazingGraceCardId(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    return replyFor(request, answer != nullptr && !answer->cardIds.isEmpty()
        ? QVariant(answer->cardIds.first()) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::cardResponse(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    if (answer == nullptr || answer->cardText.isEmpty())
        return replyForCommand(request, QSanProtocol::S_COMMAND_RESPONSE_CARD, QVariant());
    QVariantList wire;
    wire << answer->cardText << QVariant(toVariantList(answer->targets))
         << answer->activationSkillName << answer->activationSkillInstanceId;
    return replyForCommand(request, QSanProtocol::S_COMMAND_RESPONSE_CARD, wire);
}

InteractionWireReply InteractionReplyEncoder::assignment(
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

InteractionWireReply InteractionReplyEncoder::rearrangement(
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

InteractionWireReply InteractionReplyEncoder::distribution(
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

InteractionWireReply InteractionReplyEncoder::generalArrangement(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::GeneralArrangementData *answer
        = response.payloadAs<InteractionResponse::GeneralArrangementData>();
    return replyFor(request, answer != nullptr
        ? QVariant(toVariantList(answer->generalNames)) : QVariant());
}

InteractionWireReply InteractionReplyEncoder::custom(
    const InteractionRequest &request, const InteractionResponse &response)
{
    const InteractionResponse::CustomData *answer
        = response.payloadAs<InteractionResponse::CustomData>();
    return replyFor(request, answer != nullptr ? answer->value : QVariant());
}
