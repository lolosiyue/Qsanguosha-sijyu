#ifndef QSAN_SERVER_CONFIG_H
#define QSAN_SERVER_CONFIG_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

struct ServerConfigLoadResult
{
    bool success = false;
    QVariantMap values;
    QStringList errors;
};

ServerConfigLoadResult loadServerConfigFile(const QString &path);
QStringList validateServerConfigValues(const QVariantMap &values);
QVariantMap defaultServerConfigValues();
bool isKnownServerConfigKey(const QString &key);
QString serverConfigText(const QVariantMap &values);
QString serverConfigJson(const QVariantMap &values);

#endif
