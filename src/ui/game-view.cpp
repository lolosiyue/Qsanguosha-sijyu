#include "game-view.h"

#include "build-features.h"
#include "roomscene.h"
#include "settings.h"
#include "skin-bank.h"
#include "startscene.h"
#if !defined(QSAN_XP_LEGACY)
#include "room-overlay-host.h"
#include "room-window-posture.h"
#endif

#if !QSAN_USE_RASTER_VIEWPORT
#include <QOpenGLWidget>
#endif
#include <QPainter>
#include <QPixmapCache>
#include <QResizeEvent>
#include <QTimer>
#include <QApplication>
#include <QKeyEvent>

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
#if !QSAN_USE_RASTER_VIEWPORT
    QOpenGLWidget *glWidget = new QOpenGLWidget(this);
    glWidget->setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    setViewport(glWidget);
#endif
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
#if !defined(QSAN_XP_LEGACY)
    // Main window and diagnostic FitViews share one Android listener. Destroying
    // a secondary view must not detach the main game's posture subscription.
    static QPointer<RoomWindowPosture> sharedPosture;
    if (!sharedPosture) {
        sharedPosture = new RoomWindowPosture(qApp);
        sharedPosture->setObjectName(QStringLiteral("roomWindowPosture"));
    }
    m_posture = sharedPosture;
    m_responsiveEnabled = Config.responsiveUiEnabled();
    m_posture->setResponsivePreview(m_responsiveEnabled);
    connect(&Config, &Settings::uiLayoutChanged, this, [this] {
        setResponsiveRoomEnabled(Config.responsiveUiEnabled());
    });
    connect(m_posture, &RoomWindowPosture::postureChanged, this, [this]() { refit(); });
#endif
}

bool FitView::event(QEvent *event)
{
    // QWidget consumes Tab before keyPressEvent. Route native gameplay keys
    // here so the table stays operable without opening the widget action panel.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        if (auto *room = qobject_cast<RoomScene *>(scene()))
            if (room->handleNativeKey(static_cast<QKeyEvent *>(event))) return true;
    }
    return QGraphicsView::event(event);
}

void FitView::setScene(QGraphicsScene *next)
{
    QGraphicsView::setScene(next);
#if !defined(QSAN_XP_LEGACY)
    if (!qobject_cast<RoomScene *>(next)) {
        // Rotation is an application preference; returning home keeps it enabled.
        if (m_posture) m_posture->setResponsivePreview(m_responsiveEnabled);
        delete m_overlay;
        m_overlay = nullptr;
        m_overlayRoom = nullptr;
        m_hasPreviousProfile = false;
    }
#endif
    refit();
}

void FitView::setStableSafeAreaMargins(const QMargins &margins)
{
    if (m_stableSafeMargins == margins)
        return;
    m_stableSafeMargins = margins;
    QTimer::singleShot(0, this, &FitView::refit);
}

void FitView::showPlayerInspector()
{
#if !defined(QSAN_XP_LEGACY)
    if (auto *room = qobject_cast<RoomScene *>(scene())) {
        ensureRoomOverlay(room);
        m_overlay->inspectPlayer(QString());
    }
#endif
}

void FitView::setResponsiveRoomEnabled(bool enabled)
{
#if !defined(QSAN_XP_LEGACY)
    m_responsiveEnabled = enabled;
    Config.setResponsiveUiEnabled(enabled);
    if (m_posture) m_posture->setResponsivePreview(enabled);
    m_hasPreviousProfile = false;
    if (m_overlay && m_overlay->responsiveEnabled() != enabled)
        m_overlay->setResponsiveEnabled(enabled);
    refit();
#else
    Q_UNUSED(enabled);
#endif
}

