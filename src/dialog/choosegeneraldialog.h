#ifndef _CHOOSE_GENERAL_DIALOG_H
#define _CHOOSE_GENERAL_DIALOG_H

class General;
class QSanCommandProgressBar;

//#include "timed-progressbar.h"

class OptionButton : public QToolButton
{
    Q_OBJECT

public:
    explicit OptionButton(const QString icon_path, const QString &caption = "", QWidget *parent = 0);
#ifdef Q_WS_X11
    virtual QSize sizeHint() const{ return iconSize(); } // it causes bugs under Windows
#endif
    void setPreselected(bool preselected);
    bool isPreselected() const { return m_preselected; }

protected:
    virtual void mouseDoubleClickEvent(QMouseEvent *);
    virtual void mousePressEvent(QMouseEvent *);

signals:
    void double_clicked();
    void clicked_once(const QString &generalName);

private:
    bool m_preselected;
    QString m_generalName;
};

class ChooseGeneralDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChooseGeneralDialog(const QStringList &general_names, QWidget *parent, bool view_only = false, const QString &title = "");

public slots:
    void done(int);

protected:
    QDialog *m_freeChooseDialog;
    QLineEdit *name_edit;

private:
    QSanCommandProgressBar *progress_bar;
    
    QScrollArea *m_teammatePoolArea;
    QWidget *m_teammatePoolContainer;
    QVBoxLayout *m_teammatePoolLayout;
    QMap<QString, QWidget*> m_teammatePoolWidgets;
    QMap<QString, QMap<QString, TeammateGeneralButton*>> m_teammateButtons;
    QString m_selfPreselectedGeneral;
    bool m_isDeputySelection;
    
    QWidget* createTeammatePoolArea();
    QWidget* createTeammateGroupBox(const QString &playerName, const QStringList &generals, bool isDeputy);
    void updateTeammateButtonState(const QString &playerName, const QString &general, 
                                    TeammateGeneralButton::State state, bool isDeputy);
    void freezeTeammateGeneral(const QString &general);

private slots:
    void freeChoose();
    void onTeammateGeneralPoolGot(const QString &playerName, const QStringList &generals, bool isDeputy);
    void onTeammatePreselectGot(const QString &playerName, const QString &general, bool confirmed, bool isHidden, bool isDeputy);
    void onSelfPreselectChanged(const QString &general);
    void onOptionButtonClickedOnce(const QString &general);
};

class FreeChooseDialog : public QDialog
{
    Q_OBJECT
        Q_ENUMS(ButtonGroupType)

public:
    enum ButtonGroupType
    {
        Exclusive, Pair, Multi
    };

    explicit FreeChooseDialog(const QString &name, QWidget *parent, ButtonGroupType type = Exclusive);

protected slots:
    virtual void onAvatarHoverEnter();

private:
    QButtonGroup *group;
    ButtonGroupType type;
    QWidget *createTab(const QList<const General *> &generals);

private slots:
    void chooseGeneral();
    void uncheckExtraButton(QAbstractButton *button);

signals:
    void general_chosen(const QString &name);
    void pair_chosen(const QString &first, const QString &second);
};

class TextButtonItem : public QRadioButton
{
    Q_OBJECT

public:
    explicit TextButtonItem(QString parent);

protected:
    virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);

signals:
    void hover_enter();
    void hover_leave();
};

class TeammateGeneralButton : public QToolButton
{
    Q_OBJECT

public:
    enum State {
        Normal,
        Preselected,
        Confirmed,
        Disabled
    };

    explicit TeammateGeneralButton(const QString &generalName, QWidget *parent = nullptr);

    void setState(State state);
    State getState() const { return m_state; }
    QString generalName() const { return m_generalName; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    State m_state;
    QString m_generalName;
};

#endif