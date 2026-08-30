#include "protocol-runtime.h"

#include "gameplay/protocol-gameplay-payload-registry.h"
#include "protocol-v1-codec.h"
#include "protocol-v2-codec.h"
#include "json.h"

#include <QMetaType>

#include <limits>

using namespace QSanProtocol;

namespace
{
ProtocolDecodeResult unsupportedVersion()
{
    ProtocolDecodeResult result;
    result.error = ProtocolDecodeError::UnsupportedVersion;
    result.detail = QStringLiteral("No codec is registered for the active protocol version");
    return result;
}

bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isPositiveDecimal(const QString &text)
{
    if (text.isEmpty() || text == QLatin1String("0")
        || (text.size() > 1 && text.startsWith(QLatin1Char('0')))) {
        return false;
    }
    for (const QChar character : text) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    }
    return true;
}
}

QByteArray ProtocolCodecRouter::encode(
    ProtocolVersion activeVersion, const ProtocolMessage &message, QString *error) const
{
    ProtocolMessage routed;
    if (!ProtocolGameplayPayloadRegistry::encodeForWire(
            activeVersion, message, &routed, error)) {
        return QByteArray();
    }
    routed.version = activeVersion;
    if (activeVersion == ProtocolVersion::V1)
        return ProtocolV1Codec().encode(routed, error);
    if (activeVersion == ProtocolVersion::V2)
        return ProtocolV2Codec().encode(routed, error);
    fail(error, QStringLiteral("No codec is registered for the active protocol version"));
    return QByteArray();
}

ProtocolDecodeResult ProtocolCodecRouter::decode(
    ProtocolVersion activeVersion, QByteArrayView raw, ProtocolMessage *message) const
{
    if (message == nullptr) {
        if (activeVersion == ProtocolVersion::V1)
            return ProtocolV1Codec().decode(raw, message);
        if (activeVersion == ProtocolVersion::V2)
            return ProtocolV2Codec().decode(raw, message);
        return unsupportedVersion();
    }

    ProtocolMessage wireMessage;
    ProtocolDecodeResult result;
    if (activeVersion == ProtocolVersion::V1)
        result = ProtocolV1Codec().decode(raw, &wireMessage);
    else if (activeVersion == ProtocolVersion::V2)
        result = ProtocolV2Codec().decode(raw, &wireMessage);
    else
        return unsupportedVersion();
    if (!result.success)
        return result;

    ProtocolMessage logicalMessage;
    QString payloadError;
    if (!ProtocolGameplayPayloadRegistry::decodeFromWire(
            activeVersion, wireMessage, &logicalMessage, &payloadError)) {
        ProtocolDecodeResult failure;
        failure.error = ProtocolDecodeError::InvalidPayload;
        failure.detail = payloadError;
        return failure;
    }

    *message = logicalMessage;
    return result;
}

QByteArray ProtocolCodecRouter::encodeReplayV1(
    const ProtocolMessage &message, QString *error) const
{
    ProtocolMessage replay = message;
    replay.version = ProtocolVersion::V1;
    const quint64 v1Limit = std::numeric_limits<unsigned int>::max();
    if (replay.messageId > v1Limit)
        replay.messageId = 0;
    if (replay.replyTo > v1Limit)
        replay.replyTo = 0;
    return ProtocolV1Codec().encode(replay, error);
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

QVariantMap ProtocolSwitchPayload::toVariant() const
{
    QVariantMap value;
    value.insert(QStringLiteral("schema_version"), SchemaVersion);
    value.insert(QStringLiteral("phase"), phase);
    value.insert(QStringLiteral("target_version"), static_cast<int>(targetVersion));
    value.insert(QStringLiteral("switch_id"), switchId);
    return value;
}

bool ProtocolSwitchPayload::parse(
    const QVariant &value, const QString &expectedPhase,
    ProtocolSwitchPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Protocol switch output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("Protocol switch payload must be an object"));

    const QVariantMap object = value.toMap();
    const QSet<QString> expectedKeys = {
        QStringLiteral("schema_version"), QStringLiteral("phase"),
        QStringLiteral("target_version"), QStringLiteral("switch_id")
    };
    if (QSet<QString>(object.keyBegin(), object.keyEnd()) != expectedKeys)
        return fail(error, QStringLiteral("Protocol switch payload fields are invalid"));

    const QVariant schemaValue = object.value(QStringLiteral("schema_version"));
    const QVariant versionValue = object.value(QStringLiteral("target_version"));
    const QVariant phaseValue = object.value(QStringLiteral("phase"));
    const QVariant switchIdValue = object.value(QStringLiteral("switch_id"));
    bool schemaOk = false;
    bool versionOk = false;
    const int schema = schemaValue.toInt(&schemaOk);
    const int version = versionValue.toInt(&versionOk);
    const QString phase = phaseValue.toString();
    const QString switchId = switchIdValue.toString();
    if (phaseValue.userType() != QMetaType::QString
        || switchIdValue.userType() != QMetaType::QString) {
        return fail(error, QStringLiteral("Protocol switch phase and switch_id must be strings"));
    }
    if (!JsonUtils::isNumber(schemaValue) || !JsonUtils::isNumber(versionValue)) {
        return fail(error, QStringLiteral("Protocol switch schema and target must be numbers"));
    }
    if (!schemaOk || schema != SchemaVersion)
        return fail(error, QStringLiteral("Protocol switch schema_version must be 1"));
    if (phase != expectedPhase)
        return fail(error, QStringLiteral("Protocol switch phase is invalid"));
    if (!versionOk || version != static_cast<int>(ProtocolVersion::V2))
        return fail(error, QStringLiteral("Protocol switch target_version must be 2"));
    if (!isPositiveDecimal(switchId))
        return fail(error, QStringLiteral("Protocol switch switch_id must be a positive decimal string"));

    payload->phase = phase;
    payload->targetVersion = ProtocolVersion::V2;
    payload->switchId = switchId;
    return true;
}
