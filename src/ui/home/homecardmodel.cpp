#include "homecardmodel.h"

#include "card-overview-data.h"
#include "card.h"
#include "engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>

#include <algorithm>

namespace {

const char *const cardSceneTranslations[] = {
    QT_TRANSLATE_NOOP("CardScene", "ID %1"),
    QT_TRANSLATE_NOOP("CardScene", "IDs %1"),
    QT_TRANSLATE_NOOP("CardScene", "Damage"),
    QT_TRANSLATE_NOOP("CardScene", "Single target"),
    QT_TRANSLATE_NOOP("CardScene", "Recastable"),
    QT_TRANSLATE_NOOP("CardScene", "Select a card to view its details"),
    QT_TRANSLATE_NOOP("CardScene", "Adaptive effects: %1"),
    QT_TRANSLATE_NOOP("CardScene", "Character tags: %1"),
    QT_TRANSLATE_NOOP("CardScene", "Card effect"),
    QT_TRANSLATE_NOOP("CardScene", "No description available"),
    QT_TRANSLATE_NOOP("CardScene", "Audio preview"),
    QT_TRANSLATE_NOOP("CardScene", "Male"),
    QT_TRANSLATE_NOOP("CardScene", "Female"),
    QT_TRANSLATE_NOOP("CardScene", "Effect"),
    QT_TRANSLATE_NOOP("CardScene", "Filter cards"),
    QT_TRANSLATE_NOOP("CardScene", "Name, effect, or package"),
    QT_TRANSLATE_NOOP("CardScene", "Search cards"),
    QT_TRANSLATE_NOOP("CardScene", "Type"),
    QT_TRANSLATE_NOOP("CardScene", "Kind"),
    QT_TRANSLATE_NOOP("CardScene", "Suit"),
    QT_TRANSLATE_NOOP("CardScene", "Package"),
    QT_TRANSLATE_NOOP("CardScene", "Tags"),
    QT_TRANSLATE_NOOP("CardScene", "%1 card types found"),
    QT_TRANSLATE_NOOP("CardScene", "Reset filters"),
    QT_TRANSLATE_NOOP("CardScene", "Previous page"),
    QT_TRANSLATE_NOOP("CardScene", "Page %1 of %2"),
    QT_TRANSLATE_NOOP("CardScene", "Next page"),
    QT_TRANSLATE_NOOP("CardScene", "Back"),
    QT_TRANSLATE_NOOP("CardScene", "Card Overview"),
    QT_TRANSLATE_NOOP("CardScene", "Browse card types and their physical variants"),
    QT_TRANSLATE_NOOP("CardScene", "%1 card types · %2 physical cards"),
    QT_TRANSLATE_NOOP("CardScene", "%1 physical cards · %2 variants"),
    QT_TRANSLATE_NOOP("CardScene", "%1 cards · %2 variants"),
    QT_TRANSLATE_NOOP("CardScene", "Possible cards"),
    QT_TRANSLATE_NOOP("CardScene", "Reload"),
    QT_TRANSLATE_NOOP("CardScene", "Card grid"),
    QT_TRANSLATE_NOOP("CardScene", "No cards match the current filters"),
    QT_TRANSLATE_NOOP("CardScene", "Basic cards"),
    QT_TRANSLATE_NOOP("CardScene", "Trick cards"),
    QT_TRANSLATE_NOOP("CardScene", "Equipment cards"),
    QT_TRANSLATE_NOOP("CardScene", "Skill cards"),
    QT_TRANSLATE_NOOP("CardScene", "Slash"),
    QT_TRANSLATE_NOOP("CardScene", "Jink"),
    QT_TRANSLATE_NOOP("CardScene", "Peach"),
    QT_TRANSLATE_NOOP("CardScene", "Analeptic"),
    QT_TRANSLATE_NOOP("CardScene", "Delayed trick"),
    QT_TRANSLATE_NOOP("CardScene", "Area effect"),
    QT_TRANSLATE_NOOP("CardScene", "Global effect"),
    QT_TRANSLATE_NOOP("CardScene", "Single-target trick"),
    QT_TRANSLATE_NOOP("CardScene", "Weapon"),
    QT_TRANSLATE_NOOP("CardScene", "Armor"),
    QT_TRANSLATE_NOOP("CardScene", "Offensive horse"),
    QT_TRANSLATE_NOOP("CardScene", "Defensive horse"),
    QT_TRANSLATE_NOOP("CardScene", "Treasure"),
    QT_TRANSLATE_NOOP("CardScene", "Horse"),
    QT_TRANSLATE_NOOP("CardScene", "All"),
    QT_TRANSLATE_NOOP("CardScene", "Default order"),
    QT_TRANSLATE_NOOP("CardScene", "Name order"),
    QT_TRANSLATE_NOOP("CardScene", "Number order"),
    QT_TRANSLATE_NOOP("CardScene", "Sort cards")
};

QString cardSceneTr(const char *source)
{
    return QCoreApplication::translate("CardScene", source);
}

QString typeLabel(const QString &key)
{
    if (key == QLatin1String("BasicCard"))
        return cardSceneTr("Basic cards");
    if (key == QLatin1String("TrickCard"))
        return cardSceneTr("Trick cards");
    if (key == QLatin1String("EquipCard"))
        return cardSceneTr("Equipment cards");
    if (key == QLatin1String("SkillCard"))
        return cardSceneTr("Skill cards");
    return Sanguosha ? Sanguosha->translate(key) : key;
}

QString kindLabel(const QString &key)
{
    static const QHash<QString, const char *> labels = {
        {QStringLiteral("Slash"), "Slash"},
        {QStringLiteral("Jink"), "Jink"},
        {QStringLiteral("Peach"), "Peach"},
        {QStringLiteral("Analeptic"), "Analeptic"},
        {QStringLiteral("DelayedTrick"), "Delayed trick"},
        {QStringLiteral("AOE"), "Area effect"},
        {QStringLiteral("GlobalEffect"), "Global effect"},
        {QStringLiteral("SingleTargetTrick"), "Single-target trick"},
        {QStringLiteral("Weapon"), "Weapon"},
        {QStringLiteral("Armor"), "Armor"},
        {QStringLiteral("OffensiveHorse"), "Offensive horse"},
        {QStringLiteral("DefensiveHorse"), "Defensive horse"},
        {QStringLiteral("Treasure"), "Treasure"},
        {QStringLiteral("Horse"), "Horse"}
    };
    const auto it = labels.constFind(key);
    if (it != labels.cend())
        return cardSceneTr(it.value());
    return Sanguosha ? Sanguosha->translate(key) : key;
}

QUrl existingImage(const QString &relativePath, const QString &fallback)
{
    QString path = QDir::current().absoluteFilePath(relativePath);
    if (!QFile::exists(path))
        path = QDir::current().absoluteFilePath(fallback);
    return QUrl::fromLocalFile(path);
}

QVariantMap option(const QString &key, const QString &label, int count,
    const QString &parentTypeKey = QString())
{
    return QVariantMap{
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), label},
        {QStringLiteral("count"), count},
        {QStringLiteral("parentTypeKey"), parentTypeKey}
    };
}

