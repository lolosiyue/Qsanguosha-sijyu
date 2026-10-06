#include "theme-pack-dialog.h"
#include "theme-pack.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
const int kIdRole = Qt::UserRole;
const QSize kIconSize(96, 54);

QPixmap previewIcon(const ThemePacks::Pack &pack, const QSize &size)
{
    QPixmap source;
    if (!pack.preview.isEmpty())
        source.load(pack.preview);
    QPixmap result(size);
    result.fill(QColor(40, 40, 40));
    QPainter painter(&result);
    if (!source.isNull()) {
        const QPixmap scaled = source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
    } else {
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(result.rect(), Qt::AlignCenter, pack.name.left(2));
    }
    return result;
}

QString itemText(const ThemePacks::Pack &pack)
{
    QString second = pack.author.isEmpty() ? QObject::tr("未知作者") : pack.author;
    if (!pack.version.isEmpty())
        second += QStringLiteral("  v") + pack.version;
    return pack.name + QLatin1Char('\n') + second;
}
}

void ThemePackDialog::openManager(QWidget *parent)
{
    ThemePackDialog dialog(parent);
    dialog.exec();
}

ThemePackDialog::ThemePackDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("themePackDialog"));
    setWindowTitle(tr("主題包管理"));
    resize(900, 560);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("右邊係已啟用嘅主題，越上面優先度越高：同一個素材槽由最高優先嘅主題提供，"
        "所有主題都冇嘅槽位用返預設素材。按「套用」即時儲存；之後開嘅房間同新出現嘅元件會用新素材，"
        "已經喺牌桌上嘅元件要重新入房先會換。"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *columns = new QHBoxLayout;
    layout->addLayout(columns, 1);

    auto makeList = [this](const QString &name) {
        auto *list = new QListWidget(this);
        list->setObjectName(name);
        list->setIconSize(kIconSize);
        list->setSpacing(2);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setMinimumWidth(240);
        return list;
    };

    auto *availableBox = new QGroupBox(tr("可用主題"), this);
    auto *availableLayout = new QVBoxLayout(availableBox);
    m_available = makeList(QStringLiteral("availableThemes"));
    availableLayout->addWidget(m_available);
    columns->addWidget(availableBox, 3);

    auto *moveButtons = new QVBoxLayout;
    moveButtons->addStretch();
    m_enable = new QPushButton(tr("啟用 →"), this);
    m_disable = new QPushButton(tr("← 停用"), this);
    m_up = new QPushButton(tr("↑ 提高優先"), this);
    m_down = new QPushButton(tr("↓ 降低優先"), this);
    for (QPushButton *button : {m_enable, m_disable, m_up, m_down})
        moveButtons->addWidget(button);
    moveButtons->addStretch();
    columns->addLayout(moveButtons);

    auto *enabledBox = new QGroupBox(tr("已啟用（上 = 最高優先）"), this);
    auto *enabledLayout = new QVBoxLayout(enabledBox);
    m_enabled = makeList(QStringLiteral("enabledThemes"));
    enabledLayout->addWidget(m_enabled);
    columns->addWidget(enabledBox, 3);

    auto *detailBox = new QGroupBox(tr("詳情"), this);
    auto *detailLayout = new QVBoxLayout(detailBox);
    m_preview = new QLabel(detailBox);
    m_preview->setFixedSize(256, 144);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background: #282828;"));
    m_details = new QLabel(detailBox);
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_preview, 0, Qt::AlignHCenter);
    detailLayout->addWidget(m_details, 1);
    columns->addWidget(detailBox, 3);

    auto *footer = new QHBoxLayout;
    auto *openFolder = new QPushButton(tr("開啟主題資料夾"), this);
    auto *rescan = new QPushButton(tr("重新掃描"), this);
    m_status = new QLabel(this);
    footer->addWidget(openFolder);
    footer->addWidget(rescan);
    footer->addWidget(m_status, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_apply = buttons->button(QDialogButtonBox::Apply);
    m_apply->setText(tr("套用"));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("確定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    footer->addWidget(buttons);
    layout->addLayout(footer);

    connect(m_enable, &QPushButton::clicked, this, [this] { moveSelected(m_available, m_enabled); });
    connect(m_disable, &QPushButton::clicked, this, [this] { moveSelected(m_enabled, m_available); });
    connect(m_available, &QListWidget::itemDoubleClicked, this, [this] { moveSelected(m_available, m_enabled); });
    connect(m_enabled, &QListWidget::itemDoubleClicked, this, [this] { moveSelected(m_enabled, m_available); });
    connect(m_up, &QPushButton::clicked, this, [this] { shiftSelected(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { shiftSelected(1); });
    for (QListWidget *list : {m_available, m_enabled}) {
        connect(list, &QListWidget::currentItemChanged, this, [this, list](QListWidgetItem *current) {
            if (current) {
                // One selection across both columns keeps the buttons unambiguous.
                QListWidget *other = list == m_available ? m_enabled : m_available;
                other->clearSelection();
                other->setCurrentRow(-1);
                showDetails(current);
            }
            updateButtons();
        });
    }
    connect(openFolder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ThemePacks::userThemeDirectory()));
    });
    connect(rescan, &QPushButton::clicked, this, [this] {
        const QStringList order = pendingOrder();
        ThemePacks::reload();
        populate();
        // Keep unsaved ordering across a rescan.
        for (int i = order.size() - 1; i >= 0; --i) {
            for (int row = 0; row < m_available->count(); ++row) {
                if (m_available->item(row)->data(kIdRole).toString() == order.at(i)) {
                    m_enabled->insertItem(0, m_available->takeItem(row));
                    break;
                }
            }
        }
        updateButtons();
    });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void ThemePackDialog::populate()
{
    m_available->clear();
    m_enabled->clear();
    const QList<ThemePacks::Pack> packs = ThemePacks::installed();
    const QStringList enabled = ThemePacks::enabledIds();

    auto makeItem = [](const ThemePacks::Pack &pack) {
        auto *item = new QListWidgetItem(QIcon(previewIcon(pack, kIconSize)), itemText(pack));
        item->setData(kIdRole, pack.id);
        item->setToolTip(pack.description.isEmpty() ? pack.root : pack.description);
        return item;
    };
    for (const QString &id : enabled) {
        for (const ThemePacks::Pack &pack : packs) {
            if (pack.id == id)
                m_enabled->addItem(makeItem(pack));
        }
    }
    for (const ThemePacks::Pack &pack : packs) {
        if (!enabled.contains(pack.id))
            m_available->addItem(makeItem(pack));
    }

    m_status->setText(packs.isEmpty()
        ? tr("未搵到主題包：將含 theme.json 嘅資料夾放入 %1").arg(QDir::toNativeSeparators(ThemePacks::userThemeDirectory()))
        : tr("已安裝 %1 個主題，啟用緊 %2 個").arg(packs.size()).arg(enabled.size()));
    m_available->setCurrentRow(-1);
    m_enabled->setCurrentRow(-1);
    m_preview->clear();
    m_details->setText(tr("揀一個主題睇詳情。"));
    updateButtons();
}

void ThemePackDialog::showDetails(QListWidgetItem *item)
{
    const QString id = item->data(kIdRole).toString();
    const QList<ThemePacks::Pack> packs = ThemePacks::installed();
    for (const ThemePacks::Pack &pack : packs) {
        if (pack.id != id)
            continue;
        m_preview->setPixmap(previewIcon(pack, m_preview->size()));
        QStringList provided;
        for (const ThemePacks::Slot &slot : ThemePacks::slotTable()) {
            if (pack.slotFiles.contains(slot.id))
                provided << slot.label.toHtmlEscaped();
        }
        if (!pack.files.isEmpty())
            provided << tr("另加 %1 個檔案覆蓋").arg(pack.files.size());
        QString html = QStringLiteral("<b>%1</b><br/>").arg(pack.name.toHtmlEscaped());
        html += tr("作者：%1").arg(pack.author.toHtmlEscaped()) + QStringLiteral("<br/>");
        html += tr("版本：%1").arg(pack.version.toHtmlEscaped()) + QStringLiteral("<br/>");
        html += tr("ID：%1").arg(pack.id.toHtmlEscaped()) + QStringLiteral("<br/>");
        if (!pack.description.isEmpty())
            html += QStringLiteral("<p>%1</p>").arg(pack.description.toHtmlEscaped());
        html += QStringLiteral("<p>") + tr("提供素材槽：%1").arg(provided.isEmpty() ? tr("（無）") : provided.join(QStringLiteral("、"))) + QStringLiteral("</p>");
        if (!pack.warnings.isEmpty()) {
            QStringList escaped;
            for (const QString &warning : pack.warnings)
                escaped << warning.toHtmlEscaped();
            html += QStringLiteral("<p style='color:#c0392b'>") + tr("警告：") + QStringLiteral("<br/>")
                + escaped.join(QStringLiteral("<br/>")) + QStringLiteral("</p>");
        }
        html += QStringLiteral("<p style='color:gray'>%1</p>").arg(QDir::toNativeSeparators(pack.root).toHtmlEscaped());
        m_details->setText(html);
        return;
    }
}

void ThemePackDialog::moveSelected(QListWidget *from, QListWidget *to)
{
    const int row = from->currentRow();
    if (row < 0)
        return;
    QListWidgetItem *item = from->takeItem(row);
    // A newly enabled pack goes on top, as it is usually the one the player wants to see.
    if (to == m_enabled)
        to->insertItem(0, item);
    else
        to->addItem(item);
    from->setCurrentRow(-1);
    to->setCurrentItem(item);
    updateButtons();
}

void ThemePackDialog::shiftSelected(int delta)
{
    const int row = m_enabled->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_enabled->count())
        return;
    QListWidgetItem *item = m_enabled->takeItem(row);
    m_enabled->insertItem(target, item);
    m_enabled->setCurrentItem(item);
    updateButtons();
}

void ThemePackDialog::updateButtons()
{
    // Picking an item clears the other column's current item, so currentRow() is the selection.
    const int enabledRow = m_enabled->currentRow();
    const bool enabledSelected = enabledRow >= 0;
    m_enable->setEnabled(m_available->currentRow() >= 0);
    m_disable->setEnabled(enabledSelected);
    m_up->setEnabled(enabledSelected && enabledRow > 0);
    m_down->setEnabled(enabledSelected && enabledRow + 1 < m_enabled->count());
    m_apply->setEnabled(pendingOrder() != ThemePacks::enabledIds());
}

QStringList ThemePackDialog::pendingOrder() const
{
    QStringList ids;
    for (int row = 0; row < m_enabled->count(); ++row)
        ids << m_enabled->item(row)->data(kIdRole).toString();
    return ids;
}

bool ThemePackDialog::apply()
{
    const bool changed = ThemePacks::setEnabledIds(pendingOrder());
    if (changed)
        m_status->setText(tr("已套用：之後開嘅房間同新出現嘅元件會用新素材。"));
    updateButtons();
    return changed;
}
