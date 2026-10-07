#include "input-mode-tracker.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPointer>

namespace {
QPointer<InputModeTracker> g_tracker;
int g_syntheticDepth = 0;
}

InputModeTracker::SyntheticInputScope::SyntheticInputScope() { ++g_syntheticDepth; }
InputModeTracker::SyntheticInputScope::~SyntheticInputScope() { --g_syntheticDepth; }
bool InputModeTracker::SyntheticInputScope::active() { return g_syntheticDepth > 0; }

InputModeTracker::InputModeTracker(QObject *parent)
    : QObject(parent)
{
}

InputModeTracker::~InputModeTracker()
{
    if (m_cursorHidden) QGuiApplication::restoreOverrideCursor();
}

InputModeTracker *InputModeTracker::instance()
{
    if (!g_tracker && QCoreApplication::instance()) {
        g_tracker = new InputModeTracker(QCoreApplication::instance());
        QCoreApplication::instance()->installEventFilter(g_tracker);
    }
    return g_tracker;
}

InputModeTracker *InputModeTracker::existing()
{
    return g_tracker;
}

void InputModeTracker::setHideCursorInGamepadMode(bool hide)
{
    m_hideCursor = hide;
    applyCursor();
}

void InputModeTracker::noteGamepadInput()
{
    setMode(Mode::Gamepad);
}

void InputModeTracker::noteKeyboardInput()
{
    if (SyntheticInputScope::active()) return;
    setMode(Mode::Keyboard);
}

void InputModeTracker::noteMouseMove(const QPointF &globalPos)
{
    if (m_mode == Mode::Mouse) return;
    // The first move after a switch only records where the pointer rests;
    // repaint/hover jitter there must not undo gamepad mode.
    if (!m_anchorValid) {
        m_anchor = globalPos;
        m_anchorValid = true;
        return;
    }
    if (QLineF(m_anchor, globalPos).length() > MouseSwitchDistance) setMode(Mode::Mouse);
}

void InputModeTracker::noteMouseButton()
{
    setMode(Mode::Mouse);
}

void InputModeTracker::setMode(Mode mode)
{
    if (mode != Mode::Mouse) m_anchorValid = false;
    if (mode == m_mode) return;
    m_mode = mode;
    applyCursor();
    emit modeChanged(mode);
}

void InputModeTracker::applyCursor()
{
    const bool hide = m_hideCursor && m_mode == Mode::Gamepad;
    if (hide == m_cursorHidden || !qobject_cast<QGuiApplication *>(QCoreApplication::instance())) return;
    m_cursorHidden = hide;
    if (hide) QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
    else QGuiApplication::restoreOverrideCursor();
}

bool InputModeTracker::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        if (event->spontaneous() && !static_cast<QKeyEvent *>(event)->isAutoRepeat()) noteKeyboardInput();
        break;
    case QEvent::MouseMove:
        // Hover forwarding and scene updates synthesize moves; only real pointer motion counts.
        if (event->spontaneous()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            noteMouseMove(static_cast<QMouseEvent *>(event)->globalPosition());
#else
            noteMouseMove(static_cast<QMouseEvent *>(event)->globalPos());
#endif
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (event->spontaneous()) noteMouseButton();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
