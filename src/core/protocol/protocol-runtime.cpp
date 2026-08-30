#include "protocol-runtime.h"

#include "gameplay/protocol-gameplay-payload-registry.h"
#include "protocol-payload-registry.h"
#include "protocol-v2-codec.h"

#include <limits>

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

}
QByteArray ProtocolCodecRouter::encode(const ProtocolMessage &message, QString *error) const
{
    ProtocolMessage routed;
    if (ProtocolGameplayPayloadRegistry::isMigratedFlow(message)) {
        if (!ProtocolGameplayPayloadRegistry::encodeForWire(
                message, &routed, error)) {
            return QByteArray();
        }
    } else {
        routed = message;
    }
    routed.version = ProtocolVersion::V2;
    if (!ProtocolPayloadRegistry::encodeObjectPayload(routed, &routed, error))
        return QByteArray();
    if (!ProtocolPayloadRegistry::validateObjectPayload(routed, error))
        return QByteArray();
    return ProtocolV2Codec().encode(routed, error);
}

ProtocolDecodeResult ProtocolCodecRouter::decode(QByteArrayView raw,
                                                  ProtocolMessage *message) const
{
    if (message == nullptr)
        return ProtocolV2Codec().decode(raw, message);

    ProtocolMessage wireMessage;
    ProtocolDecodeResult result = ProtocolV2Codec().decode(raw, &wireMessage);
    if (!result.success)
        return result;
    QString payloadError;
    if (!ProtocolPayloadRegistry::validateObjectPayload(wireMessage, &payloadError)) {
        ProtocolDecodeResult failure;
        failure.error = ProtocolDecodeError::InvalidPayload;
        failure.detail = payloadError;
        return failure;
    }

    if (ProtocolGameplayPayloadRegistry::isMigratedFlow(wireMessage)) {
        if (!ProtocolGameplayPayloadRegistry::decodeFromWire(
                wireMessage, message, &payloadError)) {
            ProtocolDecodeResult failure;
            failure.error = ProtocolDecodeError::InvalidPayload;
            failure.detail = payloadError;
            return failure;
        }
    } else {
        *message = wireMessage;
    }
    return result;
}

quint64 ProtocolMessageIdGenerator::next()
{
    if (m_next == 0)
        return 0;

    const quint64 value = m_next;
    if (m_next == std::numeric_limits<quint64>::max())
        m_next = 0;
    else
        ++m_next;
    return value;
}

void ProtocolMessageIdGenerator::reset()
{
    m_next = 1;
}

quint64 ProtocolMessageIdGenerator::nextValue() const
{
    return m_next;
}

bool ProtocolMessageIdGenerator::setNextValue(quint64 value)
{
    if (value == 0)
        return false;
    m_next = value;
    return true;
}

ProtocolFrameAppendResult ProtocolFrameBuffer::append(QByteArrayView bytes)
{
    ProtocolFrameAppendResult result;
    m_buffer.append(bytes.data(), bytes.size());

    while (true) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        const bool hasCrDelimiter = newline > 0 && m_buffer.at(newline - 1) == '\r';
        const qsizetype frameSize = newline - (hasCrDelimiter ? 1 : 0);
        if (frameSize > MaxFrameSize) {
            result.success = false;
            result.detail = QStringLiteral("Protocol frame exceeds 65535 encoded bytes");
            m_buffer.clear();
            return result;
        }

        QByteArray frame = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (frame.endsWith('\r'))
            frame.chop(1);
        result.frames.append(frame);
    }

    const bool mayEndWithCrDelimiter = m_buffer.size() == MaxFrameSize + 1
        && m_buffer.endsWith('\r');
    if (m_buffer.size() > MaxFrameSize && !mayEndWithCrDelimiter) {
        result.success = false;
        result.detail = QStringLiteral("Protocol frame exceeds 65535 encoded bytes without a delimiter");
        m_buffer.clear();
    }
    return result;
}

void ProtocolFrameBuffer::clear()
{
    m_buffer.clear();
}

qsizetype ProtocolFrameBuffer::bufferedSize() const
{
    return m_buffer.size();
}
