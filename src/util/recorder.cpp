#include "recorder.h"
#include "protocol.h"
#include "record-analysis.h"
#include "engine.h"
#include "replay-index.h"
#include "game-snapshot.h"
#include "replay/replay-container.h"

#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QtAlgorithms>

#include <cmath>
#include <limits>

using namespace QSanProtocol;
using namespace QSanReplay;

namespace
{
int elapsedSecondsForUi(qint64 elapsedMs)
{
    const qint64 seconds = elapsedMs / 1000;
    return static_cast<int>(qMin<qint64>(
        seconds, std::numeric_limits<int>::max()));
}

bool isPositiveJsonInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number && number > 0;
}

bool isSha256Hex(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (QChar character : value) {
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
            return false;
    }
    return true;
}
}

Recorder::Recorder(QObject *parent, bool takeover)
    : QObject(parent),
    buffer(Sanguosha ? Sanguosha->getVersion() : QStringLiteral("unknown"),
           Sanguosha ? Sanguosha->getMODName() : QStringLiteral("unknown"),
           takeover),
    m_analysis(new RecAnalysis(this))
{
}

bool Recorder::recordMessage(const ProtocolMessage &message, QString *error)
{
    if (!buffer.recordMessage(message, error))
        return false;

    // Update the existing statistics reducer as messages arrive, not at game end.
    m_analysis->recordMessage(message);
    return true;
}

bool Recorder::save(const QString &filename) const
{
    qDebug("%s", filename.toUtf8().constData());
    if (filename.endsWith(".txt")) {
        return buffer.saveText(filename);
    } else if (filename.endsWith(".png")) {
        return TXT2PNG(buffer.rawData()).save(filename);
    } else
        return false;
}

QList<QByteArray> Recorder::getRecords() const
{
    return buffer.getRecords();
}

const RecAnalysis *Recorder::getAnalysis() const
{
    return m_analysis;
}

QByteArray Recorder::rawReplayData() const
{
    return buffer.rawReplayData();
}

QImage Recorder::TXT2PNG(QByteArray txtData)
{
    return ReplayContainer::encodePng(txtData);
}

QByteArray Recorder::PNG2TXT(const QString filename)
{
    return ReplayContainer::decodePng(filename);
}

Replayer::Replayer(QObject *parent, const QString &filename)
    : QThread(parent), m_commandSeriesCounter(1),
    filename(filename), speed(1.0), playing(true), m_currentPairIndex(0),
    m_stateCaptureState(StateCaptureState::Idle), m_stateCaptureRequestId(0),
    m_stateCaptureResumePlaying(false),
    m_loadError(ReplayLoadError::None),
    m_formatVersion(ReplayFormatVersion::V2),
    m_messageProtocolVersion(ProtocolVersion::V2), m_index(nullptr),
    m_takeoverSnapshotsValid(false), m_stopRequested(false)
{
    qRegisterMetaType<ProtocolMessage>("QSanProtocol::ProtocolMessage");

    QByteArray replayData;
    if (filename.endsWith(".png")) {
        replayData = Recorder::PNG2TXT(filename);
    } else if (filename.endsWith(".txt")) {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly)) {
            m_loadError = ReplayLoadError::FileOpenFailure;
            m_errorString = QStringLiteral("Replay file could not be opened: %1").arg(filename);
            return;
        }
        replayData = file.readAll();
    } else {
        m_loadError = ReplayLoadError::UnsupportedContainer;
        m_errorString = QStringLiteral("Replay container must be .txt or .png");
        return;
    }

    const ReplayLoadResult load = ReplayReader().read(replayData);
    if (!load.success) {
        m_loadError = load.error;
        m_errorString = load.detail;
        return;
    }
    m_formatVersion = load.header.formatVersion;
    m_messageProtocolVersion = load.header.protocolVersion;
    m_events = load.events;

    m_index = new ReplayIndex(this);
    buildIndex();
    loadSnapshots();
    loadPerspectives();
}

