#include "replay-codec.h"

#include "protocol/protocol-payload-registry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <limits>

using namespace QSanProtocol;
using namespace QSanReplay;

namespace
{
ReplayLoadResult failure(ReplayLoadError error, const QString &detail)
{
    ReplayLoadResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

bool exactKeys(const QJsonObject &object, const QStringList &keys)
{
    if (object.size() != keys.size())
        return false;
    for (const QString &key : keys) {
        if (!object.contains(key))
            return false;
    }
    return true;
}

bool parseUnsignedDecimal(const QString &text, qint64 *value)
{
    if (text.isEmpty() || value == nullptr)
        return false;

    qint64 parsed = 0;
    for (const QChar character : text) {
        if (!character.isDigit())
            return false;
        const int digit = character.digitValue();
        if (parsed > (std::numeric_limits<qint64>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

bool parseJsonObject(QByteArrayView line, QJsonObject *object, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(line.data(), line.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr)
            *error = QStringLiteral("Replay line is not a JSON object: %1")
                .arg(parseError.errorString());
        return false;
    }
    *object = document.object();
    return true;
}
}

ReplayLoadResult ReplayReader::read(QByteArrayView data) const
{
    if (data.isEmpty())
        return failure(ReplayLoadError::EmptyInput,
                       QStringLiteral("Replay input is empty"));

    const QByteArray bytes(data.data(), data.size());
    const QList<QByteArray> lines = bytes.split('\n');
    if (lines.isEmpty() || lines.constFirst().isEmpty())
        return failure(ReplayLoadError::InvalidHeader,
                       QStringLiteral("Replay header is missing"));
    if (lines.constFirst().size() > MaxHeaderSize)
        return failure(ReplayLoadError::InvalidHeader,
                       QStringLiteral("Replay header exceeds the size limit"));

    QJsonObject headerObject;
    QString jsonError;
    if (!parseJsonObject(lines.constFirst(), &headerObject, &jsonError))
        return failure(ReplayLoadError::InvalidHeader, jsonError);

    const QStringList headerKeys {
        QStringLiteral("format"),
        QStringLiteral("schema_version"),
        QStringLiteral("format_version"),
        QStringLiteral("protocol_version"),
        QStringLiteral("game_version"),
        QStringLiteral("mod_name"),
        QStringLiteral("takeover")
    };
    if (!exactKeys(headerObject, headerKeys)
        || headerObject.value(QStringLiteral("format")).toString()
            != QLatin1String("qsanguosha-replay")
        || headerObject.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || !headerObject.value(QStringLiteral("game_version")).isString()
        || !headerObject.value(QStringLiteral("mod_name")).isString()
        || !headerObject.value(QStringLiteral("takeover")).isBool()) {
        return failure(ReplayLoadError::InvalidHeader,
                       QStringLiteral("Replay header does not match schema version 1"));
    }
    if (headerObject.value(QStringLiteral("format_version")).toInt(-1) != 2)
        return failure(ReplayLoadError::UnsupportedFormatVersion,
                       QStringLiteral("Only Replay V2 is supported"));
    if (headerObject.value(QStringLiteral("protocol_version")).toInt(-1) != 2)
        return failure(ReplayLoadError::UnsupportedProtocolVersion,
                       QStringLiteral("Only Protocol V2 replay messages are supported"));

    ReplayLoadResult result;
    result.header.format = QStringLiteral("qsanguosha-replay");
    result.header.schemaVersion = 1;
    result.header.formatVersion = ReplayFormatVersion::V2;
    result.header.protocolVersion = ProtocolVersion::V2;
    result.header.gameVersion = headerObject.value(QStringLiteral("game_version")).toString();
    result.header.modName = headerObject.value(QStringLiteral("mod_name")).toString();
    result.header.takeover = headerObject.value(QStringLiteral("takeover")).toBool();

    ProtocolCodecRouter router;
    qint64 lastElapsed = 0;
    bool hasEvents = false;
    QSet<quint64> messageIds;
    for (qsizetype index = 1; index < lines.size(); ++index) {
        QByteArray line = lines.at(index);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty()) {
            if (index + 1 == lines.size())
                continue;
            return failure(ReplayLoadError::InvalidTimelineEntry,
                           QStringLiteral("Blank replay event at line %1").arg(index + 1));
        }

        QJsonObject eventObject;
        if (!parseJsonObject(line, &eventObject, &jsonError))
            return failure(ReplayLoadError::InvalidTimelineEntry,
                           QStringLiteral("Line %1: %2").arg(index + 1).arg(jsonError));
        const QStringList eventKeys {
            QStringLiteral("schema_version"),
            QStringLiteral("elapsed_ms"),
            QStringLiteral("message")
        };
        if (!exactKeys(eventObject, eventKeys)
            || eventObject.value(QStringLiteral("schema_version")).toInt(-1) != 1
            || !eventObject.value(QStringLiteral("elapsed_ms")).isString()
            || !eventObject.value(QStringLiteral("message")).isObject()) {
            return failure(ReplayLoadError::InvalidTimelineEntry,
                           QStringLiteral("Line %1 does not match the Replay V2 event schema")
                               .arg(index + 1));
        }

        qint64 elapsed = 0;
        if (!parseUnsignedDecimal(
                eventObject.value(QStringLiteral("elapsed_ms")).toString(), &elapsed)
            || (hasEvents && elapsed < lastElapsed)) {
            return failure(ReplayLoadError::InvalidElapsedTime,
                           QStringLiteral("Line %1 has an invalid or non-monotonic elapsed time")
                               .arg(index + 1));
        }

        const QByteArray encodedMessage = QJsonDocument(
            eventObject.value(QStringLiteral("message")).toObject())
                                              .toJson(QJsonDocument::Compact);
        ProtocolMessage message;
        const ProtocolDecodeResult decoded = router.decode(encodedMessage, &message);
        if (!decoded.success) {
            return failure(ReplayLoadError::ProtocolDecodeFailure,
                           QStringLiteral("Line %1: %2").arg(index + 1).arg(decoded.detail));
        }
        if (message.messageId == 0 || messageIds.contains(message.messageId)) {
            return failure(ReplayLoadError::ProtocolDecodeFailure,
                           QStringLiteral("Line %1 has a missing or duplicate message_id")
                               .arg(index + 1));
        }
        if (!ProtocolPayloadRegistry::isReplayEligible(message, result.header.takeover)) {
            return failure(ReplayLoadError::ProtocolDecodeFailure,
                           QStringLiteral("Line %1 contains a flow not allowed by the replay policy")
                               .arg(index + 1));
        }

        messageIds.insert(message.messageId);
        result.events.append(ReplayEvent { elapsed, message });
        lastElapsed = elapsed;
        hasEvents = true;
    }

    result.success = true;
    result.error = ReplayLoadError::None;
    return result;
}

ReplayWriter::ReplayWriter(const QString &gameVersion,
                           const QString &modName,
                           bool takeover)
{
    m_header.gameVersion = gameVersion;
    m_header.modName = modName;
    m_header.takeover = takeover;
}

bool ReplayWriter::appendEvent(qint64 elapsedMs,
                               const ProtocolMessage &message,
                               QString *error)
{
    if (elapsedMs < 0 || (m_hasEvents && elapsedMs < m_lastElapsedMs)) {
        if (error != nullptr)
            *error = QStringLiteral("Replay elapsed time must be non-negative and monotonic");
        return false;
    }
    if (!ProtocolPayloadRegistry::isReplayEligible(message, m_header.takeover)) {
        if (error != nullptr)
            *error = QStringLiteral("Protocol flow is not eligible for this replay mode");
        return false;
    }

    ProtocolMessage replayMessage = message;
    replayMessage.version = ProtocolVersion::V2;
    if (replayMessage.messageId == 0)
        replayMessage.messageId = nextAvailableMessageId();
    else if (m_usedMessageIds.contains(replayMessage.messageId)) {
        if (error != nullptr)
            *error = QStringLiteral("Replay message_id must be unique");
        return false;
    } else {
        m_usedMessageIds.insert(replayMessage.messageId);
    }
    if (replayMessage.messageId == 0) {
        if (error != nullptr)
            *error = QStringLiteral("Replay message_id space is exhausted");
        return false;
    }

    QString encodeError;
    const QByteArray encoded = m_router.encode(replayMessage, &encodeError);
    QJsonParseError parseError;
    const QJsonDocument messageDocument = QJsonDocument::fromJson(encoded, &parseError);
    if (encoded.isEmpty() || parseError.error != QJsonParseError::NoError
        || !messageDocument.isObject()) {
        if (error != nullptr)
            *error = encodeError.isEmpty() ? parseError.errorString() : encodeError;
        return false;
    }

    QJsonObject eventObject;
    eventObject.insert(QStringLiteral("schema_version"), 1);
    eventObject.insert(QStringLiteral("elapsed_ms"), QString::number(elapsedMs));
    eventObject.insert(QStringLiteral("message"), messageDocument.object());
    m_eventRecords.append(QJsonDocument(eventObject).toJson(QJsonDocument::Compact));
    m_lastElapsedMs = elapsedMs;
    m_hasEvents = true;
    return true;
}

void ReplayWriter::reset()
{
    m_messageIds.reset();
    m_usedMessageIds.clear();
    m_eventRecords.clear();
    m_lastElapsedMs = 0;
    m_hasEvents = false;
}

ReplayHeader ReplayWriter::header() const
{
    return m_header;
}

QList<QByteArray> ReplayWriter::eventRecords() const
{
    return m_eventRecords;
}

QByteArray ReplayWriter::rawReplayData() const
{
    QByteArray data = headerLine(
        m_header.gameVersion, m_header.modName, m_header.takeover);
    data.append('\n');
    for (const QByteArray &record : m_eventRecords) {
        data.append(record);
        data.append('\n');
    }
    return data;
}

QByteArray ReplayWriter::headerLine(const QString &gameVersion,
                                    const QString &modName,
                                    bool takeover)
{
    QJsonObject headerObject;
    headerObject.insert(QStringLiteral("format"), QStringLiteral("qsanguosha-replay"));
    headerObject.insert(QStringLiteral("schema_version"), 1);
    headerObject.insert(QStringLiteral("format_version"), 2);
    headerObject.insert(QStringLiteral("protocol_version"), 2);
    headerObject.insert(QStringLiteral("game_version"), gameVersion);
    headerObject.insert(QStringLiteral("mod_name"), modName);
    headerObject.insert(QStringLiteral("takeover"), takeover);
    return QJsonDocument(headerObject).toJson(QJsonDocument::Compact);
}

quint64 ReplayWriter::nextAvailableMessageId()
{
    while (true) {
        const quint64 candidate = m_messageIds.next();
        if (candidate == 0)
            return 0;
        if (!m_usedMessageIds.contains(candidate)) {
            m_usedMessageIds.insert(candidate);
            return candidate;
        }
    }
}
