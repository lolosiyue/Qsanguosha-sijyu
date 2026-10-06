#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

// Explicit opt-in, passive server evidence for controller GUI runs. Never used
// for rules, defaults or reply transport. The path is chosen by the test runner.
inline void writeControllerServerEvidence(const QString &kind, QJsonObject data)
{
    const QString path = qEnvironmentVariable("QSAN_CONTROLLER_SERVER_TRACE");
    if (path.isEmpty()) return;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    data.insert("kind", kind);
    data.insert("time_ms", QString::number(QDateTime::currentMSecsSinceEpoch()));
    file.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
}