#if !defined(QSAN_XP_LEGACY)
void FitView::ensureRoomOverlay(RoomScene *room)
{
    if (m_overlay && m_overlayRoom == room)
        return;
    delete m_overlay;
    m_hasPreviousProfile = false;
    m_overlayRoom = room;
    m_overlay = new RoomOverlayHost(viewport());
    if (m_posture) m_posture->setResponsivePreview(m_responsiveEnabled);
    m_overlay->setResponsiveEnabled(m_responsiveEnabled);
    room->attachOverlay(m_overlay);
    connect(room, &RoomScene::seatCountChanged, this, &FitView::refit, Qt::QueuedConnection);
    connect(m_overlay, &RoomOverlayHost::responsiveEnabledChanged, this,
            [this](bool enabled) { setResponsiveRoomEnabled(enabled); });
    connect(m_overlay, &RoomOverlayHost::layoutPreferencesChanged, this, [this]() { refit(); });
    connect(room, &RoomScene::responsiveGeometryChanged, m_overlay, [this, room]() {
        if (m_overlay && (m_responsiveEnabled || room->largeRoomRequired()))
            m_overlay->setLayoutResult(room->responsiveLayout());
    });
    connect(room, &QObject::destroyed, m_overlay, &QObject::deleteLater);
    m_overlay->show();
}
#endif

void FitView::setSafeAreaMargins(const QMargins &margins)
{
    if (viewportMargins() == margins)
        return;
    // These margins already include system/keyboard occlusion. RoomLayoutEngine
    // receives the remaining viewport, so it must not subtract the insets again.
    setViewportMargins(margins);
    refit();
}