QStringList propertyKeys(const Card *card, const char *propertyName)
{
    if (!card)
        return {};
    const QVariant value = card->property(propertyName);
    if (value.metaType().id() == QMetaType::QStringList)
        return value.toStringList();
    const QString key = value.toString();
    return key.isEmpty() ? QStringList() : QStringList{key};
}

QStringList filterStringList(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QStringList)
        return value.toStringList();
    QStringList result;
    const QVariantList values = value.toList();
    result.reserve(values.size());
    for (const QVariant &item : values)
        result.append(item.toString());
    return result;
}

void appendUnique(QStringList &values, const QString &value)
{
    if (!value.isEmpty() && !values.contains(value))
        values.append(value);
}

QVariantList idList(const QList<int> &ids)
{
    QVariantList values;
    values.reserve(ids.size());
    for (int id : ids)
        values.append(id);
    return values;
}

}

HomeCardModel::HomeCardModel(QObject *parent)
    : QAbstractListModel(parent)
{
    Q_UNUSED(cardSceneTranslations);
}

int HomeCardModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    const int first = m_pageIndex * pageSize();
    return qMax(0, qMin(pageSize(), m_filtered.size() - first));
}

QVariant HomeCardModel::data(const QModelIndex &index, int role) const
{
    const Row *row = pageRow(index.row());
    if (!row)
        return {};
    switch (role) {
    case CardIdRole: return row->cardId;
    case ObjectNameRole: return row->objectName;
    case BaseNameRole: return row->baseName;
    case OverviewNameRole: return row->overviewName;
    case SuitKeyRole: return row->suitKey;
    case SuitDisplayRole: return row->suitDisplay;
    case SuitIconRole: return row->suitIcon;
    case NumberRole: return row->number;
    case NumberDisplayRole: return row->numberDisplay;
    case TypeIdRole: return row->typeId;
    case TypeKeyRole: return row->typeKey;
    case TypeDisplayRole: return row->typeDisplay;
    case SubtypeKeyRole: return row->subtypeKey;
    case SubtypeDisplayRole: return row->subtypeDisplay;
    case KindKeyRole: return row->kindKey;
    case KindDisplayRole: return row->kindDisplay;
    case PackageKeyRole: return row->packageKey;
    case PackageDisplayRole: return row->packageDisplay;
    case ImageUrlRole: return row->imageUrl;
    case DamageRole: return row->damage;
    case SingleTargetRole: return row->singleTarget;
    case CanRecastRole: return row->canRecast;
    case YingBianLabelsRole: return row->yingBianLabels;
    case CharTagLabelsRole: return row->charTagLabels;
    case PhysicalCountRole: return row->cardIds.size();
    case VariantCountRole: return row->variants.size();
    case PackageSummaryRole: return row->packageSummary;
    case TagKeysRole: return row->tagKeys;
    case TagLabelsRole: return row->tagLabels;
    case VariantsRole: return row->variants;
    default: return {};
    }
}

