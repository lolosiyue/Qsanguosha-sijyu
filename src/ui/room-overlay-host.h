#ifndef ROOM_OVERLAY_HOST_H
#define ROOM_OVERLAY_HOST_H

#include "room-layout-engine.h"
#include "game-action-model.h"
#include "game-view-state.h"

#include <QPointer>
#include <QWidget>

class DesktopGamePresentation;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollBar;
class QTextBrowser;
class QTextDocument;
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
    bool chatVisible() const;
    bool inspectorRequested() const;
    void inspectPlayer(const QString &player);
    void setChatDocument(QTextDocument *chat, QLineEdit *draft);
    RoomLayoutEngine::Handedness handedness() const;
    int firstVisibleSeat() const;

signals:
    void layoutPreferencesChanged();
    void responsiveEnabledChanged(bool enabled);
    void sendChatRequested();
    void controlsRequested();

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
    void saveHandedness(RoomLayoutEngine::Handedness value);

    QPointer<DesktopGamePresentation> m_presentation;
    RoomLayoutEngine::ResponsiveResult m_layout;
    RoomLayoutEngine::Handedness m_handedness = RoomLayoutEngine::Handedness::None;
    GameViewState m_view;
    QPointer<QTextDocument> m_chatDocument;
    QPointer<QLineEdit> m_legacyDraft;
    bool m_responsiveEnabled = false;
    bool m_inspectorRequested = false;
    bool m_registeredLive = false;
    bool m_logVisible = false;
    bool m_chatVisible = false;
    QString m_inspectedPlayer;
    QToolButton *m_launcher = nullptr;
    QScrollBar *m_nativeSeatScroll = nullptr;
    QWidget *m_inspector = nullptr;
    QWidget *m_chatPanel = nullptr;
    QToolButton *m_chatClose = nullptr;
    QTextBrowser *m_chatView = nullptr;
    QLineEdit *m_chatDraft = nullptr;
    QVBoxLayout *m_inspectorLayout = nullptr;
};

#endif
