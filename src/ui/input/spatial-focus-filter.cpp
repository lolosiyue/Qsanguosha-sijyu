#include "spatial-focus-filter.h"

#include <algorithm>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>

namespace {

// Dim wash painted over the window hosting a modal dialog so the table
// reads as inactive behind it. Child of the host window: it stacks above
// the host's own widgets but below the separate dialog window.
class ModalScrim : public QWidget
{
public:
    explicit ModalScrim(QWidget *host)
        : QWidget(host)
    {
        setObjectName(QStringLiteral("qsanModalScrim"));
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 140));
    }
};

bool isTextEditingWidget(QWidget *widget)
{
    return qobject_cast<QLineEdit *>(widget)
        || qobject_cast<QTextEdit *>(widget)
        || qobject_cast<QPlainTextEdit *>(widget);
}

// Widgets that already do something useful with arrows and Enter
// (cursor movement, selection, popup opening).
bool keepsOwnKeys(QWidget *widget)
{
    return isTextEditingWidget(widget)
        || qobject_cast<QAbstractItemView *>(widget)
        || qobject_cast<QComboBox *>(widget);
}

// All visible, enabled focus-takers inside root, in reading order.
QList<QWidget *> focusablesIn(QWidget *root)
{
    QList<QWidget *> result;
    const QList<QWidget *> all = root->findChildren<QWidget *>();
    result.reserve(all.size());
    foreach (QWidget *widget, all) {
        if (widget->focusPolicy() == Qt::NoFocus
            || !widget->isEnabled() || !widget->isVisibleTo(root))
            continue;
        result << widget;
    }
    std::sort(result.begin(), result.end(), [root](QWidget *a, QWidget *b) {
        const QPoint pa = a->mapTo(root, a->rect().center());
        const QPoint pb = b->mapTo(root, b->rect().center());
        return pa.y() == pb.y() ? pa.x() < pb.x() : pa.y() < pb.y();
    });
    return result;
}

} // namespace

SpatialFocusFilter::SpatialFocusFilter(QObject *parent)
    : QObject(parent)
{
}

bool SpatialFocusFilter::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress: {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        // ControllerRouter owns every SDL action and its synthetic keys.
        // This filter supplies only physical keyboard/Steam-keyboard navigation.
        if (event->spontaneous() && dialog
            && handleDialogKey(dialog, static_cast<QKeyEvent *>(event)))
            return true;
        break;
    }
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WindowActivate:
        // Modal state may not be final while this event is being delivered,
        // so the scrim decision is deferred to the next event loop turn.
        queueScrimUpdate();
        break;
    case QEvent::Resize:
        if (watched == m_scrimHost.data() && m_scrim)
            m_scrim->setGeometry(m_scrimHost->rect());
        break;
    case QEvent::Close:
        if (watched == m_scrimHost.data() || watched == m_scrim.data())
            removeModalScrim();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool SpatialFocusFilter::handleDialogKey(QDialog *dialog, QKeyEvent *event)
{
    QWidget *focus = QApplication::focusWidget();
    // Keys a focus owner already uses stay with it (cursor movement in
    // lists/edits, popup opening on combo boxes).
    if (!focus || (focus != dialog && !dialog->isAncestorOf(focus)))
        return false;
    const bool focusKeepsKeys = keepsOwnKeys(focus);

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (focusKeepsKeys)
            return false;
        QWidget *next = nearestFocusable(dialog, focus, static_cast<Qt::Key>(event->key()));
        if (!next)
            return false;
        next->setFocus(Qt::OtherFocusReason);
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (focusKeepsKeys)
            return false;
        if (QAbstractButton *button = qobject_cast<QAbstractButton *>(focus)) {
            if (button->isCheckable() && !qobject_cast<QPushButton *>(button)) {
                button->toggle();
            } else {
                button->click();
            }
            return true;
        }
        // Preserve the native default-button and mandatory-choice contract.
        return false;
    }
    case Qt::Key_Backspace:
        return false;
    case Qt::Key_Escape:
        // Native QDialog handling already rejects on Escape; leaving it to Qt
        // keeps the "only cancelable dialogs may be cancelled" contract.
        return false;
    case Qt::Key_PageDown:
    case Qt::Key_PageUp: {
        // LB/RB stub: jump to the next/previous focusable in reading order.
        const QList<QWidget *> focusables = focusablesIn(dialog);
        if (focusables.isEmpty())
            return false;
        const int current = focusables.indexOf(focus);
        const int step = (event->key() == Qt::Key_PageDown) ? 1 : -1;
        const int nextIndex = (current < 0)
            ? 0
            : (current + step + focusables.size()) % focusables.size();
        focusables.at(nextIndex)->setFocus(Qt::OtherFocusReason);
        return true;
    }
    default:
        break;
    }
    return false;
}

QWidget *SpatialFocusFilter::nearestFocusable(QWidget *root, QWidget *from, Qt::Key direction) const
{
    const QList<QWidget *> candidates = focusablesIn(root);
    if (candidates.isEmpty())
        return nullptr;
    if (!from || !candidates.contains(from))
        return candidates.first();

    const QPoint origin = from->mapTo(root, from->rect().center());
    QWidget *best = nullptr;
    double bestScore = -1.0;
    foreach (QWidget *candidate, candidates) {
        if (candidate == from)
            continue;
        const QPoint center = candidate->mapTo(root, candidate->rect().center());
        const int dx = center.x() - origin.x();
        const int dy = center.y() - origin.y();
        int primary = 0, orthogonal = 0;
        switch (direction) {
        case Qt::Key_Left:  primary = -dx; orthogonal = qAbs(dy); break;
        case Qt::Key_Right: primary = dx;  orthogonal = qAbs(dy); break;
        case Qt::Key_Up:    primary = -dy; orthogonal = qAbs(dx); break;
        case Qt::Key_Down:  primary = dy;  orthogonal = qAbs(dx); break;
        default: break;
        }
        if (primary <= 0)
            continue;
        // Distance along the travel direction dominates; drifting sideways is
        // penalized so a stacked column stays navigable with Up/Down alone.
        const double score = primary + orthogonal * 2.0;
        if (bestScore < 0.0 || score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

void SpatialFocusFilter::queueScrimUpdate()
{
    if (m_scrimUpdateQueued)
        return;
    m_scrimUpdateQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_scrimUpdateQueued = false;
        updateModalScrim();
    });
}

void SpatialFocusFilter::updateModalScrim()
{
    QDialog *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (!modal || !modal->isVisible()) {
        removeModalScrim();
        return;
    }
    QWidget *host = modal->parentWidget();
    if (!host) {
        // Unparented exec() dialogs still dim the main window behind them.
        const QWidgetList tops = QApplication::topLevelWidgets();
        foreach (QWidget *top, tops) {
            if (top != modal && top->isWindow() && top->isVisible()
                && !qobject_cast<QDialog *>(top)) {
                host = top;
                break;
            }
        }
    }
    if (!host) {
        removeModalScrim();
        return;
    }

    host = host->window();
    if (m_scrim && m_scrimHost == host)
        return;
    removeModalScrim();
    m_scrimHost = host;
    m_scrim = new ModalScrim(host);
    m_scrim->setGeometry(host->rect());
    m_scrim->show();
    m_scrim->raise();
}

void SpatialFocusFilter::removeModalScrim()
{
    if (m_scrim)
        m_scrim->deleteLater();
    m_scrim = nullptr;
    m_scrimHost = nullptr;
}
