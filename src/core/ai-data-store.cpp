#include "ai-data-store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>

namespace {

QMutex &aiDataMutex()
{
    static QMutex mutex;
    return mutex;
}

const QString AiDataPath = QStringLiteral("lua/ai/data/AiData");
const qsizetype AiDataMaximumBytes = 8 * 1024 * 1024;

}

QString AiDataStore::read()
{
    QMutexLocker locker(&aiDataMutex());
    QFile file(AiDataPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray data = file.read(AiDataMaximumBytes + 1);
    if (data.size() > AiDataMaximumBytes)
        return QString();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull())
        return QString();
    return QString::fromUtf8(data);
}

bool AiDataStore::write(const QString &json, QString *error)
{
    const QByteArray data = json.toUtf8();
    if (data.size() > AiDataMaximumBytes) {
        if (error)
            *error = QStringLiteral("AI data exceeds the size limit");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        if (error)
            *error = QStringLiteral("AI data must be valid JSON");
        return false;
    }

    QMutexLocker locker(&aiDataMutex());
    QLockFile processLock(AiDataPath + QStringLiteral(".lock"));
    processLock.setStaleLockTime(30000);
    if (!processLock.tryLock(100)) {
        if (error)
            *error = QStringLiteral("AI data store is busy");
        return false;
    }
    QSaveFile file(AiDataPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("Unable to save AI data");
        return false;
    }
    return true;
}
