#ifndef QSAN_PACKAGE_STORE_H
#define QSAN_PACKAGE_STORE_H

#include "package-catalog.h"

#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Local, restart-staged installer for declared Lua/content packages.
// Mutable data is kept under userDataRoot/package-store, never under packages/.
class PackageStore
{
public:
    explicit PackageStore(const QString &runtimeRoot, const QString &userDataRoot);

    QString runtimeRoot() const { return m_runtimeRoot; }
    QString packagesRoot() const;
    QString userStoreRoot() const;

    // Applies the complete pending set before the package catalog is loaded.
    bool prepareStartup(QString *error = nullptr);
    bool beginBootAttempt(QString *error = nullptr);
    bool markBootSuccessful(QString *error = nullptr);
    bool hasUnsuccessfulBootAttempt() const;
    bool recoverPrevious(QString *error = nullptr);

    // Imports only stage immutable package trees. Active files change on the
    // next prepareStartup() call, as one catalog transaction.
    bool stageDirectory(const QString &packageDirectory, QString *error = nullptr);
    bool stageArchive(QIODevice &source, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
    bool rollback(const QString &id, QString *error = nullptr);

    QSanPackages::Catalog packages() const;
    QVariantList overview() const;
    QStringList pendingIds() const;
    bool hasPending() const;

private:
    QString m_runtimeRoot;
    QString m_userDataRoot;
    bool writeJournal(const QVariantMap &journal, QString *error) const;
    QVariantMap readJournal(QString *error) const;
    bool stageValidatedPackage(const QString &source, QString *error);
    bool checkRemoval(const QString &id, QString *error) const;
    bool validateEffectiveSet(const QVariantMap &changes, const QVariantMap &overrideSources,
                              QString *error) const;
    bool configured() const { return !m_runtimeRoot.isEmpty() && !m_userDataRoot.isEmpty(); }
};

#endif