void Replayer::loadPerspectives()
{
    // Only an explicit sibling manifest grants access to other seat streams.
    // Reject the whole catalogue on malformed identities or escaping paths.
    const QFileInfo current(filename);
    const QDir directory = current.absoluteDir();
    QFile file(directory.filePath(QStringLiteral("index.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qsanguosha-headless-game")
        || root.value(QStringLiteral("schema_version")) != QJsonValue(1)
        || !root.value(QStringLiteral("perspectives")).isArray())
        return;
    const QJsonArray entries = root.value(QStringLiteral("perspectives")).toArray();
    if (entries.isEmpty())
        return;
    const QString canonicalDirectory = directory.canonicalPath();
    if (canonicalDirectory.isEmpty())
        return;
    QList<Perspective> parsed;
    QSet<QString> names, paths;
    QString source;
    for (const QJsonValue &entry : entries) {
        if (!entry.isObject()) return;
        const QJsonObject object = entry.toObject();
        Perspective seat;
        seat.playerName = object.value(QStringLiteral("player_name")).toString();
        seat.screenName = object.value(QStringLiteral("screen_name")).toString();
        const QString basename = object.value(QStringLiteral("file")).toString();
        const QString elapsed = object.value(QStringLiteral("available_from_ms")).toString();
        bool elapsedOk = false;
        seat.availableFromMs = elapsed.toLongLong(&elapsedOk);
        if (seat.playerName.isEmpty() || names.contains(seat.playerName)
            || !object.value(QStringLiteral("screen_name")).isString()
            || basename.isEmpty() || basename.contains('/') || basename.contains('\\')
            || basename.contains(':') || basename == QLatin1String(".")
            || basename == QLatin1String("..")
            || !basename.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
            || !elapsedOk || seat.availableFromMs < 0
            || elapsed != QString::number(seat.availableFromMs))
            return;
        const QFileInfo sibling(directory.filePath(basename));
        if (!sibling.isFile() || sibling.canonicalPath() != canonicalDirectory)
            return;
        seat.path = sibling.canonicalFilePath();
        if (paths.contains(seat.path)) return;
        names.insert(seat.playerName);
        paths.insert(seat.path);
        if (seat.path == current.canonicalFilePath()) source = seat.playerName;
        parsed << seat;
    }
    if (source.isEmpty()) return;
    m_perspectives = parsed;
    m_sourcePlayerName = source;
}

int Replayer::positionAtElapsed(qint64 elapsedMs) const
{
    int position = -1;
    for (int i = 0; i < m_events.size(); ++i) {
        if (m_events.at(i).elapsedMs > elapsedMs) break;
        position = i;
    }
    return position;
}

bool Replayer::preparePlayback(int pairIndex, bool paused, qreal playbackSpeed)
{
    if (isRunning() || m_stopRequested || !isValid() || m_events.isEmpty()
        || !std::isfinite(playbackSpeed) || playbackSpeed <= 0)
        return false;
    // Even an initial seek needs SETUP to create the native replay scene.
    int setupIndex = 0;
    for (int i = 0; i < m_events.size(); ++i) {
        if (m_events.at(i).message.command == S_COMMAND_SETUP) {
            setupIndex = i;
            break;
        }
    }
    m_startupPosition = qBound(setupIndex, pairIndex, int(m_events.size()) - 1);
    playing = !paused;
    speed = qBound<qreal>(0.5, playbackSpeed, 6.0);
    return true;
}

Replayer::~Replayer()
{
    stopAndWait();
    if (m_index)
        delete m_index;
}

void Replayer::buildIndex()
{
    if (!m_index)
        return;

    m_index->buildIndex(m_events);

    QString snapshotPath = GameSnapshot::getSnapshotDir(filename);
    m_index->setSnapshotPath(snapshotPath);
}

void Replayer::loadSnapshots()
{
    m_takeoverSnapshotPaths.clear();
    m_takeoverSnapshotHashes.clear();
    m_takeoverSnapshotSerials.clear();
    m_takeoverSnapshotsByNode.clear();
    m_takeoverSnapshotsValid = false;

    // The PNG container deliberately has no takeover capability.  A sidecar
    // next to a PNG must not change that policy.
    if (filename.endsWith(".png", Qt::CaseInsensitive))
        return;

    QString snapshotPath = GameSnapshot::getSnapshotDir(filename);
    QDir dir(snapshotPath);
    if (!dir.exists())
        return;

    QFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(
        manifestFile.readAll(), &parseError);
    manifestFile.close();
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject())
        return;

    const QJsonObject manifest = manifestDoc.object();
    // This is intentionally a new, strict sidecar contract.  In particular,
    // do not fall back to scanning arbitrary JSON files in the directory.
    if (!manifest.value(QStringLiteral("schema")).isString()
        || !manifest.value(QStringLiteral("sessionId")).isString()
        || !manifest.value(QStringLiteral("replaySha256")).isString()
        || manifest.value(QStringLiteral("schema")).toString()
            != QStringLiteral("qsanguosha-takeover-manifest-v1")
        || manifest.value(QStringLiteral("sessionId")).toString().isEmpty()
        || !isSha256Hex(manifest.value(QStringLiteral("replaySha256")).toString()))
        return;

    QFile replayFile(filename);
    if (!replayFile.open(QIODevice::ReadOnly))
        return;
    const QByteArray replayHash = QCryptographicHash::hash(
        replayFile.readAll(), QCryptographicHash::Sha256).toHex();
    replayFile.close();
    if (manifest.value(QStringLiteral("replaySha256")).toString().toLatin1()
            != replayHash)
        return;

    const QJsonValue entriesValue = manifest.value(QStringLiteral("snapshots"));
    if (!entriesValue.isArray())
        return;

    struct VerifiedSnapshot {
        QString path;
        QString sha256;
        quint64 turnSerial = 0;
        QString playerName;
        int playerTurnCount = 0;
    };
    QList<VerifiedSnapshot> verifiedSnapshots;
    QSet<QString> seenFiles;
    QSet<quint64> seenSerials;
    QSet<QString> seenTurnIdentities;
    const QJsonArray entries = entriesValue.toArray();
    for (const QJsonValue &entryValue : entries) {
        if (!entryValue.isObject()) {
            return;
        }
        const QJsonObject entry = entryValue.toObject();
        const QString file = entry.value(QStringLiteral("file")).toString();
        const QString expectedHash = entry.value(QStringLiteral("sha256")).toString();
        const QString turnSerialText = entry.value(QStringLiteral("turnSerial")).toString();
        bool serialOk = false;
        const quint64 turnSerial = turnSerialText.toULongLong(&serialOk);
        const QString playerName = entry.value(
            QStringLiteral("playerName")).toString();
        const int playerTurnCount = entry.value(
            QStringLiteral("playerTurnCount")).toInt(0);
        const QString turnIdentity = playerName + QChar(0x1f)
            + QString::number(playerTurnCount);
        if (!entry.value(QStringLiteral("file")).isString()
            || !entry.value(QStringLiteral("sha256")).isString()
            || !entry.value(QStringLiteral("turnSerial")).isString()
            || !entry.value(QStringLiteral("playerName")).isString()
            || !isPositiveJsonInteger(entry.value(QStringLiteral("playerTurnCount")))
            || file.isEmpty() || !isSha256Hex(expectedHash)
            || !serialOk || turnSerial == 0 || playerName.isEmpty()
            || playerTurnCount <= 0
            || QFileInfo(file).fileName() != file
            || !file.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
            || seenFiles.contains(file) || seenSerials.contains(turnSerial)
            || seenTurnIdentities.contains(turnIdentity)) {
            return;
        }
        seenFiles.insert(file);
        seenSerials.insert(turnSerial);
        seenTurnIdentities.insert(turnIdentity);

        QFile snapshotFile(dir.filePath(file));
        if (!snapshotFile.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray bytes = snapshotFile.readAll();
        snapshotFile.close();
        if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
                != expectedHash.toLatin1()) {
            return;
        }

        GameSnapshot *snapshot = new GameSnapshot(this);
        if (!snapshot->load(dir.filePath(file)) || !snapshot->isEligible()
            || snapshot->getTurnSerial() != turnSerial) {
            delete snapshot;
            return;
        }
        const GlobalSnapshot state = snapshot->getState();
        int expectedPlayerTurnCount = 1;
        bool currentPlayerFound = false;
        for (const PlayerSnapshot &player : state.players) {
            if (player.objectName != state.currentPlayer)
                continue;
            expectedPlayerTurnCount = player.marks.value(
                QStringLiteral("Global_TurnCount"), 0) + 1;
            currentPlayerFound = true;
            break;
        }
        if (!currentPlayerFound || state.currentPlayer != playerName
            || expectedPlayerTurnCount != playerTurnCount) {
            delete snapshot;
            return;
        }
        VerifiedSnapshot verified;
        verified.path = dir.filePath(file);
        verified.sha256 = expectedHash;
        verified.turnSerial = turnSerial;
        verified.playerName = playerName;
        verified.playerTurnCount = playerTurnCount;
        verifiedSnapshots.append(verified);
        delete snapshot;
    }

    // Every manifest entry must identify exactly one replay node.  Silently
    // accepting a missing or ambiguous identity could resume the wrong turn.
    QMap<int, QString> snapshotPaths;
    QMap<int, QSharedPointer<GameSnapshot> > snapshotsByNode;
    for (const VerifiedSnapshot &verified : verifiedSnapshots) {
        int matchedNode = -1;
        int matchCount = 0;
        for (int i = 0; i < m_index->getNodeCount(); ++i) {
            const ReplayNode node = m_index->getNode(i);
            if (node.playerName == verified.playerName
                && node.playerTurnCount == verified.playerTurnCount
                && node.type == ReplayNodeType::TurnStart) {
                matchedNode = i;
                ++matchCount;
            }
        }
        if (matchCount != 1) {
            return;
        }
        snapshotPaths.insert(matchedNode, verified.path);
        m_takeoverSnapshotHashes.insert(matchedNode, verified.sha256);
        m_takeoverSnapshotSerials.insert(matchedNode, verified.turnSerial);
        // Keep only the verified path here.  Resolution history is an
        // immutable journal and can be large; load it when takeover actually
        // requests this node instead of retaining every payload in memory.
        snapshotsByNode.insert(matchedNode, QSharedPointer<GameSnapshot>());
    }

    // A manifest with no entries is valid but simply offers no takeover node.
    m_takeoverSnapshotPaths = snapshotPaths;
    m_takeoverSnapshotsByNode = snapshotsByNode;
    m_takeoverSnapshotsValid = !m_takeoverSnapshotPaths.isEmpty();
}

