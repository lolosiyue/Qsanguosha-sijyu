#ifndef QSAN_UI_ACTION_DISPATCHER_H
#define QSAN_UI_ACTION_DISPATCHER_H

#include "ui-action.h"

#include <QObject>
#include <QPointer>

#include <functional>

// Routes UiActions to whatever currently owns input. The game table registers a
// semantic handler; every other surface (home QML, settings, QWidget dialogs)
// receives the equivalent key press/release from the shared convention, so
// keyboard focus work in those surfaces serves gamepads unchanged.
class UiActionDispatcher : public QObject
{
    Q_OBJECT
public:
    using Handler = std::function<bool(UiAction)>;

    explicit UiActionDispatcher(QObject *parent = nullptr);
    static UiActionDispatcher *instance();

    // The handler is dropped automatically when owner is destroyed. It returns
    // false when its surface is not the active one.
    void setTableHandler(QObject *owner, Handler handler);
    void clearTableHandler(QObject *owner);

    // Marks gamepad mode, then delivers the action. Returns false when nothing took it.
    bool dispatch(UiAction action);

    // Sends a key press/release pair, flagged so InputModeTracker ignores it.
    static bool sendKeyClick(QObject *receiver, int key, Qt::KeyboardModifiers modifiers);

signals:
    // Emitted for every dispatched action, before delivery, for surfaces that
    // want actions without a key equivalent (Details, View, Menu).
    void actionDispatched(UiAction action);

private:
    QPointer<QObject> m_tableOwner;
    Handler m_tableHandler;
};

#endif