QHash<int, QByteArray> HomeCardModel::roleNames() const
{
    return {
        {CardIdRole, "cardId"}, {ObjectNameRole, "objectName"},
        {BaseNameRole, "baseDisplayName"}, {OverviewNameRole, "overviewDisplayName"},
        {SuitKeyRole, "suitKey"}, {SuitDisplayRole, "suitDisplay"},
        {SuitIconRole, "suitIcon"}, {NumberRole, "number"},
        {NumberDisplayRole, "numberDisplay"}, {TypeIdRole, "typeId"},
        {TypeKeyRole, "typeKey"}, {TypeDisplayRole, "typeDisplay"},
        {SubtypeKeyRole, "subtypeKey"}, {SubtypeDisplayRole, "subtypeDisplay"},
        {KindKeyRole, "kindKey"}, {KindDisplayRole, "kindDisplay"},
        {PackageKeyRole, "packageKey"}, {PackageDisplayRole, "packageDisplay"},
        {ImageUrlRole, "imageUrl"}, {DamageRole, "damageCard"},
        {SingleTargetRole, "singleTarget"}, {CanRecastRole, "canRecast"},
        {YingBianLabelsRole, "yingBianLabels"}, {CharTagLabelsRole, "charTagLabels"},
        {PhysicalCountRole, "physicalCount"}, {VariantCountRole, "variantCount"},
        {PackageSummaryRole, "packageSummary"}, {TagKeysRole, "tagKeys"},
        {TagLabelsRole, "tagLabels"}, {VariantsRole, "variants"}
    };
}

int HomeCardModel::count() const
{
    return rowCount();
}

int HomeCardModel::filteredCount() const
{
    return m_filtered.size();
}

int HomeCardModel::pageIndex() const
{
    return m_pageIndex;
}

int HomeCardModel::pageCount() const
{
    return qMax(1, (m_filtered.size() + pageSize() - 1) / pageSize());
}

int HomeCardModel::pageSize() const
{
    return 12;
}

bool HomeCardModel::isLoaded() const
{
    return m_loaded;
}

