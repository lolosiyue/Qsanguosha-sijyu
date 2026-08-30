#ifndef QSAN_REPLAY_CODEC_H
#define QSAN_REPLAY_CODEC_H

#include "protocol/protocol-message.h"
#include "protocol/protocol-runtime.h"

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

namespace QSanReplay {

enum class ReplayFormatVersion : quint8
{
    V2 = 2
};

struct ReplayHeader
{
    QString format = QStringLiteral("qsanguosha-replay");
    ReplayFormatVersion formatVersion = ReplayFormatVersion::V2;
    QSanProtocol::ProtocolVersion protocolVersion = QSanProtocol::ProtocolVersion::V2;
    int schemaVersion = 1;
    QString gameVersion;
    QString modName;
    bool takeover = false;
};

struct ReplayEvent
{
    qint64 elapsedMs = 0;
    QSanProtocol::ProtocolMessage message;
};

enum class ReplayLoadError
{
    None,
    FileOpenFailure,
    UnsupportedContainer,
    EmptyInput,
    InvalidHeader,
    UnsupportedFormatVersion,
    UnsupportedProtocolVersion,
    InvalidTimelineEntry,
    InvalidElapsedTime,
    ProtocolDecodeFailure,
    PacketTooLarge
};

struct ReplayLoadResult
{
    bool success = false;
    ReplayLoadError error = ReplayLoadError::None;
    QString detail;
    ReplayHeader header;
    QList<ReplayEvent> events;
};

class ReplayReader
{
public:
    static constexpr qsizetype MaxHeaderSize = 4096;

    ReplayLoadResult read(QByteArrayView data) const;
};

class ReplayWriter
{
public:
    explicit ReplayWriter(const QString &gameVersion = QStringLiteral("unknown"),
                          const QString &modName = QStringLiteral("unknown"),
                          bool takeover = false);

    bool appendEvent(qint64 elapsedMs,
                     const QSanProtocol::ProtocolMessage &message,
                     QString *error = nullptr);
    void reset();

    ReplayHeader header() const;
    QList<QByteArray> eventRecords() const;
    QByteArray rawReplayData() const;

    static QByteArray headerLine(
        const QString &gameVersion = QStringLiteral("unknown"),
        const QString &modName = QStringLiteral("unknown"),
        bool takeover = false);

private:
    quint64 nextAvailableMessageId();

    QSanProtocol::ProtocolCodecRouter m_router;
    QSanProtocol::ProtocolMessageIdGenerator m_messageIds;
    QSet<quint64> m_usedMessageIds;
    QList<QByteArray> m_eventRecords;
    qint64 m_lastElapsedMs = 0;
    bool m_hasEvents = false;
    ReplayHeader m_header;
};

}

#endif
