#ifndef _REPLAY_TAKEOVER_H
#define _REPLAY_TAKEOVER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariant>

class Replayer;
class ReplayGameState;
class Recorder;
class ServerPlayer;
class GameSnapshot;
class Client;

class ReplayTakeoverManager : public QObject
{
    Q_OBJECT

public:
    explicit ReplayTakeoverManager(Replayer *replayer, QObject *parent = nullptr);
    ~ReplayTakeoverManager();

    void setTakeoverTarget(const QString &playerName);
    void enableTakeover();
    void disableTakeover();

    bool isTakeoverEnabled() const;
    QString getTakeoverTarget() const;

    void processRequest(const QString &cmd);
    QVariant generateAIResponse(const QString &cmd, const QString &playerName);

    void saveNewReplay(const QString &filepath);
    QString generateNewReplayFilename() const;

signals:
    void takeoverEnabled(const QString &playerName);
    void takeoverDisabled();
    void requestProcessed(const QString &playerName, const QVariant &response);
    void perspectiveChanged(const QString &playerName);

private slots:
    void onCommandParsed(const QString &cmd);
    void onSeekFinished();

private:
    void initializeFromReplay();
    void syncHandcards(const QString &playerName);
    void recordCommand(const QString &cmd);

    Replayer *m_replayer;
    ReplayGameState *m_gameState;
    QString m_takeoverTarget;
    bool m_takeoverEnabled;
    int m_startPairIndex;

    Recorder *m_newRecorder;
    QList<QString> m_newCommands;
    QDateTime m_startTime;

    QMap<QString, bool> m_playerAIEnabled;
};

#endif