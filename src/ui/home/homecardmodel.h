#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class HomeCardModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY filterChanged)
    Q_PROPERTY(int filteredCount READ filteredCount NOTIFY filterChanged)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY filterChanged)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY catalogChanged)
    Q_PROPERTY(int physicalCount READ physicalCount NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList typeOptions READ typeOptions NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList kindOptions READ kindOptions NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList suitOptions READ suitOptions NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList packageOptions READ packageOptions NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList tagOptions READ tagOptions NOTIFY catalogChanged)

public:
    enum Role {
        CardIdRole = Qt::UserRole + 1,
        ObjectNameRole,
        BaseNameRole,
        OverviewNameRole,
        SuitKeyRole,
        SuitDisplayRole,
        SuitIconRole,
        NumberRole,
        NumberDisplayRole,
        TypeIdRole,
        TypeKeyRole,
        TypeDisplayRole,
        SubtypeKeyRole,
        SubtypeDisplayRole,
        KindKeyRole,
        KindDisplayRole,
        PackageKeyRole,
        PackageDisplayRole,
        ImageUrlRole,
        DamageRole,
        SingleTargetRole,
        CanRecastRole,
        YingBianLabelsRole,
        CharTagLabelsRole,
        PhysicalCountRole,
        VariantCountRole,
        PackageSummaryRole,
        TagKeysRole,
        TagLabelsRole,
        VariantsRole
    };

    explicit HomeCardModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int filteredCount() const;
    int pageIndex() const;
    int pageCount() const;
    int pageSize() const;
    bool isLoaded() const;
    int physicalCount() const;
    QVariantList typeOptions() const;
    QVariantList kindOptions() const;
    QVariantList suitOptions() const;
    QVariantList packageOptions() const;
    QVariantList tagOptions() const;

    Q_INVOKABLE void ensureLoaded();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void applyFilter(const QVariantMap &filters);
    Q_INVOKABLE void setPageIndex(int pageIndex);
    Q_INVOKABLE bool containsCardId(int cardId) const;
    Q_INVOKABLE int cardIdAt(int row) const;
    Q_INVOKABLE int indexOfCardId(int cardId) const;
    Q_INVOKABLE QVariantMap cardAt(int row) const;
    Q_INVOKABLE QVariantMap cardDetails(int cardId) const;
    Q_INVOKABLE QUrl cardImage(int cardId) const;

signals:
    void catalogChanged();
    void filterChanged();
    void pageChanged();

private:
    struct Row {
        int cardId = -1;
        QList<int> cardIds;
        QString objectName;
        QString baseName;
        QString overviewName;
        QString suitKey;
        QString suitDisplay;
        QUrl suitIcon;
        int number = 0;
        QString numberDisplay;
        int typeId = 0;
        QString typeKey;
        QString typeDisplay;
        QString subtypeKey;
        QString subtypeDisplay;
        QString kindKey;
        QString kindDisplay;
        QString packageKey;
        QString packageDisplay;
        QString packageSummary;
        QStringList suitKeys;
        QStringList packageKeys;
        QStringList packageDisplays;
        QUrl imageUrl;
        bool damage = false;
        bool singleTarget = false;
        bool canRecast = false;
        QStringList yingBianLabels;
        QStringList charTagLabels;
        QStringList tagKeys;
        QStringList tagLabels;
        QVariantList variants;
        int sortNumber = 0;
        QString searchText;
    };

    const Row *pageRow(int row) const;
    const Row *rowForId(int cardId) const;
    QVariantMap rowMap(const Row &row) const;
    void rebuildOptions();
    void resetFilter();

    QVector<Row> m_all;
    QVector<int> m_filtered;
    QHash<int, int> m_idToRow;
    QVariantMap m_filters;
    QVariantList m_typeOptions;
    QVariantList m_kindOptions;
    QVariantList m_suitOptions;
    QVariantList m_packageOptions;
    QVariantList m_tagOptions;
    int m_physicalCount = 0;
    int m_pageIndex = 0;
    bool m_loaded = false;
};
