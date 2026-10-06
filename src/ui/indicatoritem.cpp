#include "indicatoritem.h"
#include "engine.h"
#include "skin-bank.h"
#include "effects/effects-policy.h"
#include "effects/effects-completion.h"

IndicatorItem::IndicatorItem(const QPointF &start, const QPointF &real_finish, Player *player)
    : start(start), finish(start), real_finish(real_finish)
{
	color = QColor(Sanguosha->getKingdomColor("red"));
    width = 7;
    if(player){
		color = QColor(Sanguosha->getKingdomColor(player->getKingdom()));
		if(player->isLord()) width = 8;
	}
	linePixmap = G_ROOM_SKIN.getSlotPixmap(QStringLiteral("indicator-line"), true);
}

void IndicatorItem::doAnimation()
{
    if (!G_EFFECTS.animationsEnabled()) {
        // The indicator line has no final state to preserve: finish immediately, but
        // always tear itself down via the event loop - it must not blow up in the caller's hands right after addItem put it into the scene.
        G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
        EffectsCompletion::completeNow(this, [this]() { deleteLater(); });
        return;
    }

    QSequentialAnimationGroup *group = new QSequentialAnimationGroup(this);

    QPropertyAnimation *animation = new QPropertyAnimation(this, "finish");
    animation->setEndValue(real_finish);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setDuration(G_EFFECTS.scaledDuration(500));

    QPropertyAnimation *pause = new QPropertyAnimation(this, "opacity");
    pause->setEndValue(0);
    pause->setEasingCurve(QEasingCurve::InQuart);
    pause->setDuration(G_EFFECTS.scaledDuration(600));

    group->addAnimation(animation);
    group->addAnimation(pause);

    G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
    group->start(QAbstractAnimation::DeleteWhenStopped);

    EffectsCompletion::whenFinished(group, this, [this]() { deleteLater(); });
}

QPointF IndicatorItem::getFinish() const
{
    return finish;
}

void IndicatorItem::setFinish(const QPointF &finish)
{
    this->finish = finish;
    update();
}

void IndicatorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);

    if (!linePixmap.isNull()) {
        // Lay the art along the full line and reveal it as the line grows.
        const QPointF from = mapFromScene(start);
        const QLineF line(from, mapFromScene(finish));
        const qreal fullLength = QLineF(start, real_finish).length();
        if (line.length() < 1 || fullLength < 1)
            return;
        const qreal height = linePixmap.height() / linePixmap.devicePixelRatio();
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->translate(from);
        painter->rotate(-line.angle());
        painter->setClipRect(QRectF(0, -height / 2, line.length(), height));
        painter->drawPixmap(QRectF(0, -height / 2, fullLength, height), linePixmap, QRectF(linePixmap.rect()));
        painter->restore();
        return;
    }

    QPen pen(color);
    pen.setWidthF(width);

    int baseX = qMin(start.x(), finish.x());
    int baseY = qMin(start.y(), finish.y());

    QLinearGradient linearGrad(start - QPoint(baseX, baseY), finish - QPoint(baseX, baseY));
    QColor start_color(255, 255, 255, 0);
    linearGrad.setColorAt(0, start_color);
    linearGrad.setColorAt(1, color.lighter());

    QBrush brush(linearGrad);
    pen.setBrush(brush);

    painter->setPen(pen);
    painter->drawLine(mapFromScene(start), mapFromScene(finish));

    QPen pen2(QColor(200, 200, 200, 30));
    pen2.setWidth(6);
    painter->setPen(pen2);
    painter->drawLine(mapFromScene(start), mapFromScene(finish));
}

QRectF IndicatorItem::boundingRect() const
{
    qreal width = qAbs(start.x() - real_finish.x());
    qreal height = qAbs(start.y() - real_finish.y());
    // Line art can be thicker than the drawn line; leave room for half of it on each side.
    const qreal margin = linePixmap.isNull() ? 2 : linePixmap.height() / linePixmap.devicePixelRatio() / 2 + 2;
    return QRectF(0, 0, width, height).adjusted(-margin, -margin, margin, margin);
}

