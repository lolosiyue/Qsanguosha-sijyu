#include "recorder.h"
#include "protocol.h"
#include "engine.h"
#include "replay-index.h"
#include "game-snapshot.h"
#include "replay/replay-container.h"

#include <QFile>
#include <QDir>
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
           takeover)
{
}

bool Recorder::recordMessage(const ProtocolMessage &message, QString *error)
{
    return buffer.recordMessage(message, error);
}

bool Recorder::save(const QString &filename) const
{
    qDebug(filename.toUtf8().data());
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
    filename(filename), speed(1.0), playing(true), m_seeking(false),
    m_seekGeneration(0), m_currentPairIndex(0),
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
}

Replayer::~Replayer()
{
    stopAndWait();
    foreach (GameSnapshot *snapshot, m_snapshots)
        delete snapshot;
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
    qDeleteAll(m_snapshots);
    m_snapshots.clear();
    m_takeoverSnapshotPaths.clear();
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
        GameSnapshot *snapshot = nullptr;
        QString playerName;
        int playerTurnCount = 0;
    };
    QList<VerifiedSnapshot> verifiedSnapshots;
    const auto discardVerified = [&verifiedSnapshots]() {
        for (const VerifiedSnapshot &verified : verifiedSnapshots)
            delete verified.snapshot;
        verifiedSnapshots.clear();
    };
    QSet<QString> seenFiles;
    QSet<quint64> seenSerials;
    QSet<QString> seenTurnIdentities;
    const QJsonArray entries = entriesValue.toArray();
    for (const QJsonValue &entryValue : entries) {
        if (!entryValue.isObject()) {
            discardVerified();
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
            discardVerified();
            return;
        }
        seenFiles.insert(file);
        seenSerials.insert(turnSerial);
        seenTurnIdentities.insert(turnIdentity);

        QFile snapshotFile(dir.filePath(file));
        if (!snapshotFile.open(QIODevice::ReadOnly)) {
            discardVerified();
            return;
        }
        const QByteArray bytes = snapshotFile.readAll();
        snapshotFile.close();
        if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
                != expectedHash.toLatin1()) {
            discardVerified();
            return;
        }

        GameSnapshot *snapshot = new GameSnapshot(this);
        if (!snapshot->load(dir.filePath(file)) || !snapshot->isEligible()
            || snapshot->getTurnSerial() != turnSerial) {
            delete snapshot;
            discardVerified();
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
            discardVerified();
            return;
        }
        VerifiedSnapshot verified;
        verified.path = dir.filePath(file);
        verified.snapshot = snapshot;
        verified.playerName = playerName;
        verified.playerTurnCount = playerTurnCount;
        verifiedSnapshots.append(verified);
    }

    // Every manifest entry must identify exactly one replay node.  Silently
    // accepting a missing or ambiguous identity could resume the wrong turn.
    QMap<int, QString> snapshotPaths;
    QMap<int, GameSnapshot *> snapshotsByNode;
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
            discardVerified();
            return;
        }
        snapshotPaths.insert(matchedNode, verified.path);
        snapshotsByNode.insert(matchedNode, verified.snapshot);
    }

    // A manifest with no entries is valid but simply offers no takeover node.
    for (const VerifiedSnapshot &verified : verifiedSnapshots)
        m_snapshots.append(verified.snapshot);
    m_takeoverSnapshotPaths = snapshotPaths;
    m_takeoverSnapshotsByNode = snapshotsByNode;
    m_takeoverSnapshotsValid = !m_takeoverSnapshotPaths.isEmpty();
}

