#ifndef DESKTOP_GAME_PRESENTATION_H
#define DESKTOP_GAME_PRESENTATION_H

#include "game-action-model.h"
#include "game-event-stream.h"
#include "game-view-state.h"
#include <QObject>
#include <QPointer>
#include <QHash>

class Client;
class RoomScene;
class GameControlPanel;
class GameTextSnapshotDialog;
class QAbstractButton;
class QKeyEvent;
class QGraphicsObject;

// The Qt desktop/Android adapter projects existing RoomScene/Dashboard selections. It owns
// no second card draft, rule engine or interaction session.
class DesktopGamePresentation : public QObject
{
    Q_OBJECT
public:
    explicit DesktopGamePresentation(RoomScene *scene);
    ~DesktopGamePresentation() override;
    // Register responsive views that need live state; the receiver is tracked by QObject lifetime.
    void setLiveConsumer(QObject *consumer, bool live);
    void requestRefresh();
    // Intents are queued, then revalidated against the current generation, revision and request.
    void submitIntent(const QString &kind, const QString &id, bool selected,
                      quint64 generation, quint64 revision, quint64 requestId);
    void showSnapshot();
    void showControls();
    // Native table navigation uses the same draft/intents without opening a panel.
    bool handleTableKey(QKeyEvent *event);
    void clearKeyboardCursor();

signals:
    void presentationChanged(const GameViewState &view, const GameActionModel &actions);

private:
    void scheduleRefresh();
    void refresh();
    GameActionModel actionModel() const;
    GameViewState viewState() const;
    QAbstractButton *optionButton(const QString &id) const;
    QString playerLabel(const QString &name) const;
    QString cardLabel(int id) const;
    void updateKeyboardCursor();
    void applyIntent(const QString &kind, const QString &id, bool selected,
                     quint64 generation, quint64 revision, quint64 requestId);
    RoomScene *m_scene;
    QPointer<Client> m_client;
    QPointer<GameControlPanel> m_panel;
    QPointer<GameTextSnapshotDialog> m_snapshot;
    GameActionModel m_model;
    GameEventStream m_events;
    QJsonObject m_lastActions;
    QString m_lastPrompt;
    bool m_stateDirty = true;
    bool m_viewDirty = true;
    GameViewState m_cachedView;
    quint64 m_revision = 0;
    quint64 m_draftRequest = 0;
    quint64 m_draftGeneration = 0;
    QString m_option;
    bool m_refreshPending = false;
    QHash<QObject *, QMetaObject::Connection> m_liveConsumers;
    quint64 m_lastPublishedRevision = 0;
    bool m_forcePresentation = false;
    QString m_keyboardKind;
    QString m_keyboardId;
    QPointer<QGraphicsObject> m_keyboardMarker;
};

#endif