QSharedPointer<GameSnapshot> Replayer::getSnapshot(int nodeIndex) const
{
    if (!m_index || !m_takeoverSnapshotPaths.contains(nodeIndex))
        return {};

    const QSharedPointer<GameSnapshot> cached =
        m_takeoverSnapshotsByNode.value(nodeIndex);
    if (cached)
        return cached;

    const QString path = m_takeoverSnapshotPaths.value(nodeIndex);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
            != m_takeoverSnapshotHashes.value(nodeIndex).toLatin1())
        return {};
    QSharedPointer<GameSnapshot> snapshot(new GameSnapshot());
    if (!snapshot->load(path) || !snapshot->isEligible()
        || snapshot->getTurnSerial() != m_takeoverSnapshotSerials.value(nodeIndex)) {
        return {};
    }
    const ReplayNode node = m_index->getNode(nodeIndex);
    const GlobalSnapshot state = snapshot->getState();
    int playerTurnCount = 0;
    for (const PlayerSnapshot &player : state.players) {
        if (player.objectName == state.currentPlayer) {
            playerTurnCount = player.marks.value(
                QStringLiteral("Global_TurnCount"), 0) + 1;
            break;
        }
    }
    if (state.currentPlayer != node.playerName
        || playerTurnCount != node.playerTurnCount) {
        return {};
    }
    // QSharedPointer keeps a caller's in-use snapshot alive while the
    // one-entry cache advances to another replay node.
    m_takeoverSnapshotsByNode.clear();
    m_takeoverSnapshotsByNode.insert(nodeIndex, snapshot);
    return snapshot;
}

