#ifndef QSAN_RULES_CONTENT_MANIFEST_H
#define QSAN_RULES_CONTENT_MANIFEST_H

#include <QList>
#include <QString>
#include <QStringList>

namespace QSanRules {
struct ManifestEntry {
    QString script;
    QStringList libs;
    QStringList lang;
    QStringList ai;
};

struct ContentManifest {
    QList<ManifestEntry> entries;
    QString error;
    bool isValid() const { return error.isEmpty(); }
};

ContentManifest parseContentManifest(const QStringList &declared);
QStringList manifestScripts(const ContentManifest &manifest);
QStringList manifestHashedFiles(const ContentManifest &manifest);
QStringList manifestDeliveredFiles(const ContentManifest &manifest);
QStringList manifestServerOnlyFiles(const ContentManifest &manifest);
}

#endif
