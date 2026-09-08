#ifndef QSAN_LOCAL_SERVER_CONTROLLER_H
#define QSAN_LOCAL_SERVER_CONTROLLER_H
#include "xp-control-protocol.h"
#include "game-session-config.h"
#include <QElapsedTimer>
#include <QHash>
#include <QLocalServer>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class LocalServerController : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Launching, Handshaking, Initializing, Ready, Stopping };
    enum class Ownership { OwnedPrivate, OwnedHost };
    explicit LocalServerController(QObject *parent = nullptr);
    ~LocalServerController() override;
    bool start(Ownership ownership, bool hostOnly, const GameSessionConfig &config,
               const QString &replayPath = QString());
    void stop();
    QString request(const QString &type, const QJsonObject &body = {});
    QString finalizeReplay(const QString &path, const QString &playerId);
    bool active() const { return m_state != State::Idle; }
    bool isReady() const { return m_state == State::Ready; }
    bool hostOnly() const { return m_hostOnly; }
    bool takeoverSession() const { return active() && m_takeoverSession; }
    QString endpoint() const { return m_endpoint; }
    QString generation() const { return QString::number(m_generation); }
    QStringList startupMessages() const { return m_messages; }
    QJsonObject status() const { return m_status; }
signals:
    void ready();
    void failed(const QString &error);
    void stopped(bool graceful);
    void progress(const QString &phase);
    void logMessage(const QString &message);
    void statusChanged(const QJsonObject &status);
    void commandResult(const QString &id, bool success, const QJsonObject &body);
    void takeoverReady();
    void takeoverFailed(const QString &error);
    void replayFinalized(const QString &path, bool success, const QString &detail);
private:
    void acceptConnections();
    void receive(XpControl::Channel *channel, const QJsonObject &message);
    void fail(const QString &code);
    void transition(State state, int timeoutMs);
    bool send(const QString &id, const QString &type, const QJsonObject &body = {});
    void reap(bool graceful);
    QLocalServer m_listener;
    QPointer<XpControl::Channel> m_channel;
    QProcess *m_process = nullptr;
    QList<QPointer<XpControl::Channel>> m_candidates;
    std::unique_ptr<QTemporaryDir> m_directory;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_deadline = 0;
    State m_state = State::Idle;
    quint64 m_generation = 0;
    quint64 m_nextId = 2;
    QString m_session, m_token, m_endpoint;
    QStringList m_messages;
    QJsonObject m_initialize, m_status;
    QHash<QString, qint64> m_requests;
    QHash<QString, QJsonObject> m_banRequests;
    QHash<QString, QString> m_replayRequests;
    bool m_hostOnly = false;
    bool m_takeoverSession = false;
    bool m_failed = false;
    bool m_shutdownComplete = false;
    bool m_forced = false;
    bool m_takeoverPending = false;
    qint64 m_takeoverDeadline = 0;
};
#endif