int Replayer::getNearestTakeoverNodeAtOrBefore(int pairIndex) const
{
    if (!m_index || !m_takeoverSnapshotsValid)
        return -1;

    int bestNode = -1;
    int bestPair = -1;
    for (auto it = m_takeoverSnapshotPaths.constBegin();
         it != m_takeoverSnapshotPaths.constEnd(); ++it) {
        const ReplayNode node = m_index->getNode(it.key());
        if (node.pairIndex > pairIndex)
            continue;
        if (node.pairIndex > bestPair) {
            bestPair = node.pairIndex;
            bestNode = it.key();
        }
    }
    return bestNode;
}

int Replayer::getNearestTakeoverNodeAtOrBeforeCurrent() const
{
    return getNearestTakeoverNodeAtOrBefore(getCurrentPairIndex());
}

QString Replayer::getTakeoverSnapshotPath(int nodeIndex) const
{
    return m_takeoverSnapshotPaths.value(nodeIndex);
}

QString Replayer::getTakeoverManifestPath() const
{
    if (!m_takeoverSnapshotsValid)
        return QString();
    return QDir(GameSnapshot::getSnapshotDir(filename)).filePath(
        QStringLiteral("manifest.json"));
}

QStringList Replayer::getTakeoverSnapshotPaths() const
{
    return m_takeoverSnapshotsValid
        ? m_takeoverSnapshotPaths.values() : QStringList();
}

