#ifndef QSAN_PACKAGE_CATALOG_H
#define QSAN_PACKAGE_CATALOG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace QSanPackages
{
struct Package
{
    QString id;
    QString version;
    QString root;
    QStringList dependencies;
    QJsonObject manifest;
};

struct Catalog
{
    QList<Package> packages;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

// Android opts out of filesystem inventory and digest validation; declared Lua remains required.
Catalog loadCatalog(const QString &runtimeRoot, bool verifyFiles = true, bool inspectFiles = true);
Package parsePackage(const QString &directory, QString *error, bool verifyFiles = true, bool inspectFiles = true);
QJsonArray extensionDescriptors(const Catalog &catalog);
QJsonArray packageDescriptors(const Catalog &catalog);
QString resolve(const QString &runtimeRoot, const QString &reference, QString *error = nullptr);

// The application installs one immutable catalog at startup. resolve() never re-hashes
// that catalog; callers that replace content must install the replacement explicitly.
void installCatalog(const Catalog &catalog);
void clearCatalog();
Catalog activeCatalog();

// Writable package data follows the engine's platform-specific user-data location.
QString packageDataPath(const QString &id);
}

#endif
