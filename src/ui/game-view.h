#ifndef GAME_VIEW_H
#define GAME_VIEW_H

#include <QEvent>
#include <QGraphicsView>
#include <QMargins>
#include <QPointer>
#include "build-features.h"
#include "room-layout-engine.h"

class RoomScene;
class RoomOverlayHost;
class RoomWindowPosture;
#if !QSAN_USE_RASTER_VIEWPORT
class GameViewGlFilter;
#endif

class FitView final : public QGraphicsView
{
public:
    explicit FitView(QGraphicsScene *scene = nullptr, QWidget *parent = nullptr);
    void setSafeAreaMargins(const QMargins &margins);
    void setStableSafeAreaMargins(const QMargins &margins);
    void setScene(QGraphicsScene *scene);
    void showPlayerInspector();
    void setResponsiveRoomEnabled(bool enabled);

    void setUiScale(qreal scale);
    void refit();
    void setBackgroundBrush(bool centerAsOrigin);
    // 灰階/高對比:牌桌是 QGraphicsView,吃不到 palette,要對畫面後製。
    void applyVisualMode();

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void fitCurrentScene(const QSize &viewportSize);
#if !QSAN_USE_RASTER_VIEWPORT
    GameViewGlFilter *m_glFilter = nullptr;
#endif
#if !defined(QSAN_XP_LEGACY)
    void ensureRoomOverlay(RoomScene *room);
    QPointer<RoomOverlayHost> m_overlay;
    QPointer<RoomScene> m_overlayRoom;
    RoomWindowPosture *m_posture = nullptr;
    RoomLayoutEngine::Profile m_previousProfile = RoomLayoutEngine::Profile::LegacyLandscape;
    bool m_hasPreviousProfile = false;
    bool m_responsiveEnabled = false;
    bool m_fitting = false;
#endif
    QMargins m_stableSafeMargins;

    qreal m_uiScale = 1.0;
};

#endif