bool Replayer::hasTakeoverSnapshots() const
{
    return m_takeoverSnapshotsValid;
}

quint64 Replayer::requestStateCaptureBoundary()
{
    QMutexLocker dispatchLocker(&m_dispatchMutex);
    quint64 requestId = 0;
    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested || !isRunning()
            || m_stateCaptureState != StateCaptureState::Idle)
            return 0;

        ++m_stateCaptureRequestId;
        if (m_stateCaptureRequestId == 0)
            ++m_stateCaptureRequestId;
        requestId = m_stateCaptureRequestId;
        m_stateCaptureResumePlaying = playing;
        playing = false;
        m_stateCaptureState = StateCaptureState::Requested;
    }

    // Wake a replay that is already waiting in its ordinary paused state.
    // waitWhilePlaybackPaused() rechecks the flag, so an unused permit cannot
    // accidentally dispatch an event later.
    play_sem.release();
    return requestId;
}

bool Replayer::releaseStateCaptureBoundary(quint64 requestId)
{
    {
        QMutexLocker locker(&mutex);
        if (requestId == 0 || requestId != m_stateCaptureRequestId
            || m_stateCaptureState != StateCaptureState::Reached)
            return false;
        playing = m_stateCaptureResumePlaying;
        m_stateCaptureState = StateCaptureState::Idle;
    }
    m_stateCaptureSemaphore.release();
    return true;
}

bool Replayer::cancelStateCaptureBoundary(quint64 requestId)
{
    bool releaseBoundary = false;
    {
        QMutexLocker locker(&mutex);
        if (requestId == 0 || requestId != m_stateCaptureRequestId
            || m_stateCaptureState == StateCaptureState::Idle)
            return false;
        releaseBoundary = m_stateCaptureState == StateCaptureState::Reached;
        playing = m_stateCaptureResumePlaying;
        m_stateCaptureState = StateCaptureState::Idle;
    }
    if (releaseBoundary)
        m_stateCaptureSemaphore.release();
    return true;
}

int Replayer::getCurrentPairIndex() const
{
    if (m_events.isEmpty())
        return 0;
    return qBound(0, m_lastDispatchedPairIndex.load(std::memory_order_relaxed),
                  m_events.size() - 1);
}

