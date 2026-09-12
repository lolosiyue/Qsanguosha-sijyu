#ifndef QSAN_ANDROID_CONTENT_STORE_H
#define QSAN_ANDROID_CONTENT_STORE_H

#include <QIODevice>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <functional>

// Android 私有內容的 staged store。
//
// 所有變更先寫入新版本，下一次啟動由 prepareStartup() 驗證後切換；
// 因此 Engine 永遠只會看到一棵完整、可回復的實體 runtime 樹。
class AndroidContentStore
{
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

    // appDataRoot 為空時使用 QStandardPaths::AppDataLocation。
    explicit AndroidContentStore(const QString &appDataRoot = QString());

    // 必須在 QSanRuntimePaths::resolve() 前呼叫，套用 pending journal。
    bool prepareStartup(QString *error = nullptr);
    QString runtimeRoot() const;
    bool mediaReady() const;
    bool needsRecovery() const;
    bool hasPending() const;
    QVariantMap status() const;
    QList<PackageInfo> packages() const;

    // 啟動成功清除 boot marker；保留 previous 供明確 rollback。
    bool beginBootAttempt(QString *error = nullptr);
    bool markBootSuccessful(QString *error = nullptr);
    bool recoverPrevious(QString *error = nullptr);
    bool discardPendingAndDisableLastImport(QString *error = nullptr);

    // source 可為 QFile 或 SAF content URI 對應的非 seekable QIODevice。
    // 實作需分塊讀取；必要時只 spool 到私有 temp，不得 readAll 大型媒體包。
    bool stageMedia(QIODevice &source, Cancelled *cancel = nullptr,
                    const Progress &progress = Progress(), QString *error = nullptr);
    bool stageExtension(QIODevice &source, const QString &filename,
                        const QString &bundleId, Cancelled *cancel = nullptr,
                        const Progress &progress = Progress(), QString *error = nullptr);

    // 以下操作只修改 pending 版本，下一次啟動才生效。
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
                      const QVariantMap &validated = {}, QVariantMap *receipt = nullptr) const;
    bool publishSnapshot(const QVariantMap &snapshot, QString *version, QString *error, Cancelled *cancel = nullptr);
    bool stageSnapshot(const QVariantMap &snapshot, QString *error, Cancelled *cancel = nullptr);
    bool commitState(const QVariantMap &state, QString *error);
    void refreshPackages();
    void collectUnusedVersions();
};

#endif // QSAN_ANDROID_CONTENT_STORE_H
