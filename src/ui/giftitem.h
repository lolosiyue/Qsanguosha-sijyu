#ifndef _GIFT_ITEM_H
#define _GIFT_ITEM_H

#include <QGraphicsObject>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QPainter>
#include <QPixmap>

class Player;

class GiftItem : public QGraphicsObject
{
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ getRotation WRITE setRotation)

public:
    GiftItem(const QPointF &start, const QPointF &real_finish, const QString &gift_type, Player *from);
    void doAnimation();

    qreal getRotation() const;
    void setRotation(qreal rotation);

    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    virtual QRectF boundingRect() const;

private:
    QPointF start, real_finish;
    QPixmap gift_pixmap;
    QString gift_type;
    qreal rotation_angle;
};

#endif