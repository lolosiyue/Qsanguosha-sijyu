#ifndef QSAN_PACKAGE_STORE_H
#define QSAN_PACKAGE_STORE_H

#include "package-catalog.h"

#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>

// Local, restart-staged installer for declared Lua/content packages.
// Mutable data is kept under userDataRoot/package-store, never under packages/.
class PackageStore
{
public:
    explicit PackageStore(const QString &runtimeRoot, const QString &userDataRoot);

    struct TreeSize { quint64 bytes = 0; quint64 entries = 0; };
    struct Limits {
        quint64 maxInstalledBytes = 8ULL * 1024 * 1024 * 1024;
        quint64 maxInstalledEntries = 200000;
        quint64 reserveBytes = 512ULL * 1024 * 1024;
    };
    struct SpacePlan {
        quint64 stageUserBytes = 0;
        quint64 restartUserBytes = 0;
        quint64 restartRuntimeBytes = 0;
    };
    // Metadata-only accounting, shared by real preflight and boundary tests.
    // Existing active/pending/rollback/download files already reduce free space.
    static bool fitsQuota(const TreeSize &size, const Limits &limits, QString *error = nullptr);
    static bool planSpace(const TreeSize &active, const TreeSize &installs,
                          const TreeSize &backup, const TreeSize &incoming,
                          quint64 incomingCopies, quint64 allocationUnit,
                          SpacePlan *plan, QString *error = nullptr);
    static bool fitsAvailableSpace(const SpacePlan &plan, quint64 userAvailable,
                                   quint64 runtimeAvailable, bool sameVolume,
                                   const Limits &limits, QString *error = nullptr);
    Limits limits() const { return m_limits; }

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
    bool stageArchive(QIODevice &source, QString *error = nullptr,
                      std::atomic_bool *cancel = nullptr,
                      const QString &expectedId = QString(),
                      const QString &expectedVersion = QString());
    bool remove(const QString &id, QString *error = nullptr);
    bool rollback(const QString &id, QString *error = nullptr);

    QSanPackages::Catalog packages() const;
    QVariantList overview() const;
    QStringList pendingIds() const;
    bool hasPending() const;

private:
    QString m_runtimeRoot;
    QString m_userDataRoot;
    Limits m_limits;
    QString m_limitsError;
    bool preflight(const QVariantMap &changes, const QVariantMap &overrideSources,
                   const QString &incomingId, const TreeSize &incoming,
                   quint64 incomingCopies, QString *error) const;
    bool checkStorage(const SpacePlan &plan, QString *error) const;
    bool checkUserSpace(quint64 bytes, QString *error) const;
    bool writeJournal(const QVariantMap &journal, QString *error) const;
    QVariantMap readJournal(QString *error) const;
    bool stageValidatedPackage(const QString &source, QString *error);
    bool checkRemoval(const QString &id, QString *error) const;
    bool validateEffectiveSet(const QVariantMap &changes, const QVariantMap &overrideSources,
                              QString *error) const;
    bool configured() const { return !m_runtimeRoot.isEmpty() && !m_userDataRoot.isEmpty(); }
};

#endif
