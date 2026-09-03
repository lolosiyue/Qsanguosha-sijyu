#include "game-snapshot-service.h"

#include "game-snapshot.h"
#include "room.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtAlgorithms>
#include <QUuid>

GameSnapshotService::GameSnapshotService(Room &room)
    : m_room(room), m_nextTurnSerial(0)
{
}

GameSnapshotService::~GameSnapshotService()
{
    qDeleteAll(m_snapshots);
}

void GameSnapshotService::saveSnapshot(const QString &type, const QString &playerName)
{
    Q_UNUSED(playerName);

    // Only RoomThread::actionNormal may request a resumable node. Death
    // snapshots and arbitrary calls belonged to the old what-if mechanism.
    if (type != QStringLiteral("turn") || m_replayPath.isEmpty())
        return;

    const quint64 turnSerial = ++m_nextTurnSerial;

    // GameSnapshot v2 reads this scoped tag into its explicit turnSerial field.
    // Do not leave the implementation detail in the live room tags.
    m_room.setTag(QStringLiteral("ReplaySnapshotTurnSerial"),
                  QString::number(turnSerial));
    GameSnapshot *snapshot = new GameSnapshot(&m_room);
    m_room.removeTag(QStringLiteral("ReplaySnapshotTurnSerial"));
    snapshot->setTurnSerial(turnSerial);
    snapshot->setSnapshotType(type);
    snapshot->setReplayPath(m_replayPath);
    snapshot->setDescription(QStringLiteral("Turn %1").arg(turnSerial));

    const QString snapshotDir = getSnapshotDir();
    QDir dir;
    if (!dir.exists(snapshotDir) && !dir.mkpath(snapshotDir)) {
        qWarning() << "GameSnapshotService: failed to create snapshot directory"
                   << snapshotDir;
        delete snapshot;
        return;
    }

    const QString filename = GameSnapshot::generateSnapshotFilename(
        static_cast<int>(turnSerial), QStringLiteral("turn"));
    const QString filepath = snapshotDir + QStringLiteral("/") + filename;

    // GameSnapshot owns the atomic QSaveFile write. A failed node is omitted
    // and never affects the running game.
    if (snapshot->save(filepath)) {
        m_snapshots.append(snapshot);
    } else {
        qWarning() << "GameSnapshotService: failed to save eligible turn snapshot"
                   << filepath;
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

GameSnapshot *GameSnapshotService::getSnapshotBySerial(quint64 turnSerial) const
{
    foreach (GameSnapshot *snapshot, m_snapshots) {
        if (snapshot->getTurnSerial() == turnSerial)
            return snapshot;
    }
    return nullptr;
}

quint64 GameSnapshotService::getNextTurnSerial() const
{
    return m_nextTurnSerial + 1;
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
    if (!path.isEmpty() && m_sessionId.isEmpty()) {
        m_sessionId = QUuid::createUuid().toString();
        m_sessionId.remove(QLatin1Char('{'));
        m_sessionId.remove(QLatin1Char('}'));
    }
}

QString GameSnapshotService::getReplayPath() const
{
    return m_replayPath;
}

QString GameSnapshotService::getSessionId() const
{
    return m_sessionId;
}

bool GameSnapshotService::finalizeManifest(const QString &replayPath, QString *error) const
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (replayPath.isEmpty())
        return fail(QStringLiteral("replay path is empty"));
    if (m_sessionId.isEmpty())
        return fail(QStringLiteral("snapshot session id is empty"));

    QFile replay(replayPath);
    if (!replay.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open replay: %1").arg(replayPath));
    const QByteArray replayHash = QCryptographicHash::hash(
        replay.readAll(), QCryptographicHash::Sha256).toHex();
    replay.close();

    const QString sourceSnapshotDir = getSnapshotDir();
    const QString manifestDir = GameSnapshot::getSnapshotDir(replayPath);
    QDir dir;
    if (!dir.exists(manifestDir) && !dir.mkpath(manifestDir))
        return fail(QStringLiteral("cannot create snapshot directory: %1").arg(manifestDir));

    QJsonArray entries;
    foreach (GameSnapshot *snapshot, m_snapshots) {
        if (!snapshot)
            continue;
        const QString fileName = GameSnapshot::generateSnapshotFilename(
            static_cast<int>(snapshot->getTurnSerial()), QStringLiteral("turn"));
        const QString sourcePath = sourceSnapshotDir + QStringLiteral("/") + fileName;
        const QString targetPath = manifestDir + QStringLiteral("/") + fileName;
        QFile snapshotFile(sourcePath);
        if (!snapshotFile.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot open snapshot: %1").arg(sourcePath));
        const QByteArray snapshotBytes = snapshotFile.readAll();
        snapshotFile.close();

        if (QDir::cleanPath(sourcePath) != QDir::cleanPath(targetPath)) {
            QSaveFile targetFile(targetPath);
            if (!targetFile.open(QIODevice::WriteOnly)
                || targetFile.write(snapshotBytes) != snapshotBytes.size()
                || !targetFile.commit()) {
                return fail(QStringLiteral("cannot copy snapshot: %1").arg(targetPath));
            }
        }
        const QByteArray hash = QCryptographicHash::hash(
            snapshotBytes, QCryptographicHash::Sha256).toHex();

        QJsonObject entry;
        entry.insert(QStringLiteral("file"), fileName);
        entry.insert(QStringLiteral("sha256"), QString::fromLatin1(hash));
        entry.insert(QStringLiteral("turnSerial"),
                     QString::number(snapshot->getTurnSerial()));
        const GlobalSnapshot state = snapshot->getState();
        int playerTurnCount = 1;
        bool currentPlayerFound = false;
        for (const PlayerSnapshot &player : state.players) {
            if (player.objectName != state.currentPlayer)
                continue;
            playerTurnCount = player.marks.value(
                QStringLiteral("Global_TurnCount"), 0) + 1;
            currentPlayerFound = true;
            break;
        }
        if (!currentPlayerFound)
            return fail(QStringLiteral("snapshot current player is missing"));
        entry.insert(QStringLiteral("playerName"), state.currentPlayer);
        entry.insert(QStringLiteral("playerTurnCount"), playerTurnCount);
        entries.append(entry);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("qsanguosha-takeover-manifest-v1"));
    manifest.insert(QStringLiteral("sessionId"), m_sessionId);
    manifest.insert(QStringLiteral("replaySha256"), QString::fromLatin1(replayHash));
    manifest.insert(QStringLiteral("snapshots"), entries);

    const QString path = manifestDir + QStringLiteral("/manifest.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write manifest: %1").arg(path));
    const QByteArray payload = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
        return fail(QStringLiteral("cannot commit manifest: %1").arg(path));
    return true;
}
