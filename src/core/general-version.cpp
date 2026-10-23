#include "general-version.h"

#include <QSet>

QStringList filterGeneralVersionsForMode(const QStringList &names, bool hegemony)
{
    QSet<QString> pool;
    foreach (const QString &name, names)
        pool.insert(name);
    QStringList kept;
    foreach (const QString &name, names) {
        // heg_X is the hegemony version of exactly X: heg_huangzhong replaces huangzhong only,
        // never other versions such as s4_cloud_huangzhong or ol_huangzhong.
        const bool hegemonyVersion = name.startsWith(QLatin1String("heg_"));
        const QString counterpart = hegemonyVersion ? name.mid(4) : QStringLiteral("heg_") + name;
        if (hegemonyVersion == hegemony || !pool.contains(counterpart))
            kept << name;
    }
    return kept;
}

int generalVersionPriority(const QString &objectName)
{
    const int separator = objectName.indexOf('_');
    if (separator < 0)
        return 0;

    const QString prefix = objectName.left(separator);
    if (prefix == "third") return 10;
    if (prefix == "second") return 9;
    if (prefix == "mobilemou") return 8;
    if (prefix == "oljie") return 7;
    if (prefix == "tenyear") return 6;
    if (prefix == "new") return 5;
    if (prefix == "mobile") return 4;
    if (prefix == "ol") return 3;
    if (prefix == "neo") return 2;
    if (prefix == "nos") return 1;
    return 0;
}

QStringList dedupByVersion(
    const QStringList &names,
    const std::function<bool(const QString &, const QString &)> &sameCharacter)
{
    QStringList kept;
    foreach (const QString &name, names) {
        int matchingIndex = -1;
        for (int i = 0; i < kept.size(); ++i) {
            if (sameCharacter(kept.at(i), name)) {
                matchingIndex = i;
                break;
            }
        }

        if (matchingIndex < 0) {
            kept << name;
        } else if (generalVersionPriority(name)
                   > generalVersionPriority(kept.at(matchingIndex))) {
            kept[matchingIndex] = name;
        }
    }
    return kept;
}
