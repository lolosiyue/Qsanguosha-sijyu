#ifndef ROOM_DEBUG_DIALOGS_H
#define ROOM_DEBUG_DIALOGS_H

#include <QCoreApplication>
#include <QDialog>

class QComboBox;
class QSpinBox;
class QWidget;

class ScriptExecutor : public QDialog
{
    Q_OBJECT

public:
    ScriptExecutor(QWidget *parent);

public slots:
    void doScript();
};

class DeathNoteDialog : public QDialog
{
    Q_OBJECT

public:
    DeathNoteDialog(QWidget *parent);

protected:
    virtual void accept();

private:
    QComboBox *killer, *victim;
};

class DamageMakerDialog : public QDialog
{
    Q_OBJECT

public:
    DamageMakerDialog(QWidget *parent);

protected:
    virtual void accept();

private:
    QComboBox *damage_source;
    QComboBox *damage_target;
    QComboBox *damage_nature;
    QSpinBox *damage_point;

private slots:
    void disableSource();
};

class StateEditorDialog : public QDialog
{
    Q_OBJECT

public:
    StateEditorDialog(QWidget *parent);

protected:
    virtual void accept();

private:
    QComboBox *target;
    QComboBox *type;
    QSpinBox *point;
};

// Cheat entry points, previously RoomScene slots.  RoomScene keeps the slots so
// that the mainwindow SIGNAL/SLOT string connections stay valid, and forwards
// here with main_window as parent_window.  A class rather than a namespace
// because Q_DECLARE_TR_FUNCTIONS -- which gives these strings their own
// translation context, and lets lupdate find them -- is class-only.
class RoomDebugDialogs
{
    Q_DECLARE_TR_FUNCTIONS(RoomDebugDialogs)

public:
    static void fillPlayerNames(QComboBox *ComboBox, bool add_none);

    static void makeDamage(QWidget *parent_window);
    static void changeState(QWidget *parent_window);
    static void makeKilling(QWidget *parent_window);
    static void makeReviving(QWidget *parent_window);
    static void doScript(QWidget *parent_window);
};

#endif // ROOM_DEBUG_DIALOGS_H
