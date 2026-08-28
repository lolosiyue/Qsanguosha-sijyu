#include "card-overview-data.h"

#include "card.h"
#include "engine.h"
#include "server-info.h"

#include <QFile>
#include <QHash>
#include <QSet>

namespace CardOverviewData {

QList<const Card *> collectCards()
{
    QList<const Card *> cards;
    if (!Sanguosha)
        return cards;

    const bool cstring = !ServerInfo.DuringGame && QFile::exists(QStringLiteral("lua/ai/cstring"));
    const QList<int> availableIds = Sanguosha->getRandomCards(true);
    const QSet<int> available(availableIds.cbegin(), availableIds.cend());

    const int count = Sanguosha->getCardCount();
    cards.reserve(count);
    for (int id = 0; id < count; ++id) {
        if (ServerInfo.DuringGame && !available.contains(id))
            continue;

        const Card *card = Sanguosha->getEngineCard(id);
        if (!card)
            continue;
        if (!cstring && (card->objectName().contains(QStringLiteral("_zhizhe_"))
                        || card->objectName().startsWith(QStringLiteral("__"))))
            continue;
        cards.append(card);
    }
    return cards;
}

QList<CardGroup> groupCardsByObjectName(const QList<const Card *> &cards)
{
    QList<CardGroup> groups;
    QHash<QString, int> groupIndexes;
    for (const Card *card : cards) {
        if (!card)
            continue;
        const QString key = card->objectName();
        const auto existing = groupIndexes.constFind(key);
        if (existing != groupIndexes.cend()) {
            groups[existing.value()].cards.append(card);
            continue;
        }
        CardGroup group;
        group.representative = card;
        group.cards.append(card);
        groupIndexes.insert(key, groups.size());
        groups.append(std::move(group));
    }
    return groups;
}

QList<PhysicalVariantGroup> groupPhysicalVariants(const QList<const Card *> &cards)
{
    QList<PhysicalVariantGroup> variants;
    QHash<QString, int> variantIndexes;
    for (const Card *card : cards) {
        if (!card)
            continue;
        const QString suitKey = card->getSuitString();
        const QString packageKey = card->getPackage();
        const QString key = suitKey + QChar(0x1f)
            + QString::number(card->getNumber()) + QChar(0x1f) + packageKey;
        const auto existing = variantIndexes.constFind(key);
        if (existing != variantIndexes.cend()) {
            variants[existing.value()].cardIds.append(card->getId());
            continue;
        }
        PhysicalVariantGroup variant;
        variant.suitKey = suitKey;
        variant.number = card->getNumber();
        variant.numberDisplay = card->getNumberString();
        variant.packageKey = packageKey;
        variant.cardIds.append(card->getId());
        variantIndexes.insert(key, variants.size());
        variants.append(std::move(variant));
    }
    return variants;
}

bool matchesAnyTag(const QStringList &availableTags, const QStringList &selectedTags)
{
    if (selectedTags.isEmpty())
        return true;
    for (const QString &tag : selectedTags) {
        if (availableTags.contains(tag))
            return true;
    }
    return false;
}

QString overviewName(const Card *card)
{
    if (!card || !Sanguosha)
        return {};

    QString name = Sanguosha->translate(card->objectName());
    const QString yingbian = card->property("YingBianEffects").toString();
    if (!yingbian.isEmpty())
        name += QStringLiteral("(%1)").arg(Sanguosha->translate(yingbian));
    const QStringList tags = card->property("CharTag").toStringList();
    for (const QString &tag : tags)
        name += QStringLiteral("(%1)").arg(Sanguosha->translate(tag));
    return name;
}

Classification classify(const Card *card)
{
    Classification result;
    if (!card)
        return result;

    if (card->isKindOf("BasicCard")) {
        result.typeKey = QStringLiteral("BasicCard");
        if (card->isKindOf("Slash"))
            result.kindKey = QStringLiteral("Slash");
        else if (card->isKindOf("Jink"))
            result.kindKey = QStringLiteral("Jink");
        else if (card->isKindOf("Peach"))
            result.kindKey = QStringLiteral("Peach");
        else if (card->isKindOf("Analeptic"))
            result.kindKey = QStringLiteral("Analeptic");
    } else if (card->isKindOf("TrickCard")) {
        result.typeKey = QStringLiteral("TrickCard");
        if (card->isKindOf("DelayedTrick"))
            result.kindKey = QStringLiteral("DelayedTrick");
        else if (card->isKindOf("AOE"))
            result.kindKey = QStringLiteral("AOE");
        else if (card->isKindOf("GlobalEffect"))
            result.kindKey = QStringLiteral("GlobalEffect");
        else if (card->isKindOf("SingleTargetTrick"))
            result.kindKey = QStringLiteral("SingleTargetTrick");
    } else if (card->isKindOf("EquipCard")) {
        result.typeKey = QStringLiteral("EquipCard");
        if (card->isKindOf("Weapon"))
            result.kindKey = QStringLiteral("Weapon");
        else if (card->isKindOf("Armor"))
            result.kindKey = QStringLiteral("Armor");
        else if (card->isKindOf("OffensiveHorse"))
            result.kindKey = QStringLiteral("OffensiveHorse");
        else if (card->isKindOf("DefensiveHorse"))
            result.kindKey = QStringLiteral("DefensiveHorse");
        else if (card->isKindOf("Treasure"))
            result.kindKey = QStringLiteral("Treasure");
        else if (card->isKindOf("Horse"))
            result.kindKey = QStringLiteral("Horse");
    } else if (card->getTypeId() == Card::TypeSkill) {
        result.typeKey = QStringLiteral("SkillCard");
    }

    if (result.typeKey.isEmpty())
        result.typeKey = card->getType();
    if (result.kindKey.isEmpty()) {
        result.kindKey = card->getSubtype();
        if (result.kindKey.isEmpty())
            result.kindKey = result.typeKey;
    }
    return result;
}

}
