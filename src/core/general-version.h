#ifndef _GENERAL_VERSION_H
#define _GENERAL_VERSION_H

#include <QString>
#include <QStringList>

#include <functional>

int generalVersionPriority(const QString &objectName);

QStringList dedupByVersion(
    const QStringList &names,
    const std::function<bool(const QString &, const QString &)> &sameCharacter);

#endif
