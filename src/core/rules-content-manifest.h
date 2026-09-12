#ifndef QSAN_RULES_CONTENT_MANIFEST_H
#define QSAN_RULES_CONTENT_MANIFEST_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QByteArray>

namespace QSanRules {
struct ManifestEntry {
    QString name;
    QString script;
    QStringList dependencies;
    QStringList libs;
    QStringList lang;
    QStringList ai;
};

struct ContentManifest {
    QList<ManifestEntry> entries;
    QString error;
    bool descriptorPresent = false;
    bool isValid() const { return error.isEmpty(); }
};

ContentManifest parseContentManifest(const QStringList &declared);
// A descriptor is optional at runtime.  When absent, Engine supplies the
// legacy config.lua entries through parseContentManifest().
ContentManifest parseRuntimeContent(const QJsonObject &descriptor);
QJsonObject runtimeContentDescriptor(const ContentManifest &manifest);
QByteArray runtimeContentCanonical(const ContentManifest &manifest);
QString runtimeContentDigest(const ContentManifest &manifest);
QStringList manifestScripts(const ContentManifest &manifest);
QStringList manifestHashedFiles(const ContentManifest &manifest);
QStringList manifestDeliveredFiles(const ContentManifest &manifest);
QStringList manifestServerOnlyFiles(const ContentManifest &manifest);
}

#endif
