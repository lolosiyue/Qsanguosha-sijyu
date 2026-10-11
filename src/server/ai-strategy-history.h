#ifndef AI_STRATEGY_HISTORY_H
#define AI_STRATEGY_HISTORY_H

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>

// Scope is a trusted server identity, never a Lua value or Room seat name.
// No default path/profile is chosen here. In particular this service does not
// consult HOME or import the unpartitioned legacy AiData into a viewer profile.
struct AIStrategyHistorySnapshot {
    bool known = false;
    QJsonObject data;
    QString revision;
    QString error;
};

class AIStrategyHistoryWrite {
public:
    ~AIStrategyHistoryWrite();
    AIStrategyHistoryWrite(const AIStrategyHistoryWrite &) = delete;
    AIStrategyHistoryWrite &operator=(const AIStrategyHistoryWrite &) = delete;
    // Call only after the containing event has committed successfully. Until
    // then destroying this object cancels the unpublished write and unlocks it.
    bool commit(QString *error = nullptr);
    QString revision() const;
private:
    struct Impl;
    explicit AIStrategyHistoryWrite(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    friend class AIStrategyHistoryStore;
    friend std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
        const QString &, const QString &, QString *, const QString &);
};

class AIStrategyHistoryStore {
public:
    // rootDirectory must already exist; no implicit creation outside an
    // explicitly configured location. Existing symlink targets are rejected.
    AIStrategyHistoryStore(const QString &rootDirectory, const QString &trustedScope);
    AIStrategyHistorySnapshot read(const QSet<QString> &allowedSkills,
                                  const QSet<QString> &allowedCardNames) const;
    // Replacement is only for currently projected keys. Nonprojected prior
    // data survives; values outside these native allowlists are rejected.
    // CAS and a thread/process lock reject concurrent lost updates explicitly.
    std::unique_ptr<AIStrategyHistoryWrite> prepare(
        const QString &expectedRevision, const QJsonObject &visibleReplacement,
        const QSet<QString> &allowedSkills, const QSet<QString> &allowedCardNames,
        QString *error = nullptr) const;
    QString error() const { return m_error; }
    // Native snapshot binding: reject a later configuration change before
    // preparing data collected under a different trusted identity.
    QString scopeId() const { return m_scopeHash; }
private:
    QString m_root, m_scopeHash, m_error;
};

struct AIStrategyLegacyHistory {
    bool valid = false;
    QJsonObject data;
    QString error;
    QStringList sourceFiles;
    QStringList warnings;
};

// Read-only inspection. Valid nonempty AiData wins over the four old category
// files. The Engine's AiDataStore rejects malformed JSON before the Lua caller,
// so that case takes the legacy branch and records a warning. Numeric strings are normalized
// for the typed contract. Unattributed samples are NEVER autoimported.
// Packaged deployments can read modern AiData from userData while the four old
// files remain under assetRoot. The optional trusted path models that split.
AIStrategyLegacyHistory inspectAIStrategyLegacyHistory(const QString &legacyDirectory,
                                                     const QString &modernAiDataPath = QString());

// Byte-for-byte content-addressed recovery archive, independent of all viewer
// profiles. The staged write is published only when its transaction commits.
// Its manifest contains validated parsed data for later explicit review/import.
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
    const QString &archiveDirectory, const QString &legacyDirectory,
    QString *error = nullptr, const QString &modernAiDataPath = QString());

#endif
