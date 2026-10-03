#ifndef QSAN_ANDROID_CONTENT_STORE_H
#define QSAN_ANDROID_CONTENT_STORE_H

#include <QCoreApplication>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <functional>

// Staged store for Android private content.
//
// Write every change to a new version and let prepareStartup() validate and activate it on the next launch;
// Engine then sees a complete, recoverable runtime tree. Immutable media references share the same private blob,
// while rules and Lua remain separate physical files for each version.
class AndroidContentStore
{
    Q_DECLARE_TR_FUNCTIONS(AndroidContentStore)
public:
    struct ImportLimits
    {
        quint64 maxArchiveBytes = 0;
        quint64 maxExpandedBytes = 0;
        quint64 maxEntryBytes = 0;
        quint64 maxEntries = 0;
        quint32 maxCompressionRatio = 0;
    };

    struct PackageInfo
    {
        QString id;
        QString version;
        QString packageVersion;
        QString role;
        QString state;
        bool enabled = true;
        bool bundled = false;
        quint64 archiveBytes = 0;
        quint64 expandedBytes = 0;
        QString sha256;
        QStringList files;
    };

    using Cancelled = std::atomic_bool;
    using Progress = std::function<void(quint64 completed, quint64 total)>;

    // Use QStandardPaths::AppDataLocation when appDataRoot is empty.
    explicit AndroidContentStore(const QString &appDataRoot = QString());

    // Call before QSanRuntimePaths::resolve() to apply the pending journal.
    bool prepareStartup(QString *error = nullptr);
    QString runtimeRoot() const;
    bool mediaReady() const;
    bool needsRecovery() const;
    bool hasPending() const;
    QVariantMap status() const;
    QList<PackageInfo> packages() const;

    // Clear the boot marker after a successful launch; retain previous for explicit rollback.
    bool beginBootAttempt(QString *error = nullptr);
    bool markBootSuccessful(QString *error = nullptr);
    bool recoverPrevious(QString *error = nullptr);
    bool discardPendingAndDisableLastImport(QString *error = nullptr);

    // source may be a QFile or a non-seekable QIODevice backed by a SAF content URI.
    // Read in chunks; spool only to private temporary storage when needed, never readAll a large media package.
    bool stageMedia(QIODevice &source, Cancelled *cancel = nullptr,
                    const Progress &progress = Progress(), QString *error = nullptr);
    bool stageExtension(QIODevice &source, const QString &filename,
                        const QString &bundleId, Cancelled *cancel = nullptr,
                        const Progress &progress = Progress(), QString *error = nullptr);
    bool stageModularPackage(QIODevice &source, const QString &filename,
                             Cancelled *cancel = nullptr,
                             const Progress &progress = Progress(), QString *error = nullptr);

    // These operations modify only the pending version and take effect on the next launch.
    bool setPackageEnabled(const QString &packageId, bool enabled, QString *error = nullptr);
    bool removePackage(const QString &packageId, QString *error = nullptr);
    bool reorderPackages(const QStringList &packageIds, QString *error = nullptr);
    bool rollback(QString *error = nullptr);
    bool exportDescriptor(QIODevice &destination, QString *error = nullptr) const;

private:
    QString m_appDataRoot;
    QString m_storeRoot;
    QString m_runtimeRoot;
    QString m_statePath;
    QString m_lockPath;
    QList<PackageInfo> m_packages;
    QVariantMap m_state;
    QVariantMap m_snapshot;
    QString m_baseRoot;
    bool m_prepared = false;
    bool loadSnapshot(const QString &id, QVariantMap *snapshot, QString *error,
                      bool inspectMedia = false) const;
    bool publishSnapshot(const QVariantMap &snapshot, QString *version, QString *error, Cancelled *cancel = nullptr);
    bool stageSnapshot(const QVariantMap &snapshot, QString *error, Cancelled *cancel = nullptr);
    bool commitState(const QVariantMap &state, QString *error);
    void refreshPackages();
    void collectUnusedVersions();
};

#endif // QSAN_ANDROID_CONTENT_STORE_H
