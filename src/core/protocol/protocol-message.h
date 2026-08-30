#ifndef PROTOCOL_MESSAGE_H
#define PROTOCOL_MESSAGE_H

#include "protocol-version.h"

#include <QString>
#include <QVariant>

namespace QSanProtocol {

// Stable semantic values serialized by the Protocol V2 envelope codec.
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
    ProtocolVersion version = ProtocolVersion::V2;

    ProtocolMessageType type = ProtocolMessageType::Notification;
    ProtocolEndpoint source = ProtocolEndpoint::Unknown;
    ProtocolEndpoint destination = ProtocolEndpoint::Unknown;

    quint64 messageId = 0;
    quint64 replyTo = 0;

    int command = 0;

    // Internal logical payload. ProtocolCodecRouter always converts it to a
    // registered, schema-versioned object before it reaches the wire.
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

Q_DECLARE_METATYPE(QSanProtocol::ProtocolMessage)

#endif
