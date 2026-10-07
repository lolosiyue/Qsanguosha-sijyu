#ifndef QSAN_UPDATE_CATALOG_H
#define QSAN_UPDATE_CATALOG_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QUrl>

namespace QSanUpdates {
constexpr qint64 CatalogLimit = 4 * 1024 * 1024;
constexpr qint64 PackageLimit = 256 * 1024 * 1024;
constexpr qint64 GameLimit = 2LL * 1024 * 1024 * 1024;

struct Entry {
    bool material = false;
    QString id, version, currentVersion, name, notes, sha256;
    QUrl url, releasePage;
    qint64 size = 0;
    bool downloadable = false;
    QString reason;
};
struct Catalog {
    QList<Entry> entries;
    QString error;
    bool valid() const { return error.isEmpty(); }
};

bool safeHttps(const QUrl &url);
// Strict date (YYYYMMDD) or numeric semantic versions (optional v, prerelease).
// Incompatible/unknown version families are never guessed to be newer.
int compareVersions(const QString &a, const QString &b, bool *comparable = nullptr);
QString platform();
Catalog materials(const QByteArray &json, const QString &gameVersion,
                  const QMap<QString, QString> &installed,
                  const QMap<QString, QString> &pending = {});
Catalog releases(const QByteArray &json, const QString &currentVersion,
                 const QString &platform, bool includePrerelease);
}
#endif
