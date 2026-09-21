#ifndef POINTER_HOVER_DELIVERY_H
#define POINTER_HOVER_DELIVERY_H

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QObject>
#include <QWidget>

// Wayland (WSLg, GNOME, …) often delivers pointer motion as QEvent::Hover*
// without a matching QEvent::MouseMove. QGraphicsView and QQuickWidget only
// turn MouseMove into scene / QML hover, so cards and home buttons stay idle.
inline bool qsanIsPointerHoverEvent(QEvent::Type type)
{
    return type == QEvent::HoverEnter || type == QEvent::HoverMove;
}

inline void qsanEnableWidgetPointerHover(QWidget *widget)
{
    if (!widget)
        return;
    widget->setMouseTracking(true);
    widget->setAttribute(Qt::WA_Hover, true);
}

// Convert a HoverEnter/Move on `receiver` into the MouseMove those widgets
// already know how to map onto items. Guarded against WA_Hover synthesizing
// another HoverMove from that MouseMove. QMouseEvent is not movable.
inline bool qsanForwardPointerHoverAsMouseMove(QObject *receiver, QEvent *event)
{
    if (!receiver || !event || !qsanIsPointerHoverEvent(event->type()))
        return false;

    static thread_local bool forwarding = false;
    if (forwarding)
        return false;

    const auto *hover = static_cast<const QHoverEvent *>(event);
    QMouseEvent mouse(QEvent::MouseMove, hover->position(), hover->position(),
        hover->globalPosition(), Qt::NoButton, QGuiApplication::mouseButtons(),
        hover->modifiers());
    mouse.setTimestamp(hover->timestamp());

    forwarding = true;
    const bool sent = QCoreApplication::sendEvent(receiver, &mouse);
    forwarding = false;
    return sent;
}

class PointerHoverForwardFilter final : public QObject
{
public:
    explicit PointerHoverForwardFilter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        qsanForwardPointerHoverAsMouseMove(watched, event);
        return QObject::eventFilter(watched, event);
    }
};

#endif
