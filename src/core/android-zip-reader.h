#ifndef QSAN_ANDROID_ZIP_READER_H
#define QSAN_ANDROID_ZIP_READER_H

#include "android-content-store.h"

#include <QTemporaryFile>

class AndroidZipReader
{
public:
    explicit AndroidZipReader(const QString &spoolDirectory = QString());
    struct Entry {
        QString path;
        QString originalName;
        QByteArray rawName;
        quint16 flags = 0;
        quint16 method = 0;
        quint32 externalAttributes = 0;
        quint64 compressedSize = 0;
        quint64 uncompressedSize = 0;
        quint32 crcValue = 0;
        quint64 localHeaderOffset = 0;
        bool directory = false;
    };

    bool open(QIODevice &source, const AndroidContentStore::ImportLimits &limits,
              std::atomic_bool *cancel, QString *error = nullptr);
    const QList<Entry> &entries() const { return m_entries; }
    bool extract(const Entry &entry, QIODevice &destination,
                 std::atomic_bool *cancel, const AndroidContentStore::Progress &progress,
                 QString *error = nullptr);
    quint64 archiveSize() const { return m_archiveSize; }

private:
    bool fail(QString *error, const QString &message);
    QTemporaryFile m_spool;
    QList<Entry> m_entries;
    quint64 m_archiveSize = 0;
    quint64 m_centralOffset = 0;
    AndroidContentStore::ImportLimits m_limits;
};

#endif
