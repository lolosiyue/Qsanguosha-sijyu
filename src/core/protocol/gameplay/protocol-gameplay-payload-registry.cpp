#include "protocol-gameplay-payload-registry.h"

#include "multiple-choice-payload.h"
#include "protocol.h"
#include "simple-choice-payloads.h"

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

enum class MigrationKind
{
    Identity,
    MultipleChoiceRequest,
    MultipleChoiceReply,
    ChooseGeneralRequest,
    ChooseGeneralReply,
    ChooseSuitRequest,
    ChooseSuitReply,
    ChooseKingdomRequest,
    ChooseKingdomReply,
    ChooseOrderRequest,
    ChooseOrderReply,
    InvokeSkillRequest,
    InvokeSkillReply,
    SurrenderVoteRequest,
    SurrenderVoteReply
};

bool isRoomToClientRequest(const ProtocolMessage &message)
{
    return message.type == ProtocolMessageType::Request
        && message.source == ProtocolEndpoint::Room
        && message.destination == ProtocolEndpoint::Client;
}

bool isClientToRoomReply(const ProtocolMessage &message)
{
    return message.type == ProtocolMessageType::Reply
        && message.source == ProtocolEndpoint::Client
        && message.destination == ProtocolEndpoint::Room;
}

MigrationKind migrationKind(const ProtocolMessage &message)
{
    if (isRoomToClientRequest(message)) {
        switch (message.command) {
        case S_COMMAND_MULTIPLE_CHOICE: return MigrationKind::MultipleChoiceRequest;
        case S_COMMAND_CHOOSE_GENERAL: return MigrationKind::ChooseGeneralRequest;
        case S_COMMAND_CHOOSE_SUIT: return MigrationKind::ChooseSuitRequest;
        case S_COMMAND_CHOOSE_KINGDOM: return MigrationKind::ChooseKingdomRequest;
        case S_COMMAND_CHOOSE_ORDER: return MigrationKind::ChooseOrderRequest;
        case S_COMMAND_INVOKE_SKILL: return MigrationKind::InvokeSkillRequest;
        case S_COMMAND_SURRENDER: return MigrationKind::SurrenderVoteRequest;
        default: return MigrationKind::Identity;
        }
    }
    if (isClientToRoomReply(message)) {
        switch (message.command) {
        case S_COMMAND_MULTIPLE_CHOICE: return MigrationKind::MultipleChoiceReply;
        case S_COMMAND_CHOOSE_GENERAL: return MigrationKind::ChooseGeneralReply;
        case S_COMMAND_CHOOSE_SUIT: return MigrationKind::ChooseSuitReply;
        case S_COMMAND_CHOOSE_KINGDOM: return MigrationKind::ChooseKingdomReply;
        case S_COMMAND_CHOOSE_ORDER: return MigrationKind::ChooseOrderReply;
        case S_COMMAND_INVOKE_SKILL: return MigrationKind::InvokeSkillReply;
        case S_COMMAND_SURRENDER: return MigrationKind::SurrenderVoteReply;
        default: return MigrationKind::Identity;
        }
    }
    return MigrationKind::Identity;
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

bool encodeSuitRequest(const ProtocolMessage &logicalMessage,
                       ProtocolMessage *wireMessage, QString *error)
{
    if (logicalMessage.hasPayload) {
        return fail(error,
                    QStringLiteral("Legacy choose suit request must not contain a payload"));
    }

    ProtocolMessage transformed = logicalMessage;
    transformed.payload = ChooseSuitRequestPayload().toV2Variant();
    transformed.hasPayload = true;
    *wireMessage = transformed;
    return true;
}

bool decodeSuitRequest(const ProtocolMessage &wireMessage,
                       ProtocolMessage *logicalMessage, QString *error)
{
    if (!wireMessage.hasPayload)
        return fail(error, QStringLiteral("Migrated Protocol V2 message requires a payload"));

    ChooseSuitRequestPayload parsed;
    if (!ChooseSuitRequestPayload::parseV2(wireMessage.payload, &parsed, error))
        return false;

    ProtocolMessage transformed = wireMessage;
    // CHOOSE_SUIT historically has no logical payload; keep presence exact for V1 replay.
    transformed.payload = QVariant();
    transformed.hasPayload = false;
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
    switch (migrationKind(logicalMessage)) {
    case MigrationKind::MultipleChoiceRequest:
        return encodePayload<MultipleChoiceRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::MultipleChoiceReply:
        return encodePayload<MultipleChoiceReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseGeneralRequest:
        return encodePayload<ChooseGeneralRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseGeneralReply:
        return encodePayload<ChooseGeneralReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseSuitRequest:
        return encodeSuitRequest(logicalMessage, wireMessage, error);
    case MigrationKind::ChooseSuitReply:
        return encodePayload<ChooseSuitReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseKingdomRequest:
        return encodePayload<ChooseKingdomRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseKingdomReply:
        return encodePayload<ChooseKingdomReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseOrderRequest:
        return encodePayload<ChooseOrderRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::ChooseOrderReply:
        return encodePayload<ChooseOrderReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::InvokeSkillRequest:
        return encodePayload<InvokeSkillRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::InvokeSkillReply:
        return encodePayload<InvokeSkillReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::SurrenderVoteRequest:
        return encodePayload<SurrenderVoteRequestPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::SurrenderVoteReply:
        return encodePayload<SurrenderVoteReplyPayload>(
            logicalMessage, wireMessage, error);
    case MigrationKind::Identity:
        break;
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
    switch (migrationKind(wireMessage)) {
    case MigrationKind::MultipleChoiceRequest:
        return decodePayload<MultipleChoiceRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::MultipleChoiceReply:
        return decodePayload<MultipleChoiceReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseGeneralRequest:
        return decodePayload<ChooseGeneralRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseGeneralReply:
        return decodePayload<ChooseGeneralReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseSuitRequest:
        return decodeSuitRequest(wireMessage, logicalMessage, error);
    case MigrationKind::ChooseSuitReply:
        return decodePayload<ChooseSuitReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseKingdomRequest:
        return decodePayload<ChooseKingdomRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseKingdomReply:
        return decodePayload<ChooseKingdomReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseOrderRequest:
        return decodePayload<ChooseOrderRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::ChooseOrderReply:
        return decodePayload<ChooseOrderReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::InvokeSkillRequest:
        return decodePayload<InvokeSkillRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::InvokeSkillReply:
        return decodePayload<InvokeSkillReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::SurrenderVoteRequest:
        return decodePayload<SurrenderVoteRequestPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::SurrenderVoteReply:
        return decodePayload<SurrenderVoteReplyPayload>(
            wireMessage, logicalMessage, error);
    case MigrationKind::Identity:
        break;
    }

    *logicalMessage = wireMessage;
    return true;
}

bool ProtocolGameplayPayloadRegistry::isMigratedFlow(const ProtocolMessage &message)
{
    return migrationKind(message) != MigrationKind::Identity;
}

bool ProtocolGameplayPayloadRegistry::isMigratedCommand(int command)
{
    switch (command) {
    case S_COMMAND_MULTIPLE_CHOICE:
    case S_COMMAND_CHOOSE_GENERAL:
    case S_COMMAND_CHOOSE_SUIT:
    case S_COMMAND_CHOOSE_KINGDOM:
    case S_COMMAND_CHOOSE_ORDER:
    case S_COMMAND_INVOKE_SKILL:
    case S_COMMAND_SURRENDER:
        return true;
    default:
        return false;
    }
}

int ProtocolGameplayPayloadRegistry::migratedCommandCount()
{
    return 7;
}
