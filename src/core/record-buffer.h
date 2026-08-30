#ifndef QSAN_RECORD_BUFFER_H
#define QSAN_RECORD_BUFFER_H

#include "replay/replay-codec.h"

#include <QElapsedTimer>
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

private:
    QElapsedTimer watch;
    QSanReplay::ReplayWriter writer;
};

#endif
