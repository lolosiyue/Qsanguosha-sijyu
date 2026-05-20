#include "replay-timeline.h"
#include "replay-index.h"

#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QStyleOptionGraphicsItem>
#include <QCursor>

const qreal ReplayTimeline::SLIDER_HEIGHT = 8.0;
const qreal ReplayTimeline::SLIDER_WIDTH = 400.0;
const qreal ReplayTimeline::NODE_RADIUS = 6.0;
const qreal ReplayTimeline::NODE_GAP = 2.0;

ReplayTimeline::ReplayTimeline(QGraphicsItem *parent)
    : QGraphicsObject(parent), m_duration(0), m_currentTime(0),
      m_index(nullptr), m_dragging(false), m_hoveredNode(-1)
{
    setAcceptHoverEvents(true);
    setFlag(ItemIsSelectable);
}

void ReplayTimeline::setDuration(int secs)
{
    m_duration = secs;
    updateNodePositions();
    update();
}

void ReplayTimeline::setCurrentTime(int secs)
{
    m_currentTime = secs;
    update();
}

void ReplayTimeline::setNodes(const QList<ReplayNode> &nodes)
{
    m_nodes = nodes;
    updateNodePositions();
    update();
}

void ReplayTimeline::setIndex(ReplayIndex *index)
{
    m_index = index;
    if (m_index) {
        setNodes(m_index->getNodes());
    }
}

QRectF ReplayTimeline::boundingRect() const
{
    return QRectF(0, -NODE_RADIUS * 2, SLIDER_WIDTH, SLIDER_HEIGHT + NODE_RADIUS * 4);
}

void ReplayTimeline::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(60, 60, 60, 180));
    painter->drawRoundedRect(QRectF(0, 0, SLIDER_WIDTH, SLIDER_HEIGHT), 4, 4);

    qreal progressX = timeToPosition(m_currentTime);
    painter->setBrush(QColor(100, 150, 200, 200));
    painter->drawRoundedRect(QRectF(0, 0, progressX, SLIDER_HEIGHT), 4, 4);

    painter->setPen(QPen(QColor(200, 200, 200), 2));
    painter->drawLine(QPointF(progressX, -2), QPointF(progressX, SLIDER_HEIGHT + 2));

    for (int i = 0; i < m_nodeRects.size(); i++) {
        QRectF rect = m_nodeRects[i];
        bool isHovered = (i == m_hoveredNode);

        QColor nodeColor;
        if (m_nodes[i].type == ReplayNodeType::TurnStart) {
            nodeColor = isHovered ? QColor(100, 200, 100) : QColor(80, 160, 80);
        } else if (m_nodes[i].type == ReplayNodeType::PlayerDeath) {
            nodeColor = isHovered ? QColor(200, 100, 100) : QColor(160, 80, 80);
        } else {
            nodeColor = isHovered ? QColor(200, 200, 100) : QColor(160, 160, 80);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(nodeColor);
        painter->drawEllipse(rect.center(), NODE_RADIUS, NODE_RADIUS);

        if (isHovered) {
            painter->setPen(QPen(QColor(255, 255, 255), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(rect.center(), NODE_RADIUS + 2, NODE_RADIUS + 2);
        }
    }
}

int ReplayTimeline::getCurrentTime() const
{
    return m_currentTime;
}

int ReplayTimeline::getDuration() const
{
    return m_duration;
}

void ReplayTimeline::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    int nodeIndex = findNodeAtPosition(event->pos());
    if (nodeIndex >= 0) {
        emit nodeClicked(nodeIndex);
        return;
    }

    m_dragging = true;
    int newTime = positionToTime(event->pos().x());
    m_currentTime = qBound(0, newTime, m_duration);
    emit timeChanged(m_currentTime);
    update();
}

void ReplayTimeline::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_dragging) {
        int newTime = positionToTime(event->pos().x());
        m_currentTime = qBound(0, newTime, m_duration);
        emit timeChanged(m_currentTime);
        update();
    } else {
        int nodeIndex = findNodeAtPosition(event->pos());
        if (nodeIndex != m_hoveredNode) {
            m_hoveredNode = nodeIndex;
            update();
        }
    }
}

void ReplayTimeline::mouseReleaseEvent(QGraphicsSceneMouseEvent *)
{
    m_dragging = false;
}

void ReplayTimeline::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    int nodeIndex = findNodeAtPosition(event->pos());
    if (nodeIndex != m_hoveredNode) {
        m_hoveredNode = nodeIndex;
        if (nodeIndex >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        update();
    }
}

void ReplayTimeline::updateNodePositions()
{
    m_nodeRects.clear();

    if (m_duration <= 0 || m_nodes.isEmpty())
        return;

    for (int i = 0; i < m_nodes.size(); i++) {
        const ReplayNode &node = m_nodes[i];
        qreal x = timeToPosition(node.elapsed / 1000);
        qreal y = SLIDER_HEIGHT / 2;
        QRectF rect(x - NODE_RADIUS, y - NODE_RADIUS, NODE_RADIUS * 2, NODE_RADIUS * 2);
        m_nodeRects.append(rect);
    }
}

int ReplayTimeline::positionToTime(qreal x) const
{
    if (SLIDER_WIDTH <= 0)
        return 0;
    return static_cast<int>((x / SLIDER_WIDTH) * m_duration);
}

qreal ReplayTimeline::timeToPosition(int secs) const
{
    if (m_duration <= 0)
        return 0;
    return (static_cast<qreal>(secs) / m_duration) * SLIDER_WIDTH;
}

int ReplayTimeline::findNodeAtPosition(const QPointF &pos) const
{
    for (int i = 0; i < m_nodeRects.size(); i++) {
        QPointF center = m_nodeRects[i].center();
        qreal distance = QLineF(pos, center).length();
        if (distance <= NODE_RADIUS + NODE_GAP) {
            return i;
        }
    }
    return -1;
}