#ifndef QSAN_PACKAGE_DIALOGS_H
#define QSAN_PACKAGE_DIALOGS_H

#include "generaloverview.h"

#include <QDialog>
#include <QHash>
#include <QStringList>

class QAbstractButton;
class QButtonGroup;
class QGroupBox;
class QVBoxLayout;
class Card;

class GuhuoDialog : public QDialog
{
    Q_OBJECT

public:
    static GuhuoDialog *getInstance(const QString &object, bool left = true, bool right = true,
        bool play_only = true, bool slash_combined = false, bool delayed_tricks = false, bool update = false);
    void prepareOptions();
    QStringList getOptionNames() const;
    const Card *getOptionCard(const QString &option_name) const;
    bool applyOption(const QString &option_name);
    void clearChoice() const;
    bool shouldPopup() const;
    bool hasEnabledOptions() const;
    bool isButtonEnabled(const QString &button_name) const;

public slots:
    void popup();
    void selectCard(QAbstractButton *button);

protected:
    explicit GuhuoDialog(const QString &object, bool left = true, bool right = true,
        bool play_only = true, bool slash_combined = false, bool delayed_tricks = false);
    QAbstractButton *createButton(Card *card);

    QHash<QString, const Card *> map;
    QStringList option_names;

private:
    QGroupBox *createLeft();
    QGroupBox *createRight();
    QButtonGroup *group;
    bool play_only;
    bool slash_combined;
    bool delayed_tricks;

signals:
    void onButtonClick();
};

class JuguanDialog : public QDialog
{
    Q_OBJECT

public:
    static JuguanDialog *getInstance(const QString &object, const QString &card_names);
    void prepareOptions();
    QStringList getOptionNames() const;
    const Card *getOptionCard(const QString &option_name) const;
    bool applyOption(const QString &option_name);
    void clearChoice() const;
    bool shouldPopup() const;
    bool hasEnabledOptions() const;
    bool isButtonEnabled(const QString &button_name) const;

public slots:
    void popup();
    void selectCard(QAbstractButton *button);

private:
    explicit JuguanDialog(const QString &object, const QString &card_names);
    void clearButtons();
    QAbstractButton *createButton(Card *card);
    QHash<QString, const Card *> map;
    QStringList option_names;
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    QString cards;

signals:
    void onButtonClick();
};

class TiansuanDialog : public QDialog
{
    Q_OBJECT

public:
    static TiansuanDialog *getInstance(const QString &name, const QString &choices = QString());

public slots:
    void popup();
    void selectChoice(QAbstractButton *button);

private:
    explicit TiansuanDialog(const QString &name, const QString &choices = QString());
    QAbstractButton *createChoiceButton(const QString &choice);
    bool MarkJudge(const QString &choice);
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    QString tiansuan_choices;

signals:
    void onButtonClick();
};

class HuashenDialog : public GeneralOverview
{
    Q_OBJECT

public:
    explicit HuashenDialog(const QString &propertyName = "Huashens");

public slots:
    void popup();

private:
    QString m_propertyName;
};

#endif
