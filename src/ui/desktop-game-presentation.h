#ifndef DESKTOP_GAME_PRESENTATION_H
#define DESKTOP_GAME_PRESENTATION_H

#include "game-action-model.h"
#include "game-event-stream.h"
#include "game-view-state.h"
#include <QObject>
#include <QPointer>

class Client;
class RoomScene;
class GameControlPanel;
class GameTextSnapshotDialog;
class QAbstractButton;

// The Qt desktop/Android adapter projects existing RoomScene/Dashboard selections. It owns
// no second card draft, rule engine or interaction session.
class DesktopGamePresentation : public QObject
{
    Q_OBJECT
public:
    explicit DesktopGamePresentation(RoomScene *scene);
    ~DesktopGamePresentation() override;
    void showSnapshot();
    void showControls();

private:
    void scheduleRefresh();
    void refresh();
    GameActionModel actionModel() const;
    GameViewState viewState() const;
    QAbstractButton *optionButton(const QString &id) const;
    QString playerLabel(const QString &name) const;
    QString cardLabel(int id) const;
    void applyIntent(const QString &kind, const QString &id, bool selected,
                     quint64 generation, quint64 revision, quint64 requestId);
    RoomScene *m_scene;
    QPointer<Client> m_client;
    QPointer<GameControlPanel> m_panel;
    QPointer<GameTextSnapshotDialog> m_snapshot;
    GameActionModel m_model;
    GameEventStream m_events;
    QJsonObject m_lastState;
    bool m_stateDirty = true;
    quint64 m_revision = 0;
    quint64 m_draftRequest = 0;
    quint64 m_draftGeneration = 0;
    QString m_option;
    bool m_refreshPending = false;
};

#endif
