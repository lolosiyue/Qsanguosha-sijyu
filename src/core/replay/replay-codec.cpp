#include "replay-codec.h"

#include "protocol.h"
#include "protocol/protocol-v1-codec.h"
#include "protocol/protocol-v2-codec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <cstring>
#include <limits>

using namespace QSanProtocol;
using namespace QSanReplay;

namespace
{
const QByteArray ReplayHeaderPrefix("QSAN_REPLAY ");
const QByteArray ReplayHeaderMagic("QSAN_REPLAY");

ReplayLoadResult failure(ReplayLoadError error, const QString &detail)
{
    ReplayLoadResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

bool jsonInteger(const QJsonValue &value, int *output)
{
    if (!value.isDouble() || output == nullptr)
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return false;
    }
    *output = static_cast<int>(number);
    return true;
}

bool parseElapsed(QByteArrayView text, qint64 *elapsed)
{
    if (text.isEmpty() || elapsed == nullptr)
        return false;

    qint64 value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9')
            return false;
        const int digit = character - '0';
        if (value > (std::numeric_limits<qint64>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    *elapsed = value;
    return true;
}

bool startsWith(QByteArrayView value, const QByteArray &prefix)
{
    return value.size() >= prefix.size()
        && std::memcmp(value.data(), prefix.constData(),
                       static_cast<size_t>(prefix.size())) == 0;
}

bool isBlank(QByteArrayView line)
{
    for (const char character : line) {
        if (character != ' ' && character != '\t'
            && character != '\r' && character != '\n') {
            return false;
        }
    }
    return true;
}

bool nextLine(QByteArrayView data, qsizetype *cursor,
              qsizetype *lineNumber, QByteArrayView *line)
{
    if (cursor == nullptr || lineNumber == nullptr || line == nullptr
        || *cursor < 0 || *cursor > data.size()) {
        return false;
    }

    const qsizetype start = *cursor;
    qsizetype end = data.indexOf('\n', start);
    if (end < 0) {
        end = data.size();
        *cursor = data.size() + 1;
    } else {
        *cursor = end + 1;
    }

    *line = QByteArrayView(data.data() + start, end - start);
    if (!line->isEmpty() && line->at(line->size() - 1) == '\r')
        *line = line->first(line->size() - 1);
    ++(*lineNumber);
    return true;
}
}

ReplayLoadResult ReplayReader::read(QByteArrayView data) const
{
    if (data.isEmpty())
        return failure(ReplayLoadError::EmptyInput, QStringLiteral("Replay input is empty"));

    qsizetype cursor = 0;
    qsizetype lineNumber = 0;
    qsizetype firstLineStart = 0;
    qsizetype firstLineNumber = 0;
    QByteArrayView first;
    while (cursor <= data.size()) {
        const qsizetype lineStart = cursor;
        QByteArrayView line;
        if (!nextLine(data, &cursor, &lineNumber, &line))
            break;
        if (isBlank(line))
            continue;
        firstLineStart = lineStart;
        firstLineNumber = lineNumber;
        first = line;
        break;
    }
    if (first.isEmpty())
        return failure(ReplayLoadError::EmptyInput, QStringLiteral("Replay input contains no events"));

    ReplayHeader header;
    if (startsWith(first, ReplayHeaderMagic)) {
        if (first.size() > MaxHeaderSize) {
            return failure(ReplayLoadError::PacketTooLarge,
                           QStringLiteral("Replay header exceeds 4096 bytes"));
        }
        if (!startsWith(first, ReplayHeaderPrefix)) {
            return failure(ReplayLoadError::InvalidHeader,
                           QStringLiteral("Replay header must begin with 'QSAN_REPLAY '"));
        }

        QJsonParseError parseError;
        const QByteArray json(first.data() + ReplayHeaderPrefix.size(),
                              first.size() - ReplayHeaderPrefix.size());
        const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return failure(ReplayLoadError::InvalidHeader,
                           QStringLiteral("Replay header JSON must be an object: %1")
                               .arg(parseError.errorString()));
        }

        const QJsonObject object = document.object();
        int formatVersion = 0;
        int protocolVersion = 0;
        if (!object.contains(QStringLiteral("format_version"))
            || !jsonInteger(object.value(QStringLiteral("format_version")), &formatVersion)
            || !object.contains(QStringLiteral("protocol_version"))
            || !jsonInteger(object.value(QStringLiteral("protocol_version")), &protocolVersion)) {
            return failure(ReplayLoadError::InvalidHeader,
                           QStringLiteral("Replay header requires integer format_version and protocol_version"));
        }
        if (formatVersion != static_cast<int>(ReplayFormatVersion::V2)) {
            const QString detail = formatVersion > static_cast<int>(ReplayFormatVersion::V2)
                ? QStringLiteral("Replay format version %1 is newer than supported version 2").arg(formatVersion)
                : QStringLiteral("Replay format version %1 is too old").arg(formatVersion);
            return failure(ReplayLoadError::UnsupportedFormatVersion, detail);
        }
        if (protocolVersion != static_cast<int>(ProtocolVersion::V2)) {
            return failure(ReplayLoadError::UnsupportedProtocolVersion,
                           QStringLiteral("Unsupported replay protocol version %1").arg(protocolVersion));
        }

        header.formatVersion = ReplayFormatVersion::V2;
        header.protocolVersion = ProtocolVersion::V2;
    } else {
        header.formatVersion = ReplayFormatVersion::LegacyV1;
        header.protocolVersion = ProtocolVersion::V1;
        cursor = firstLineStart;
        lineNumber = firstLineNumber - 1;
    }

    const ProtocolCodecRouter router;
    QList<ReplayEvent> decodedEvents;
    qint64 previousElapsed = 0;
    bool hasPreviousElapsed = false;
    QByteArrayView line;
    while (nextLine(data, &cursor, &lineNumber, &line)) {
        if (isBlank(line))
            continue;

        const qsizetype space = line.indexOf(' ');
        if (space <= 0 || space == line.size() - 1) {
            return failure(ReplayLoadError::InvalidTimelineEntry,
                           QStringLiteral("Replay line %1 must contain elapsed time and a message")
                               .arg(lineNumber));
        }

        qint64 elapsedMs = 0;
        if (!parseElapsed(QByteArrayView(line).first(space), &elapsedMs)) {
            return failure(ReplayLoadError::InvalidElapsedTime,
                           QStringLiteral("Replay line %1 has an invalid elapsed time")
                               .arg(lineNumber));
        }
        if (header.formatVersion == ReplayFormatVersion::V2
            && hasPreviousElapsed && elapsedMs < previousElapsed) {
            return failure(ReplayLoadError::InvalidElapsedTime,
                           QStringLiteral("Replay line %1 decreases elapsed time")
                               .arg(lineNumber));
        }

        const QByteArrayView rawMessage = line.sliced(space + 1);
        const qsizetype maxPacketSize = header.protocolVersion == ProtocolVersion::V2
            ? ProtocolV2Codec::MaxPacketSize : ProtocolV1Codec::MaxPacketSize;
        if (rawMessage.size() > maxPacketSize) {
            return failure(ReplayLoadError::PacketTooLarge,
                           QStringLiteral("Replay line %1 message exceeds 65535 bytes")
                               .arg(lineNumber));
        }

        ProtocolMessage message;
        const ProtocolDecodeResult decode = router.decode(
            header.protocolVersion, rawMessage, &message);
        if (!decode.success) {
            return failure(ReplayLoadError::ProtocolDecodeFailure,
                           QStringLiteral("Replay line %1 protocol decode failed: %2")
                               .arg(lineNumber).arg(decode.detail));
        }
        if (message.command == S_COMMAND_PROTOCOL_SWITCH) {
            return failure(ReplayLoadError::ProtocolDecodeFailure,
                           QStringLiteral("Replay line %1 contains a protocol switch event")
                               .arg(lineNumber));
        }

        ReplayEvent event;
        event.elapsedMs = elapsedMs;
        event.message = message;
        decodedEvents.append(event);
        previousElapsed = elapsedMs;
        hasPreviousElapsed = true;
    }

    ReplayLoadResult result;
    result.success = true;
    result.header = header;
    result.events = decodedEvents;
    return result;
}

ReplayWriter::ReplayWriter()
{
    reset();
}

bool ReplayWriter::appendEvent(
    qint64 elapsedMs, const ProtocolMessage &message, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (message.command == S_COMMAND_PROTOCOL_SWITCH)
        return true;
    if (elapsedMs < 0) {
        if (error != nullptr)
            *error = QStringLiteral("Replay elapsed time must not be negative");
        return false;
    }
    if (m_hasEvents && elapsedMs < m_lastElapsedMs) {
        if (error != nullptr)
            *error = QStringLiteral("Replay elapsed time must be monotonic");
        return false;
    }

    ProtocolMessage replayMessage = message;
    if (replayMessage.messageId == 0) {
        replayMessage.messageId = nextAvailableMessageId();
        if (replayMessage.messageId == 0) {
            if (error != nullptr)
                *error = QStringLiteral("Replay-local message IDs are exhausted");
            return false;
        }
    } else {
        m_usedMessageIds.insert(replayMessage.messageId);
    }

    QString encodeError;
    const QByteArray encoded = m_router.encode(
        ProtocolVersion::V2, replayMessage, &encodeError);
    if (encoded.isEmpty()) {
        if (error != nullptr)
            *error = encodeError;
        return false;
    }

    m_eventRecords.append(QByteArray::number(elapsedMs) + ' ' + encoded);
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
    return ReplayHeader();
}

QList<QByteArray> ReplayWriter::eventRecords() const
{
    return m_eventRecords;
}

QByteArray ReplayWriter::rawReplayData() const
{
    QByteArray data = headerLine();
    data.append('\n');
    for (const QByteArray &record : m_eventRecords) {
        data.append(record);
        data.append('\n');
    }
    return data;
}

QByteArray ReplayWriter::headerLine()
{
    return QByteArrayLiteral("QSAN_REPLAY {\"format_version\":2,\"protocol_version\":2}");
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
