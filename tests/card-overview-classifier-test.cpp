#include "card-overview-data.h"
#include "maneuvering.h"
#include "standard-cards.h"

#include <QCoreApplication>
#include <QSet>
#include <QTextStream>

namespace {

bool expectKind(const Card &card, const QString &typeKey, const QString &kindKey)
{
    const CardOverviewData::Classification result = CardOverviewData::classify(&card);
    if (result.typeKey == typeKey && result.kindKey == kindKey)
        return true;
    QTextStream(stderr) << card.metaObject()->className() << ": expected "
                        << typeKey << '/' << kindKey << ", got "
                        << result.typeKey << '/' << result.kindKey << '\n';
    return false;
}

bool expectGrouping()
{
    QObject standardPackage;
    standardPackage.setObjectName(QStringLiteral("standard"));
    QObject alternatePackage;
    alternatePackage.setObjectName(QStringLiteral("alternate"));

    Slash first(Card::Spade, 7);
    first.setId(11);
    first.setParent(&standardPackage);
    Slash second(Card::Spade, 7);
    second.setId(12);
    second.setParent(&standardPackage);
    Slash alternate(Card::Heart, 10);
    alternate.setId(13);
    alternate.setParent(&alternatePackage);
    FireSlash fire(Card::Heart, 4);
    fire.setId(14);
    fire.setParent(&standardPackage);

    const QList<CardOverviewData::CardGroup> groups
        = CardOverviewData::groupCardsByObjectName({&first, &second, &alternate, &fire});
    if (groups.size() != 2 || groups.at(0).representative != &first
        || groups.at(0).cards.size() != 3 || groups.at(1).representative != &fire) {
        QTextStream(stderr) << "objectName grouping did not preserve stable representatives\n";
        return false;
    }

    const QList<CardOverviewData::PhysicalVariantGroup> variants
        = CardOverviewData::groupPhysicalVariants(groups.at(0).cards);
    if (variants.size() != 2 || variants.at(0).cardIds != QList<int>({11, 12})
        || variants.at(0).packageKey != QStringLiteral("standard")
        || variants.at(1).cardIds != QList<int>({13})
        || variants.at(1).packageKey != QStringLiteral("alternate")) {
        QTextStream(stderr) << "physical variants did not merge suit, number, and package\n";
        return false;
    }

    if (!CardOverviewData::matchesAnyTag({QStringLiteral("damage"), QStringLiteral("red")},
                                         {QStringLiteral("recast"), QStringLiteral("red")})
        || CardOverviewData::matchesAnyTag({QStringLiteral("damage")},
                                           {QStringLiteral("recast"), QStringLiteral("red")})
        || !CardOverviewData::matchesAnyTag({QStringLiteral("damage")}, {})) {
        QTextStream(stderr) << "tag filter did not use OR semantics\n";
        return false;
    }
    return true;
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    Slash slash(Card::Spade, 7);
    FireSlash fireSlash(Card::Heart, 4);
    ThunderSlash thunderSlash(Card::Spade, 4);
    Jink jink(Card::Heart, 2);
    Peach peach(Card::Heart, 3);
    Analeptic analeptic(Card::Spade, 9);
    Indulgence indulgence(Card::Heart, 6);
    Lightning lightning(Card::Spade, 1);
    SavageAssault savageAssault(Card::Spade, 7);
    ArcheryAttack archeryAttack(Card::Heart, 1);
    GodSalvation godSalvation(Card::Heart, 1);
    AmazingGrace amazingGrace(Card::Heart, 1);
    Duel duel(Card::Spade, 1);
    Crossbow crossbow(Card::Club, 1);
    EightDiagram eightDiagram(Card::Spade, 2);
    OffensiveHorse offensiveHorse(Card::Spade, 5);
    DefensiveHorse defensiveHorse(Card::Heart, 5);
    WoodenOx woodenOx(Card::Diamond, 5);

    const QList<bool> results = {
        expectKind(slash, "BasicCard", "Slash"),
        expectKind(fireSlash, "BasicCard", "Slash"),
        expectKind(thunderSlash, "BasicCard", "Slash"),
        expectKind(jink, "BasicCard", "Jink"),
        expectKind(peach, "BasicCard", "Peach"),
        expectKind(analeptic, "BasicCard", "Analeptic"),
        expectKind(indulgence, "TrickCard", "DelayedTrick"),
        expectKind(lightning, "TrickCard", "DelayedTrick"),
        expectKind(savageAssault, "TrickCard", "AOE"),
        expectKind(archeryAttack, "TrickCard", "AOE"),
        expectKind(godSalvation, "TrickCard", "GlobalEffect"),
        expectKind(amazingGrace, "TrickCard", "GlobalEffect"),
        expectKind(duel, "TrickCard", "SingleTargetTrick"),
        expectKind(crossbow, "EquipCard", "Weapon"),
        expectKind(eightDiagram, "EquipCard", "Armor"),
        expectKind(offensiveHorse, "EquipCard", "OffensiveHorse"),
        expectKind(defensiveHorse, "EquipCard", "DefensiveHorse"),
        expectKind(woodenOx, "EquipCard", "Treasure"),
        expectGrouping()
    };
    return results.contains(false) ? 1 : 0;
}
