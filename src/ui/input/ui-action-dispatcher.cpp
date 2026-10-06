#include "ui-action-dispatcher.h"

#include "input-mode-tracker.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>

namespace {
QPointer<UiActionDispatcher> g_dispatcher;
}

UiActionDispatcher::UiActionDispatcher(QObject *parent)
    : QObject(parent)
{
}

UiActionDispatcher *UiActionDispatcher::instance()
{
    if (!g_dispatcher && QCoreApplication::instance())
        g_dispatcher = new UiActionDispatcher(QCoreApplication::instance());
    return g_dispatcher;
}

void UiActionDispatcher::setTableHandler(QObject *owner, Handler handler)
{
    m_tableOwner = owner;
    m_tableHandler = owner ? std::move(handler) : Handler();
}

void UiActionDispatcher::clearTableHandler(QObject *owner)
{
    if (m_tableOwner != owner) return;
    m_tableOwner = nullptr;
    m_tableHandler = Handler();
}

bool UiActionDispatcher::dispatch(UiAction action)
{
    if (auto *tracker = InputModeTracker::instance()) tracker->noteGamepadInput();
    emit actionDispatched(action);
    if (m_tableOwner && m_tableHandler && m_tableHandler(action)) return true;
    QWindow *window = QGuiApplication::focusWindow();
    if (!window) return false;
    Qt::KeyboardModifiers modifiers;
    const int key = QSanInput::uiActionKey(action, UiKeyMap::Generic, &modifiers);
    if (!key) return false;
    return sendKeyClick(window, key, modifiers);
}

bool UiActionDispatcher::sendKeyClick(QObject *receiver, int key, Qt::KeyboardModifiers modifiers)
{
    if (!receiver || !key) return false;
    InputModeTracker::SyntheticInputScope synthetic;
    QPointer<QObject> guard = receiver;
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(receiver, &press);
    // The press may close a dialog and destroy its window.
    if (guard) {
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(receiver, &release);
    }
    return true;
}
