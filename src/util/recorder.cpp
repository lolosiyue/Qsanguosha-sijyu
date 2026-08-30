#include "recorder.h"
#include "protocol.h"
#include "engine.h"
#include "replay-index.h"
#include "game-snapshot.h"
#include "replay/replay-container.h"

#include <QFile>
#include <QDir>

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
    filename(filename), speed(1.0), playing(true), m_seeking(false), m_currentPairIndex(0),
    m_loadError(ReplayLoadError::None),
    m_formatVersion(ReplayFormatVersion::V2),
    m_messageProtocolVersion(ProtocolVersion::V2), m_index(nullptr)
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
    QString snapshotPath = GameSnapshot::getSnapshotDir(filename);
    QDir dir(snapshotPath);
    if (!dir.exists())
        return;

    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

    foreach (const QString &file, files) {
        QString filepath = snapshotPath + "/" + file;
        GameSnapshot *snapshot = new GameSnapshot(filepath, this);
        m_snapshots.append(snapshot);
    }
}

GameSnapshot* Replayer::getSnapshot(int nodeIndex) const
{
    if (!m_index || nodeIndex < 0 || nodeIndex >= m_index->getNodeCount())
        return nullptr;

    ReplayNode node = m_index->getNode(nodeIndex);
    if (node.snapshotIndex < 0)
        return nullptr;

    foreach (GameSnapshot *snapshot, m_snapshots) {
        if (snapshot->getTurnCount() == node.turnCount)
            return snapshot;
    }
    return nullptr;
}

int Replayer::getCurrentPairIndex() const
{
    return m_currentPairIndex;
}

qint64 Replayer::getCurrentElapsed() const
{
    if (m_currentPairIndex >= 0 && m_currentPairIndex < m_events.size())
        return m_events[m_currentPairIndex].elapsedMs;
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
    playing = !playing;
    if (playing)
        play_sem.release(); // to play
}

void Replayer::run()
{
    qint64 last = 0;

    QList<CommandType> nondelays;
    nondelays << S_COMMAND_ADD_PLAYER
        << S_COMMAND_REMOVE_PLAYER
        << S_COMMAND_SPEAK;

    for (m_currentPairIndex = 0; m_currentPairIndex < m_events.size(); m_currentPairIndex++) {
        if (m_seeking) {
            mutex.lock();
            bool shouldSeek = m_seeking;
            int seekTarget = m_currentPairIndex;
            mutex.unlock();

            if (shouldSeek) {
                emit seek_finished();
            }
        }

        const ReplayEvent event = m_events.at(m_currentPairIndex);
        qint64 delay = qMin<qint64>(event.elapsedMs - last, 2500);
        last = event.elapsedMs;

        bool delayed = true;
        if (nondelays.contains(static_cast<CommandType>(event.message.command)))
            delayed = false;

        if (delayed) {
            delay /= getSpeed();

            msleep(static_cast<unsigned long>(delay));
            emit elasped(elapsedSecondsForUi(event.elapsedMs));

            if (!playing)
                play_sem.acquire();
        }

        emit command_parsed(event.message);

        int nodeIndex = m_index ? m_index->findNearestNode(m_currentPairIndex) : -1;
        if (nodeIndex >= 0) {
            emit node_reached(nodeIndex);
        }
    }
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

    mutex.lock();
    m_seeking = true;
    m_currentPairIndex = pairIndex;
    mutex.unlock();

    emit elasped(elapsedSecondsForUi(m_events.at(pairIndex).elapsedMs));

    for (int i = 0; i <= pairIndex; i++) {
        emit command_parsed(m_events.at(i).message);
    }

    emit seek_finished();
}

void Replayer::emitCommand(int pairIndex)
{
    if (pairIndex >= 0 && pairIndex < m_events.size()) {
        emit command_parsed(m_events.at(pairIndex).message);
    }
}

QString Replayer::getPath() const
{
    return filename;
}

