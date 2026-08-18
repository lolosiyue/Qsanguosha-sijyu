#include "game-snapshot-service.h"

#include "game-snapshot.h"
#include "room.h"

#include <QDir>

GameSnapshotService::GameSnapshotService(Room &room)
    : m_room(room), m_lastSnapshotTurn(0)
{
}

GameSnapshotService::~GameSnapshotService()
{
    foreach (GameSnapshot *snapshot, m_snapshots)
        delete snapshot;
}

void GameSnapshotService::saveSnapshot(const QString &type, const QString &playerName)
{
    if (m_replayPath.isEmpty())
        return;

    const int turnCount = m_room.getTag("TurnLengthCount").toInt();
    if (type == "turn" && turnCount == m_lastSnapshotTurn)
        return;

    GameSnapshot *snapshot = new GameSnapshot(&m_room);
    snapshot->setSnapshotType(type);
    snapshot->setReplayPath(m_replayPath);

    QString description;
    if (type == "turn") {
        description = QString("Turn %1").arg(turnCount);
        // 先標記回合，保留原本存檔失敗後同回合不重試的行為。
        m_lastSnapshotTurn = turnCount;
    } else if (type == "death") {
        description = QString("%1 died").arg(playerName);
    }
    snapshot->setDescription(description);

    const QString snapshotDir = getSnapshotDir();
    QDir dir;
    if (!dir.exists(snapshotDir))
        dir.mkpath(snapshotDir);

    const QString filename = GameSnapshot::generateSnapshotFilename(turnCount, type, playerName);
    const QString filepath = snapshotDir + "/" + filename;

    if (snapshot->save(filepath)) {
        m_snapshots.append(snapshot);
    } else {
        delete snapshot;
    }
}

GameSnapshot *GameSnapshotService::getSnapshot(int turnCount) const
{
    foreach (GameSnapshot *snapshot, m_snapshots) {
        if (snapshot->getTurnCount() == turnCount)
            return snapshot;
    }
    return nullptr;
}

QString GameSnapshotService::getSnapshotDir() const
{
    if (m_replayPath.isEmpty())
        return QString();
    return GameSnapshot::getSnapshotDir(m_replayPath);
}

void GameSnapshotService::setReplayPath(const QString &path)
{
    m_replayPath = path;
}

QString GameSnapshotService::getReplayPath() const
{
    return m_replayPath;
}
