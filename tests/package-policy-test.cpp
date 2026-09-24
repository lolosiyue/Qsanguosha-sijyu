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

bool generalModePreferencePreservesUnpairedVersions()
{
    struct Case {
        QStringList input;
        QStringList identity;
        QStringList hegemony;
    };
    const QList<Case> cases = {
        {{}, {}, {}},
        {{"caocao", "heg_caocao"}, {"caocao"}, {"heg_caocao"}},
        {{"heg_caocao", "caocao"}, {"caocao"}, {"heg_caocao"}},
        // A disabled/banned counterpart is absent from the admitted input.
        {{"heg_caocao", "sunquan"}, {"heg_caocao", "sunquan"}, {"heg_caocao", "sunquan"}},
        {{"nos_caocao", "heg_caocao", "ol_caocao"},
         {"nos_caocao", "ol_caocao"}, {"heg_caocao"}},
        // Mode filtering must not perform the optional identity-version dedup.
        {{"caocao", "third_caocao"}, {"caocao", "third_caocao"}, {"caocao", "third_caocao"}},
        {{"heg_liubei", "liushanliubei"}, {"heg_liubei", "liushanliubei"}, {"heg_liubei", "liushanliubei"}},
        {{"heg_yanliangwenchou", "yanliang"}, {"heg_yanliangwenchou", "yanliang"}, {"heg_yanliangwenchou", "yanliang"}},
        {{"sunquan", "heg_caocao", "caocao", "heg_zhangfei"},
         {"sunquan", "caocao", "heg_zhangfei"}, {"sunquan", "heg_caocao", "heg_zhangfei"}}
    };
    for (const Case &test : cases) {
        if (filterGeneralVersionsForMode(test.input, false) != test.identity
            || filterGeneralVersionsForMode(test.input, true) != test.hegemony) {
            qCritical() << "general mode preference changed counterparts or order" << test.input;
            return false;
        }
    }
    return true;
}

