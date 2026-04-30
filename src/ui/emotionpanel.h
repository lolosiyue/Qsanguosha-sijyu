#ifndef EMOTIONPANEL_H
#define EMOTIONPANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QMouseEvent>
#include <QMovie>

class EmotionItem : public QLabel
{
    Q_OBJECT

public:
    explicit EmotionItem(const QString &imagePath, int emotionId, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_emotionId;
    QString m_imagePath;
    QPixmap m_originalPixmap;
    QMovie *m_movie;

signals:
    void emotionClicked(int emotionId);
};

class EmotionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EmotionPanel(QWidget *parent = nullptr);
    ~EmotionPanel();

    void showPanel();
    void hidePanel();
    void loadEmotions();

    static QString getEmotionPath(int emotionId);

private slots:
    void onEmotionClicked(int emotionId);
    void onPrevCategory();
    void onNextCategory();

private:
    void setupUI();
    void loadEmotionCategory(const QString &categoryPath, const QString &categoryName);
    void loadDefaultEmotions();
    void showCategory(int index);
    void updateCategoryButtons();

    QVBoxLayout *mainLayout;
    QHBoxLayout *headerLayout;
    QScrollArea *scrollArea;
    QWidget *containerWidget;
    QVBoxLayout *containerLayout;
    QPushButton *prevButton;
    QPushButton *nextButton;
    QLabel *categoryLabel;

    QStringList categoryNames;
    QList<QWidget*> categoryWidgets;
    int currentCategoryIndex;

    QString emotionBasePath;

    static QMap<int, QString> emotionIdToPath;

signals:
    void emotionSelected(int emotionId);
};

#endif // EMOTIONPANEL_H