#ifndef _RECORDER_H
#define _RECORDER_H

#include <QElapsedTimer>
#include <QSemaphore>
#include <QMutex>

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
    void recordLine(const QString &line);
    QList<QByteArray> getRecords() const;

public slots:
    void record(const char *line);

private:
    QElapsedTimer watch;
    QByteArray data;
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
    int getCurrentElapsed() const;

    ReplayIndex* getIndex() const;
    GameSnapshot* getSnapshot(int nodeIndex) const;

    int m_commandSeriesCounter;

public slots:
    void uniform();
    void toggle();
    void speedUp();
    void slowDown();
    void jumpToNode(int nodeIndex);
    void jumpToElapsed(int elapsed);
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

    struct Pair
    {
        int elapsed;
        QString cmd;
    };
    QList<Pair> pairs;

    ReplayIndex *m_index;
    QList<GameSnapshot*> m_snapshots;

signals:
    void command_parsed(const QString &cmd);
    void elasped(int secs);
    void speed_changed(qreal speed);
    void node_reached(int nodeIndex);
    void seek_finished();
};

#endif