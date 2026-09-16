#ifndef ROOM_REPLAY_CONTROLLER_H
#define ROOM_REPLAY_CONTROLLER_H

#include <QGraphicsObject>
#include <QHash>
#include <QJsonObject>
#include <QString>

class Dashboard;
class QLabel;
class QMainWindow;
class QPushButton;
class ReplayTimeline;
class RoomScene;
#ifdef QSAN_XP_LEGACY
class LocalServerController;
#endif

class ReplayerControlBar : public QGraphicsObject
{
    Q_OBJECT

public:
    ReplayerControlBar(Dashboard *dashboard);
    static QString FormatTime(int secs);
    virtual QRectF boundingRect() const;
    void setExportInProgress(bool inProgress);

public slots:
    void setTime(int secs);
    void setSpeed(qreal speed);

signals:
    // MainWindow owns the transactional teardown/restart.  The control bar
    // only chooses a verified snapshot and a live seat key.
    void takeoverRequested(const QString &snapshotPath, const QString &seatObjectName);
    void exportRequested();

protected:
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    static const int S_BUTTON_GAP = 3;
    static const int S_BUTTON_WIDTH = 25;
    static const int S_BUTTON_HEIGHT = 21;

private:
    void requestTakeover();
    void updateTakeoverAvailability();

    QLabel *time_label;
    QPushButton *takeover_button;
    QPushButton *export_button;
    QString duration_str;
    qreal speed;
    bool export_in_progress;
};

// Owns everything replay: the playback control bar and the seek timeline a
// replay session builds, the bug diagnostic bundle export, and the record an
// ordinary game writes when it ends.  RoomScene holds only a pointer and the
// few calls below.
class RoomReplayController : public QObject
{
    Q_OBJECT

public:
    RoomReplayController(RoomScene *scene, QMainWindow *mainWindow);
    ~RoomReplayController() override;

    // Builds the control bar and the seek timeline.  Only a replay session
    // calls it; an ordinary game leaves both null.
    void createPlaybackUi(Dashboard *dashboard);

    // True once createPlaybackUi() has built the control bar.  Skill buttons
    // stay inert while a replay is being watched.
    bool isPlayingBack() const { return m_replayControl != nullptr; }

    // Record writing.  Both are no-ops of their own accord during playback.
    void saveReplayRecord();
    void recorderAutoSave();

signals:
    // Forwarded from the control bar; MainWindow owns the teardown/restart.
    void takeoverRequested(const QString &snapshotPath, const QString &seatObjectName);

private:
    void createReplayControlBar(Dashboard *dashboard);
    void createReplayTimeline();
    void exportReplayDiagnosticBundle();
    void onReplayStateCaptureReady(quint64 requestId,
        const QJsonObject &clientCore, int lastAppliedPairIndex,
        qint64 elapsedMs);
    void finishReplayDiagnosticExport(const QJsonObject &stateNow,
        bool includeStateNow, const QString &stateNowOmission);
    void setReplayExportInProgress(bool inProgress);
    void updateReplayTimeline(int secs);
    void onReplayTimelineTimeChanged(int secs);
    void onReplayTimelineNodeClicked(int nodeIndex);
#ifdef QSAN_XP_LEGACY
    LocalServerController *localReplayController() const;
    void finalizeLocalReplay(const QString &filename, bool reportFailure);
#endif

    RoomScene *m_scene;
    QMainWindow *m_mainWindow;

    ReplayerControlBar *m_replayControl;
    ReplayTimeline *m_replayTimeline;
    QString m_pendingReplayBundlePath;
    quint64 m_pendingReplayCaptureId = 0;
    bool m_replayExportInProgress = false;
#ifdef QSAN_XP_LEGACY
    QString m_xpReplayGeneration;
    QHash<QString, bool> m_xpReplayExports;
#endif
};

#endif // ROOM_REPLAY_CONTROLLER_H
