#ifndef _RECORDER_H
#define _RECORDER_H

#include <QSemaphore>
#include <QMutex>
#include <QImage>
#include <QThread>

#include "record-buffer.h"
#include "replay/replay-codec.h"

class ReplayIndex;
class GameSnapshot;

class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);

    static QImage TXT2PNG(QByteArray data);
    static QByteArray PNG2TXT(const QString filename);

    bool save(const QString &filename) const;
    bool recordMessage(const QSanProtocol::ProtocolMessage &message,
                       QString *error = nullptr);
    QList<QByteArray> getRecords() const;
    QByteArray rawReplayData() const;

private:
    RecordBuffer buffer;
};

class Replayer : public QThread
{
    Q_OBJECT

public:
    explicit Replayer(QObject *parent, const QString &filename);
    ~Replayer();

    int getDuration() const;
    qreal getSpeed();
    QString getPath() const;
    int getCurrentPairIndex() const;
    qint64 getCurrentElapsed() const;

    bool isValid() const;
    QSanReplay::ReplayLoadError loadError() const;
    QString errorString() const;
    QSanReplay::ReplayFormatVersion formatVersion() const;
    QSanProtocol::ProtocolVersion messageProtocolVersion() const;
    const QList<QSanReplay::ReplayEvent> &events() const;

    ReplayIndex* getIndex() const;
    GameSnapshot* getSnapshot(int nodeIndex) const;

    int m_commandSeriesCounter;

public slots:
    void uniform();
    void toggle();
    void speedUp();
    void slowDown();
    void jumpToNode(int nodeIndex);
    void jumpToElapsed(qint64 elapsed);
    void seekToPosition(int pairIndex);

protected:
    virtual void run();

private:
    void buildIndex();
    void loadSnapshots();
    void emitCommand(int pairIndex);

    QString filename;
    qreal speed;
    bool playing;
    bool m_seeking;
    int m_currentPairIndex;
    QMutex mutex;
    QSemaphore play_sem;

    QList<QSanReplay::ReplayEvent> m_events;
    QSanReplay::ReplayLoadError m_loadError;
    QString m_errorString;
    QSanReplay::ReplayFormatVersion m_formatVersion;
    QSanProtocol::ProtocolVersion m_messageProtocolVersion;

    ReplayIndex *m_index;
    QList<GameSnapshot*> m_snapshots;

signals:
    void command_parsed(const QSanProtocol::ProtocolMessage &message);
    void elasped(int secs);
    void speed_changed(qreal speed);
    void node_reached(int nodeIndex);
    void seek_finished();
};

#endif