int HomeCardModel::physicalCount() const
{
    return m_physicalCount;
}

QVariantList HomeCardModel::typeOptions() const
{
    return m_typeOptions;
}

QVariantList HomeCardModel::kindOptions() const
{
    return m_kindOptions;
}

QVariantList HomeCardModel::suitOptions() const
{
    return m_suitOptions;
}

QVariantList HomeCardModel::packageOptions() const
{
    return m_packageOptions;
}

QVariantList HomeCardModel::tagOptions() const
{
    return m_tagOptions;
}

void HomeCardModel::ensureLoaded()
{
    if (!m_loaded)
        reload();
}

void HomeCardModel::reload()
{
    beginResetModel();
    m_all.clear();
    m_filtered.clear();
    m_idToRow.clear();
    m_physicalCount = 0;
    m_pageIndex = 0;

    const QList<const Card *> cards = CardOverviewData::collectCards();
    const QList<CardOverviewData::CardGroup> groups = CardOverviewData::groupCardsByObjectName(cards);
    m_physicalCount = cards.size();
    m_all.reserve(groups.size());
    for (const CardOverviewData::CardGroup &group : groups) {
        const Card *card = group.representative;
        if (!card)
            continue;
        const CardOverviewData::Classification classification = CardOverviewData::classify(card);
        Row row;
        row.cardId = card->getId();
        row.objectName = card->objectName();
        row.baseName = Sanguosha->translate(row.objectName);
        row.overviewName = CardOverviewData::overviewName(card);
        row.suitKey = card->getSuitString();
        row.suitDisplay = Sanguosha->translate(row.suitKey);
        row.suitIcon = existingImage(QStringLiteral("image/system/cardsuit/%1.png").arg(row.suitKey),
                                     QStringLiteral("image/system/cardsuit/no_suit.png"));
        row.number = card->getNumber();
        row.numberDisplay = card->getNumberString();
        row.typeId = card->getTypeId();
        row.typeKey = classification.typeKey;
        row.typeDisplay = typeLabel(row.typeKey);
        row.subtypeKey = card->getSubtype();
        row.subtypeDisplay = Sanguosha->translate(row.subtypeKey);
        row.kindKey = classification.kindKey;
        row.kindDisplay = kindLabel(row.kindKey);
        row.packageKey = card->getPackage();
        row.packageDisplay = Sanguosha->translate(row.packageKey);
        row.imageUrl = existingImage(QStringLiteral("image/card/%1.jpg").arg(row.objectName),
                                     QStringLiteral("image/card/unknown.jpg"));
        row.sortNumber = card->getNumber();

        auto appendTag = [&row](const QString &key, const QString &label) {
            if (key.isEmpty() || row.tagKeys.contains(key))
                return;
            row.tagKeys.append(key);
            row.tagLabels.append(label);
        };
        for (const Card *member : group.cards) {
            row.cardIds.append(member->getId());
            appendUnique(row.suitKeys, member->getSuitString());
            appendUnique(row.packageKeys, member->getPackage());
            appendUnique(row.packageDisplays, Sanguosha->translate(member->getPackage()));
            row.sortNumber = qMin(row.sortNumber, member->getNumber());

            row.damage = row.damage || member->isDamageCard();
            row.singleTarget = row.singleTarget || member->isSingleTargetCard();
            row.canRecast = row.canRecast || member->canRecast();
            if (member->isDamageCard())
                appendTag(QStringLiteral("behavior:damage"), cardSceneTr("Damage"));
            if (member->isSingleTargetCard())
                appendTag(QStringLiteral("behavior:single_target"), cardSceneTr("Single target"));
            if (member->canRecast())
                appendTag(QStringLiteral("behavior:recastable"), cardSceneTr("Recastable"));

            const QStringList yingBianKeys = propertyKeys(member, "YingBianEffects");
            for (const QString &key : yingBianKeys) {
                appendUnique(row.yingBianLabels, Sanguosha->translate(key));
                appendTag(QStringLiteral("yingbian:") + key, Sanguosha->translate(key));
            }
            const QStringList charKeys = propertyKeys(member, "CharTag");
            for (const QString &key : charKeys) {
                appendUnique(row.charTagLabels, Sanguosha->translate(key));
                appendTag(QStringLiteral("char:") + key, Sanguosha->translate(key));
            }
        }
        row.packageSummary = row.packageDisplays.join(QStringLiteral(" · "));

        const QList<CardOverviewData::PhysicalVariantGroup> variants
            = CardOverviewData::groupPhysicalVariants(group.cards);
        row.variants.reserve(variants.size());
        for (const CardOverviewData::PhysicalVariantGroup &variant : variants) {
            const QString suitDisplay = Sanguosha->translate(variant.suitKey);
            QStringList idTexts;
            idTexts.reserve(variant.cardIds.size());
            for (int id : variant.cardIds)
                idTexts.append(QString::number(id));
            row.variants.append(QVariantMap{
                {QStringLiteral("suitKey"), variant.suitKey},
                {QStringLiteral("suitDisplay"), suitDisplay},
                {QStringLiteral("suitIcon"), existingImage(
                    QStringLiteral("image/system/cardsuit/%1.png").arg(variant.suitKey),
                    QStringLiteral("image/system/cardsuit/no_suit.png"))},
                {QStringLiteral("number"), variant.number},
                {QStringLiteral("numberDisplay"), variant.numberDisplay},
                {QStringLiteral("packageKey"), variant.packageKey},
                {QStringLiteral("packageDisplay"), Sanguosha->translate(variant.packageKey)},
                {QStringLiteral("cardIds"), idList(variant.cardIds)},
                {QStringLiteral("idDisplay"), idTexts.join(QStringLiteral(", "))}
            });
        }
        row.searchText = QStringList{row.objectName, row.baseName, row.overviewName,
            row.typeKey, row.typeDisplay, row.subtypeKey, row.subtypeDisplay,
            row.kindKey, row.kindDisplay, row.packageSummary,
            row.tagLabels.join(QLatin1Char(' ')), card->getDescription()}
                             .join(QLatin1Char(' ')).toCaseFolded();
        const int groupIndex = m_all.size();
        for (int id : std::as_const(row.cardIds))
            m_idToRow.insert(id, groupIndex);
        m_all.append(std::move(row));
    }
    m_loaded = true;
    resetFilter();
    endResetModel();
    rebuildOptions();
    emit catalogChanged();
    emit filterChanged();
    emit pageChanged();
}

