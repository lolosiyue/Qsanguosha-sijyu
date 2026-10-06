#ifndef DESKTOP_GAME_PRESENTATION_H
#define DESKTOP_GAME_PRESENTATION_H

#include "game-action-model.h"
#include "game-event-stream.h"
#include "game-view-state.h"
#include "ui-action.h"
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
class TableButtonLegend;

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
    // Gamepad/remote entry point: the same groups and intents as handleTableKey,
    // plus seat-ring navigation, initial focus and clear-then-cancel on Back.
    bool handleUiAction(UiAction action);
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
    struct KeyboardGroup { QString kind; QList<GameActionEntry> entries; };
    bool tableInteractionSupported() const;
    QList<KeyboardGroup> keyboardGroups(bool *booleanPrompt) const;
    void locateCursor(const QList<KeyboardGroup> &groups, int *groupIndex, int *entryIndex) const;
    void submitCurrent(const QString &kind, const QString &id, bool selected);
    QGraphicsObject *playerItem(const QString &name) const;
    int initialEntry(const KeyboardGroup &group) const;
    void placeInitialFocus(const QList<KeyboardGroup> &groups);
    QString ringNeighbour(const QList<GameActionEntry> &players, const QString &from, UiAction action) const;
    bool clearDraftSelection();
    bool focusLayerActive() const;
    void updateFocusLayer();
    void updateLegend(const QList<KeyboardGroup> &groups, bool booleanPrompt);
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
    // Big-picture focus layer: TV focus ring, button legend, automatic focus.
    bool m_tvFocus = false;
    quint64 m_autoFocusRequest = 0;
    bool m_cursorFromAutoFocus = false;
    bool m_suppressAutoFocus = false;
    QPointer<TableButtonLegend> m_legend;
};

#endif
