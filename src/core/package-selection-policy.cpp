#include "package-selection-policy.h"

#include <QHash>
#include <QSet>

namespace PackageSelectionPolicy {

namespace {

QStringList originalHegemonyPackages()
{
    return QStringList()
        << "heg_standard"
        << "heg_standard_cards"
        << "heg_formation" << "heg_formation_equip"
        << "heg_momentum" << "heg_momentum_equip" << "heg_strategic_advantage";
}

QStringList expandedHegemonyPackages()
{
    return QStringList()
        << "heg_transformation" << "heg_transformation_equip"
        << "heg_power" << "heg_power_equip"
        << "heg_manoeuvre" << "heg_newsgs" << "heg_mol" << "heg_overseas"
        << "heg_lord_ex" << "heg_lord_ex_card";
}

QString canonicalName(const QString &name)
{
    QString result;
    result.reserve(name.size());
    foreach (const QChar character, name.toCaseFolded()) {
        if (character.isLetterOrNumber())
            result.append(character);
    }

    static const QHash<QString, QString> aliases = {
        // Read legacy settings into the single replacement namespace.
        { QStringLiteral("hegemony"), QStringLiteral("hegstandard") },
        { QStringLiteral("hformation"), QStringLiteral("hegformation") },
        { QStringLiteral("hmomentum"), QStringLiteral("hegmomentum") },
        { QStringLiteral("hstandard"), QStringLiteral("hegstandard") },
        { QStringLiteral("hstandardcard"), QStringLiteral("hegstandardcards") },
        { QStringLiteral("hstrategicadvantage"), QStringLiteral("hegstrategicadvantage") },
        { QStringLiteral("hformationequip"), QStringLiteral("hegformationequip") },
        { QStringLiteral("hmomentumequip"), QStringLiteral("hegmomentumequip") },
        { QStringLiteral("htransformation"), QStringLiteral("hegtransformation") },
        { QStringLiteral("htransformationequip"), QStringLiteral("hegtransformationequip") },
        { QStringLiteral("hpower"), QStringLiteral("hegpower") },
        { QStringLiteral("hpowerequip"), QStringLiteral("hegpowerequip") },
        { QStringLiteral("hmanoeuvre"), QStringLiteral("hegmanoeuvre") },
        { QStringLiteral("hnewsgs"), QStringLiteral("hegnewsgs") },
        { QStringLiteral("hmol"), QStringLiteral("hegmol") },
        { QStringLiteral("hoverseas"), QStringLiteral("hegoverseas") },
        { QStringLiteral("hlordex"), QStringLiteral("heglordex") },
        { QStringLiteral("hlordexcard"), QStringLiteral("heglordexcard") },
        { QStringLiteral("ohstandard"), QStringLiteral("hegstandard") },
        { QStringLiteral("ohstandardcards"), QStringLiteral("hegstandardcards") },
        { QStringLiteral("ohformation"), QStringLiteral("hegformation") },
        { QStringLiteral("ohformationequip"), QStringLiteral("hegformationequip") },
        { QStringLiteral("ohmomentum"), QStringLiteral("hegmomentum") },
        { QStringLiteral("ohmomentumequip"), QStringLiteral("hegmomentumequip") },
        { QStringLiteral("ohstrategicadvantage"), QStringLiteral("hegstrategicadvantage") },
        { QStringLiteral("ohstandardcard"), QStringLiteral("hegstandardcards") },
        { QStringLiteral("nostalyjcm2012"), QStringLiteral("nostalyjcm") },
        { QStringLiteral("nostalyjcm2013"), QStringLiteral("nostalyjcm") },
        { QStringLiteral("nostalyjcm2014"), QStringLiteral("nostalyjcm") },
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
        << "standard_cards" << "standard_ex_cards" << "maneuvering"
        << originalHegemonyPackages() << expandedHegemonyPackages();
}

QStringList migrateEnabledPackages(const QStringList &universe,
                                  const QStringList &requested, int migrationVersion)
{
    QStringList enabled = requested;
    // Old whitelists predate the replacement HEG content. Seed it once without
    // removing identity packages or expanding an explicit original HEG selection.
    // Version 4 only canonicalizes names; version-3 empty selections stay empty.
    const QStringList originalHegemony = originalHegemonyPackages();
    QStringList explicitReplacement;
    foreach (const QString &name, requested) {
        QString legacyName = name.toCaseFolded();
        legacyName.remove(QLatin1Char('_'));
        // Old HEG general-only selections still need the replacement deck.
        if (legacyName != QLatin1String("hegemony") && legacyName != QLatin1String("hformation")
            && legacyName != QLatin1String("hmomentum"))
            explicitReplacement << name;
    }
    if (migrationVersion < 3
        && normalize(originalHegemony, explicitReplacement).isEmpty()) {
        enabled << originalHegemony;
    }
    // Upgrade a complete old HEG selection once. An empty or explicitly reduced
    // selection remains the user's choice; subsequent settings loads never re-enable it.
    if (migrationVersion < 5
        && normalize(originalHegemony, enabled).size() == originalHegemony.size()
        && normalize(expandedHegemonyPackages(), requested).isEmpty()) {
        enabled << expandedHegemonyPackages();
    }
    return normalize(universe, enabled);
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
