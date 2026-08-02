#include "record-buffer.h"

#include <QFile>

RecordBuffer::RecordBuffer()
{
    watch.start();
}

void RecordBuffer::recordLine(const QString &line)
{
    const int elapsed = watch.elapsed();
    if (line.endsWith("\n"))
        data.append(QString("%1 %2").arg(elapsed).arg(line).toUtf8());
    else
        data.append(QString("%1 %2\n").arg(elapsed).arg(line).toUtf8());
}

QList<QByteArray> RecordBuffer::getRecords() const
{
    return data.split('\n');
}

QByteArray RecordBuffer::rawData() const
{
    return data;
}

bool RecordBuffer::saveText(const QString &filename) const
{
    if (!filename.endsWith(".txt"))
        return false;

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return file.write(data) != -1;
}
