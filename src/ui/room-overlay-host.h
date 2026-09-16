#ifndef ROOM_OVERLAY_HOST_H
#define ROOM_OVERLAY_HOST_H

#include "room-layout-engine.h"
#include "game-action-model.h"
#include "game-view-state.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class DesktopGamePresentation;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QTextDocument;
class QToolButton;
class QAbstractButton;
class QHBoxLayout;
class QVBoxLayout;

// A viewport child which paints only the responsive controls and inspector.
// Selection and rules remain owned by DesktopGamePresentation/RoomScene.
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
    void setDocuments(QTextDocument *log, QTextDocument *chat, QLineEdit *draft);
    RoomLayoutEngine::Handedness handedness() const;

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
    void putEntryButtons(QVBoxLayout *layout, const QString &kind,
                         const QList<GameActionEntry> &entries);
    QToolButton *entryButton(const QString &key, const QString &label,
                             const QString &kind, const QString &id, bool selected,
                             bool enabled, int votes = 0, int maxVotes = 0);
    void submit(const QString &kind, const QString &id, bool selected);
    void submitCaptured(const QString &kind, const QString &id, bool selected,
                        quint64 generation, quint64 revision, quint64 requestId);
    void bindIntentButton(QAbstractButton *button, const QString &kind, const QString &id,
                          bool selectedOnClick = false);
    void setInspectorOpen(bool open);
    void saveHandedness(RoomLayoutEngine::Handedness value);

    QPointer<DesktopGamePresentation> m_presentation;
    RoomLayoutEngine::ResponsiveResult m_layout;
    RoomLayoutEngine::Handedness m_handedness = RoomLayoutEngine::Handedness::None;
    GameViewState m_view;
    GameActionModel m_actions;
    QPointer<QTextDocument> m_logDocument;
    QPointer<QTextDocument> m_chatDocument;
    QPointer<QLineEdit> m_legacyDraft;
    bool m_responsiveEnabled = false;
    bool m_inspectorRequested = false;
    bool m_registeredLive = false;
    bool m_logVisible = false;
    bool m_chatVisible = false;
    QString m_inspectedPlayer;
    quint64 m_generation = 0;
    quint64 m_revision = 0;
    quint64 m_requestId = 0;

    QToolButton *m_launcher = nullptr;
    QWidget *m_interaction = nullptr;
    QWidget *m_interactionFooter = nullptr;
    QWidget *m_contentWidget = nullptr;
    QScrollArea *m_contentScroll = nullptr;
    QVBoxLayout *m_interactionLayout = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QWidget *m_handPanel = nullptr;
    QLabel *m_promptLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_reasonLabel = nullptr;
    QPushButton *m_arrangeButton = nullptr;
    QHBoxLayout *m_handLayout = nullptr;
    QScrollArea *m_seatsScroll = nullptr;
    QScrollArea *m_actionsScroll = nullptr;
    QScrollArea *m_handScroll = nullptr;
    QScrollArea *m_ribbonScroll = nullptr;
    QWidget *m_seatRibbon = nullptr;
    QWidget *m_actionsPanel = nullptr;
    QWidget *m_inspector = nullptr;
    QWidget *m_logPanel = nullptr;
    QWidget *m_chatPanel = nullptr;
    QToolButton *m_logClose = nullptr;
    QToolButton *m_chatClose = nullptr;
    QTextBrowser *m_logView = nullptr;
    QTextBrowser *m_chatView = nullptr;
    QLineEdit *m_chatDraft = nullptr;
    QVBoxLayout *m_actionsLayout = nullptr;
    QWidget *m_actionsSpacer = nullptr;
    QVBoxLayout *m_seatsLayout = nullptr;
    QHBoxLayout *m_ribbonLayout = nullptr;
    QVBoxLayout *m_inspectorLayout = nullptr;
    QList<QPushButton *> m_footerButtons;
    QHash<QString, QToolButton *> m_entryButtons;
    QHash<QString, QWidget *> m_playerRows;
    QHash<QString, QWidget *> m_ribbonButtons;
};

#endif
