#include "pointer-hover-delivery.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>
#include <QHoverEvent>
#include <QtTest>

namespace {

class HoverProbe final : public QGraphicsRectItem
{
public:
    int enters = 0;

    HoverProbe()
    {
        setRect(0, 0, 80, 80);
        setPos(10, 10);
        setAcceptHoverEvents(true);
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        ++enters;
        QGraphicsRectItem::hoverEnterEvent(event);
    }
};

QGraphicsView *makeHoverView(QGraphicsScene *scene)
{
    auto *view = new QGraphicsView(scene);
    view->resize(200, 200);
    view->setSceneRect(0, 0, 200, 200);
    view->show();
    return view;
}

} // namespace

class PointerHoverDeliveryTest final : public QObject
{
    Q_OBJECT

private slots:
    void vanillaGraphicsViewIgnoresHoverMove()
    {
        QGraphicsScene scene;
        auto *item = new HoverProbe;
        scene.addItem(item);
        QScopedPointer<QGraphicsView> view(makeHoverView(&scene));
        QVERIFY(QTest::qWaitForWindowExposed(view.data()));

        const QPointF local = view->mapFromScene(item->mapToScene(item->rect().center()));
        QHoverEvent hover(QEvent::HoverMove, local, view->mapToGlobal(local.toPoint()),
            local - QPointF(4, 4), Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &hover);
        QCoreApplication::processEvents();

        QCOMPARE(item->enters, 0);
    }

    void forwardedHoverMoveEntersGraphicsItem()
    {
        QGraphicsScene scene;
        auto *item = new HoverProbe;
        scene.addItem(item);
        QScopedPointer<QGraphicsView> view(makeHoverView(&scene));
        qsanEnableWidgetPointerHover(view.data());
        qsanEnableWidgetPointerHover(view->viewport());
        QVERIFY(QTest::qWaitForWindowExposed(view.data()));

        const QPointF local = view->mapFromScene(item->mapToScene(item->rect().center()));
        QHoverEvent hover(QEvent::HoverMove, local, view->mapToGlobal(local.toPoint()),
            local - QPointF(4, 4), Qt::NoModifier);
        QVERIFY(qsanForwardPointerHoverAsMouseMove(view->viewport(), &hover));
        QCoreApplication::processEvents();

        QCOMPARE(item->enters, 1);
    }
};

QTEST_MAIN(PointerHoverDeliveryTest)
#include "pointer-hover-delivery-test.moc"