void HomeCardModel::applyFilter(const QVariantMap &filters)
{
    ensureLoaded();
    m_filters = filters;
    beginResetModel();
    resetFilter();
    endResetModel();
    emit filterChanged();
    emit pageChanged();
}

void HomeCardModel::setPageIndex(int pageIndex)
{
    const int bounded = qBound(0, pageIndex, pageCount() - 1);
    if (m_pageIndex == bounded)
        return;
    beginResetModel();
    m_pageIndex = bounded;
    endResetModel();
    emit pageChanged();
}

bool HomeCardModel::containsCardId(int cardId) const
{
    return m_idToRow.contains(cardId);
}

int HomeCardModel::cardIdAt(int row) const
{
    const Row *item = pageRow(row);
    return item ? item->cardId : -1;
}

int HomeCardModel::indexOfCardId(int cardId) const
{
    const auto groupIt = m_idToRow.constFind(cardId);
    if (groupIt == m_idToRow.cend())
        return -1;
    for (int i = 0; i < m_filtered.size(); ++i) {
        if (m_filtered.at(i) == groupIt.value())
            return i;
    }
    return -1;
}

QVariantMap HomeCardModel::cardAt(int row) const
{
    const Row *item = pageRow(row);
    return item ? rowMap(*item) : QVariantMap();
}

