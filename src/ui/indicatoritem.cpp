#include "indicatoritem.h"
#include "engine.h"
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
}

void IndicatorItem::doAnimation()
{
    if (!G_EFFECTS.animationsEnabled()) {
        // 指示線冇最終狀態要保留:即刻收工,但一定經 event loop 拆自己,
        // 唔可以喺 caller 手上炸咗個啱啱 addItem 落 scene 嘅 item。
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
    return QRectF(0, 0, width, height).adjusted(-2, -2, 2, 2);
}

