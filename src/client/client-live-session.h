#ifndef CLIENT_LIVE_SESSION_H
#define CLIENT_LIVE_SESSION_H

#include "core/client-game-state.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/client-session-controller.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTimer>

class ClientCore;
class ClientSocket;
struct InteractionResponse;

struct ClientLiveSessionOptions
{
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 9527;
    QString screenName = QStringLiteral("TUI");
    QString avatar = QStringLiteral("caocao");
    bool reconnectRequested = false;
    bool automaticSignup = true;
    int connectTimeoutMs = 10000;
    int handshakeTimeoutMs = 30000;
};

class ClientLiveSession final : public QObject
{
    Q_OBJECT

public:
    explicit ClientLiveSession(ClientCore *core, QObject *parent = nullptr);

    void connectToServer(const ClientLiveSessionOptions &options,
                         ClientSocket *injectedSocket = nullptr);
    void reconnect();
    void disconnectGracefully();
    bool isActive() const;
    quint64 generation() const;
    ClientLiveSessionOptions options() const { return m_options; }

    bool requestSignup(QString *error = nullptr);
    bool sendControl(QSanProtocol::CommandType command, const QVariant &payload,
                     QString *error = nullptr);
    bool sendRequest(QSanProtocol::CommandType command, const QVariant &payload,
                     QString *error = nullptr);
    bool sendReply(QSanProtocol::CommandType command, const QVariant &payload,
                   quint64 replyTo, QString *error = nullptr);
    bool submitInteractionResponse(InteractionResponse response,
                                   QString *error = nullptr);

signals:
    void transportConnected();
    void connectionChanged(const QString &state);
    void sessionActive(bool reconnected);
    void protocolMessageReceived(const QSanProtocol::ProtocolMessage &message);
    void frontendMessageReceived(const QSanProtocol::ProtocolMessage &message);
    void stateChanged();
    void interactionRequested(const QSanProtocol::ProtocolMessage &message);
    void presentationEvent(int command, const QString &text);
    void commandResult(int command, bool success, const QString &message);
    void disconnected();
    void fatalError(int exitCode, const QString &code, const QString &message);

private:
    void beginConnection(bool reconnectRequested, ClientSocket *injectedSocket = nullptr);
    bool consumeFrame(const QByteArray &frame, quint64 generation);
    bool dispatchMessage(const QSanProtocol::ProtocolMessage &message);
    bool sendPrepared(QSanProtocol::ProtocolMessage *message, bool applicationMessage,
                      QString *error = nullptr);
    bool writeFrame(const QSanProtocol::ProtocolMessage &message, QString *error);
    void startPhaseTimeout(const QString &phase, int timeoutMs);
    void fail(int exitCode, const QString &code, const QString &detail);

    ClientCore *m_core = nullptr;
    QPointer<ClientSocket> m_socket;
    ClientLiveSessionOptions m_options;
    QSanProtocol::ProtocolCodecRouter m_router;
    QSanProtocol::ClientSessionController m_session;
    QTimer m_phaseTimer;
    QHash<quint64, int> m_pendingRequests;
    QHash<quint64, qint64> m_requestStartedAt;
    ClientGameState m_pendingState;
    QList<QPair<int, QString>> m_pendingPresentationEvents;
    QList<QSanProtocol::ProtocolMessage> m_pendingFrontendMessages;
    QString m_syncId;
    bool m_syncActive = false;
    bool m_reconnectAttempt = false;
    bool m_shuttingDown = false;
    bool m_failureEmitted = false;
};

#endif
