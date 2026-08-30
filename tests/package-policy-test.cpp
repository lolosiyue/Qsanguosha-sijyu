#include "general-version.h"
#include "package-selection-policy.h"

#include <QtCore>

#include <QDebug>

namespace {

QString characterName(const QString &generalName)
{
    const int separator = generalName.indexOf('_');
    return separator < 0 ? generalName : generalName.mid(separator + 1);
}

bool generalVersionPriorityIsStable()
{
    const QStringList names = {
        QStringLiteral("caocao"),
        QStringLiteral("nos_caocao"),
        QStringLiteral("neo_caocao"),
        QStringLiteral("ol_caocao"),
        QStringLiteral("mobile_caocao"),
        QStringLiteral("new_caocao"),
        QStringLiteral("tenyear_caocao"),
        QStringLiteral("oljie_caocao"),
        QStringLiteral("mobilemou_caocao"),
        QStringLiteral("second_caocao"),
        QStringLiteral("third_caocao")
    };

    for (int i = 0; i < names.size(); ++i) {
        if (generalVersionPriority(names.at(i)) != i) {
            qCritical() << "unexpected general version priority" << names.at(i)
                        << generalVersionPriority(names.at(i));
            return false;
        }
    }
    return generalVersionPriority(QStringLiteral("unknown_caocao")) == 0;
}

bool generalVersionDedupPreservesSlots()
{
    const QStringList input = {
        QStringLiteral("nos_caocao"),
        QStringLiteral("ol_xiahoudun"),
        QStringLiteral("mobile_caocao"),
        QStringLiteral("third_caocao"),
        QStringLiteral("xiahoudun"),
        QStringLiteral("second_sunquan"),
        QStringLiteral("ol_sunquan")
    };
    const QStringList expected = {
        QStringLiteral("third_caocao"),
        QStringLiteral("ol_xiahoudun"),
        QStringLiteral("second_sunquan")
    };

    const QStringList actual = dedupByVersion(input, [](const QString &left,
                                                        const QString &right) {
        return characterName(left) == characterName(right);
    });
    if (actual != expected)
        qCritical() << "general version dedup changed slots or priority" << actual;
    return actual == expected;
}

bool packageWhitelistPolicyIsStable()
{
    const QStringList expectedDefaults = {
        QStringLiteral("standard"), QStringLiteral("wind"),
        QStringLiteral("fire"), QStringLiteral("thicket"),
        QStringLiteral("mountain"), QStringLiteral("YJCM"),
        QStringLiteral("YJCM2012"), QStringLiteral("standard_cards"),
        QStringLiteral("standard_ex_cards"), QStringLiteral("maneuvering")
    };
    if (PackageSelectionPolicy::defaultEnabledPackages() != expectedDefaults)
        return false;

    const QStringList universe = {
        QStringLiteral("standard"), QStringLiteral("wind"),
        QStringLiteral("fire"), QStringLiteral("mountain")
    };
    const QStringList requested = {
        QStringLiteral("mountain"), QStringLiteral("wind"),
        QStringLiteral("wind"), QStringLiteral("missing")
    };
    const QStringList enabled = {
        QStringLiteral("wind"), QStringLiteral("mountain")
    };
    const QStringList banned = {
        QStringLiteral("standard"), QStringLiteral("fire")
    };

    if (PackageSelectionPolicy::normalize(universe, requested) != enabled
        || PackageSelectionPolicy::complement(universe, enabled) != banned) {
        return false;
    }

    const QStringList localNames = {
        QStringLiteral("OLStYJ2011"), QStringLiteral("MobileStLei"),
        QStringLiteral("SecondYJCM2012"), QStringLiteral("li"),
        QStringLiteral("jie_package"), QStringLiteral("HulaoPass")
    };
    const QStringList giteeNames = {
        QStringLiteral("ol_st_yj2011"), QStringLiteral("mobile_st_lei"),
        QStringLiteral("second_yjcm2012"), QStringLiteral("ol_li"),
        QStringLiteral("ol_jie"), QStringLiteral("hulaoguan")
    };
    return PackageSelectionPolicy::normalize(localNames, giteeNames) == localNames
        && PackageSelectionPolicy::complement(localNames, giteeNames).isEmpty();
}

}

int runPackagePolicyTests()
{
    if (!generalVersionPriorityIsStable())
        return 1;
    if (!generalVersionDedupPreservesSlots())
        return 2;
    if (!packageWhitelistPolicyIsStable())
        return 3;

    qInfo() << "package whitelist and general version policy tests passed";
    return 0;
}
