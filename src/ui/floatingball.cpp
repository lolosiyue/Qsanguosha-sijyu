#include "floatingball.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>

namespace {
class ActionPanel final : public QFrame
{
public:
    explicit ActionPanel(QWidget *parent) : QFrame(parent, Qt::Popup)
    {
        // Dismissal must not replay the same tap into the table or launcher.
        setAttribute(Qt::WA_NoMouseReplay);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
            hide();
            event->accept();
            return;
        }
        QFrame::keyPressEvent(event);
    }
};
}

FloatingBall::FloatingBall(QWidget *parent)
    : QToolButton(parent), m_panel(new ActionPanel(this)),
      m_scroll(new QScrollArea(m_panel)), m_grid(new QGridLayout)
{
    setObjectName(QStringLiteral("androidFloatingBall"));
    setFixedSize(56, 56);

    m_panel->setObjectName(QStringLiteral("androidFloatingPanel"));
    m_panel->setFrameShape(QFrame::StyledPanel);
    // Keep action styling local to the popup.
    m_panel->setStyleSheet(QStringLiteral(
        "QFrame#androidFloatingPanel { background: #202832; border: 1px solid #aaccee; }"
        "QToolButton { border: 1px solid #71849a; border-radius: 6px;"
        " padding: 6px; background: #345779; color: white; font-size: 16px; }"
        "QToolButton:disabled { background: #393e44; color: #aaaaaa; }"
        "QToolButton:pressed, QToolButton:checked { background: #487dac; }"));
    auto *layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(m_scroll);
    m_scroll->setMinimumSize(0, 0);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(m_scroll);
    content->setLayout(m_grid);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(8);
    m_grid->setColumnStretch(0, 1);
    m_grid->setColumnStretch(1, 1);
    m_scroll->setWidget(content);
    QScroller::grabGesture(m_scroll->viewport(), QScroller::TouchGesture);
    connect(this, &QToolButton::clicked, this, &FloatingBall::togglePanel);
}

void FloatingBall::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // The launcher must not depend on the Android font containing a menu glyph.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(!isEnabled() ? QColor(85, 85, 85)
        : isDown() ? QColor(36, 77, 120) : QColor(50, 110, 170));
    painter.setPen(QPen(QColor(170, 204, 238), 2));
    painter.drawEllipse(QRectF(rect()).adjusted(2, 2, -2, -2));
    painter.setPen(QPen(isEnabled() ? Qt::white : Qt::gray, 3, Qt::SolidLine, Qt::RoundCap));
    for (int y : {20, 28, 36})
        painter.drawLine(QPointF(18, y), QPointF(38, y));
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawEllipse(QRectF(rect()).adjusted(6, 6, -6, -6));
    }
}

void FloatingBall::addPanelAction(QAction *action, QAction *availability)
{
    if (!action)
        return;
    auto *button = new QToolButton(m_scroll->widget());
    button->setMinimumSize(0, 48);
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_grid->addWidget(button, m_actionCount / 2, m_actionCount % 2);
    ++m_actionCount;

    const QPointer<QAction> safeAction(action);
    const QPointer<QAction> gate(availability);
    const bool guarded = availability != nullptr;
    const auto sync = [button, safeAction, gate, guarded]() {
        const bool allowed = !guarded || (gate && gate->isEnabled() && gate->isVisible());
        button->setVisible(safeAction && safeAction->isVisible());
        button->setEnabled(safeAction && safeAction->isEnabled() && allowed);
        if (safeAction) {
            button->setText(safeAction->text());
            button->setToolTip(safeAction->toolTip());
            button->setAccessibleName(safeAction->text());
            button->setCheckable(safeAction->isCheckable());
            button->setChecked(safeAction->isChecked());
        }
    };
    connect(action, &QAction::changed, button, sync);
    connect(action, &QObject::destroyed, button, sync);
    if (availability) {
        connect(availability, &QAction::changed, button, sync);
        connect(availability, &QObject::destroyed, button, sync);
    }
    connect(button, &QToolButton::clicked, this, [this, safeAction, gate, guarded]() {
        // Close before triggering: the action may open a modal dialog or leave the room.
        m_panel->hide();
        if (isEnabled() && safeAction && safeAction->isEnabled() && safeAction->isVisible()
            && (!guarded || (gate && gate->isEnabled() && gate->isVisible())))
            safeAction->trigger();
    });
    sync();
}

QPoint FloatingBall::boundedPosition(const QPoint &point) const
{
    return QPoint(qBound(m_available.left(), point.x(),
                         qMax(m_available.left(), m_available.right() - width() + 1)),
                  qBound(m_available.top(), point.y(),
                         qMax(m_available.top(), m_available.bottom() - height() + 1)));
}

void FloatingBall::setAvailableGeometry(const QRect &rect)
{
    // QStackedLayout raises the incoming page even when its size is unchanged.
    // Restore the overlay's stacking order independently of geometry updates.
    raise();
    if (m_available == rect)
        return;
    m_panel->hide();
    m_pressed = false;
    setDown(false);
    m_available = rect;
    // Keep the chosen relative position across rotation and keyboard changes.
    move(boundedPosition(rect.topLeft() + QPoint(
        qRound(qMax(0, rect.width() - width()) * m_relativePosition.x()),
        qRound(qMax(0, rect.height() - height()) * m_relativePosition.y()))));
}

void FloatingBall::togglePanel()
{
    if (m_panel->isVisible()) {
        m_panel->hide();
        return;
    }
    if (!isEnabled() || m_available.isEmpty())
        return;
    const QRect bounds(parentWidget()->mapToGlobal(m_available.topLeft()), m_available.size());
    m_panel->resize(qMin(440, bounds.width()),
                    qMin(m_grid->sizeHint().height() + 20, bounds.height()));
    const QPoint center = mapToGlobal(rect().center());
    int x = center.x() < bounds.center().x() ? mapToGlobal(rect().topRight()).x() + 8
        : mapToGlobal(QPoint(0, 0)).x() - m_panel->width() - 8;
    x = qBound(bounds.left(), x, qMax(bounds.left(), bounds.right() - m_panel->width() + 1));
    const int y = qBound(bounds.top(), center.y() - m_panel->height() / 2,
                        qMax(bounds.top(), bounds.bottom() - m_panel->height() + 1));
    m_panel->move(x, y);
    m_panel->show();
    m_panel->setFocus(Qt::PopupFocusReason);
}

void FloatingBall::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressPosition = pos();
    m_pressed = true;
    m_dragging = false;
    setDown(true);
    event->accept();
}

void FloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed) {
        event->ignore();
        return;
    }
    const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
    if (delta.manhattanLength() >= QApplication::startDragDistance())
        m_dragging = true;
    if (m_dragging) {
        setDown(false);
        move(boundedPosition(m_pressPosition + delta));
    }
    event->accept();
}

void FloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_pressed || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_pressed = false;
    setDown(false);
    if (m_dragging) {
        m_relativePosition = QPointF(
            qreal(x() - m_available.left()) / qMax(1, m_available.width() - width()),
            qreal(y() - m_available.top()) / qMax(1, m_available.height() - height()));
    } else if (rect().contains(event->position().toPoint())) {
        click();
    }
    event->accept();
}

void FloatingBall::hideEvent(QHideEvent *event)
{
    m_panel->hide();
    m_pressed = false;
    setDown(false);
    QToolButton::hideEvent(event);
}

void FloatingBall::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        m_panel->hide();
        m_pressed = false;
        setDown(false);
    }
    QToolButton::changeEvent(event);
}
