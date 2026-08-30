#include "protocol-message.h"

using namespace QSanProtocol;

namespace
{
ProtocolMessageValidationResult validationFailure(
    ProtocolMessageValidationError error, const QString &detail)
{
    ProtocolMessageValidationResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

bool isKnownType(ProtocolMessageType type)
{
    return type == ProtocolMessageType::Request
        || type == ProtocolMessageType::Reply
        || type == ProtocolMessageType::Notification;
}

bool isKnownEndpoint(ProtocolEndpoint endpoint)
{
    return endpoint == ProtocolEndpoint::Room
        || endpoint == ProtocolEndpoint::Lobby
        || endpoint == ProtocolEndpoint::Client;
}
}

ProtocolMessageValidationResult QSanProtocol::validateProtocolMessage(
    const ProtocolMessage &message)
{
    if (message.version != ProtocolVersion::V2) {
        return validationFailure(ProtocolMessageValidationError::UnsupportedVersion,
                                 QStringLiteral("Unknown protocol version"));
    }
    if (!isKnownType(message.type)) {
        return validationFailure(ProtocolMessageValidationError::UnknownType,
                                 QStringLiteral("Unknown protocol message type"));
    }
    if (!isKnownEndpoint(message.source)) {
        return validationFailure(ProtocolMessageValidationError::UnknownSource,
                                 QStringLiteral("Unknown protocol message source"));
    }
    if (!isKnownEndpoint(message.destination)) {
        return validationFailure(ProtocolMessageValidationError::UnknownDestination,
                                 QStringLiteral("Unknown protocol message destination"));
    }

    ProtocolMessageValidationResult result;
    result.valid = true;
    return result;
}
