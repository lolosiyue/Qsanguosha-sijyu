#ifndef EASYTEXTPANEL_H
#define EASYTEXTPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>

class EasyTextPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EasyTextPanel(QWidget *parent = nullptr);
    ~EasyTextPanel();

    void updateEasyTexts(const QString &general_name = QString());
    void showPanel();
    void hidePanel();

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onRefreshClicked();

private:
    void setupUI();
    void loadEasyTexts();
    void playItemAudio(QListWidgetItem *item);

    QVBoxLayout *mainLayout;
    QHBoxLayout *headerLayout;
    QLabel *titleLabel;
    QPushButton *refreshButton;
    QListWidget *textList;

    QString currentGeneralName;

signals:
    void textSelected(const QString &text);
};

#endif // EASYTEXTPANEL_H