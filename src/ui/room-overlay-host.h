#ifndef ROOM_OVERLAY_HOST_H
#define ROOM_OVERLAY_HOST_H

#include "room-layout-engine.h"
#include "game-action-model.h"
#include "game-view-state.h"

#include <QPointer>
#include <QWidget>

class DesktopGamePresentation;
class QLabel;
class QScrollBar;
class QToolButton;
class QVBoxLayout;

// Auxiliary views and seat paging only. Native Dashboard/Photo items own input.
class RoomOverlayHost final : public QWidget
{
    Q_OBJECT
public:
    explicit RoomOverlayHost(QWidget *parent = nullptr);

    void setPresentation(DesktopGamePresentation *presentation);
    void setLayoutResult(const RoomLayoutEngine::ResponsiveResult &layout);
    void setResponsiveEnabled(bool enabled);
    bool responsiveEnabled() const;
    bool logVisible() const;
    bool logShown() const;
    bool logToggleEnabled() const;
    void toggleLog();
    bool inspectorRequested() const;
    void inspectPlayer(const QString &player);
    RoomLayoutEngine::Handedness handedness() const;
    int firstVisibleSeat() const;

signals:
    void layoutPreferencesChanged();
    void responsiveEnabledChanged(bool enabled);
    // Native controls (seat scroller, inspector) in the parent's coordinates.
    void nativeRegionChanged(const QRegion &region);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void createPersistentUi();
    void updateFromPresentation(const GameViewState &view, const GameActionModel &actions);
    void updateGeometry();
    void updateMask();
    void updateInspector();
    void setInspectorOpen(bool open);

    QPointer<DesktopGamePresentation> m_presentation;
    RoomLayoutEngine::ResponsiveResult m_layout;
    RoomLayoutEngine::Handedness m_handedness = RoomLayoutEngine::Handedness::None;
    GameViewState m_view;
    bool m_responsiveEnabled = false;
    bool m_inspectorRequested = false;
    bool m_registeredLive = false;
    bool m_logVisible = false;
    QString m_inspectedPlayer;
    QScrollBar *m_nativeSeatScroll = nullptr;
    QWidget *m_inspector = nullptr;
    QVBoxLayout *m_inspectorLayout = nullptr;
};

#endif
