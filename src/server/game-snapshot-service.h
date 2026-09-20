#ifndef GAME_SNAPSHOT_SERVICE_H
#define GAME_SNAPSHOT_SERVICE_H

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QSharedPointer>
#include <QString>
#include <QtGlobal>

class GameSnapshot;
class Room;

class GameSnapshotService
{
public:
    enum class StoragePolicy { Automatic, InMemory, DiskBacked };

    explicit GameSnapshotService(Room &room,
                                 StoragePolicy policy = StoragePolicy::Automatic);
    ~GameSnapshotService();

    void saveSnapshot(const QString &type, const QString &playerName = QString());
    QSharedPointer<GameSnapshot> getSnapshot(int turnCount) const;
    QSharedPointer<GameSnapshot> getSnapshotBySerial(quint64 turnSerial) const;
    quint64 getNextTurnSerial() const;
    QString getSnapshotDir() const;
    void setReplayPath(const QString &path);
    QString getReplayPath() const;
    QString getSessionId() const;
    bool finalizeManifest(const QString &replayPath, QString *error = nullptr) const;
    static bool copyFinalizedManifest(const QString &sourceManifestPath,
                                     const QString &replayPath, QString *error = nullptr);

private:
    friend struct GameSnapshotServiceTestAccess;

    struct SnapshotRecord {
        QSharedPointer<GameSnapshot> snapshot;
        QString path;
        QByteArray sha256;
        quint64 turnSerial = 0;
        int turnCount = 0;
        QString currentPlayer;
        int playerTurnCount = 0;
    };

    QSharedPointer<GameSnapshot> loadDiskSnapshot(const SnapshotRecord &record) const;
    bool publishSnapshot(const QSharedPointer<GameSnapshot> &snapshot,
                         const QString &path, QString *error = nullptr);

    Room &m_room;
    mutable QMutex m_snapshotMutex;
    QList<SnapshotRecord> m_snapshots;
    QString m_replayPath;
    QString m_sessionId;
    quint64 m_nextTurnSerial;
    StoragePolicy m_storagePolicy;
    mutable QSharedPointer<GameSnapshot> m_loadedSnapshot;
    mutable QString m_loadedSnapshotPath;
    mutable QByteArray m_loadedSnapshotHash;
};

#endif
