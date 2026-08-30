#ifndef CLIENT_SESSION_CONTROLLER_H
#define CLIENT_SESSION_CONTROLLER_H

#include "protocol/protocol-runtime.h"
#include "session-payloads.h"

namespace QSanProtocol {

enum class ClientSessionPhase
{
    AwaitingHello,
    HelloAccepted,
    AwaitingSignupReply,
    AwaitingSetup,
    ReadyPending,
    Active,
    Failed
};

class ClientSessionController
{
public:
    void reset();

    ClientSessionPhase phase() const;
    quint64 generation() const;
    quint64 lastIncomingMessageId() const;

    bool acceptIncoming(const ProtocolMessage &message,
                        QString *error = nullptr);
    bool makeSignupRequest(const SignupRequestPayload &payload,
                           ProtocolMessage *message,
                           QString *error = nullptr);
    bool makeReadyNotification(ProtocolMessage *message,
                               QString *error = nullptr);
    bool prepareApplicationMessage(ProtocolMessage *message,
                                   QString *error = nullptr);
    void fail();

private:
    bool assignMessageId(ProtocolMessage *message, QString *error);

    ClientSessionPhase m_phase = ClientSessionPhase::AwaitingHello;
    ProtocolMessageIdGenerator m_outgoingIds;
    quint64 m_generation = 1;
    quint64 m_lastIncomingId = 0;
    quint64 m_signupRequestId = 0;
};

}

#endif