bool packageWhitelistPolicyIsStable()
{
    const QStringList expectedDefaults = {
        QStringLiteral("standard"), QStringLiteral("wind"),
        QStringLiteral("fire"), QStringLiteral("thicket"),
        QStringLiteral("mountain"), QStringLiteral("YJCM"),
        QStringLiteral("YJCM2012"), QStringLiteral("standard_cards"),
        QStringLiteral("standard_ex_cards"), QStringLiteral("maneuvering"),
        QStringLiteral("heg_standard"), QStringLiteral("heg_standard_cards"),
        QStringLiteral("heg_formation"), QStringLiteral("heg_formation_equip"),
        QStringLiteral("heg_momentum"), QStringLiteral("heg_momentum_equip"),
        QStringLiteral("heg_strategic_advantage"),
        QStringLiteral("heg_transformation"), QStringLiteral("heg_transformation_equip"),
        QStringLiteral("heg_power"), QStringLiteral("heg_power_equip"),
        QStringLiteral("heg_manoeuvre"), QStringLiteral("heg_newsgs"),
        QStringLiteral("heg_mol"), QStringLiteral("heg_overseas"),
        QStringLiteral("heg_lord_ex"), QStringLiteral("heg_lord_ex_card")
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

bool expandedHegemonySelectionPreservesUserChoices()
{
    const QStringList original = {
        "heg_standard", "heg_standard_cards", "heg_formation", "heg_formation_equip",
        "heg_momentum", "heg_momentum_equip", "heg_strategic_advantage"
    };
    const QStringList expanded = {
        "heg_transformation", "heg_transformation_equip", "heg_power", "heg_power_equip",
        "heg_manoeuvre", "heg_newsgs", "heg_mol", "heg_overseas", "heg_lord_ex", "heg_lord_ex_card"
    };
    const QStringList universe = QStringList{"standard"} + original + expanded;
    const QStringList fullOld = QStringList{"standard"} + original;
    if (PackageSelectionPolicy::migrateEnabledPackages(universe, fullOld, 4) != universe)
        return false;
    // Explicit opt-outs and partial expansion choices must survive the one-time upgrade.
    for (const QStringList &selection : QList<QStringList>{
             {}, {"standard"}, {"heg_standard", "heg_standard_cards"},
             fullOld + QStringList{"heg_power"}}) {
        if (PackageSelectionPolicy::migrateEnabledPackages(universe, selection, 4) != selection)
            return false;
    }
    return PackageSelectionPolicy::migrateEnabledPackages(universe, fullOld, 5) == fullOld
        && PackageSelectionPolicy::normalize(universe,
            {"HTransformation", "HPower", "HManoeuvre", "HNewSGS", "HMOL", "HOverseas", "HLordEX"})
            == QStringList{"heg_transformation", "heg_power", "heg_manoeuvre", "heg_newsgs", "heg_mol", "heg_overseas", "heg_lord_ex"};
}

bool originalHegemonySelectionMigratesOnce()
{
    const QStringList hegemony = {
        "heg_standard", "heg_standard_cards", "heg_formation", "heg_formation_equip",
        "heg_momentum", "heg_momentum_equip", "heg_strategic_advantage"
    };
    const QStringList universe = QStringList{"standard", "wind", "hegemony_sp"} + hegemony;
    const QStringList oldWhitelist{"wind", "hegemony_sp"};
    const QStringList expected = oldWhitelist + hegemony;
    if (PackageSelectionPolicy::migrateEnabledPackages(universe, oldWhitelist, 2) != expected)
        return false;

    // Both previously shipped spellings map to the same runtime package. Never
    // preserve duplicate old registrations or expand an explicit HEG subset.
    const QStringList oldNames{"hegemony", "h_formation", "h_momentum"};
    const QStringList renamed{"heg_standard", "heg_formation", "heg_momentum"};
    if (PackageSelectionPolicy::migrateEnabledPackages(
            universe, oldNames, 2) != hegemony
        || PackageSelectionPolicy::normalize(universe,
            {"oh_standard", "oh_formation", "oh_momentum"}) != renamed
        || PackageSelectionPolicy::normalize(universe,
            {"HStandard", "HFormation", "HMomentum"}) != renamed)
        return false;

    const QStringList oldPortNames{"oh_standard", "oh_standard_cards", "oh_formation",
        "oh_formation_equip", "oh_momentum", "oh_momentum_equip", "oh_strategic_advantage"};
    if (PackageSelectionPolicy::migrateEnabledPackages(
            universe, oldPortNames, 3) != hegemony
        || PackageSelectionPolicy::normalize(universe, oldPortNames + hegemony)
            != hegemony)
        return false;

    // Version 3 already completed content migration. Renaming must preserve a
    // deliberately empty selection and must not enable new packages on restart.
    for (int version : {3, PackageSelectionPolicy::CurrentMigrationVersion}) {
        if (!PackageSelectionPolicy::migrateEnabledPackages(universe, {}, version).isEmpty()
            || PackageSelectionPolicy::migrateEnabledPackages(universe, oldWhitelist, version) != oldWhitelist
            || PackageSelectionPolicy::migrateEnabledPackages(universe, expected, version) != expected)
            return false;
    }

    QStringList allowed = universe;
    allowed.removeAll("heg_momentum");
    allowed.removeAll("heg_formation");
    if (PackageSelectionPolicy::complement(universe, {"h_momentum", "oh_formation"}) != allowed)
        return false;
    const QStringList defaults = QStringList{"standard", "wind"} + hegemony;
    return PackageSelectionPolicy::normalize(universe,
        PackageSelectionPolicy::defaultEnabledPackages()) == defaults;
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
    if (!originalHegemonySelectionMigratesOnce())
        return 4;
    if (!generalModePreferencePreservesUnpairedVersions())
        return 5;
    if (!expandedHegemonySelectionPreservesUserChoices())
        return 6;

    qInfo() << "package whitelist and general version policy tests passed";
    return 0;
}