qint64 Replayer::getCurrentElapsed() const
{
    const int pairIndex = getCurrentPairIndex();
    if (pairIndex >= 0 && pairIndex < m_events.size())
        return m_events[pairIndex].elapsedMs;
    return 0;
}

bool Replayer::isValid() const
{
    return m_loadError == ReplayLoadError::None;
}

ReplayLoadError Replayer::loadError() const
{
    return m_loadError;
}

QString Replayer::errorString() const
{
    return m_errorString;
}

ReplayFormatVersion Replayer::formatVersion() const
{
    return m_formatVersion;
}

ProtocolVersion Replayer::messageProtocolVersion() const
{
    return m_messageProtocolVersion;
}

const QList<ReplayEvent> &Replayer::events() const
{
    return m_events;
}

ReplayIndex* Replayer::getIndex() const
{
    return m_index;
}

int Replayer::getDuration() const
{
    if (m_events.isEmpty())
        return 0;
    return elapsedSecondsForUi(m_events.constLast().elapsedMs);
}

qreal Replayer::getSpeed()
{
    qreal speed;
    mutex.lock();
    speed = this->speed;
    mutex.unlock();
    return speed;
}

bool Replayer::isPlaying()
{
    mutex.lock();
    const bool result = playing && !m_stopRequested;
    mutex.unlock();
    return result;
}

void Replayer::uniform()
{
    mutex.lock();

    if (speed != 1.0) {
        speed = 1.0;
        emit speed_changed(1.0);
    }

    mutex.unlock();
}

void Replayer::speedUp()
{
    mutex.lock();

    if (speed < 6.0) {
        qreal inc = speed >= 2.0 ? 1.0 : 0.5;
        speed += inc;
        emit speed_changed(speed);
    }

    mutex.unlock();
}

void Replayer::slowDown()
{
    mutex.lock();

    if (speed >= 1.0) {
        qreal dec = speed > 2.0 ? 1.0 : 0.5;
        speed -= dec;
        emit speed_changed(speed);
    }

    mutex.unlock();
}

void Replayer::toggle()
{
    bool resume = false;
    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested || m_stateCaptureState != StateCaptureState::Idle)
            return;
        playing = !playing;
        resume = playing;
    }
    if (resume)
        play_sem.release(); // to play
}

bool Replayer::waitForStateCaptureBoundary()
{
    quint64 requestId = 0;
    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested)
            return false;
        if (m_stateCaptureState != StateCaptureState::Requested)
            return true;
        m_stateCaptureState = StateCaptureState::Reached;
        requestId = m_stateCaptureRequestId;
    }

    emit stateCaptureBoundaryReached(requestId);
    m_stateCaptureSemaphore.acquire();

    QMutexLocker locker(&mutex);
    return !m_stopRequested;
}

bool Replayer::waitWhilePlaybackPaused()
{
    while (true) {
        if (!waitForStateCaptureBoundary())
            return false;

        bool shouldPause = false;
        {
            QMutexLocker locker(&mutex);
            if (m_stopRequested)
                return false;
            shouldPause = !playing;
        }
        if (!shouldPause)
            return true;
        play_sem.acquire();
    }
}

