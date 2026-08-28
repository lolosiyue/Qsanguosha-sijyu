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

// AI 學習資料係執行期產生嘅使用者資料,唔可以寫返入安裝樹 —— AppImage 係
// 唯讀 squashfs,/usr/share 亦通常唔屬於使用者。寫一律去 user data root;
// 讀就 user data 行先,搵唔到先返去資產樹入面隨包附帶嗰份(舊有部署同開發樹
// 嘅 user data root 本身就係資產樹,行為同以前一樣)。
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
