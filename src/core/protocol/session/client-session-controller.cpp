#include "client-session-controller.h"

#include "protocol.h"

#include <limits>

using namespace QSanProtocol;

namespace
{
bool reject(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isFlow(const ProtocolMessage &message, ProtocolMessageType type,
            ProtocolEndpoint source, ProtocolEndpoint destination, int command)
{
    return message.type == type && message.source == source
        && message.destination == destination && message.command == command;
}
}

void ClientSessionController::reset()
{
    if (m_generation != std::numeric_limits<quint64>::max())
        ++m_generation;
    m_phase = ClientSessionPhase::AwaitingHello;
    m_outgoingIds.reset();
    m_lastIncomingId = 0;
    m_signupRequestId = 0;
}

ClientSessionPhase ClientSessionController::phase() const
{
    return m_phase;
}

quint64 ClientSessionController::generation() const
{
    return m_generation;
}

quint64 ClientSessionController::lastIncomingMessageId() const
{
    return m_lastIncomingId;
}

bool ClientSessionController::acceptIncoming(const ProtocolMessage &message,
                                             QString *error)
{
    if (error != nullptr)
        error->clear();
    if (m_phase == ClientSessionPhase::Failed)
        return reject(error, QStringLiteral("Client protocol session has failed"));
    if (message.messageId == 0 || message.messageId <= m_lastIncomingId)
        return reject(error, QStringLiteral("Server message_id must increase monotonically"));

    if (m_phase == ClientSessionPhase::AwaitingHello) {
        if (!isFlow(message, ProtocolMessageType::Notification,
                    ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                    S_COMMAND_CHECK_VERSION)) {
            return reject(error, QStringLiteral("First server frame must be V2 SERVER_HELLO"));
        }
        ServerHelloPayload payload;
        if (!ServerHelloPayload::parse(message.payload, &payload, error))
            return false;
        m_phase = ClientSessionPhase::HelloAccepted;
    } else if (m_phase == ClientSessionPhase::AwaitingSignupReply) {
        if (!isFlow(message, ProtocolMessageType::Reply,
                    ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                    S_COMMAND_SIGNUP)
            || message.replyTo != m_signupRequestId) {
            return reject(error, QStringLiteral("Expected correlated V2 SIGNUP reply"));
        }
        SignupReplyPayload payload;
        if (!SignupReplyPayload::parse(message.payload, &payload, error))
            return false;
        m_phase = payload.accepted ? ClientSessionPhase::AwaitingSetup
                                   : ClientSessionPhase::Failed;
    } else if (m_phase == ClientSessionPhase::AwaitingSetup) {
        if (!isFlow(message, ProtocolMessageType::Notification,
                    ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                    S_COMMAND_SETUP)) {
            return reject(error, QStringLiteral("Expected typed V2 SETUP after SIGNUP"));
        }
        SetupPayload payload;
        if (!SetupPayload::parse(message.payload, &payload, error))
            return false;
        m_phase = ClientSessionPhase::ReadyPending;
    } else if (m_phase != ClientSessionPhase::Active) {
        return reject(error, QStringLiteral("Server application traffic arrived before READY"));
    }

    m_lastIncomingId = message.messageId;
    return true;
}

bool ClientSessionController::makeSignupRequest(
    const SignupRequestPayload &payload, ProtocolMessage *message, QString *error)
{
    if (m_phase != ClientSessionPhase::HelloAccepted)
        return reject(error, QStringLiteral("SIGNUP may only follow SERVER_HELLO"));
    if (message == nullptr)
        return reject(error, QStringLiteral("SIGNUP output is null"));

    ProtocolMessage request;
    request.type = ProtocolMessageType::Request;
    request.source = ProtocolEndpoint::Client;
    request.destination = ProtocolEndpoint::Lobby;
    request.command = S_COMMAND_SIGNUP;
    request.hasPayload = true;
    request.payload = payload.toVariant();
    if (!assignMessageId(&request, error))
        return false;
    m_signupRequestId = request.messageId;
    m_phase = ClientSessionPhase::AwaitingSignupReply;
    *message = request;
    return true;
}

bool ClientSessionController::makeReadyNotification(
    ProtocolMessage *message, QString *error)
{
    if (m_phase != ClientSessionPhase::ReadyPending)
        return reject(error, QStringLiteral("READY may only follow SETUP"));
    if (message == nullptr)
        return reject(error, QStringLiteral("READY output is null"));

    ProtocolMessage ready;
    ready.type = ProtocolMessageType::Notification;
    ready.source = ProtocolEndpoint::Client;
    ready.destination = ProtocolEndpoint::Room;
    ready.command = S_COMMAND_READY;
    ready.hasPayload = true;
    ready.payload = ReadyPayload().toVariant();
    if (!assignMessageId(&ready, error))
        return false;
    m_phase = ClientSessionPhase::Active;
    *message = ready;
    return true;
}

bool ClientSessionController::prepareApplicationMessage(
    ProtocolMessage *message, QString *error)
{
    if (m_phase != ClientSessionPhase::Active)
        return reject(error, QStringLiteral("Client application traffic requires an active V2 session"));
    return assignMessageId(message, error);
}

void ClientSessionController::fail()
{
    m_phase = ClientSessionPhase::Failed;
}

bool ClientSessionController::assignMessageId(
    ProtocolMessage *message, QString *error)
{
    if (message == nullptr)
        return reject(error, QStringLiteral("Protocol message output is null"));
    if (message->messageId == 0)
        message->messageId = m_outgoingIds.next();
    if (message->messageId == 0)
        return reject(error, QStringLiteral("Client message IDs are exhausted"));
    message->version = ProtocolVersion::V2;
    return true;
}