QVariantMap HomeCardModel::cardDetails(int cardId) const
{
    const Row *row = rowForId(cardId);
    if (!row || !Sanguosha)
        return {};
    const Card *card = Sanguosha->getEngineCard(row->cardId);
    if (!card || card->getId() != row->cardId)
        return {};
    QVariantMap detail = rowMap(*row);
    detail.insert(QStringLiteral("description"), card->getDescription());
    detail.insert(QStringLiteral("hasMaleAudio"), card->getTypeId() != Card::TypeEquip);
    detail.insert(QStringLiteral("hasFemaleAudio"), card->getTypeId() != Card::TypeEquip);
    detail.insert(QStringLiteral("hasEffectAudio"), card->getTypeId() == Card::TypeEquip);
    return detail;
}

QUrl HomeCardModel::cardImage(int cardId) const
{
    const Row *row = rowForId(cardId);
    return row ? row->imageUrl : QUrl();
}

const HomeCardModel::Row *HomeCardModel::pageRow(int row) const
{
    const int filteredIndex = m_pageIndex * pageSize() + row;
    if (row < 0 || filteredIndex < 0 || filteredIndex >= m_filtered.size())
        return nullptr;
    return &m_all.at(m_filtered.at(filteredIndex));
}

const HomeCardModel::Row *HomeCardModel::rowForId(int cardId) const
{
    const auto it = m_idToRow.constFind(cardId);
    return it == m_idToRow.cend() ? nullptr : &m_all.at(it.value());
}

QVariantMap HomeCardModel::rowMap(const Row &row) const
{
    return QVariantMap{
        {QStringLiteral("cardId"), row.cardId}, {QStringLiteral("objectName"), row.objectName},
        {QStringLiteral("baseDisplayName"), row.baseName},
        {QStringLiteral("overviewDisplayName"), row.overviewName},
        {QStringLiteral("suitKey"), row.suitKey}, {QStringLiteral("suitDisplay"), row.suitDisplay},
        {QStringLiteral("suitIcon"), row.suitIcon}, {QStringLiteral("number"), row.number},
        {QStringLiteral("numberDisplay"), row.numberDisplay}, {QStringLiteral("typeId"), row.typeId},
        {QStringLiteral("typeKey"), row.typeKey}, {QStringLiteral("typeDisplay"), row.typeDisplay},
        {QStringLiteral("subtypeKey"), row.subtypeKey}, {QStringLiteral("subtypeDisplay"), row.subtypeDisplay},
        {QStringLiteral("kindKey"), row.kindKey}, {QStringLiteral("kindDisplay"), row.kindDisplay},
        {QStringLiteral("packageKey"), row.packageKey}, {QStringLiteral("packageDisplay"), row.packageDisplay},
        {QStringLiteral("imageUrl"), row.imageUrl}, {QStringLiteral("damageCard"), row.damage},
        {QStringLiteral("singleTarget"), row.singleTarget}, {QStringLiteral("canRecast"), row.canRecast},
        {QStringLiteral("yingBianLabels"), row.yingBianLabels},
        {QStringLiteral("charTagLabels"), row.charTagLabels},
        {QStringLiteral("physicalCount"), row.cardIds.size()},
        {QStringLiteral("variantCount"), row.variants.size()},
        {QStringLiteral("packageSummary"), row.packageSummary},
        {QStringLiteral("tagKeys"), row.tagKeys},
        {QStringLiteral("tagLabels"), row.tagLabels},
        {QStringLiteral("variants"), row.variants}
    };
}

