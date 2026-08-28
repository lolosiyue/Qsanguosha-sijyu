#include "engine-bootstrap.h"
#include "homecardmodel.h"

#include <QCoreApplication>
#include <QSet>
#include <QTextStream>

#include <limits>

namespace {

bool expect(bool condition, const QString &message)
{
    if (condition)
        return true;
    QTextStream(stderr) << message << '\n';
    return false;
}

QVariantMap filters(const QStringList &tags = {}, const QString &type = QStringLiteral("all"),
                    const QString &query = {}, const QString &sort = QStringLiteral("engine"))
{
    return QVariantMap{
        {QStringLiteral("query"), query},
        {QStringLiteral("type"), type},
        {QStringLiteral("kind"), QStringLiteral("all")},
        {QStringLiteral("suit"), QStringLiteral("all")},
        {QStringLiteral("package"), QStringLiteral("all")},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("sort"), sort}
    };
}

QList<QVariantMap> allFilteredRows(HomeCardModel &model)
{
    QList<QVariantMap> rows;
    const int oldPage = model.pageIndex();
    for (int page = 0; page < model.pageCount(); ++page) {
        model.setPageIndex(page);
        for (int row = 0; row < model.rowCount(); ++row)
            rows.append(model.cardAt(row));
    }
    model.setPageIndex(qBound(0, oldPage, model.pageCount() - 1));
    return rows;
}

QSet<int> cardIds(const QList<QVariantMap> &rows)
{
    QSet<int> ids;
    for (const QVariantMap &row : rows)
        ids.insert(row.value(QStringLiteral("cardId")).toInt());
    return ids;
}

bool hasOption(const QVariantList &options, const QString &key)
{
    for (const QVariant &option : options) {
        if (option.toMap().value(QStringLiteral("key")).toString() == key)
            return true;
    }
    return false;
}

bool verifyCatalogAndRoles(HomeCardModel &model)
{
    if (!expect(model.isLoaded(), QStringLiteral("card catalog was not marked loaded"))
        || !expect(model.filteredCount() > model.pageSize(), QStringLiteral("catalog did not paginate"))
        || !expect(model.physicalCount() >= model.filteredCount(),
                   QStringLiteral("physical count is smaller than merged card-type count"))
        || !expect(model.count() == model.pageSize(), QStringLiteral("first page size is unexpected")))
        return false;

    const QHash<int, QByteArray> roles = model.roleNames();
    for (const QByteArray &name : {QByteArray("physicalCount"), QByteArray("variantCount"),
                                  QByteArray("tagKeys"), QByteArray("variants")}) {
        if (!expect(roles.values().contains(name), QStringLiteral("missing model role: %1").arg(name)))
            return false;
    }

    const QVariantMap first = model.cardAt(0);
    const QModelIndex index = model.index(0, 0);
    return expect(model.data(index, HomeCardModel::CardIdRole).toInt()
                      == first.value(QStringLiteral("cardId")).toInt(),
                  QStringLiteral("cardAt and CardIdRole disagree"))
        && expect(model.data(index, HomeCardModel::PhysicalCountRole).toInt()
                      == first.value(QStringLiteral("physicalCount")).toInt(),
                  QStringLiteral("cardAt and PhysicalCountRole disagree"));
}

bool verifyMergedIdMapping(HomeCardModel &model)
{
    model.applyFilter(filters());
    const QList<QVariantMap> rows = allFilteredRows(model);
    for (const QVariantMap &row : rows) {
        const int representativeId = row.value(QStringLiteral("cardId")).toInt();
        const QVariantList variants = row.value(QStringLiteral("variants")).toList();
        for (const QVariant &entry : variants) {
            const QVariantList ids = entry.toMap().value(QStringLiteral("cardIds")).toList();
            for (const QVariant &idValue : ids) {
                const int physicalId = idValue.toInt();
                if (physicalId == representativeId)
                    continue;
                const QVariantMap detail = model.cardDetails(physicalId);
                return expect(model.containsCardId(physicalId),
                              QStringLiteral("merged physical ID is not indexed"))
                    && expect(model.indexOfCardId(physicalId) == model.indexOfCardId(representativeId),
                              QStringLiteral("merged physical ID resolves to a different row"))
                    && expect(detail.value(QStringLiteral("cardId")).toInt() == representativeId,
                              QStringLiteral("detail did not use the stable representative"));
            }
        }
    }
    return expect(false, QStringLiteral("catalog had no merged physical card IDs to verify"));
}