void FitView::setUiScale(qreal scale)
{
    m_uiScale = qBound<qreal>(1.0, scale, 2.0);
#if !defined(QSAN_XP_LEGACY)
    if (m_responsiveEnabled) {
        refit();
        return;
    }
#endif
    if (auto *roomScene = qobject_cast<RoomScene *>(scene())) {
        roomScene->applyUiElementScale(m_uiScale);
        roomScene->refreshTouchTargets(transform().m11());
    }
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
    const bool portrait = Config.responsiveUiEnabled() && targetSize.height() > targetSize.width();
    const QString path = portrait ? Config.value(QStringLiteral("UI/PortraitBackgroundImage"),
        QStringLiteral("image/system/portrait/portrait-background.svg")).toString() : Config.BackgroundImage;
    const QString sourceKey = QStringLiteral("qsan-background:") + path;
    if (!QPixmapCache::find(sourceKey, &source)) {
        source.load(path);
        if (source.isNull() && portrait)
            source.load(QStringLiteral("image/system/portrait/portrait-background.svg"));
        if (!source.isNull())
            QPixmapCache::insert(sourceKey, source);
    }
    if (portrait && source.isNull()) {
        scene()->setBackgroundBrush(QColor(QStringLiteral("#344f65")));
        return;
    }
    if (portrait && !source.isNull()) {
        source = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        source = source.copy((source.width() - targetSize.width()) / 2,
            (source.height() - targetSize.height()) / 2, targetSize.width(), targetSize.height());
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
#if !defined(QSAN_XP_LEGACY)
        if (m_fitting)
            return;
        m_fitting = true;
        ensureRoomOverlay(roomScene);
        m_overlay->setGeometry(viewport()->rect());
        RoomLayoutEngine::ResponsiveInput input;
        input.availableRect = QRectF(QPointF(0, 0), QSizeF(viewportSize));
        // The profile basis excludes system insets but not the IME. Both rects
        // share viewport-local coordinates, so a keyboard cannot select a new profile.
        const QMargins actual = viewportMargins();
        input.stableRect = QRectF(QPointF(0, 0), QSizeF(
            viewportSize.width() + actual.left() + actual.right()
                - m_stableSafeMargins.left() - m_stableSafeMargins.right(),
            viewportSize.height() + actual.top() + actual.bottom()
                - m_stableSafeMargins.top() - m_stableSafeMargins.bottom()));
        input.headerHeight = 56.0;
        input.previousProfile = m_previousProfile;
        input.hasPreviousProfile = m_hasPreviousProfile;
        input.inspectorRequested = m_overlay->inspectorRequested();
        input.logVisible = m_overlay->logVisible();
        input.chatVisible = m_overlay->chatVisible();
        input.handedness = m_overlay->handedness();
        input.firstVisibleSeat = m_overlay->firstVisibleSeat();
        if (m_posture) {
            const auto posture = m_posture->value();
            input.fold.posture = posture.mode == RoomWindowPosture::Mode::Book
                ? RoomLayoutEngine::FoldPosture::Book
                : posture.mode == RoomWindowPosture::Mode::Tabletop
                ? RoomLayoutEngine::FoldPosture::Tabletop : RoomLayoutEngine::FoldPosture::None;
            input.fold.bounds = posture.bounds.translated(-viewport()->mapTo(window(), QPoint(0, 0)));
            // WindowManager reports full-window folds, including system insets.
            // Clip to the safe viewport while retaining a zero-width crease.
            const QRectF hinge = input.fold.bounds;
            if (hinge.right() >= input.stableRect.left() && hinge.left() <= input.stableRect.right()
                && hinge.bottom() >= input.stableRect.top() && hinge.top() <= input.stableRect.bottom()) {
                const qreal left = qMax(hinge.left(), input.stableRect.left());
                const qreal top = qMax(hinge.top(), input.stableRect.top());
                input.fold.bounds = QRectF(left, top,
                    qMax<qreal>(0.0, qMin(hinge.right(), input.stableRect.right()) - left),
                    qMax<qreal>(0.0, qMin(hinge.bottom(), input.stableRect.bottom()) - top));
            }
            input.fold.separating = posture.separating;
            input.fold.occluding = posture.occluding;
        }
        // Ordinary landscape restores the original GUI, even after portrait rotation.
        bool largeRoom = false;
#if !defined(Q_OS_ANDROID)
        largeRoom = roomScene->largeRoomRequired();
#endif
        input.largeRoom = largeRoom;
        const bool responsiveRoom = largeRoom || (m_responsiveEnabled
            && (input.stableRect.height() > input.stableRect.width()
                || input.fold.posture != RoomLayoutEngine::FoldPosture::None
                || input.fold.separating || input.fold.occluding));
        roomScene->setResponsiveLayout(input, responsiveRoom);
#endif
        const QRectF newSceneRect(QPointF(0, 0), QSizeF(viewportSize));
        roomScene->adjustItems(QSizeF(viewportSize));
        setSceneRect(roomScene->sceneRect());
        if (newSceneRect != roomScene->sceneRect())
            fitInView(roomScene->sceneRect(), Qt::KeepAspectRatio);
        roomScene->applyUiElementScale(m_uiScale);
        roomScene->refreshTouchTargets(transform().m11());
        setBackgroundBrush(false);
#if !defined(QSAN_XP_LEGACY)
        if (responsiveRoom) {
            const auto &layout = roomScene->responsiveLayout();
            m_previousProfile = layout.profile;
            m_hasPreviousProfile = layout.valid;
            m_overlay->setLayoutResult(layout);
        } else {
            // Legacy table keeps its exact geometry; Inspector is an on-demand drawer.
            RoomLayoutEngine::ResponsiveResult overlayLayout;
            overlayLayout.valid = true;
            overlayLayout.mainRect = newSceneRect;
            overlayLayout.profile = RoomLayoutEngine::Profile::LegacyLandscape;
            if (m_overlay->inspectorRequested()) {
                const qreal width = qMin<qreal>(360.0, viewportSize.width());
                overlayLayout.inspectorRect = QRectF(viewportSize.width() - width, 0,
                                                      width, viewportSize.height());
            }
            m_overlay->setLayoutResult(overlayLayout);
        }
        m_overlay->raise();
        m_fitting = false;
#endif
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
