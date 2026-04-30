#include "emotionpanel.h"
#include <QApplication>
#include <QDebug>
#include <algorithm>

QMap<int, QString> EmotionPanel::emotionIdToPath;

EmotionItem::EmotionItem(const QString &imagePath, int emotionId, QWidget *parent)
    : QLabel(parent), m_emotionId(emotionId), m_imagePath(imagePath), m_movie(nullptr)
{
    if (imagePath.toLower().endsWith(".gif")) {
        m_movie = new QMovie(imagePath, QByteArray(), this);
        if (m_movie->isValid()) {
            m_movie->setScaledSize(QSize(32, 32));
            setMovie(m_movie);
            m_movie->start();
        } else {
            delete m_movie;
            m_movie = nullptr;
            setText(QString::number(emotionId));
            setStyleSheet("QLabel { background-color: #ffcccc; border: 1px solid red; }");
        }
    } else {
        m_originalPixmap = QPixmap(imagePath);
        if (!m_originalPixmap.isNull()) {
            QPixmap scaledPixmap = m_originalPixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setPixmap(scaledPixmap);
        } else {
            setText(QString::number(emotionId));
            setStyleSheet("QLabel { background-color: #ffcccc; border: 1px solid red; }");
        }
    }

    setFixedSize(40, 40);
    setAlignment(Qt::AlignCenter);
    setProperty("emotionItem", true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QString("表情 %1").arg(emotionId));
}

void EmotionItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit emotionClicked(m_emotionId);
    }
    QLabel::mousePressEvent(event);
}

void EmotionItem::enterEvent(QEvent *event)
{
    if (m_movie && m_movie->isValid()) {
        m_movie->setScaledSize(QSize(36, 36));
    } else if (!m_originalPixmap.isNull()) {
        QPixmap scaledPixmap = m_originalPixmap.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        setPixmap(scaledPixmap);
    }
    QLabel::enterEvent(event);
}

void EmotionItem::leaveEvent(QEvent *event)
{
    if (m_movie && m_movie->isValid()) {
        m_movie->setScaledSize(QSize(32, 32));
    } else if (!m_originalPixmap.isNull()) {
        QPixmap scaledPixmap = m_originalPixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        setPixmap(scaledPixmap);
    }
    QLabel::leaveEvent(event);
}

EmotionPanel::EmotionPanel(QWidget *parent)
    : QWidget(parent), currentCategoryIndex(0)
{
    emotionBasePath = "image/system/chatface";
    setupUI();
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedSize(400, 300);
    hide();

    loadEmotions();
}

EmotionPanel::~EmotionPanel()
{
}

void EmotionPanel::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    headerLayout = new QHBoxLayout();
    prevButton = new QPushButton("◀");
    categoryLabel = new QLabel("表情分类");
    nextButton = new QPushButton("▶");

    prevButton->setFixedSize(30, 25);
    nextButton->setFixedSize(30, 25);
    categoryLabel->setAlignment(Qt::AlignCenter);

    headerLayout->addWidget(prevButton);
    headerLayout->addWidget(categoryLabel, 1);
    headerLayout->addWidget(nextButton);

    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    containerWidget = new QWidget();
    containerLayout = new QVBoxLayout(containerWidget);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);
    scrollArea->setWidget(containerWidget);

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(scrollArea);

    connect(prevButton, SIGNAL(clicked()), this, SLOT(onPrevCategory()));
    connect(nextButton, SIGNAL(clicked()), this, SLOT(onNextCategory()));
}

void EmotionPanel::loadEmotions()
{
    categoryNames.clear();
    categoryWidgets.clear();
    currentCategoryIndex = 0;

    QDir baseDir(emotionBasePath);
    if (!baseDir.exists()) {
        categoryLabel->setText("表情目录不存在");
        return;
    }

    loadDefaultEmotions();

    QStringList subDirs = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    foreach (const QString &subDir, subDirs) {
        QString categoryPath = baseDir.absoluteFilePath(subDir);
        loadEmotionCategory(categoryPath, subDir);
    }

    if (!categoryWidgets.isEmpty()) {
        currentCategoryIndex = 0;
        showCategory(0);
        updateCategoryButtons();
    } else {
        categoryLabel->setText("没有表情分类");
        prevButton->setEnabled(false);
        nextButton->setEnabled(false);
    }
}

