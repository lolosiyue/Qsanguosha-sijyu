#ifndef QSAN_RECORD_BUFFER_H
#define QSAN_RECORD_BUFFER_H

#include "replay/replay-codec.h"

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QString>

class RecordBuffer
{
public:
    explicit RecordBuffer(
        const QString &gameVersion = QStringLiteral("unknown"),
        const QString &modName = QStringLiteral("unknown"),
        bool takeover = false);

    bool recordMessage(const QSanProtocol::ProtocolMessage &message,
                       QString *error = nullptr);
    QList<QByteArray> getRecords() const;
    QByteArray rawData() const;
    QByteArray rawReplayData() const;
    bool saveText(const QString &filename) const;
    bool openTextStream(const QString &filename, QString *error = nullptr);
    void beginTimeline(const QElapsedTimer &clock, qint64 bootstrapElapsedMs);
    void completeBootstrap();
    QString errorString() const;
    int roundCount() const;

private:
    QElapsedTimer watch;
    QSanReplay::ReplayWriter writer;
    qint64 m_bootstrapElapsedMs = -1;
    qint64 m_lastFlushElapsedMs = 0;
    int m_pendingWrites = 0;
    int m_roundCount = 0;
    mutable QFile m_stream;
    mutable QString m_error;
};

#endif
