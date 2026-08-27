#include "game-view.h"

#include "roomscene.h"
#include "settings.h"
#include "skin-bank.h"
#include "startscene.h"

#include <QOpenGLWidget>
#include <QPainter>
#include <QPixmapCache>
#include <QResizeEvent>

FitView::FitView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setSceneRect(UiConfig.Rect);
    setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing
        | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignCenter);
    m_uiScale = qBound<qreal>(1.0, Config.UIScale, 2.0);
    QOpenGLWidget *glWidget = new QOpenGLWidget(this);
    glWidget->setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    setViewport(glWidget);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

void FitView::setUiScale(qreal scale)
{
    m_uiScale = qBound<qreal>(1.0, scale, 2.0);
    if (auto *roomScene = qobject_cast<RoomScene *>(scene()))
        roomScene->applyUiElementScale(m_uiScale);
}

void FitView::refit()
{
    fitCurrentScene(viewport()->size());
}

void FitView::setBackgroundBrush(bool centerAsOrigin)
{
    if (!scene())
        return;

    const QSize targetSize = viewport()->size();
    QTransform transform;
    if (centerAsOrigin)
        transform.translate(-targetSize.width() / 2.0, -targetSize.height() / 2.0);
    QPixmap source;
    const QString sourceKey = QStringLiteral("qsan-background:") + Config.BackgroundImage;
    if (!QPixmapCache::find(sourceKey, &source)) {
        source.load(Config.BackgroundImage);
        if (!source.isNull())
            QPixmapCache::insert(sourceKey, source);
    }
    QBrush brush(scaledPixmapForDevice(source, targetSize, devicePixelRatioF()));
    brush.setTransform(transform);
    scene()->setBackgroundBrush(brush);
}

void FitView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    fitCurrentScene(viewport()->size());
}

void FitView::fitCurrentScene(const QSize &viewportSize)
{
    if (!scene() || viewportSize.isEmpty())
        return;

    resetTransform();

    if (auto *roomScene = qobject_cast<RoomScene *>(scene())) {
        const QRectF newSceneRect(QPointF(0, 0), QSizeF(viewportSize));
        roomScene->setSceneRect(newSceneRect);
        roomScene->adjustItems();
        setSceneRect(roomScene->sceneRect());
        if (newSceneRect != roomScene->sceneRect())
            fitInView(roomScene->sceneRect(), Qt::KeepAspectRatio);
        roomScene->applyUiElementScale(m_uiScale);
        setBackgroundBrush(false);
        return;
    }

    if (auto *startScene = qobject_cast<StartScene *>(scene())) {
        const QRectF newSceneRect(-viewportSize.width() / 2.0,
            -viewportSize.height() / 2.0, viewportSize.width(), viewportSize.height());
        startScene->setSceneRect(newSceneRect);
        setSceneRect(startScene->sceneRect());
        if (newSceneRect != startScene->sceneRect())
            fitInView(startScene->sceneRect(), Qt::KeepAspectRatio);
    }
    setBackgroundBrush(true);
}
