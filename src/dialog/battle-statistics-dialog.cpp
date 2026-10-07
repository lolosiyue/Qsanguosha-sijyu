#include "battle-statistics-dialog.h"
#include "battle-statistics.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString text(const QVariant &value)
{
    if (value.userType() == QMetaType::QVariantMap) {
        QStringList lines;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            lines << it.key() + QStringLiteral(": ") + text(it.value());
        return lines.join(QStringLiteral("; "));
    }
    if (value.userType() == QMetaType::QStringList)
        return value.toStringList().join(QStringLiteral(", "));
    if (value.userType() == QMetaType::QVariantList) {
        QStringList lines;
        for (const QVariant &item : value.toList()) lines << text(item);
        return lines.join(QStringLiteral(", "));
    }
    return value.toString();
}
QString statusText(const QString &status)
{
    if (status == QStringLiteral("provisional")) return BattleStatisticsDialog::tr("Provisional (too few samples)");
    if (status == QStringLiteral("unknown")) return BattleStatisticsDialog::tr("Unknown (insufficient data)");
    if (status == QStringLiteral("isolated")) return BattleStatisticsDialog::tr("Isolated (not classified)");
    if (status == QStringLiteral("observed")) return BattleStatisticsDialog::tr("Observed in matches");
    return status;
}
QString measured(const QVariant &value)
{
    return value.isNull() ? BattleStatisticsDialog::tr("Unknown") : QString::number(value.toDouble(), 'g', 5);
}
QString datasetText(const QString &key)
{
    if (key == QStringLiteral("human")) return BattleStatisticsDialog::tr("Human controlled");
    if (key == QStringLiteral("ai_self_play")) return BattleStatisticsDialog::tr("AI self-play");
    if (key == QStringLiteral("human_ai_opponent")) return BattleStatisticsDialog::tr("AI opponents in human games");
    if (key == QStringLiteral("mixed")) return BattleStatisticsDialog::tr("Mixed control / changed generals");
    if (key == QStringLiteral("excluded")) return BattleStatisticsDialog::tr("Excluded games");
    return key;
}
QString labelText(const QVariant &value)
{
    // Stored labels are stable analysis values; translate only their UI display.
    const QMap<QString, QString> names{
        {QStringLiteral("控制"), BattleStatisticsDialog::tr("Control")},
        {QStringLiteral("菜刀"), BattleStatisticsDialog::tr("Slash offense")},
        {QStringLiteral("坦克"), BattleStatisticsDialog::tr("Tank")},
        {QStringLiteral("卖血"), BattleStatisticsDialog::tr("Damage-triggered benefits")},
        {QStringLiteral("辅助"), BattleStatisticsDialog::tr("Support")},
        {QStringLiteral("爆发"), BattleStatisticsDialog::tr("Burst")},
        {QStringLiteral("过牌"), BattleStatisticsDialog::tr("Card cycling")},
        {QStringLiteral("AOE"), BattleStatisticsDialog::tr("Area damage")},
        {QStringLiteral("速战速决"), BattleStatisticsDialog::tr("Quick finish")},
        {QStringLiteral("拉长战线"), BattleStatisticsDialog::tr("Prolonged game")},
        {QStringLiteral("发育"), BattleStatisticsDialog::tr("Development")}};
    QStringList translated;
    for (const QString &label : value.toStringList())
        translated << names.value(label, label);
    return translated.join(QStringLiteral(", "));
}
QString metricText(const QString &key)
{
    const QMap<QString, QString> names{
        {"damage", BattleStatisticsDialog::tr("Damage dealt (HP + armor)")}, {"hp_damage", BattleStatisticsDialog::tr("HP damage dealt")},
        {"armor_damage", BattleStatisticsDialog::tr("Armor damage dealt")}, {"received_damage", BattleStatisticsDialog::tr("Damage received")},
        {"recovery", BattleStatisticsDialog::tr("Actual recovery caused")}, {"support_recovery", BattleStatisticsDialog::tr("Recovery for others")},
        {"card_uses", BattleStatisticsDialog::tr("Cards used (excluding skill cards)")}, {"slash_uses", BattleStatisticsDialog::tr("Slash uses")},
        {"slash_damage", BattleStatisticsDialog::tr("Slash damage")}, {"aoe_damage", BattleStatisticsDialog::tr("AOE damage")},
        {"aoe_uses", BattleStatisticsDialog::tr("AOE uses")}, {"completed_turns", BattleStatisticsDialog::tr("Completed normal own turns")},
        {"extra_turns", BattleStatisticsDialog::tr("Extra own turns")}, {"truncated_turns", BattleStatisticsDialog::tr("Truncated normal own turns")}};
    return names.value(key, key);
}
QString environmentText(const QVariantMap &row)
{
    const auto environment = row.value(QStringLiteral("environment_details")).toMap();
    const auto identity = QCryptographicHash::hash(row.value(QStringLiteral("environment")).toString().toUtf8(),
        QCryptographicHash::Sha256).toHex().left(10);
    return BattleStatisticsDialog::tr("%1 · %2 players · %3 · %4 · %5")
        .arg(environment.value(QStringLiteral("mode")).toString())
        .arg(environment.value(QStringLiteral("player_count")).toInt())
        .arg(environment.value(QStringLiteral("role")).toString())
        .arg(environment.value(QStringLiteral("extra_turns_present")).toBool()
            ? BattleStatisticsDialog::tr("With extra turns") : BattleStatisticsDialog::tr("Without extra turns"))
        .arg(QString::fromLatin1(identity));
}
}

