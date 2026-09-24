#ifndef _PACKAGE_SELECTION_POLICY_H
#define _PACKAGE_SELECTION_POLICY_H

#include <QStringList>

namespace PackageSelectionPolicy {

constexpr int CurrentMigrationVersion = 5;

QStringList defaultEnabledPackages();
// Applies only to a persisted whitelist; legacy/runtime blacklists keep their bans.
QStringList migrateEnabledPackages(const QStringList &universe,
                                  const QStringList &requested, int migrationVersion);
QStringList normalize(const QStringList &universe, const QStringList &requested);
QStringList complement(const QStringList &universe, const QStringList &enabled);

}

#endif
