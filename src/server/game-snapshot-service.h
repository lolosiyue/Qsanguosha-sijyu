#ifndef GAME_SNAPSHOT_SERVICE_H
#define GAME_SNAPSHOT_SERVICE_H

#include <QList>
#include <QString>

class GameSnapshot;
class Room;

class GameSnapshotService
{
public:
    explicit GameSnapshotService(Room &room);
    ~GameSnapshotService();

    void saveSnapshot(const QString &type, const QString &playerName);
    GameSnapshot *getSnapshot(int turnCount) const;
    QString getSnapshotDir() const;
    void setReplayPath(const QString &path);
    QString getReplayPath() const;

private:
    Room &m_room;
    QList<GameSnapshot *> m_snapshots;
    QString m_replayPath;
    int m_lastSnapshotTurn;
};

#endif
