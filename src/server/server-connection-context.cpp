#include "server-connection-context.h"

#include "protocol.h"
#include "socket.h"

using namespace QSanProtocol;

namespace
{
bool reportFailure(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}
}

ServerConnectionContext::ServerConnectionContext(
    ClientSocket *socket, quint64 generation, QObject *parent)
    : QObject(parent), m_socket(socket), m_generation(generation),
      m_signupDeadline(30000)
{
}

ClientSocket *ServerConnectionContext::socket() const
{
    return m_socket.data();
}

quint64 ServerConnectionContext::generation() const
{
    return m_generation;
}

ServerConnectionPhase ServerConnectionContext::phase() const
{
    return m_phase;
}

bool ServerConnectionContext::sendHello(
    const ServerHelloPayload &payload, QString *error)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Lobby;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_CHECK_VERSION;
    message.hasPayload = true;
    message.payload = payload.toVariant();
    return send(message, error);
}

bool ServerConnectionContext::acceptSignupFrame(
    QByteArrayView frame, SignupRequestPayload *payload, quint64 *requestId,
    QString *error)
{
    if (error != nullptr)
        error->clear();
    if (m_phase != ServerConnectionPhase::AwaitingSignup)
        return reportFailure(error, QStringLiteral("Connection is not awaiting SIGNUP"));
    if (m_signupDeadline.hasExpired())
        return reportFailure(error, QStringLiteral("SIGNUP deadline expired"));

    ProtocolMessage message;
    const ProtocolDecodeResult decoded = m_router.decode(frame, &message);
    if (!decoded.success)
        return reportFailure(error, QStringLiteral("V2 SIGNUP decode failed: %1").arg(decoded.detail));
    if (message.type != ProtocolMessageType::Request
        || message.source != ProtocolEndpoint::Client
        || message.destination != ProtocolEndpoint::Lobby
        || message.command != S_COMMAND_SIGNUP
        || message.replyTo != 0) {
        return reportFailure(error, QStringLiteral("Expected V2 Client-to-Lobby SIGNUP request"));
    }
    if (message.messageId == 0 || message.messageId <= m_lastIncomingId)
        return reportFailure(error, QStringLiteral("Client message_id must increase monotonically"));

    SignupRequestPayload parsed;
    if (!SignupRequestPayload::parse(message.payload, &parsed, error))
        return false;
    m_lastIncomingId = message.messageId;
    if (payload != nullptr)
        *payload = parsed;
    if (requestId != nullptr)
        *requestId = message.messageId;
    return true;
}

bool ServerConnectionContext::sendSignupReply(
    const SignupReplyPayload &payload, quint64 replyTo, QString *error)
{
    if (replyTo == 0)
        return reportFailure(error, QStringLiteral("SIGNUP reply requires reply_to"));
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Lobby;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_SIGNUP;
    message.replyTo = replyTo;
    message.hasPayload = true;
    message.payload = payload.toVariant();
    return send(message, error);
}

bool ServerConnectionContext::sendDiagnostic(
    const DiagnosticPayload &payload, QString *error)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Lobby;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_WARN;
    message.hasPayload = true;
    message.payload = payload.toVariant();
    return send(message, error);
}

ProtocolConnectionState ServerConnectionContext::releaseProtocolState()
{
    ProtocolConnectionState state;
    state.generation = m_generation;
    state.nextOutgoingMessageId = m_outgoingIds.nextValue();
    state.lastIncomingMessageId = m_lastIncomingId;
    m_phase = ServerConnectionPhase::Transferred;
    m_socket.clear();
    return state;
}

void ServerConnectionContext::fail()
{
    m_phase = ServerConnectionPhase::Failed;
}

bool ServerConnectionContext::send(ProtocolMessage message, QString *error)
{
    if (m_socket.isNull())
        return reportFailure(error, QStringLiteral("Connection socket is unavailable"));
    if (message.messageId == 0)
        message.messageId = m_outgoingIds.next();
    if (message.messageId == 0)
        return reportFailure(error, QStringLiteral("Server message IDs are exhausted"));
    const QByteArray frame = m_router.encode(message, error);
    if (frame.isEmpty())
        return false;
    m_socket->send(frame);
    return true;
}
