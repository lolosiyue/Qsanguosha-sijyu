#ifndef FLOATINGBALL_H
#define FLOATINGBALL_H

#include <QToolButton>
#include <QPointF>

class QAction;
class QFrame;
class QGridLayout;
class QScrollArea;

// Android L2 launcher, adapted from TODO/human without duplicating game actions.
class FloatingBall final : public QToolButton
{
public:
    explicit FloatingBall(QWidget *parent);
    void addPanelAction(QAction *action, QAction *availability = nullptr);
    void setAvailableGeometry(const QRect &rect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void togglePanel();
    QPoint boundedPosition(const QPoint &point) const;

    QFrame *m_panel;
    QScrollArea *m_scroll;
    QGridLayout *m_grid;
    QRect m_available;
    QPointF m_relativePosition = QPointF(1.0, 0.0);
    QPoint m_pressGlobal;
    QPoint m_pressPosition;
    bool m_pressed = false;
    bool m_dragging = false;
    int m_actionCount = 0;
};

#endif
