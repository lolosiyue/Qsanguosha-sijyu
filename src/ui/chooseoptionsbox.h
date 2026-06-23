#ifndef CHOOSEOPTIONSBOX_H
#define CHOOSEOPTIONSBOX_H

#include "graphicsbox.h"

class Button;
class QSanCommandProgressBar;
class QGraphicsProxyWidget;

class ChooseOptionsBox : public GraphicsBox
{
    Q_OBJECT

public:
    explicit ChooseOptionsBox();

    QRectF boundingRect() const override;

    void setSkillName(const QString &skillName)
    {
        this->skillName = skillName;
    }
    void setTip(const QString &tip)
    {
        this->tip = tip;
    }
    void clear();

public slots:
    void chooseOption(const QStringList &options);
    void chooseOption(const QStringList &options, const QStringList &disabledOptions);

protected:
    void paintLayout(QPainter *painter) override;

private:
    void populateButtons();
    void setupProgressBar();
    int getButtonWidth() const;
    QString translate(const QString &option) const;
    void addButton(const QString &choice, bool enabled = true);

    QStringList options;
    QStringList disabledOptions;
    QString skillName;
    QString tip;
    QList<Button *> buttons;
    QList<Button *> disabledButtons;
    static const int minButtonWidth = 100;
    static const int defaultButtonHeight = 30;
    static const int topBlankWidth = 42;
    static const int bottomBlankWidth = 25;
    static const int interval = 15;
    static const int outerBlankWidth = 37;
    static const int tipHeight = 20;

    QGraphicsProxyWidget *progressBarItem;
    QSanCommandProgressBar *progressBar;

public slots:
    void reply();
};

#endif