bool verifyFacetComposition(HomeCardModel &model)
{
    const QString damage = QStringLiteral("behavior:damage");
    const QString singleTarget = QStringLiteral("behavior:single_target");
    if (!expect(hasOption(model.tagOptions(), damage), QStringLiteral("damage tag option is missing"))
        || !expect(hasOption(model.tagOptions(), singleTarget),
                   QStringLiteral("single-target tag option is missing")))
        return false;

    model.applyFilter(filters({damage}));
    const QList<QVariantMap> damageRows = allFilteredRows(model);
    model.applyFilter(filters({singleTarget}));
    const QList<QVariantMap> singleRows = allFilteredRows(model);
    model.applyFilter(filters({damage, singleTarget}));
    const QList<QVariantMap> unionRows = allFilteredRows(model);

    QSet<int> expectedUnion = cardIds(damageRows);
    expectedUnion.unite(cardIds(singleRows));
    if (!expect(cardIds(unionRows) == expectedUnion,
                QStringLiteral("multiple tags did not compose with OR semantics")))
        return false;

    const QString type = unionRows.constFirst().value(QStringLiteral("typeKey")).toString();
    QSet<int> expectedTypeIds;
    for (const QVariantMap &row : unionRows) {
        if (row.value(QStringLiteral("typeKey")).toString() == type)
            expectedTypeIds.insert(row.value(QStringLiteral("cardId")).toInt());
    }
    model.applyFilter(filters({damage, singleTarget}, type));
    return expect(cardIds(allFilteredRows(model)) == expectedTypeIds,
                  QStringLiteral("type facet did not combine with the tag union using AND"));
}

bool verifySearchSortAndPaging(HomeCardModel &model)
{
    model.applyFilter(filters());
    QVariantMap searchableDetail;
    for (const QVariantMap &row : allFilteredRows(model)) {
        const QVariantMap detail = model.cardDetails(row.value(QStringLiteral("cardId")).toInt());
        if (!detail.value(QStringLiteral("description")).toString().trimmed().isEmpty()) {
            searchableDetail = detail;
            break;
        }
    }
    if (!expect(!searchableDetail.isEmpty(), QStringLiteral("catalog had no searchable description")))
        return false;

    const int describedCardId = searchableDetail.value(QStringLiteral("cardId")).toInt();
    model.applyFilter(filters({}, QStringLiteral("all"),
                              searchableDetail.value(QStringLiteral("description")).toString().trimmed()));
    if (!expect(cardIds(allFilteredRows(model)).contains(describedCardId),
                QStringLiteral("card effect text was not searchable")))
        return false;

    model.applyFilter(filters({}, QStringLiteral("all"), {}, QStringLiteral("name")));
    const QList<QVariantMap> nameRows = allFilteredRows(model);
    for (int i = 1; i < nameRows.size(); ++i) {
        const QString previous = nameRows.at(i - 1).value(QStringLiteral("baseDisplayName")).toString();
        const QString current = nameRows.at(i).value(QStringLiteral("baseDisplayName")).toString();
        if (!expect(QString::localeAwareCompare(previous, current) <= 0,
                    QStringLiteral("name sort is not monotonic")))
            return false;
    }

    model.applyFilter(filters());
    model.setPageIndex(std::numeric_limits<int>::max());
    if (!expect(model.pageIndex() == model.pageCount() - 1,
                QStringLiteral("large page index was not bounded")))
        return false;
    model.setPageIndex(-1);
    if (!expect(model.pageIndex() == 0, QStringLiteral("negative page index was not bounded")))
        return false;
    model.setPageIndex(1);
    model.applyFilter(filters({QStringLiteral("behavior:damage")}));
    return expect(model.pageIndex() == 0, QStringLiteral("filter change did not reset pagination"));
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << "engine initialization failed: " << error << '\n';
        return 1;
    }

    bool passed = false;
    {
        HomeCardModel model;
        int catalogSignals = 0;
        int filterSignals = 0;
        int pageSignals = 0;
        QObject::connect(&model, &HomeCardModel::catalogChanged, [&catalogSignals] { ++catalogSignals; });
        QObject::connect(&model, &HomeCardModel::filterChanged, [&filterSignals] { ++filterSignals; });
        QObject::connect(&model, &HomeCardModel::pageChanged, [&pageSignals] { ++pageSignals; });
        model.ensureLoaded();

        passed = expect(catalogSignals == 1 && filterSignals >= 1 && pageSignals >= 1,
                        QStringLiteral("initial catalog signals were not emitted"))
            && verifyCatalogAndRoles(model)
            && verifyMergedIdMapping(model)
            && verifyFacetComposition(model)
            && verifySearchSortAndPaging(model);
    }
    EngineBootstrap::shutdown();
    return passed ? 0 : 1;
}
