#ifndef SERVER_CONNECTION_CONTEXT_H
#define SERVER_CONNECTION_CONTEXT_H

#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>

class ClientSocket;

enum class ServerConnectionPhase
{
    AwaitingSignup,
    Transferred,
    Failed
};

class ServerConnectionContext : public QObject
{
    Q_OBJECT

public:
    ServerConnectionContext(ClientSocket *socket, quint64 generation,
                            QObject *parent = nullptr);

    ClientSocket *socket() const;
    quint64 generation() const;
    ServerConnectionPhase phase() const;

    bool sendHello(const QSanProtocol::ServerHelloPayload &payload,
                   QString *error = nullptr);
    bool acceptSignupFrame(QByteArrayView frame,
                           QSanProtocol::SignupRequestPayload *payload,
                           quint64 *requestId,
                           QString *error = nullptr);
    bool sendSignupReply(const QSanProtocol::SignupReplyPayload &payload,
                         quint64 replyTo, QString *error = nullptr);
    bool sendDiagnostic(const QSanProtocol::DiagnosticPayload &payload,
                        QString *error = nullptr);

    QSanProtocol::ProtocolConnectionState releaseProtocolState();
    void fail();

private:
    bool send(QSanProtocol::ProtocolMessage message, QString *error);

    QPointer<ClientSocket> m_socket;
    quint64 m_generation;
    ServerConnectionPhase m_phase = ServerConnectionPhase::AwaitingSignup;
    QDeadlineTimer m_signupDeadline;
    QSanProtocol::ProtocolCodecRouter m_router;
    QSanProtocol::ProtocolMessageIdGenerator m_outgoingIds;
    quint64 m_lastIncomingId = 0;
};

#endif