// One bounded reader per open dialog; SQLite connections are created and removed
// inside run(), and widget mutations stay on the GUI thread.
class BattleStatisticsReader final : public QThread
{
    Q_OBJECT
public:
    explicit BattleStatisticsReader(QObject *parent) : QThread(parent) {}
    ~BattleStatisticsReader() override { wait(); }
signals:
    void ready(const QVariantList &rows, const QString &error, quint64 revision);
protected:
    void run() override {
        QString error;
        const quint64 revision = BattleStatistics::projectionRevision();
        const auto rows = BattleStatistics::readSummaries(QString(), QString(), &error);
        emit ready(rows, error, revision);
    }
};

BattleStatisticsDialog::BattleStatisticsDialog(const QString &general, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(BattleStatisticsDialog::tr("Local match statistics and general classification"));
    resize(1080, 700);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(BattleStatisticsDialog::tr("Classification uses completed matches, grouped separately by mode, player count, role, version and general pair.\nHuman play and AI self-play use separate datasets; mixed control, takeovers and other excluded games do not enter formal classification."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *filters = new QHBoxLayout;
    auto *dataset = new QComboBox(this);
    dataset->setAccessibleName(BattleStatisticsDialog::tr("Dataset"));
    dataset->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *search = new QLineEdit(general, this);
    search->setPlaceholderText(BattleStatisticsDialog::tr("General internal name (leave empty to show all)"));
    search->setAccessibleName(BattleStatisticsDialog::tr("General filter"));
    auto *refresh = new QPushButton(BattleStatisticsDialog::tr("Refresh"), this);
    filters->addWidget(dataset);
    filters->addWidget(search, 1);
    filters->addWidget(refresh);
    layout->addLayout(filters);
    auto *state = new QLabel(this);
    state->setWordWrap(true);
    layout->addWidget(state);
    auto *table = new QTableWidget(0, 8, this);
    table->setHorizontalHeaderLabels({BattleStatisticsDialog::tr("General / second general"), BattleStatisticsDialog::tr("Dataset"),
        BattleStatisticsDialog::tr("Games in the same environment"), BattleStatisticsDialog::tr("Status"), BattleStatisticsDialog::tr("Observed labels"),
        BattleStatisticsDialog::tr("Damage variance"), BattleStatisticsDialog::tr("Development data"), BattleStatisticsDialog::tr("Environment")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 3);
    auto *details = new QTextBrowser(this);
    details->setAccessibleName(BattleStatisticsDialog::tr("Sample metrics and data coverage"));
    layout->addWidget(details, 1);
    auto *path = new QLabel(BattleStatisticsDialog::tr("Local data: ") + BattleStatistics::defaultDatabasePath(), this);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const auto filter = [table, dataset, search]() {
        for (int i = 0; i < table->rowCount(); ++i) {
            const QVariantMap row = table->item(i, 0)->data(Qt::UserRole).toMap();
            const bool matchesDataset = dataset->currentData().toString().isEmpty()
                || row.value(QStringLiteral("dataset")).toString() == dataset->currentData().toString();
            const bool matchesGeneral = row.value(QStringLiteral("general")).toString().contains(search->text(), Qt::CaseInsensitive)
                || row.value(QStringLiteral("general2")).toString().contains(search->text(), Qt::CaseInsensitive);
            table->setRowHidden(i, !matchesDataset || !matchesGeneral);
        }
    };
    connect(dataset, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [filter](int) { filter(); });
    connect(search, &QLineEdit::textChanged, this, [filter](const QString &) { filter(); });
    connect(table, &QTableWidget::itemSelectionChanged, this, [table, details]() {
        if (table->currentRow() < 0) { details->clear(); return; }
        const auto row = table->item(table->currentRow(), 0)->data(Qt::UserRole).toMap();
        const auto development = row.value(QStringLiteral("development")).toMap();
        QStringList lines;
        lines << BattleStatisticsDialog::tr("Required event coverage: ") + (row.value(QStringLiteral("coverage")).toBool()
            ? BattleStatisticsDialog::tr("Complete") : BattleStatisticsDialog::tr("Insufficient; classification unknown"));
        lines << BattleStatisticsDialog::tr("Mean damage across games in the same environment: ") + measured(row.value(QStringLiteral("damage_mean")));
        lines << BattleStatisticsDialog::tr("Damage variance across games in the same environment: ") + (row.value(QStringLiteral("damage_variance")).isNull()
            ? BattleStatisticsDialog::tr("Unknown (insufficient samples or data)") : text(row.value(QStringLiteral("damage_variance"))));
        lines << BattleStatisticsDialog::tr("Labels without sufficient indicators: ") + labelText(row.value(QStringLiteral("unsupported_labels")));
        lines << BattleStatisticsDialog::tr("Development: %1; %2 / %3 observable games, %4 completed normal own turns")
            .arg(statusText(development.value(QStringLiteral("status")).toString()))
            .arg(development.value(QStringLiteral("games")).toInt()).arg(row.value(QStringLiteral("games")).toInt())
            .arg(development.value(QStringLiteral("cycles")).toInt());
        lines << BattleStatisticsDialog::tr("Trend slopes per own turn: hand cards %1; maximum HP %2; own-turn damage %3")
            .arg(measured(development.value(QStringLiteral("hand_slope"))))
            .arg(measured(development.value(QStringLiteral("maxhp_slope"))))
            .arg(measured(development.value(QStringLiteral("damage_slope"))));
        lines << BattleStatisticsDialog::tr("Development trends only cover turns observed while alive with complete data, so they have survival bias; they do not imply improved survival. Unobserved resources are not filled with zeros.");
        lines << BattleStatisticsDialog::tr("Analysis version: ") + row.value(QStringLiteral("analysis_version")).toString();
        lines << BattleStatisticsDialog::tr("Fraction of completed normal own turns with at least 3 damage: ") + measured(row.value(QStringLiteral("burst_frequency")));
        lines << BattleStatisticsDialog::tr("75th percentile damage in completed normal own turns: ") + measured(row.value(QStringLiteral("turn_damage_p75")));
        lines << BattleStatisticsDialog::tr("Variance uses per-game damage in the same environment; duplicate seats with the same general in one game are averaged first. Classification thresholds are first-version observation rules.");
        if (!row.value(QStringLiteral("coverage")).toBool()) {
            lines << BattleStatisticsDialog::tr("Event coverage is insufficient; complete-game metrics are withheld and missing data is not treated as zero.");
            details->setPlainText(lines.join(QLatin1Char('\n')));
            return;
        }
        lines << BattleStatisticsDialog::tr("Cumulative values for this sample group:");
        const auto metrics = row.value(QStringLiteral("metrics")).toMap();
        for (auto it = metrics.cbegin(); it != metrics.cend(); ++it)
            lines << metricText(it.key()) + QStringLiteral(": ") + text(it.value());
        details->setPlainText(lines.join(QLatin1Char('\n')));
    });
    auto *reader = new BattleStatisticsReader(this);
    connect(reader, &BattleStatisticsReader::ready, this,
        [table, dataset, state, details, filter](const QVariantList &rows, const QString &error, quint64 revision) {
        details->clear();
        table->setRowCount(0);
        const QString previous = dataset->currentData().toString();
        dataset->clear();
        dataset->addItem(BattleStatisticsDialog::tr("All datasets (calculated separately)"), QString());
        const int pending = BattleStatistics::pendingTimelineInvalidations();
        if (revision != BattleStatistics::projectionRevision()) {
            state->setText(BattleStatisticsDialog::tr("Statistics changed; refreshing…"));
            return;
        }
        // The shared reader returns only verified rows, even when other roots
        // are quarantined. Keep those usable and clearly report withheld data.
        for (const QVariant &value : rows) {
            const auto row = value.toMap();
            const auto key = row.value(QStringLiteral("dataset")).toString();
            if (dataset->findData(key) < 0) dataset->addItem(datasetText(key), key);
            const int index = table->rowCount();
            table->insertRow(index);
            QString name = row.value(QStringLiteral("general")).toString();
            if (!row.value(QStringLiteral("general2")).toString().isEmpty())
                name += QStringLiteral(" / ") + row.value(QStringLiteral("general2")).toString();
            const QStringList cells = {name, datasetText(key), row.value(QStringLiteral("games")).toString(),
                statusText(row.value(QStringLiteral("status")).toString()), labelText(row.value(QStringLiteral("labels"))),
                measured(row.value(QStringLiteral("damage_variance"))),
                statusText(row.value(QStringLiteral("development")).toMap().value(QStringLiteral("status")).toString()),
                environmentText(row)};
            for (int column = 0; column < cells.size(); ++column)
                table->setItem(index, column, new QTableWidgetItem(cells.at(column)));
            table->item(index, 0)->setData(Qt::UserRole, row);
        }
        const int previousIndex = dataset->findData(previous);
        dataset->setCurrentIndex(previousIndex < 0 ? 0 : previousIndex);
        QString notice = rows.isEmpty()
            ? BattleStatisticsDialog::tr("No local match data to display yet. Complete a game, wait for background saving, then refresh.")
            : BattleStatisticsDialog::tr("%1 sample groups. Classification requires at least %2 games; no label does not mean zero or balanced ability.")
                .arg(rows.size()).arg(BattleStatistics::MinimumGames);
        if (pending > 0)
            notice = BattleStatisticsDialog::tr("Statistics updating / awaiting verification: %1 games are isolated; only verified samples are shown. Restore saving and recompute from valid match data.\n").arg(pending) + notice;
        if (!error.isEmpty())
            notice = BattleStatisticsDialog::tr("Some data is uncertain / could not be read: ") + error + QLatin1Char('\n') + notice;
        state->setText(notice);
        filter();
    });
    connect(reader, &QThread::finished, this, [refresh]() { refresh->setEnabled(true); });
    const auto reload = [reader, refresh, state]() {
        if (reader->isRunning()) return;
        refresh->setEnabled(false);
        state->setText(BattleStatisticsDialog::tr("Reading local data…"));
        reader->start();
    };
    connect(refresh, &QPushButton::clicked, this, reload);
    // An already open viewer must not continue presenting a previously loaded
    // contribution after a restore's durable fence fails.
    auto *pendingWatch = new QTimer(this);
    connect(pendingWatch, &QTimer::timeout, this,
        [table, details, state, reader, reload,
         lastRevision = BattleStatistics::projectionRevision()]() mutable {
        if (lastRevision != BattleStatistics::projectionRevision()) {
            table->setRowCount(0);
            details->clear();
            state->setText(BattleStatisticsDialog::tr("Statistics updating; verifying the timeline…"));
            if (reader->isRunning()) return;
            lastRevision = BattleStatistics::projectionRevision();
            reload();
        }
    });
    pendingWatch->start(500);
    reload();
}

#include "battle-statistics-dialog.moc"
