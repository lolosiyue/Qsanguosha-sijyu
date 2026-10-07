#ifndef QSAN_MANAGED_REWIND_LAB_H
#define QSAN_MANAGED_REWIND_LAB_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QHash>
#include <QElapsedTimer>
#include "protocol/session/managed-rewind-payloads.h"

class Room;
class RoomThread;
class ServerPlayer;

// Explicit restricted debug entry: local console or an opt-in two-seat TCP room.
// Network mutations require the current owner and a connection-scoped token.
// Statistics exclude this profile. Every game operation executes on RoomThread.
class ManagedRewindLab final : public QObject
{
    Q_OBJECT
public:
    explicit ManagedRewindLab(QObject *parent = nullptr);
    ~ManagedRewindLab() override;
    bool start(quint64 seed, QString *error = nullptr);
    bool attach(Room *room, QString *error = nullptr);
    bool startAttached(QString *error = nullptr);
    void connected(ServerPlayer *player);
    void disconnected(ServerPlayer *player);
    bool isConnected(ServerPlayer *player);
    void control(ServerPlayer *player, const QVariant &value);
    void sendStatus(ServerPlayer *player, const QString &message = {}, const QString &ack = QStringLiteral("0"));
    bool submit(const QString &command, QString *error = nullptr);
    void stop();

signals:
    void lineReady(const QString &json);
    void stopped();

private:
    friend class RoomThread;
    bool initializePlayers(QString *error);
    void run(RoomThread &worker);
    void beforeTurn();
    void networkReport(const QString &message);
    void report(const QString &command, bool ok, const QString &error = QString());
    Room *m_room = nullptr;
    QMutex m_mutex;
    QWaitCondition m_wake;
    QString m_command;
    quint64 m_expectedGeneration = 0;
    quint64 m_visibleGeneration = 0;
    quint64 m_restoreNotifications = 0; // Worker-only post-publication observer.
    struct Peer { QString token; quint64 sequence = 0; bool connected = false; bool owner = false; QElapsedTimer resyncCooldown; };
    QHash<ServerPlayer *, Peer> m_peers;
    ServerPlayer *m_requestPeer = nullptr;
    QString m_requestToken, m_requestSequence, m_rootGameId, m_worldId;
    QElapsedTimer m_deadline;
    QString m_failure;
    bool m_requestReadOnly = false;
    bool m_executing = false;
    bool m_network = false;
    bool m_resyncRequested = false;
    bool m_initialSynced = false;
    bool m_ownedRoom = true;
    bool m_busy = true;
    bool m_stop = false;
    bool m_started = false;
};

#endif
