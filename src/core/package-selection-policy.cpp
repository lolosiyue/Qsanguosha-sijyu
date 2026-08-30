#include "package-selection-policy.h"

#include <QHash>
#include <QSet>

namespace PackageSelectionPolicy {

namespace {

QString canonicalName(const QString &name)
{
    QString result;
    result.reserve(name.size());
    foreach (const QChar character, name.toCaseFolded()) {
        if (character.isLetterOrNumber())
            result.append(character);
    }

    static const QHash<QString, QString> aliases = {
        { QStringLiteral("olli"), QStringLiteral("li") },
        { QStringLiteral("olbei"), QStringLiteral("bei") },
        { QStringLiteral("olguo"), QStringLiteral("guo") },
        { QStringLiteral("oljie"), QStringLiteral("jiepackage") },
        { QStringLiteral("olyue"), QStringLiteral("yue") },
        { QStringLiteral("hulaoguan"), QStringLiteral("hulaopass") }
    };
    return aliases.value(result, result);
}

QSet<QString> canonicalNames(const QStringList &names)
{
    QSet<QString> result;
    foreach (const QString &name, names)
        result.insert(canonicalName(name));
    return result;
}

}

QStringList defaultEnabledPackages()
{
    return QStringList()
        << "standard" << "wind" << "fire" << "thicket" << "mountain"
        << "YJCM" << "YJCM2012"
        << "standard_cards" << "standard_ex_cards" << "maneuvering";
}

QStringList normalize(const QStringList &universe, const QStringList &requested)
{
    QStringList result;
    const QSet<QString> requestedNames = canonicalNames(requested);
    foreach (const QString &name, universe) {
        if (requestedNames.contains(canonicalName(name)) && !result.contains(name))
            result << name;
    }
    return result;
}

QStringList complement(const QStringList &universe, const QStringList &enabled)
{
    QStringList result;
    const QSet<QString> enabledNames = canonicalNames(enabled);
    foreach (const QString &name, universe) {
        if (!enabledNames.contains(canonicalName(name)))
            result << name;
    }
    return result;
}

}
