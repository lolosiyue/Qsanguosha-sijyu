#include "easytextpanel.h"
#include "engine.h"
#include <QListWidgetItem>
#include <QFont>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFile>

EasyTextPanel::EasyTextPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedSize(300, 400);
    hide();
}

EasyTextPanel::~EasyTextPanel()
{
}

void EasyTextPanel::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    headerLayout = new QHBoxLayout();
    titleLabel = new QLabel("快捷语言");
    refreshButton = new QPushButton("刷新");
    refreshButton->setMaximumWidth(50);
    refreshButton->setToolTip("刷新武将台词列表");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(refreshButton);

    textList = new QListWidget();
    textList->setAlternatingRowColors(true);

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(textList);

    connect(textList, SIGNAL(itemClicked(QListWidgetItem*)),
            this, SLOT(onItemClicked(QListWidgetItem*)));
    connect(textList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this, SLOT(onItemDoubleClicked(QListWidgetItem*)));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
}

void EasyTextPanel::updateEasyTexts(const QString &general_name)
{
    currentGeneralName = general_name;
    loadEasyTexts();
}

void EasyTextPanel::loadEasyTexts()
{
    textList->clear();

    QList<EasyTextItem> items = Sanguosha->getChattingEasyTextItems(currentGeneralName);

    foreach (const EasyTextItem &easyItem, items) {
        if (!easyItem.text.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(easyItem.text);
            item->setData(Qt::UserRole, easyItem.text);
            item->setData(Qt::UserRole + 1, easyItem.audioPath);
            item->setData(Qt::UserRole + 2, easyItem.type);

            QString tooltip = "单击发送";
            if (!easyItem.audioPath.isEmpty()) {
                tooltip += "并播放语音";
            }
            item->setToolTip(tooltip);

            QBrush brush;
            switch (easyItem.type) {
                case 0:
                    break;
                case 1:
                    brush = QBrush(QColor(255, 223, 128));
                    item->setBackground(brush);
                    item->setForeground(QBrush(QColor(0, 0, 0)));
                    break;
                case 2:
                    brush = QBrush(QColor(255, 182, 193));
                    item->setBackground(brush);
                    item->setForeground(QBrush(QColor(139, 0, 0)));
                    break;
                case 3:
                    brush = QBrush(QColor(218, 165, 255));
                    item->setBackground(brush);
                    item->setForeground(QBrush(QColor(75, 0, 130)));
                    break;
            }

            textList->addItem(item);
        }
    }

    titleLabel->setText(QString("快捷语言 (%1条)").arg(items.size()));
}

void EasyTextPanel::showPanel()
{
    show();
    raise();
    activateWindow();
}

void EasyTextPanel::hidePanel()
{
    hide();
}

void EasyTextPanel::onItemClicked(QListWidgetItem *item)
{
    if (!item || item->flags() == Qt::NoItemFlags) {
        return;
    }

    QString text = item->data(Qt::UserRole).toString();
    if (!text.isEmpty()) {
        playItemAudio(item);

        emit textSelected(text);
        hidePanel();
    }
}

void EasyTextPanel::onItemDoubleClicked(QListWidgetItem *item)
{
    onItemClicked(item);
}

void EasyTextPanel::playItemAudio(QListWidgetItem *item)
{
    if (!item) return;

    QString audioPath = item->data(Qt::UserRole + 1).toString();
    if (!audioPath.isEmpty() && QFile::exists(audioPath)) {
        Sanguosha->playAudioEffect(audioPath, false);
    }
}

void EasyTextPanel::onRefreshClicked()
{
    loadEasyTexts();
}