#include "protocol-gameplay-payload-registry.h"

#include "multiple-choice-payload.h"
#include "protocol.h"

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isMultipleChoiceRequest(const ProtocolMessage &message)
{
    return message.command == S_COMMAND_MULTIPLE_CHOICE
        && message.type == ProtocolMessageType::Request
        && message.source == ProtocolEndpoint::Room
        && message.destination == ProtocolEndpoint::Client;
}

bool isMultipleChoiceReply(const ProtocolMessage &message)
{
    return message.command == S_COMMAND_MULTIPLE_CHOICE
        && message.type == ProtocolMessageType::Reply
        && message.source == ProtocolEndpoint::Client
        && message.destination == ProtocolEndpoint::Room;
}

template <typename Payload>
bool encodePayload(const ProtocolMessage &logicalMessage,
                   ProtocolMessage *wireMessage, QString *error)
{
    if (!logicalMessage.hasPayload)
        return fail(error, QStringLiteral("Migrated Protocol V2 message requires a payload"));

    Payload parsed;
    if (!Payload::parseLegacy(logicalMessage.payload, &parsed, error))
        return false;

    ProtocolMessage transformed = logicalMessage;
    transformed.payload = parsed.toV2Variant();
    transformed.hasPayload = true;
    *wireMessage = transformed;
    return true;
}

template <typename Payload>
bool decodePayload(const ProtocolMessage &wireMessage,
                   ProtocolMessage *logicalMessage, QString *error)
{
    if (!wireMessage.hasPayload)
        return fail(error, QStringLiteral("Migrated Protocol V2 message requires a payload"));

    Payload parsed;
    if (!Payload::parseV2(wireMessage.payload, &parsed, error))
        return false;

    ProtocolMessage transformed = wireMessage;
    transformed.payload = parsed.toLegacyVariant();
    transformed.hasPayload = true;
    *logicalMessage = transformed;
    return true;
}
}

bool ProtocolGameplayPayloadRegistry::encodeForWire(
    ProtocolVersion activeVersion, const ProtocolMessage &logicalMessage,
    ProtocolMessage *wireMessage, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (wireMessage == nullptr)
        return fail(error, QStringLiteral("Protocol wire message output is null"));

    if (activeVersion != ProtocolVersion::V2) {
        *wireMessage = logicalMessage;
        return true;
    }
    if (isMultipleChoiceRequest(logicalMessage)) {
        return encodePayload<MultipleChoiceRequestPayload>(
            logicalMessage, wireMessage, error);
    }
    if (isMultipleChoiceReply(logicalMessage)) {
        return encodePayload<MultipleChoiceReplyPayload>(
            logicalMessage, wireMessage, error);
    }

    // Commands and directions outside the migrated inventory remain identity.
    *wireMessage = logicalMessage;
    return true;
}

bool ProtocolGameplayPayloadRegistry::decodeFromWire(
    ProtocolVersion activeVersion, const ProtocolMessage &wireMessage,
    ProtocolMessage *logicalMessage, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (logicalMessage == nullptr)
        return fail(error, QStringLiteral("Protocol logical message output is null"));

    if (activeVersion != ProtocolVersion::V2) {
        *logicalMessage = wireMessage;
        return true;
    }
    if (isMultipleChoiceRequest(wireMessage)) {
        return decodePayload<MultipleChoiceRequestPayload>(
            wireMessage, logicalMessage, error);
    }
    if (isMultipleChoiceReply(wireMessage)) {
        return decodePayload<MultipleChoiceReplyPayload>(
            wireMessage, logicalMessage, error);
    }

    *logicalMessage = wireMessage;
    return true;
}

bool ProtocolGameplayPayloadRegistry::isMigratedCommand(int command)
{
    return command == S_COMMAND_MULTIPLE_CHOICE;
}

int ProtocolGameplayPayloadRegistry::migratedCommandCount()
{
    return 1;
}
