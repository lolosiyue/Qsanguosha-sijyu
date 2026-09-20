#include "game-snapshot-service.h"

#include "game-snapshot.h"
#include "room.h"
#include "settings.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>

namespace {
const qint64 IoChunk = 64 * 1024;

QByteArray hashFile(QFile &file)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(IoChunk);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return QByteArray();
        hash.addData(chunk);
    }
    return hash.result().toHex();
}

QByteArray hashPath(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? hashFile(file) : QByteArray();
}

bool copyStreaming(const QString &sourcePath, const QString &targetPath,
                   const QByteArray &expectedHash)
{
    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(IoChunk);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError)
            return false;
        if (output.write(chunk) != chunk.size())
            return false;
        hash.addData(chunk);
    }
    // Verify the bytes actually copied before publishing the destination.
    if (expectedHash.isEmpty() || hash.result().toHex() != expectedHash)
        return false;
    input.close();
    return output.commit();
}

int playerTurnCount(const GlobalSnapshot &state)
{
    for (const PlayerSnapshot &player : state.players) {
        if (player.objectName == state.currentPlayer)
            return player.marks.value(QStringLiteral("Global_TurnCount"), 0) + 1;
    }
    return -1;
}
}

GameSnapshotService::GameSnapshotService(Room &room, StoragePolicy policy)
    : m_room(room), m_nextTurnSerial(0), m_storagePolicy(policy)
{
    if (m_storagePolicy == StoragePolicy::Automatic) {
#if QT_POINTER_SIZE == 4
        m_storagePolicy = StoragePolicy::DiskBacked;
#else
        m_storagePolicy = StoragePolicy::InMemory;
#endif
    }
}

GameSnapshotService::~GameSnapshotService() = default;

void GameSnapshotService::saveSnapshot(const QString &type, const QString &playerName)
{
    Q_UNUSED(playerName);
    if (type != QStringLiteral("turn") || m_replayPath.isEmpty())
        return;
    const quint64 turnSerial = ++m_nextTurnSerial;
    if (!Config.EnableAI || Config.DisableLua)
        return;

    m_room.setTag(QStringLiteral("ReplaySnapshotTurnSerial"), QString::number(turnSerial));
    QSharedPointer<GameSnapshot> snapshot(new GameSnapshot(&m_room));
    m_room.removeTag(QStringLiteral("ReplaySnapshotTurnSerial"));
    snapshot->setTurnSerial(turnSerial);
    snapshot->setSnapshotType(type);
    snapshot->setReplayPath(m_replayPath);
    snapshot->setDescription(QStringLiteral("Turn %1").arg(turnSerial));

    const QString snapshotDir = getSnapshotDir();
    QDir dir;
    if (!dir.exists(snapshotDir) && !dir.mkpath(snapshotDir)) {
        qWarning() << "GameSnapshotService: failed to create snapshot directory" << snapshotDir;
        return;
    }
    const QString filename = GameSnapshot::generateSnapshotFilename(
        static_cast<int>(turnSerial), QStringLiteral("turn"));
    const QString filepath = snapshotDir + QStringLiteral("/") + filename;
    const GameSnapshot::SaveMode saveMode =
        m_storagePolicy == StoragePolicy::DiskBacked
            ? GameSnapshot::SaveMode::Streaming
            : GameSnapshot::SaveMode::Buffered;
    if (!snapshot->save(filepath, saveMode)) {
        qWarning() << "GameSnapshotService: failed to save eligible turn snapshot"
                   << filepath << "reason:" << snapshot->getError();
        return;
    }

    QString publishError;
    if (!publishSnapshot(snapshot, filepath, &publishError)) {
        qWarning() << "GameSnapshotService: failed to publish eligible turn snapshot"
                   << filepath << "reason:" << publishError;
    }
}

bool GameSnapshotService::publishSnapshot(const QSharedPointer<GameSnapshot> &snapshot,
                                          const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!snapshot)
        return fail(QStringLiteral("snapshot is null"));
    if (path.isEmpty())
        return fail(QStringLiteral("snapshot path is empty"));

    SnapshotRecord record;
    record.path = path;
    record.turnSerial = snapshot->getTurnSerial();
    record.turnCount = snapshot->getTurnCount();
    const GlobalSnapshot state = snapshot->getState();
    record.currentPlayer = state.currentPlayer;
    record.playerTurnCount = playerTurnCount(state);
    record.sha256 = hashPath(path);
    if (record.playerTurnCount < 0 || record.sha256.isEmpty()) {
        return fail(QStringLiteral("invalid snapshot metadata or unreadable snapshot file"));
    }
    if (m_storagePolicy == StoragePolicy::InMemory)
        record.snapshot = snapshot;
    QMutexLocker lock(&m_snapshotMutex);
    m_snapshots.append(record);
    return true;
}

