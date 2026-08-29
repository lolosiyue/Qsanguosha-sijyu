#ifndef PROTOCOL_MESSAGE_H
#define PROTOCOL_MESSAGE_H

#include "protocol-version.h"

#include <QString>
#include <QVariant>

namespace QSanProtocol {

// The numeric values are internal bridge values, not a Protocol V2 wire contract.
enum class ProtocolMessageType : quint32
{
    Unknown = 0,
    Request = 1,
    Reply = 2,
    Notification = 4
};

enum class ProtocolEndpoint : quint32
{
    Unknown = 0,
    Room = 1,
    Lobby = 2,
    Client = 4
};

struct ProtocolMessage
{
    ProtocolVersion version = ProtocolVersion::V1;

    ProtocolMessageType type = ProtocolMessageType::Notification;
    ProtocolEndpoint source = ProtocolEndpoint::Unknown;
    ProtocolEndpoint destination = ProtocolEndpoint::Unknown;

    quint64 messageId = 0;
    quint64 replyTo = 0;

    int command = 0;

    // Transitional C++ bridge only. A future V2 codec must restrict this to
    // explicit JSON-domain values and must not expose QVariant type names.
    QVariant payload;
    bool hasPayload = false;
};

enum class ProtocolMessageValidationError
{
    None,
    UnsupportedVersion,
    UnknownType,
    UnknownSource,
    UnknownDestination
};

struct ProtocolMessageValidationResult
{
    bool valid = false;
    ProtocolMessageValidationError error = ProtocolMessageValidationError::None;
    QString detail;
};

ProtocolMessageValidationResult validateProtocolMessage(
    const ProtocolMessage &message);

}

#endif
