#include "record-buffer.h"
#include "protocol.h"

#include <QSaveFile>
#include <QFileInfo>

RecordBuffer::RecordBuffer(const QString &gameVersion,
                           const QString &modName,
                           bool takeover)
    : writer(gameVersion, modName, takeover)
{
    watch.start();
}

bool RecordBuffer::recordMessage(
    const QSanProtocol::ProtocolMessage &message, QString *error)
{
    const auto fail = [this, error](const QString &detail) {
        if (m_error.isEmpty()) m_error = detail;
        if (error) *error = m_error;
        return false;
    };
    if (!m_error.isEmpty()) return fail(m_error);
    QString detail;
    const qint64 elapsed = m_bootstrapElapsedMs >= 0 ? m_bootstrapElapsedMs : watch.elapsed();
    if (!writer.appendEvent(elapsed, message, &detail)) return fail(detail);
    // Keep metadata with the recorded stream; Room tags are cleared on shutdown.
    if (message.command == QSanProtocol::S_COMMAND_ADD_ROUND)
        ++m_roundCount;
    else if (message.command == QSanProtocol::S_COMMAND_STATE_SYNC) {
        const auto sync = message.payload.toMap();
        if (sync.value(QStringLiteral("phase")).toString() == QLatin1String("end")
            && sync.contains(QStringLiteral("round")))
            m_roundCount = sync.value(QStringLiteral("round")).toInt();
    }
    if (m_stream.isOpen()) {
        const QByteArray line = writer.eventRecords().constLast() + '\n';
        if (m_stream.write(line) != line.size()) return fail(m_stream.errorString());
        // A completed line is independently readable after an interrupted run.
        if (++m_pendingWrites >= 128 || elapsed - m_lastFlushElapsedMs >= 1000) {
            if (!m_stream.flush()) return fail(m_stream.errorString());
            m_pendingWrites = 0;
            m_lastFlushElapsedMs = elapsed;
        }
    }
    return true;
}

bool RecordBuffer::openTextStream(const QString &filename, QString *error)
{
    if (m_stream.isOpen() || !writer.eventRecords().isEmpty() || QFileInfo::exists(filename)) {
        m_error = QStringLiteral("Replay stream must start in a new file before its first event");
    } else {
        m_stream.setFileName(filename);
        if (m_stream.open(QIODevice::WriteOnly)) {
            const QByteArray header = writer.rawReplayData();
            if (m_stream.write(header) == header.size() && m_stream.flush()) return true;
        }
        m_error = m_stream.errorString();
        m_stream.close();
    }
    if (error) *error = m_error;
    return false;
}

void RecordBuffer::beginTimeline(const QElapsedTimer &clock, qint64 bootstrapElapsedMs)
{
    // Copies preserve the same monotonic origin for every recorded seat.
    watch = clock;
    m_bootstrapElapsedMs = bootstrapElapsedMs;
}

void RecordBuffer::completeBootstrap()
{
    m_bootstrapElapsedMs = -1;
}

QString RecordBuffer::errorString() const
{
    return m_error;
}

int RecordBuffer::roundCount() const
{
    return m_roundCount;
}

QList<QByteArray> RecordBuffer::getRecords() const
{
    return writer.eventRecords();
}

QByteArray RecordBuffer::rawData() const
{
    return rawReplayData();
}

QByteArray RecordBuffer::rawReplayData() const
{
    return writer.rawReplayData();
}

bool RecordBuffer::saveText(const QString &filename) const
{
    if (!filename.endsWith(".txt"))
        return false;

    if (m_stream.isOpen()
        && QFileInfo(m_stream.fileName()).absoluteFilePath() == QFileInfo(filename).absoluteFilePath()) {
        if (!m_stream.flush() && m_error.isEmpty()) m_error = m_stream.errorString();
        m_stream.close();
        return m_error.isEmpty();
    }
    if (!m_error.isEmpty()) return false;

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray data = rawReplayData();
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