QSharedPointer<GameSnapshot> GameSnapshotService::loadDiskSnapshot(
    const SnapshotRecord &record) const
{
    // Hash on every access so the one-entry cache cannot hide tampering.
    const QByteArray actualHash = hashPath(record.path);
    if (actualHash.isEmpty() || actualHash != record.sha256)
        return QSharedPointer<GameSnapshot>();
    if (m_loadedSnapshot && m_loadedSnapshotPath == record.path
        && m_loadedSnapshotHash == actualHash)
        return m_loadedSnapshot;
    m_loadedSnapshot.clear();
    m_loadedSnapshotPath.clear();
    m_loadedSnapshotHash.clear();
    QSharedPointer<GameSnapshot> snapshot(new GameSnapshot);
    if (!snapshot->load(record.path, record.sha256))
        return QSharedPointer<GameSnapshot>();
    const GlobalSnapshot state = snapshot->getState();
    if (snapshot->getTurnSerial() != record.turnSerial
        || snapshot->getTurnCount() != record.turnCount
        || state.currentPlayer != record.currentPlayer
        || playerTurnCount(state) != record.playerTurnCount)
        return QSharedPointer<GameSnapshot>();
    m_loadedSnapshot = snapshot;
    m_loadedSnapshotPath = record.path;
    m_loadedSnapshotHash = actualHash;
    return snapshot;
}

QSharedPointer<GameSnapshot> GameSnapshotService::getSnapshot(int turnCount) const
{
    QMutexLocker lock(&m_snapshotMutex);
    for (const SnapshotRecord &record : m_snapshots)
        if (record.turnCount == turnCount)
            return record.snapshot ? record.snapshot : loadDiskSnapshot(record);
    return QSharedPointer<GameSnapshot>();
}

QSharedPointer<GameSnapshot> GameSnapshotService::getSnapshotBySerial(quint64 turnSerial) const
{
    QMutexLocker lock(&m_snapshotMutex);
    for (const SnapshotRecord &record : m_snapshots)
        if (record.turnSerial == turnSerial)
            return record.snapshot ? record.snapshot : loadDiskSnapshot(record);
    return QSharedPointer<GameSnapshot>();
}

quint64 GameSnapshotService::getNextTurnSerial() const { return m_nextTurnSerial + 1; }