void EmotionPanel::loadDefaultEmotions()
{
    QDir baseDir(emotionBasePath);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.gif";

    QStringList imageFiles = baseDir.entryList(filters, QDir::Files);
    QList<int> emotionIds;
    foreach (const QString &fileName, imageFiles) {
        QString baseName = QFileInfo(fileName).baseName();
        bool ok;
        int id = baseName.toInt(&ok);
        if (ok && id > 0) {
            emotionIds << id;
        }
    }

    if (emotionIds.isEmpty()) {
        return;
    }

    std::sort(emotionIds.begin(), emotionIds.end());

    QWidget *defaultWidget = new QWidget();
    QGridLayout *gridLayout = new QGridLayout(defaultWidget);

    int row = 0, col = 0;
    const int maxCols = 8;

    foreach (int id, emotionIds) {
        QString imagePath = QString("%1/%2.png").arg(emotionBasePath).arg(id);
        if (QFile::exists(imagePath)) {
            emotionIdToPath[id] = QString("image/system/chatface/%1.png").arg(id);

            EmotionItem *item = new EmotionItem(imagePath, id);
            connect(item, SIGNAL(emotionClicked(int)), this, SLOT(onEmotionClicked(int)));

            gridLayout->addWidget(item, row, col);

            col++;
            if (col >= maxCols) {
                col = 0;
                row++;
            }
        }
    }

    gridLayout->setSpacing(4);
    gridLayout->setContentsMargins(8, 8, 8, 8);

    containerLayout->addWidget(defaultWidget);
    defaultWidget->hide();

    categoryNames << "默认表情";
    categoryWidgets << defaultWidget;
}

void EmotionPanel::loadEmotionCategory(const QString &categoryPath, const QString &categoryName)
{
    QDir categoryDir(categoryPath);
    if (!categoryDir.exists()) {
        return;
    }

    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.gif";
    QStringList imageFiles = categoryDir.entryList(filters, QDir::Files);

    if (imageFiles.isEmpty()) {
        return;
    }

    QWidget *categoryWidget = new QWidget();
    QGridLayout *gridLayout = new QGridLayout(categoryWidget);

    int row = 0, col = 0;
    const int maxCols = 8;

    int baseId = 1000 + categoryWidgets.count() * 100;

    foreach (const QString &fileName, imageFiles) {
        QString imagePath = categoryDir.absoluteFilePath(fileName);
        int emotionId = baseId++;

        emotionIdToPath[emotionId] = imagePath;

        EmotionItem *item = new EmotionItem(imagePath, emotionId);
        connect(item, SIGNAL(emotionClicked(int)), this, SLOT(onEmotionClicked(int)));

        gridLayout->addWidget(item, row, col);

        col++;
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }

    gridLayout->setSpacing(4);
    gridLayout->setContentsMargins(8, 8, 8, 8);

    containerLayout->addWidget(categoryWidget);
    categoryWidget->hide();

    categoryNames << categoryName;
    categoryWidgets << categoryWidget;
}

void EmotionPanel::showPanel()
{
    show();
    raise();
    activateWindow();
}

void EmotionPanel::hidePanel()
{
    hide();
}

void EmotionPanel::showCategory(int index)
{
    if (categoryWidgets.isEmpty() || index < 0 || index >= categoryWidgets.size()) {
        return;
    }

    for (int i = 0; i < categoryWidgets.size(); i++) {
        if (categoryWidgets[i]) {
            categoryWidgets[i]->hide();
        }
    }

    currentCategoryIndex = index;
    QWidget *widget = categoryWidgets[index];
    if (widget) {
        widget->show();

        categoryLabel->setText(QString("%1 (%2/%3)")
                              .arg(categoryNames[index])
                              .arg(index + 1)
                              .arg(categoryWidgets.size()));
    }
}

void EmotionPanel::updateCategoryButtons()
{
    prevButton->setEnabled(currentCategoryIndex > 0);
    nextButton->setEnabled(currentCategoryIndex < categoryWidgets.size() - 1);
}

void EmotionPanel::onPrevCategory()
{
    if (currentCategoryIndex > 0 && !categoryWidgets.isEmpty()) {
        showCategory(currentCategoryIndex - 1);
        updateCategoryButtons();
    }
}

void EmotionPanel::onNextCategory()
{
    if (currentCategoryIndex < categoryWidgets.size() - 1 && !categoryWidgets.isEmpty()) {
        showCategory(currentCategoryIndex + 1);
        updateCategoryButtons();
    }
}

QString EmotionPanel::getEmotionPath(int emotionId)
{
    if (emotionIdToPath.contains(emotionId)) {
        return emotionIdToPath[emotionId];
    }

    return QString("image/system/chatface/%1.png").arg(emotionId);
}

void EmotionPanel::onEmotionClicked(int emotionId)
{
    emit emotionSelected(emotionId);
    hidePanel();
}