#ifndef PROTOCOL_RUNTIME_H
#define PROTOCOL_RUNTIME_H

#include "protocol-codec.h"

#include <QByteArray>
#include <QList>

namespace QSanProtocol {

class ProtocolCodecRouter
{
public:
    QByteArray encode(const ProtocolMessage &message,
                      QString *error = nullptr) const;
    ProtocolDecodeResult decode(QByteArrayView raw,
                                ProtocolMessage *message) const;
};

class ProtocolMessageIdGenerator
{
public:
    quint64 next();
    void reset();
    quint64 nextValue() const;
    bool setNextValue(quint64 value);

private:
    quint64 m_next = 1;
};

struct ProtocolConnectionState
{
    quint64 generation = 1;
    quint64 nextOutgoingMessageId = 1;
    quint64 lastIncomingMessageId = 0;
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

}

#endif
