#include "recorder.h"
#include "serverplayer.h"
#include "replay-index.h"
#include "game-snapshot.h"

#include <QFile>
#include <QBuffer>
#include <QDir>

using namespace QSanProtocol;

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    watch.start();
}

void Recorder::record(const char *line)
{
    recordLine(line);
}

void Recorder::recordLine(const QString &line)
{
    int elapsed = watch.elapsed();
    if (line.endsWith("\n"))
        data.append(QString("%1 %2").arg(elapsed).arg(line));
    else
        data.append(QString("%1 %2\n").arg(elapsed).arg(line));
}

bool Recorder::save(const QString &filename) const
{
    qDebug(filename.toUtf8().data());
    if (filename.endsWith(".txt")) {
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            return file.write(data) != -1;
        else
            return false;
    } else if (filename.endsWith(".png")) {
        return TXT2PNG(data).save(filename);
    } else
        return false;
}

QList<QByteArray> Recorder::getRecords() const
{
    QList<QByteArray> records = data.split('\n');
    return records;
}

QImage Recorder::TXT2PNG(QByteArray txtData)
{
    QByteArray data = qCompress(txtData, 9);
    qint32 actual_size = data.size();
    data.prepend((const char *)&actual_size, sizeof(qint32));

    // actual data = width * height - padding
    int width = ceil(sqrt((double)data.size()));
    int height=width;
    int padding = width * height - data.size();
    QByteArray paddingData;
    paddingData.fill('\0', padding);
    data.append(paddingData);

    QImage image((const uchar *)data.constData(), width, height, QImage::Format_ARGB32);
    return image;
}

QByteArray Recorder::PNG2TXT(const QString filename)
{
    QImage image(filename);
    image = image.convertToFormat(QImage::Format_ARGB32);
    const uchar *imageData = image.bits();
    qint32 actual_size = *(const qint32 *)imageData;
    QByteArray data((const char *)(imageData + 4), actual_size);
    data = qUncompress(data);

    return data;
}

Replayer::Replayer(QObject *parent, const QString &filename)
    : QThread(parent), m_commandSeriesCounter(1),
    filename(filename), speed(1.0), playing(true), m_seeking(false), m_currentPairIndex(0),
    m_index(nullptr)
{
    QIODevice *device = nullptr;
    if (filename.endsWith(".png")) {
        QByteArray *data = new QByteArray(Recorder::PNG2TXT(filename));
        QBuffer *buffer = new QBuffer(data);
        device = buffer;
    } else if (filename.endsWith(".txt")) {
        QFile *file = new QFile(filename);
        device = file;
    }

    if (device == nullptr)
        return;

    if (!device->open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    typedef char buffer_t[16000];

    while (!device->atEnd()) {
        buffer_t line;
        memset(line, 0, sizeof(buffer_t));
        device->readLine(line, sizeof(buffer_t));

        char *space = strchr(line, ' ');
        if (space == nullptr)
            continue;

        *space = '\0';
        QString cmd = space + 1;
        int elapsed = atoi(line);

        Pair pair;
        pair.elapsed = elapsed;
        pair.cmd = cmd;

        pairs << pair;
    }

	delete device;

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

    QList<QPair<int, QString>> pairList;
    foreach (const Pair &p, pairs) {
        pairList << qMakePair(p.elapsed, p.cmd);
    }
    m_index->buildIndex(pairList);

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

int Replayer::getCurrentElapsed() const
{
    if (m_currentPairIndex >= 0 && m_currentPairIndex < pairs.size())
        return pairs[m_currentPairIndex].elapsed;
    return 0;
}

ReplayIndex* Replayer::getIndex() const
{
    return m_index;
}

int Replayer::getDuration() const
{
    return pairs.last().elapsed / 1000.0;
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
    int last = 0;

    QList<CommandType> nondelays;
    nondelays << S_COMMAND_ADD_PLAYER
        << S_COMMAND_REMOVE_PLAYER
        << S_COMMAND_SPEAK;

    for (m_currentPairIndex = 0; m_currentPairIndex < pairs.size(); m_currentPairIndex++) {
        if (m_seeking) {
            mutex.lock();
            bool shouldSeek = m_seeking;
            int seekTarget = m_currentPairIndex;
            mutex.unlock();

            if (shouldSeek) {
                emit seek_finished();
            }
        }

        Pair pair = pairs[m_currentPairIndex];
        int delay = qMin(pair.elapsed - last, 2500);
        last = pair.elapsed;

        bool delayed = true;
        Packet packet;
        if (packet.parse(pair.cmd.toLatin1().constData())) {
            if (nondelays.contains(packet.getCommandType()))
                delayed = false;
        }

        if (delayed) {
            delay /= getSpeed();

            msleep(delay);
            emit elasped(pair.elapsed / 1000.0);

            if (!playing)
                play_sem.acquire();
        }

        emit command_parsed(pair.cmd);

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

void Replayer::jumpToElapsed(int elapsed)
{
    int bestIndex = 0;
    int bestDiff = INT_MAX;

    for (int i = 0; i < pairs.size(); i++) {
        int diff = qAbs(pairs[i].elapsed - elapsed);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = i;
        }
    }

    seekToPosition(bestIndex);
}

void Replayer::seekToPosition(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= pairs.size())
        return;

    mutex.lock();
    m_seeking = true;
    m_currentPairIndex = pairIndex;
    mutex.unlock();

    emit elasped(pairs[pairIndex].elapsed / 1000.0);

    for (int i = 0; i <= pairIndex; i++) {
        emit command_parsed(pairs[i].cmd);
    }

    emit seek_finished();
}

void Replayer::emitCommand(int pairIndex)
{
    if (pairIndex >= 0 && pairIndex < pairs.size()) {
        emit command_parsed(pairs[pairIndex].cmd);
    }
}

QString Replayer::getPath() const
{
    return filename;
}

