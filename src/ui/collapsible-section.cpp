#include "collapsible-section.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const int PackageColumns = 5;
}

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title)
{
    m_header = new QToolButton;
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setAutoRaise(true);

    QPushButton *selectAllButton = new QPushButton(tr("Select All"));
    QPushButton *deselectAllButton = new QPushButton(tr("Select None"));
    QPushButton *reverseButton = new QPushButton(tr("Reverse Select"));

    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(m_header);
    headerLayout->addStretch();
    headerLayout->addWidget(selectAllButton);
    headerLayout->addWidget(deselectAllButton);
    headerLayout->addWidget(reverseButton);

    m_grid = new QGridLayout;
    m_content = new QWidget;
    m_content->setLayout(m_grid);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(m_content);
    setLayout(mainLayout);

    connect(m_header, &QToolButton::toggled, this, &CollapsibleSection::onToggled);
    connect(selectAllButton, &QPushButton::clicked, this, &CollapsibleSection::selectAll);
    connect(deselectAllButton, &QPushButton::clicked, this, &CollapsibleSection::deselectAll);
    connect(reverseButton, &QPushButton::clicked, this, &CollapsibleSection::reverseSelect);
}

void CollapsibleSection::addPackageCheckbox(QCheckBox *checkbox)
{
    const int index = m_checkboxes.size();
    m_grid->addWidget(checkbox, index / PackageColumns, index % PackageColumns);
    m_checkboxes << checkbox;
    connect(checkbox, &QCheckBox::toggled, this, &CollapsibleSection::updateCount);
    updateCount();
}

void CollapsibleSection::onToggled(bool expanded)
{
    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_content->setVisible(expanded);
}

void CollapsibleSection::selectAll()
{
    foreach (QCheckBox *checkbox, m_checkboxes) {
        if (checkbox->isEnabled())
            checkbox->setChecked(true);
    }
}

void CollapsibleSection::deselectAll()
{
    foreach (QCheckBox *checkbox, m_checkboxes) {
        if (checkbox->isEnabled())
            checkbox->setChecked(false);
    }
}

void CollapsibleSection::reverseSelect()
{
    foreach (QCheckBox *checkbox, m_checkboxes) {
        if (checkbox->isEnabled())
            checkbox->setChecked(!checkbox->isChecked());
    }
}

void CollapsibleSection::updateCount()
{
    int checked = 0;
    foreach (QCheckBox *checkbox, m_checkboxes) {
        if (checkbox->isChecked())
            ++checked;
    }
    m_header->setText(QString("%1  %2/%3")
        .arg(m_title).arg(checked).arg(m_checkboxes.size()));
}
