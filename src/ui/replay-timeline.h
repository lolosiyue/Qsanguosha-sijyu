#ifndef _REPLAY_TIMELINE_H
#define _REPLAY_TIMELINE_H

#include <QGraphicsObject>
#include <QList>
#include <QRectF>

class ReplayNode;
class ReplayIndex;

class ReplayTimeline : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit ReplayTimeline(QGraphicsItem *parent = nullptr);

    void setDuration(int secs);
    void setCurrentTime(int secs);
    void setNodes(const QList<ReplayNode> &nodes);
    void setIndex(ReplayIndex *index);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    int getCurrentTime() const;
    int getDuration() const;

signals:
    void timeChanged(int secs);
    void nodeClicked(int nodeIndex);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    void updateNodePositions();
    int positionToTime(qreal x) const;
    qreal timeToPosition(int secs) const;
    int findNodeAtPosition(const QPointF &pos) const;

    int m_duration;
    int m_currentTime;
    QList<ReplayNode> m_nodes;
    QList<QRectF> m_nodeRects;
    ReplayIndex *m_index;

    bool m_dragging;
    int m_hoveredNode;

    static const qreal SLIDER_HEIGHT;
    static const qreal SLIDER_WIDTH;
    static const qreal NODE_RADIUS;
    static const qreal NODE_GAP;
};

#endif