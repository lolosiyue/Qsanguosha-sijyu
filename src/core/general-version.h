#ifndef _GENERAL_VERSION_H
#define _GENERAL_VERSION_H

#include <QString>
#include <QStringList>

#include <functional>

int generalVersionPriority(const QString &objectName);

// Keep the mode's version only when its counterpart is also in the admitted pool.
QStringList filterGeneralVersionsForMode(const QStringList &names, bool hegemony);

QStringList dedupByVersion(
    const QStringList &names,
    const std::function<bool(const QString &, const QString &)> &sameCharacter);

#endif
