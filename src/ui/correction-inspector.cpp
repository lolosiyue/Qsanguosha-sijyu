#include "correction-inspector.h"
#include "settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Recorded category/operation values remain canonical. This table only supplies
// display labels; it never reads live gameplay objects or evaluates a skill.
QString readableKey(const QString &key)
{
    static const char *const sources[] = {
        QT_TRANSLATE_NOOP("CorrectionInspector", "Distance"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Distance correction"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Attack range"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Attack range correction"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Hand limit"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Hand limit correction"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Prohibition"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Target modifier"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Unlimited residue"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "Can Slash"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "isProhibited (boolean)"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "fixed maximum"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "add"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "add; V1 -1 maps to 1000"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "V1 -1 unlimited check"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "aggregate base"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "short circuit"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "default base"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "fixed correction"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "weapon scan disabled"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "weapon range"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "virtual weapon range"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "add correction"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "seat distance base"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "add distance_fix"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "HP base"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "odd dual-general HP compensation"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "rangefix after distance modifier"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "noEffect"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "unlimitedResidue"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "signedAmount"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "useAmount / applicable"),
        QT_TRANSLATE_NOOP("CorrectionInspector", "skipped"),
    };
    for (const char *source : sources)
        if (key == QLatin1String(source)) return CorrectionInspector::tr(source);
    return key;
}
void populate(QComboBox *combo, const QStringList &values, bool categories = false)
{
    const QString selected = combo->currentData().toString();
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(CorrectionInspector::tr("All"), QString());
    for (const QString &value : values)
        if (!value.isEmpty()) combo->addItem(categories ? readableKey(value) : value, value);
    if (!selected.isEmpty() && combo->findData(selected) < 0)
        combo->addItem(categories ? readableKey(selected) : selected, selected);
    combo->setCurrentIndex(qMax(0, combo->findData(selected)));
}
}

