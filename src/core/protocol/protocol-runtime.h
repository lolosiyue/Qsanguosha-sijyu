#ifndef PROTOCOL_RUNTIME_H
#define PROTOCOL_RUNTIME_H

#include "protocol-codec.h"

#include <QByteArray>
#include <QList>
#include <QVariantMap>

namespace QSanProtocol {

class ProtocolCodecRouter
{
public:
    QByteArray encode(ProtocolVersion activeVersion,
                      const ProtocolMessage &message,
                      QString *error = nullptr) const;
    ProtocolDecodeResult decode(ProtocolVersion activeVersion,
                                QByteArrayView raw,
                                ProtocolMessage *message) const;

    // Replay files retain the legacy logical packet schema even when the
    // connection itself is using Protocol V2.
    QByteArray encodeReplayV1(const ProtocolMessage &message,
                              QString *error = nullptr) const;
};

class ProtocolMessageIdGenerator
{
public:
    quint64 next();
    void reset();

private:
    quint64 m_next = 1;
};

struct ProtocolFrameAppendResult
{
    bool success = true;
    QList<QByteArray> frames;
    QString detail;
};

class ProtocolFrameBuffer
{
public:
    static constexpr qsizetype MaxFrameSize = 65535;

    ProtocolFrameAppendResult append(QByteArrayView bytes);
    void clear();
    qsizetype bufferedSize() const;

private:
    QByteArray m_buffer;
};

enum class ProtocolActivationState
{
    V1Active,
    OfferSent,
    AwaitingCommit,
    AckReceived,
    V2Active,
    Failed
};

struct ProtocolSwitchPayload
{
    static constexpr int SchemaVersion = 1;

    QString phase;
    ProtocolVersion targetVersion = ProtocolVersion::V2;
    QString switchId;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, const QString &expectedPhase,
                      ProtocolSwitchPayload *payload, QString *error = nullptr);
};

}

#endif