GameSnapshot* Replayer::getSnapshot(int nodeIndex) const
{
    if (!m_index || !m_takeoverSnapshotPaths.contains(nodeIndex))
        return nullptr;

    return m_takeoverSnapshotsByNode.value(nodeIndex, nullptr);
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
    // A re-entrant export request from a seek presentation callback must not
    // wait on the dispatch lock already held by the same GUI thread.
    {
        QMutexLocker locker(&mutex);
        if (m_seeking)
            return 0;
    }

    QMutexLocker dispatchLocker(&m_dispatchMutex);
    quint64 requestId = 0;
    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested || !isRunning()
            || m_seeking
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
    return qBound(0, m_currentPairIndex.load(std::memory_order_relaxed),
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
    quint64 handledSeekGeneration = 0;

    QList<CommandType> nondelays;
    nondelays << S_COMMAND_ADD_PLAYER
        << S_COMMAND_REMOVE_PLAYER
        << S_COMMAND_SPEAK;

    for (m_currentPairIndex.store(0, std::memory_order_relaxed);
         m_currentPairIndex.load(std::memory_order_relaxed) < m_events.size();
         m_currentPairIndex.fetch_add(1, std::memory_order_relaxed)) {
        const int pairIndex = m_currentPairIndex.load(std::memory_order_relaxed);
        bool stopRequested = false;
        int seekTarget = -1;
        {
            QMutexLocker dispatchLocker(&m_dispatchMutex);
            QMutexLocker locker(&mutex);
            stopRequested = m_stopRequested;
            if (m_seekGeneration != handledSeekGeneration) {
                handledSeekGeneration = m_seekGeneration;
                seekTarget = m_currentPairIndex.load(std::memory_order_relaxed);
            }
        }
        if (stopRequested)
            break;
        if (seekTarget >= 0) {
            last = m_events.at(seekTarget).elapsedMs;
            continue;
        }

        const ReplayEvent event = m_events.at(pairIndex);
        qint64 delay = qMin<qint64>(event.elapsedMs - last, 2500);
        last = event.elapsedMs;

        bool delayed = true;
        if (nondelays.contains(static_cast<CommandType>(event.message.command)))
            delayed = false;

        if (delayed) {
            delay /= getSpeed();

            qint64 remainingDelay = delay;
            bool stoppedDuringDelay = false;
            while (remainingDelay > 0) {
                if (!waitForStateCaptureBoundary()) {
                    stoppedDuringDelay = true;
                    break;
                }
                const unsigned long slice = static_cast<unsigned long>(qMin<qint64>(remainingDelay, 20));
                msleep(slice);
                remainingDelay -= slice;
                QMutexLocker locker(&mutex);
                if (m_stopRequested) {
                    stoppedDuringDelay = true;
                    break;
                }
            }
            if (stoppedDuringDelay)
                break;

            // Seek also publishes its target time under the dispatch lock.
            // Suppress this old cursor's time if that seek won the race.
            bool staleTimeAfterSeek = false;
            {
                QMutexLocker dispatchLocker(&m_dispatchMutex);
                {
                    QMutexLocker locker(&mutex);
                    stopRequested = m_stopRequested;
                    if (m_seekGeneration != handledSeekGeneration) {
                        handledSeekGeneration = m_seekGeneration;
                        seekTarget = m_currentPairIndex.load(
                            std::memory_order_relaxed);
                        staleTimeAfterSeek = true;
                    }
                }
                if (!stopRequested && !staleTimeAfterSeek)
                    emit elasped(elapsedSecondsForUi(event.elapsedMs));
            }
            if (stopRequested)
                break;
            if (staleTimeAfterSeek) {
                if (seekTarget >= 0 && seekTarget < m_events.size())
                    last = m_events.at(seekTarget).elapsedMs;
                continue;
            }

            if (!waitWhilePlaybackPaused())
                break;
        }

        // Serialize the final boundary check with seek and capture requests.
        // A seek replays through its target itself; this worker then skips the
        // stale event and resumes at target + 1.
        bool eventDispatched = false;
        bool staleAfterSeek = false;
        while (!eventDispatched && !staleAfterSeek) {
            if (!waitForStateCaptureBoundary()) {
                stopRequested = true;
                break;
            }

            bool captureRequested = false;
            {
                QMutexLocker dispatchLocker(&m_dispatchMutex);
                {
                    QMutexLocker locker(&mutex);
                    stopRequested = m_stopRequested;
                    captureRequested = m_stateCaptureState
                        == StateCaptureState::Requested;
                    if (m_seekGeneration != handledSeekGeneration) {
                        handledSeekGeneration = m_seekGeneration;
                        seekTarget = m_currentPairIndex.load(
                            std::memory_order_relaxed);
                        staleAfterSeek = true;
                    }
                }
                if (!stopRequested && !captureRequested && !staleAfterSeek) {
                    emitCommand(pairIndex);
                    eventDispatched = true;
                }
            }
            // A request may have arrived after the earlier barrier check but
            // before the dispatch lock. Loop once so the worker reaches it.
            if (captureRequested)
                continue;
        }
        if (stopRequested)
            break;
        if (staleAfterSeek) {
            if (seekTarget >= 0 && seekTarget < m_events.size())
                last = m_events.at(seekTarget).elapsedMs;
            continue;
        }

        int nodeIndex = m_index ? m_index->findNearestNode(pairIndex) : -1;
        if (nodeIndex >= 0) {
            emit node_reached(nodeIndex);
        }
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

    QMutexLocker dispatchLocker(&m_dispatchMutex);
    {
        QMutexLocker locker(&mutex);
        if (m_stopRequested || m_stateCaptureState != StateCaptureState::Idle)
            return;
        m_seeking = true;
        ++m_seekGeneration;
        m_currentPairIndex.store(pairIndex, std::memory_order_relaxed);
    }

    emit elasped(elapsedSecondsForUi(m_events.at(pairIndex).elapsedMs));

    for (int i = 0; i <= pairIndex; i++)
        emitCommand(i);

    {
        QMutexLocker locker(&mutex);
        m_seeking = false;
    }
    emit seek_finished();
}

void Replayer::emitCommand(int pairIndex)
{
    if (pairIndex >= 0 && pairIndex < m_events.size()) {
        const ReplayEvent &event = m_events.at(pairIndex);
        emit command_parsed(event.message);
        emit replayEventDispatched(event.message, pairIndex, event.elapsedMs);
    }
}

QString Replayer::getPath() const
{
    return filename;
}