CorrectionInspector::CorrectionInspector(std::shared_ptr<CorrectionTraceStore> store, int roomId, QWidget *parent)
    : QDialog(parent), m_store(std::move(store))
{
    setObjectName(QStringLiteral("correctionInspector"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Skill correction inspector (read-only) — Room %1").arg(roomId));
    resize(1100, 760);
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Local host with cheat mode required. Enable capture and reproduce an action; this window only reads completed server calculations and never re-runs skills.\n"
        "Pause capture to inspect. Filters select recorded contexts, not hypothetical calculations.\n"
        "Use-count and extra-target values are modifier subtotals. Ordinary targetFilter, card limits, and reasons for unevaluated skills are outside this view."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *controls = new QHBoxLayout;
    m_capture = new QCheckBox(tr("Capture real calculations"), this);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    auto *clearButton = new QPushButton(tr("Clear traces"), this);
    controls->addWidget(m_capture); controls->addWidget(refreshButton); controls->addWidget(clearButton);
    controls->addStretch();
    layout->addLayout(controls);
    auto *filters = new QFormLayout;
    m_source = new QComboBox(this); m_target = new QComboBox(this); m_category = new QComboBox(this);
    m_context = new QLineEdit(this);
    m_context->setPlaceholderText(tr("Card, skill name or context"));
    filters->addRow(tr("Source"), m_source);
    filters->addRow(tr("Target"), m_target);
    filters->addRow(tr("Calculation"), m_category);
    filters->addRow(tr("Card / skill"), m_context);
    layout->addLayout(filters);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_records = new QTreeWidget(splitter);
    m_records->setAccessibleName(tr("Recent real calculations"));
    m_records->setHeaderLabels({tr("ID"), tr("Parent"), tr("Revision"),
        tr("Calculation"), tr("Source"), tr("Target"), tr("Card"), tr("Result")});
    m_records->setRootIsDecorated(false);
    m_records->setSortingEnabled(false);
    m_records->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rows = new QTableWidget(splitter);
    m_rows->setAccessibleName(tr("Observed skill returns and calculation steps"));
    m_rows->setColumnCount(7);
    m_rows->setHorizontalHeaderLabels({tr("Skill"), tr("Owner"), tr("API version"),
        tr("Instance"), tr("Operation / return"), tr("Value"), tr("Observation")});
    m_rows->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rows->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rows->setWordWrap(false);
    m_rows->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(splitter, 1);
    m_result = new QLabel(this); m_result->setWordWrap(true);
    m_result->setTextFormat(Qt::PlainText); m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status = new QLabel(this); m_status->setWordWrap(true);
    layout->addWidget(m_result); layout->addWidget(m_status);
    connect(m_capture, &QCheckBox::toggled, this, [this](bool on) {
        m_store->setEnabled(on && Config.EnableCheat);
        refresh();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { m_store->clear(); refresh(); });
    for (auto *combo : {m_source, m_target, m_category})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(m_context, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_records, &QTreeWidget::itemSelectionChanged, this, [this] { showRecord(); });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(1000);
    refresh();
}
CorrectionInspector::~CorrectionInspector() { m_store->setEnabled(false); }

void CorrectionInspector::refresh()
{
    if (!Config.EnableCheat) { m_store->setEnabled(false); m_store->clear(); }
    const bool available = Config.EnableCheat && !m_store->closed();
    m_capture->setEnabled(available);
    { QSignalBlocker block(m_capture); m_capture->setChecked(m_store->enabled()); }
    const quint64 selected = m_records->currentItem() ? m_records->currentItem()->data(0, Qt::UserRole).toULongLong() : 0;
    m_snapshot = m_store->snapshot();
    std::sort(m_snapshot.begin(), m_snapshot.end(), [](const CorrectionTraceRecord &a, const CorrectionTraceRecord &b) { return a.id > b.id; });
    QStringList sources, targets, categories;
    for (const auto &record : m_snapshot) { sources << record.source; targets << record.target; categories << record.category; }
    for (auto *list : {&sources, &targets, &categories}) { list->removeDuplicates(); list->sort(); }
    populate(m_source, sources); populate(m_target, targets); populate(m_category, categories, true);
    const QString source = m_source->currentData().toString(), target = m_target->currentData().toString();
    const QString category = m_category->currentData().toString(), query = m_context->text().trimmed();
    QSignalBlocker block(m_records);
    m_records->clear();
    for (const auto &r : m_snapshot) {
        if ((!source.isEmpty() && r.source != source) || (!target.isEmpty() && r.target != target)
            || (!category.isEmpty() && r.category != category)) continue;
        bool matches = query.isEmpty() || r.card.contains(query, Qt::CaseInsensitive) || r.context.contains(query, Qt::CaseInsensitive);
        if (!matches) for (const auto &row : r.rows) if (row.skill.contains(query, Qt::CaseInsensitive)) { matches = true; break; }
        if (!matches) continue;
        auto *item = new QTreeWidgetItem(m_records, {QString::number(r.id), r.parent ? QString::number(r.parent) : QStringLiteral("—"),
            QString::number(r.revision), readableKey(r.category), r.source, r.target, r.card, r.result});
        item->setData(0, Qt::UserRole, QVariant::fromValue(r.id));
        item->setToolTip(3, r.context);
        if (r.id == selected) m_records->setCurrentItem(item);
    }
    if (!m_records->currentItem() && m_records->topLevelItemCount()) m_records->setCurrentItem(m_records->topLevelItem(0));
    for (int i = 0; i < 7; ++i) m_records->resizeColumnToContents(i);
    m_status->setText(tr("%1 / %2 recent records; max %3 rows each. Parent IDs may expire; omitted rows are counted. %4")
        .arg(m_records->topLevelItemCount()).arg(m_snapshot.size()).arg(CorrectionTraceStore::MaxRows)
        .arg(!available ? tr("Capture unavailable (room ended or cheat disabled).") : m_store->enabled() ? tr("Capturing — pause before reading.") : tr("Paused. Enable capture and reproduce an action for new evidence.")));
    showRecord();
}
void CorrectionInspector::showRecord()
{
    m_rows->setRowCount(0); m_result->clear();
    if (!m_records->currentItem()) return;
    const quint64 id = m_records->currentItem()->data(0, Qt::UserRole).toULongLong();
    for (const auto &record : m_snapshot) if (record.id == id) {
        m_result->setText(tr("Context: %1 | Result: %2 | Omitted rows: %3\n"
            "V1 owner is unknown; zero/noEffect is an observed return, not an inferred reason. Memo hits do not re-run callbacks.")
            .arg(record.context, record.result).arg(record.omitted));
        QSet<quint64> included{id};
        // IDs are assigned on entry, so parents precede children in this traversal.
        // This joins only already-captured values; no live gameplay objects are read.
        for (auto it = m_snapshot.crbegin(); it != m_snapshot.crend(); ++it) {
            const auto &part = *it;
            if (part.id != id && !included.contains(part.parent)) continue;
            included.insert(part.id);
            int index = m_rows->rowCount(); m_rows->insertRow(index);
            m_rows->setItem(index, 0, new QTableWidgetItem(QStringLiteral("#%1 %2").arg(part.id).arg(readableKey(part.category))));
            m_rows->setItem(index, 4, new QTableWidgetItem(part.context));
            m_rows->setItem(index, 6, new QTableWidgetItem(tr("Result: %1; omitted: %2").arg(part.result).arg(part.omitted)));
            for (const auto &row : part.rows) {
                index = m_rows->rowCount(); m_rows->insertRow(index);
                const QStringList cells{row.skill, row.owner, row.version, row.instance < 0 ? QString() : QString::number(row.instance),
                    readableKey(row.operation), row.operation == QLatin1String("skipped") ? QStringLiteral("—") : QString::number(row.value), row.detail};
                for (int i = 0; i < cells.size(); ++i) {
                    auto *item = new QTableWidgetItem(cells.at(i)); item->setToolTip(cells.at(i)); m_rows->setItem(index, i, item);
                }
            }
        }
        m_rows->resizeColumnsToContents();
        for (int column = 0; column < m_rows->columnCount(); ++column)
            m_rows->setColumnWidth(column, qMin(m_rows->columnWidth(column), 360));
        return;
    }
}
