#ifndef _PACKAGE_SELECTION_POLICY_H
#define _PACKAGE_SELECTION_POLICY_H

#include <QStringList>

namespace PackageSelectionPolicy {

QStringList defaultEnabledPackages();
QStringList normalize(const QStringList &universe, const QStringList &requested);
QStringList complement(const QStringList &universe, const QStringList &enabled);

}

#endif
