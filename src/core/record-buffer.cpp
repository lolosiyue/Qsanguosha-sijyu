#include "record-buffer.h"

#include <QSaveFile>

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
    return writer.appendEvent(watch.elapsed(), message, error);
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
