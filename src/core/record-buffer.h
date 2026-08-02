#ifndef QSAN_RECORD_BUFFER_H
#define QSAN_RECORD_BUFFER_H

#include <QElapsedTimer>
#include <QList>
#include <QString>

class RecordBuffer
{
public:
    RecordBuffer();

    void recordLine(const QString &line);
    QList<QByteArray> getRecords() const;
    QByteArray rawData() const;
    bool saveText(const QString &filename) const;

private:
    QElapsedTimer watch;
    QByteArray data;
};

#endif