void Replayer::run()
{
    qint64 last = 0;
    // A new Client owns an empty reducer and scene. Reconstruct its prefix
    // exactly once; queued delivery preserves SETUP/UI creation/event order.
    for (int i = 0; i <= m_startupPosition; ++i) {
        m_currentPairIndex.store(i, std::memory_order_relaxed);
        if (!emitCommand(i, false)) return;
        last = m_events.at(i).elapsedMs;
    }
    emit startup_finished();

    const QList<CommandType> nondelays{
        S_COMMAND_ADD_PLAYER, S_COMMAND_REMOVE_PLAYER, S_COMMAND_SPEAK};
    for (m_currentPairIndex.store(m_startupPosition + 1, std::memory_order_relaxed);
         m_currentPairIndex.load(std::memory_order_relaxed) < m_events.size();
         m_currentPairIndex.fetch_add(1, std::memory_order_relaxed)) {
        if (!waitWhilePlaybackPaused()) break;
        const int pairIndex = m_currentPairIndex.load(std::memory_order_relaxed);
        const ReplayEvent &event = m_events.at(pairIndex);
        const qint64 delay = qMax<qint64>(0, qMin<qint64>(event.elapsedMs - last, 2500));
        qint64 remainingDelay = nondelays.contains(static_cast<CommandType>(event.message.command))
            ? 0 : static_cast<qint64>(delay / getSpeed());
        last = event.elapsedMs;
        while (remainingDelay > 0) {
            if (!waitWhilePlaybackPaused()) return;
            QElapsedTimer sleepTimer;
            sleepTimer.start();
            msleep(static_cast<unsigned long>(qMin<qint64>(remainingDelay, 20)));
            // Count actual Windows timer ticks, excluding any pause/barrier.
            remainingDelay -= qMax<qint64>(1, sleepTimer.elapsed());
        }
        if (!emitCommand(pairIndex)) break;
        emit elasped(elapsedSecondsForUi(event.elapsedMs));
        const int nodeIndex = m_index ? m_index->findNearestNode(pairIndex) : -1;
        if (nodeIndex >= 0) emit node_reached(nodeIndex);
    }
}

bool Replayer::stopAndWait(unsigned long timeout)
{
    {
        QMutexLocker locker(&mutex);
        m_stopRequested = true;
        playing = true;
        m_stateCaptureState = StateCaptureState::Idle;
    }
    play_sem.release();
    m_stateCaptureSemaphore.release();

    if (!isRunning())
        return true;
    return wait(timeout);
}

void Replayer::jumpToNode(int nodeIndex)
{
    if (!m_index || nodeIndex < 0 || nodeIndex >= m_index->getNodeCount())
        return;

    ReplayNode node = m_index->getNode(nodeIndex);
    seekToPosition(node.pairIndex);
}

void Replayer::jumpToElapsed(qint64 elapsed)
{
    if (elapsed < 0)
        return;
    int bestIndex = 0;
    qint64 bestDiff = std::numeric_limits<qint64>::max();

    for (int i = 0; i < m_events.size(); i++) {
        const qint64 eventElapsed = m_events.at(i).elapsedMs;
        const qint64 diff = eventElapsed >= elapsed
            ? eventElapsed - elapsed : elapsed - eventElapsed;
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = i;
        }
    }

    seekToPosition(bestIndex);
}

void Replayer::seekToPosition(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= m_events.size())
        return;

    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested || m_stateCaptureState != StateCaptureState::Idle)
            return;
    }
    // MainWindow owns the clean Client/scene replacement. Reapplying a prefix
    // to live ClientPlayers would duplicate cards, marks and dynamic seats.
    emit reconstructionRequested(pairIndex);
}

bool Replayer::emitCommand(int pairIndex, bool obeyPause)
{
    if (pairIndex < 0 || pairIndex >= m_events.size()) return false;
    while (true) {
        if (obeyPause ? !waitWhilePlaybackPaused() : !waitForStateCaptureBoundary())
            return false;
        QMutexLocker dispatchLocker(&m_dispatchMutex);
        {
            QMutexLocker locker(&mutex);
            if (m_stopRequested) return false;
            // A capture can arrive between the wait and this lock. Reach its
            // boundary before emitting anything, including a startup event.
            if (m_stateCaptureState == StateCaptureState::Requested || (obeyPause && !playing))
                continue;
        }
        const ReplayEvent &event = m_events.at(pairIndex);
        m_lastDispatchedPairIndex.store(pairIndex, std::memory_order_relaxed);
        emit command_parsed(event.message);
        emit replayEventDispatched(event.message, pairIndex, event.elapsedMs);
        return true;
    }
}

QString Replayer::getPath() const
{
    return filename;
}

