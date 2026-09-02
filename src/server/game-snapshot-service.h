#ifndef GAME_SNAPSHOT_SERVICE_H
#define GAME_SNAPSHOT_SERVICE_H

#include <QList>
#include <QString>
#include <QtGlobal>

class GameSnapshot;
class Room;

class GameSnapshotService
{
public:
    explicit GameSnapshotService(Room &room);
    ~GameSnapshotService();

    // A turn snapshot is captured only by RoomThread immediately before the
    // top-level TurnStart dispatch.  Other callers are intentionally ignored.
    void saveSnapshot(const QString &type, const QString &playerName = QString());
    GameSnapshot *getSnapshot(int turnCount) const;
    GameSnapshot *getSnapshotBySerial(quint64 turnSerial) const;
    quint64 getNextTurnSerial() const;
    QString getSnapshotDir() const;
    void setReplayPath(const QString &path);
    QString getReplayPath() const;
    QString getSessionId() const;

    // Called after Recorder has atomically written the replay.  The manifest
    // binds that replay and every eligible snapshot by SHA-256.
    bool finalizeManifest(const QString &replayPath, QString *error = nullptr) const;

private:
    Room &m_room;
    QList<GameSnapshot *> m_snapshots;
    QString m_replayPath;
    QString m_sessionId;
    quint64 m_nextTurnSerial;
};

#endif
