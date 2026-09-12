#include "ai-data-store.h"

#include "runtime-paths.h"

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

const QString AiDataRelativePath = QStringLiteral("lua/ai/data/AiData");
const qsizetype AiDataMaximumBytes = 8 * 1024 * 1024;

// AI learning data is user data generated at runtime and must not be written back into
// the install tree — AppImage is a read-only squashfs and /usr/share is usually not
// user-owned. Writes always go to the user data root; reads try the user data root first
// and fall back to the bundled copy in the asset tree (for legacy deployments and the dev
// tree the user data root is the asset tree, so behavior is unchanged).
QString aiDataWritePath()
{
    return QSanRuntimePaths::userDataPath(AiDataRelativePath);
}

QString aiDataReadPath()
{
    const QString writable = QSanRuntimePaths::userDataPath(AiDataRelativePath);
    if (QFile::exists(writable))
        return writable;
    const QString bundled = QSanRuntimePaths::assetPath(AiDataRelativePath);
    return QFile::exists(bundled) ? bundled : writable;
}

}

QString AiDataStore::read()
{
    QMutexLocker locker(&aiDataMutex());
    QFile file(aiDataReadPath());
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
    const QString target = aiDataWritePath();
    QLockFile processLock(target + QStringLiteral(".lock"));
    processLock.setStaleLockTime(30000);
    if (!processLock.tryLock(100)) {
        if (error)
            *error = QStringLiteral("AI data store is busy");
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("Unable to save AI data");
        return false;
    }
    return true;
}
