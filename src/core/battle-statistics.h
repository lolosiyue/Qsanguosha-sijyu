#ifndef BATTLE_STATISTICS_H
#define BATTLE_STATISTICS_H

#include "resolution-history.h"
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Local analytical projection only: never mutates authoritative rule history.
namespace BattleStatistics {
constexpr int SchemaVersion = 1;
constexpr int AnalysisVersion = 1;
constexpr int MinimumGames = 5;
struct Match {
    QString rootMatchId;
    quint64 generation = 0;
    QString branchId;
    // mode, player_count, rules_version, excluded_reason, terminal, winner,
    // participants [{player,general,general2,role,control,control_history,
    // identity_changed}], mixed_control. Player is a seat ID, not screen name.
    QVariantMap metadata;
    ResolutionHistorySnapshot history;
};
// Pure projection; useful without a database. One row per participant, with
// dataset, environment, metrics, turns, coverage, general and general2.
QVariantList analyze(const Match &match);
QString defaultDatabasePath();
// Atomic replacement by rootMatchId, generation fenced. Equal generation is
// idempotent; lower generations never change either facts or contributions.
// invalidation=true removes the old terminal contribution, preserving a fence
// until a completed Match of that same or a newer generation arrives.
// Synchronous import/test seam: creates a closed sidecar for NEW roots. Runtime
// Rooms must use armMatch/notifyTimelineRestore/submit/closeMatch instead.
bool save(const Match &match, const QString &path, QString *error = nullptr,
          bool invalidation = false);
// Runtime lifecycle: arm a new root before play. Failed arming permanently
// prevents this process from publishing that root. Existing-root check never
// waits for a SQLite lock; restore notifications do no SQLite work.
bool armMatch(const Match &match, const QString &path = QString(), QString *error = nullptr);
// Background/import-only repair: caller supplies CURRENT authoritative terminal
// effective history from the timeline owner, never battle_matches.history. A
// valid existing journal is required; foreign active ownership is explicitly
// replaced, with same-generation full replacement allowed. No game restore here.
bool recoverMatch(const Match &match, const QString &path = QString(), QString *error = nullptr);
// Call only after successful authoritative restore and prior armMatch. True
// means the independent generation journal is durable, NOT that SQLite caught
// up. SQLite invalidation is queued; this function never waits for its lock.
// acknowledgePending is only for an owner's exact-argument retry after a false
// return: the background writer may already have made that fence durable.
bool notifyTimelineRestore(const Match &match, const QString &path = QString(), QString *error = nullptr,
                           bool acknowledgePending = false);
// Queue a lifecycle seal after earlier saves; active stays set unless the DB
// caught up and no dirty state remains. Never call while further restore/play
// for this Room is possible. Permanent generation marker is retained.
void closeMatch(const QString &rootMatchId, quint64 generation, const QString &path = QString());
void submit(const Match &match, bool invalidation = false);
// Flush owned background jobs (also drained during application shutdown).
void waitForPendingWrites();
// Dirty or uncertain journals suppress stale rows, including after restart.
// Foreign active markers, missing/corrupt journals are never assumed fresh.
// Cache-only accessor; readSummaries scans startup journals in its reader worker.
int pendingTimelineInvalidations(const QString &path = QString());
// In-process invalidation/publication notification for open Qt viewers.
quint64 projectionRevision(const QString &path = QString());
// Rows grouped ONLY within the same dataset/environment/general pair. Empty
// dataset returns all datasets. Labels are empty and status provisional below
// MinimumGames, unknown when coverage is insufficient. Error is never zero data.
QVariantList readSummaries(const QString &path = QString(),
                           const QString &dataset = QString(), QString *error = nullptr);
}
#endif
