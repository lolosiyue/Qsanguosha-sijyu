#ifndef GAME_VIEW_H
#define GAME_VIEW_H

#include <QEvent>
#include <QGraphicsView>
#include <QMargins>
#include <QPointer>
#include "room-layout-engine.h"

class RoomScene;
class RoomOverlayHost;
class RoomWindowPosture;

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

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    void fitCurrentScene(const QSize &viewportSize);
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