void HomeCardModel::rebuildOptions()
{
    QHash<QString, int> typeCounts;
    QHash<QString, int> kindCounts;
    QHash<QString, int> suitCounts;
    QHash<QString, int> packageCounts;
    QHash<QString, int> tagCounts;
    QHash<QString, QString> kindParents;
    QHash<QString, QString> tagLabels;
    for (const Row &row : std::as_const(m_all)) {
        ++typeCounts[row.typeKey];
        ++kindCounts[row.kindKey];
        for (const QString &key : row.suitKeys)
            ++suitCounts[key];
        for (const QString &key : row.packageKeys)
            ++packageCounts[key];
        for (int i = 0; i < row.tagKeys.size(); ++i) {
            ++tagCounts[row.tagKeys.at(i)];
            if (!tagLabels.contains(row.tagKeys.at(i)))
                tagLabels.insert(row.tagKeys.at(i), row.tagLabels.value(i));
        }
        if (!kindParents.contains(row.kindKey))
            kindParents.insert(row.kindKey, row.typeKey);
    }

    m_typeOptions = {option(QStringLiteral("all"), cardSceneTr("All"), m_all.size())};
    m_kindOptions = {option(QStringLiteral("all"), cardSceneTr("All"), m_all.size())};
    m_suitOptions = {option(QStringLiteral("all"), cardSceneTr("All"), m_all.size())};
    m_packageOptions = {option(QStringLiteral("all"), cardSceneTr("All"), m_all.size())};
    m_tagOptions.clear();
    QSet<QString> types;
    QSet<QString> kinds;
    QSet<QString> suits;
    QSet<QString> packages;
    QSet<QString> tags;
    for (const Row &row : std::as_const(m_all)) {
        if (!types.contains(row.typeKey)) {
            types.insert(row.typeKey);
            m_typeOptions.append(option(row.typeKey, row.typeDisplay, typeCounts.value(row.typeKey)));
        }
        if (!kinds.contains(row.kindKey)) {
            kinds.insert(row.kindKey);
            m_kindOptions.append(option(row.kindKey, row.kindDisplay,
                kindCounts.value(row.kindKey), kindParents.value(row.kindKey)));
        }
        for (const QString &key : row.suitKeys) {
            if (suits.contains(key))
                continue;
            suits.insert(key);
            m_suitOptions.append(option(key, Sanguosha->translate(key), suitCounts.value(key)));
        }
        for (int i = 0; i < row.packageKeys.size(); ++i) {
            const QString &key = row.packageKeys.at(i);
            if (packages.contains(key))
                continue;
            packages.insert(key);
            m_packageOptions.append(option(key, row.packageDisplays.value(i), packageCounts.value(key)));
        }
        for (const QString &key : row.tagKeys) {
            if (tags.contains(key))
                continue;
            tags.insert(key);
            m_tagOptions.append(option(key, tagLabels.value(key), tagCounts.value(key)));
        }
    }
}

void HomeCardModel::resetFilter()
{
    m_filtered.clear();
    const QString query = m_filters.value(QStringLiteral("query")).toString().trimmed().toCaseFolded();
    const QString type = m_filters.value(QStringLiteral("type"), QStringLiteral("all")).toString();
    const QString kind = m_filters.value(QStringLiteral("kind"), QStringLiteral("all")).toString();
    const QString suit = m_filters.value(QStringLiteral("suit"), QStringLiteral("all")).toString();
    const QString package = m_filters.value(QStringLiteral("package"), QStringLiteral("all")).toString();
    const QStringList tags = filterStringList(m_filters.value(QStringLiteral("tags")));
    const QString sort = m_filters.value(QStringLiteral("sort"), QStringLiteral("engine")).toString();
    for (int i = 0; i < m_all.size(); ++i) {
        const Row &row = m_all.at(i);
        if (!query.isEmpty() && !row.searchText.contains(query))
            continue;
        if (type != QLatin1String("all") && row.typeKey != type)
            continue;
        if (kind != QLatin1String("all") && row.kindKey != kind)
            continue;
        if (suit != QLatin1String("all") && !row.suitKeys.contains(suit))
            continue;
        if (package != QLatin1String("all") && !row.packageKeys.contains(package))
            continue;
        if (!CardOverviewData::matchesAnyTag(row.tagKeys, tags))
            continue;
        m_filtered.append(i);
    }
    if (sort == QLatin1String("name")) {
        std::stable_sort(m_filtered.begin(), m_filtered.end(), [this](int left, int right) {
            return QString::localeAwareCompare(m_all.at(left).baseName,
                                               m_all.at(right).baseName) < 0;
        });
    } else if (sort == QLatin1String("number")) {
        std::stable_sort(m_filtered.begin(), m_filtered.end(), [this](int left, int right) {
            return m_all.at(left).sortNumber < m_all.at(right).sortNumber;
        });
    }
    m_pageIndex = 0;
}
