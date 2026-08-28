#include "giftitem.h"
#include "engine.h"
#include "effects/effects-policy.h"
#include "effects/effects-completion.h"
#include <QParallelAnimationGroup>
#include <QPainter>
#include <QBrush>
#include <QPen>

GiftItem::GiftItem(const QPointF &start, const QPointF &real_finish, const QString &gift_type, Player *from)
    : start(start), real_finish(real_finish), gift_type(gift_type), rotation_angle(0)
{
    QString image_path = QString("image/system/animation/%1.png").arg(gift_type);
    gift_pixmap = QPixmap(image_path);

    if (gift_pixmap.isNull()) {
        gift_pixmap = QPixmap(64, 64);
        gift_pixmap.fill(Qt::transparent);

        QPainter painter(&gift_pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        if (gift_type == "flower") {
            painter.setBrush(QBrush(QColor(255, 105, 180)));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(20, 8, 24, 24);
            painter.drawEllipse(32, 20, 24, 24);
            painter.drawEllipse(20, 32, 24, 24);
            painter.drawEllipse(8, 20, 24, 24);
            painter.setBrush(QBrush(QColor(255, 215, 0)));
            painter.drawEllipse(24, 24, 16, 16);
            painter.setBrush(QBrush(QColor(34, 139, 34)));
            painter.drawRect(30, 40, 4, 20);
        } else {
            painter.setBrush(QBrush(QColor(255, 248, 220)));
            painter.setPen(QPen(QColor(245, 222, 179), 3));
            painter.drawEllipse(12, 8, 40, 48);
            painter.setBrush(QBrush(QColor(255, 255, 255, 180)));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(18, 16, 12, 16);
        }
    }
}

void GiftItem::doAnimation()
{
    if (!G_EFFECTS.animationsEnabled()) {
        G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
        EffectsCompletion::completeNow(this, [this]() { deleteLater(); });
        return;
    }

    QParallelAnimationGroup *group = new QParallelAnimationGroup(this);

    QPropertyAnimation *move_animation = new QPropertyAnimation(this, "pos");
    move_animation->setStartValue(start);
    move_animation->setEndValue(real_finish);
    move_animation->setEasingCurve(QEasingCurve::OutCubic);
    move_animation->setDuration(G_EFFECTS.scaledDuration(2000));

    QPropertyAnimation *rotation_animation = new QPropertyAnimation(this, "rotation");
    rotation_animation->setStartValue(0);
    rotation_animation->setEndValue(720);
    rotation_animation->setDuration(G_EFFECTS.scaledDuration(2000));

    QPropertyAnimation *fade_animation = new QPropertyAnimation(this, "opacity");
    fade_animation->setStartValue(1.0);
    fade_animation->setKeyValueAt(0.85, 1.0);
    fade_animation->setEndValue(0.0);
    fade_animation->setDuration(G_EFFECTS.scaledDuration(2000));

    group->addAnimation(move_animation);
    group->addAnimation(rotation_animation);
    group->addAnimation(fade_animation);

    G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
    group->start(QAbstractAnimation::DeleteWhenStopped);
    EffectsCompletion::whenFinished(group, this, [this]() { deleteLater(); });
}

qreal GiftItem::getRotation() const
{
    return rotation_angle;
}

void GiftItem::setRotation(qreal rotation)
{
    rotation_angle = rotation;
    update();
}

QRectF GiftItem::boundingRect() const
{
    if (gift_pixmap.isNull()) {
        return QRectF(0, 0, 64, 64);
    }
    return QRectF(0, 0, gift_pixmap.width(), gift_pixmap.height());
}

void GiftItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);

    if (!gift_pixmap.isNull()) {
        QRectF rect = boundingRect();
        QPointF center = rect.center();

        painter->save();
        painter->translate(center);
        painter->rotate(rotation_angle);
        painter->translate(-center);

        painter->drawPixmap(rect.toRect(), gift_pixmap);

        painter->restore();
    }
}