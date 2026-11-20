#ifndef QSAN_HEADLESS_REPLAY_SESSION_H
#define QSAN_HEADLESS_REPLAY_SESSION_H

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariant>

class Room;
class ServerPlayer;

// One game owns native recipient streams; no observer changes gameplay visibility.
class HeadlessReplaySession
{
public:
    HeadlessReplaySession(Room &room, const QString &batchDirectory, int gameNumber);
    bool initialize(QString *error);
    bool addPlayer(ServerPlayer *player, bool duringGame, QString *error);
    void completeBootstrap(ServerPlayer *player = nullptr);
    void setCardCatalog(const QVariant &catalog);
    QVariant cardCatalog() const;
    bool finish(const QString &status, const QString &winner,
                const QString &detail, QString *error);
    QString directory() const;

private:
    struct Perspective {
        QPointer<ServerPlayer> player;
        QString playerName;
        QString screenName;
        QString file;
        qint64 availableFromMs = 0;
    };
    bool writeIndex(const QString &status, const QString &winner,
                    const QString &detail, QString *error);

    Room &m_room;
    QString m_batchDirectory;
    QString m_directory;
    QString m_startedAt;
    int m_gameNumber;
    QElapsedTimer m_clock;
    QList<Perspective> m_perspectives;
    QVariant m_cardCatalog;
    bool m_finished = false;
};

#endif