QString GameSnapshotService::getSnapshotDir() const
{
    return m_replayPath.isEmpty() ? QString() : GameSnapshot::getSnapshotDir(m_replayPath);
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

QString GameSnapshotService::getReplayPath() const { return m_replayPath; }
QString GameSnapshotService::getSessionId() const { return m_sessionId; }

bool GameSnapshotService::finalizeManifest(const QString &replayPath, QString *error) const
{
    QMutexLocker lock(&m_snapshotMutex);
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (replayPath.isEmpty()) return fail(QStringLiteral("replay path is empty"));
    if (m_sessionId.isEmpty()) return fail(QStringLiteral("snapshot session id is empty"));
    const QByteArray replayHash = hashPath(replayPath);
    if (replayHash.isEmpty()) return fail(QStringLiteral("cannot open replay: %1").arg(replayPath));
    const QString sourceDir = getSnapshotDir();
    const QString manifestDir = GameSnapshot::getSnapshotDir(replayPath);
    if (QFileInfo(manifestDir).isSymLink()) return fail(QStringLiteral("invalid snapshot output directory"));
    QDir dir;
    if (!dir.exists(manifestDir) && !dir.mkpath(manifestDir))
        return fail(QStringLiteral("cannot create snapshot directory: %1").arg(manifestDir));

    QJsonArray entries;
    for (const SnapshotRecord &record : m_snapshots) {
        const QString name = GameSnapshot::generateSnapshotFilename(
            static_cast<int>(record.turnSerial), QStringLiteral("turn"));
        const QString sourcePath = sourceDir + QStringLiteral("/") + name;
        const QString targetPath = manifestDir + QStringLiteral("/") + name;
        if (QFileInfo(targetPath).isSymLink()) return fail(QStringLiteral("invalid snapshot output file"));
        if (hashPath(sourcePath) != record.sha256)
            return fail(QStringLiteral("snapshot hash mismatch: %1").arg(sourcePath));
        if (QDir::cleanPath(sourcePath) != QDir::cleanPath(targetPath)
            && !copyStreaming(sourcePath, targetPath, record.sha256))
            return fail(QStringLiteral("cannot copy snapshot: %1").arg(targetPath));
        QJsonObject entry;
        entry.insert(QStringLiteral("file"), name);
        entry.insert(QStringLiteral("sha256"), QString::fromLatin1(record.sha256));
        entry.insert(QStringLiteral("turnSerial"), QString::number(record.turnSerial));
        entry.insert(QStringLiteral("playerName"), record.currentPlayer);
        entry.insert(QStringLiteral("playerTurnCount"), record.playerTurnCount);
        entries.append(entry);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), QStringLiteral("qsanguosha-takeover-manifest-v1"));
    manifest.insert(QStringLiteral("sessionId"), m_sessionId);
    manifest.insert(QStringLiteral("replaySha256"), QString::fromLatin1(replayHash));
    manifest.insert(QStringLiteral("snapshots"), entries);
    const QString path = manifestDir + QStringLiteral("/manifest.json");
    if (QFileInfo(path).isSymLink()) return fail(QStringLiteral("invalid snapshot manifest output"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("cannot write manifest: %1").arg(path));
    const QByteArray payload = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
        return fail(QStringLiteral("cannot commit manifest: %1").arg(path));
    return true;
}

bool GameSnapshotService::copyFinalizedManifest(const QString &sourceManifestPath,
                                                const QString &replayPath, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    QFile source(sourceManifestPath);
    if (!source.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open finalized snapshots or replay"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(source.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("invalid finalized snapshot manifest"));
    QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("schema")).toString() != QStringLiteral("qsanguosha-takeover-manifest-v1")
        || manifest.value(QStringLiteral("sessionId")).toString().isEmpty()
        || !manifest.value(QStringLiteral("snapshots")).isArray())
        return fail(QStringLiteral("incomplete finalized snapshot manifest"));
    const QByteArray replayHash = hashPath(replayPath);
    if (replayHash.isEmpty()) return fail(QStringLiteral("cannot open replay: %1").arg(replayPath));
    const QString targetDir = GameSnapshot::getSnapshotDir(replayPath);
    if (QFileInfo(targetDir).isSymLink() || !QDir().mkpath(targetDir))
        return fail(QStringLiteral("invalid snapshot output directory"));
    const QDir sourceDir = QFileInfo(sourceManifestPath).absoluteDir();
    for (const QJsonValue &value : manifest.value(QStringLiteral("snapshots")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("file")).toString();
        if (name.isEmpty() || QFileInfo(name).fileName() != name || name.contains(QLatin1Char(':'))
            || name.contains(QLatin1Char('\\')) || !name.endsWith(QStringLiteral(".json")))
            return fail(QStringLiteral("invalid finalized snapshot filename"));
        const QString inputPath = sourceDir.filePath(name);
        if (hashPath(inputPath) != entry.value(QStringLiteral("sha256")).toString().toLatin1())
            return fail(QStringLiteral("finalized snapshot hash mismatch"));
        const QString targetPath = QDir(targetDir).filePath(name);
        if (QFileInfo(targetPath).isSymLink()
            || !copyStreaming(inputPath, targetPath,
                              entry.value(QStringLiteral("sha256")).toString().toLatin1()))
            return fail(QStringLiteral("cannot copy finalized snapshot"));
    }
    manifest.insert(QStringLiteral("replaySha256"), QString::fromLatin1(replayHash));
    const QString targetManifest = QDir(targetDir).filePath(QStringLiteral("manifest.json"));
    if (QFileInfo(targetManifest).isSymLink()) return fail(QStringLiteral("invalid snapshot manifest output"));
    QSaveFile output(targetManifest);
    const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return fail(QStringLiteral("cannot write copied snapshot manifest"));
    return true;
}
