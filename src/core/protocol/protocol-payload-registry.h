#ifndef PROTOCOL_PAYLOAD_REGISTRY_H
#define PROTOCOL_PAYLOAD_REGISTRY_H

#include "protocol-message.h"

#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace QSanProtocol {

struct ProtocolFlowKey
{
    ProtocolMessageType messageType = ProtocolMessageType::Unknown;
    ProtocolEndpoint source = ProtocolEndpoint::Unknown;
    ProtocolEndpoint destination = ProtocolEndpoint::Unknown;
    int command = 0;

    bool operator==(const ProtocolFlowKey &other) const;
};

enum class ProtocolReplayPolicy
{
    Excluded,
    Record,
    TakeoverOnly
};

enum class ProtocolCorrelationPolicy
{
    None,
    StartsRequest,
    RequiresReplyTo
};

struct ProtocolFlowDescriptor
{
    ProtocolFlowKey key;
    QString diagnosticName;
    QString commandName;
    QString producer;
    QString consumer;
    QString currentPayloadShape;
    QString targetSchema;
    QStringList requiredFields;
    QStringList optionalFields;
    int replyCommand = 0;
    ProtocolCorrelationPolicy correlation = ProtocolCorrelationPolicy::None;
    ProtocolReplayPolicy replayPolicy = ProtocolReplayPolicy::Excluded;
    QString parser;
    QString encoder;
    QString migrationStatus;
    QStringList productionEvidence;
};

class ProtocolPayloadRegistry
{
public:
    static const QList<ProtocolFlowDescriptor> &descriptors();
    static const ProtocolFlowDescriptor *find(const ProtocolFlowKey &key);
    static const ProtocolFlowDescriptor *find(const ProtocolMessage &message);

    // The object gate validates the common V2 contract plus registered
    // schema-specific field types before production routing.
    static bool validateObjectPayload(const ProtocolMessage &message,
                                      QString *error = nullptr);
    static bool encodeObjectPayload(const ProtocolMessage &logicalMessage,
                                    ProtocolMessage *wireMessage,
                                    QString *error = nullptr);
    static bool isReplayEligible(const ProtocolMessage &message,
                                 bool takeoverMode = false);

    static QJsonObject inventoryJson();
    static QByteArray inventoryBytes();
    static bool validateInventory(QString *error = nullptr);
};

}

#endif
