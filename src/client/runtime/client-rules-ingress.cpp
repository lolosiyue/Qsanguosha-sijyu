#include "client-rules-ingress.h"

#include "client-game-state-reducer.h"
#include "protocol-interaction-request-builder.h"
#include "protocol/protocol-payload-registry.h"
#include "protocol/rules-bundle-identity.h"
#include "protocol/session/session-payloads.h"
#include "protocol.h"

#include <QJsonArray>
#include <limits>
#include <utility>

using namespace QSanProtocol;

namespace {
bool reject(const QString &reason, QString *error)
{
    if (error != nullptr)
        *error = reason;
    return false;
}
QJsonObject snapshot(const ClientGameState &state)
{
    QJsonObject result = state.toJson();
    // toJson is also a diagnostics format and deliberately omits roster order.
    // The rules projection needs that order, not QMap key order or TS ordering.
    result.insert(QStringLiteral("player_names"), QJsonArray::fromStringList(state.playerNames()));
    return result;
}
}

bool ClientRulesIngress::reset(int generation, const QJsonObject &identity, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (generation < 0 || generation <= m_generation)
        return reject(QStringLiteral("stream_generation_not_new"), error);
    if (!QSanRules::validate(identity))
        return reject(QStringLiteral("rules_identity_invalid"), error);
    m_session.reset();
    m_state.reset();
    m_pending.reset();
    m_identity = identity;
    m_generation = generation;
    m_revision = 0;
    m_lastOutgoing = 0;
    m_syncId.clear();
    m_syncActive = false;
    m_failed = false;
    m_outgoingRequests.clear();
    clearRequest();
    return true;
}

void ClientRulesIngress::clearRequest()
{
    m_hasRequest = false;
    m_request = ProtocolMessage();
    m_interaction = InteractionRequest();
}

void ClientRulesIngress::invalidate()
{
    m_failed = true;
    m_session.fail();
    m_pending.reset();
    m_syncActive = false;
    m_syncId.clear();
    m_outgoingRequests.clear();
    clearRequest();
}

bool ClientRulesIngress::fail(const QString &reason, QString *error)
{
    invalidate();
    return reject(reason, error);
}

bool ClientRulesIngress::acceptFrame(int generation, bool sent, const QByteArray &frame,
                                     QString *error)
{
    if (error != nullptr)
        error->clear();
    // An obsolete transport cannot destroy or mutate the newer connection.
    if (m_generation < 0 || generation != m_generation)
        return reject(QStringLiteral("stream_stale_generation"), error);
    if (m_failed)
        return reject(QStringLiteral("stream_failed"), error);
    if (frame.isEmpty() || frame.size() > ProtocolFrameBuffer::MaxFrameSize)
        return fail(QStringLiteral("stream_frame_size"), error);
    if (m_revision == std::numeric_limits<int>::max())
        return fail(QStringLiteral("stream_revision_exhausted"), error);
    ProtocolMessage message;
    const ProtocolDecodeResult decoded = m_codec.decode(frame, &message);
    if (!decoded.success)
        return fail(QStringLiteral("stream_decode:") + decoded.detail, error);
    QString detail;
    if (!(sent ? outgoing(message, &detail) : incoming(message, &detail)))
        return fail(detail, error);
    ++m_revision;
    return true;
}

bool ClientRulesIngress::incoming(const ProtocolMessage &message, QString *error)
{
    if (message.destination != ProtocolEndpoint::Client
        || (message.source != ProtocolEndpoint::Room && message.source != ProtocolEndpoint::Lobby))
        return reject(QStringLiteral("stream_incoming_endpoint"), error);
    const auto before = m_session.phase();
    // Use the native phase/correlation implementation; do not duplicate its
    // Hello -> Signup -> Setup -> Ready transitions in this adapter.
    if (!m_session.acceptIncoming(message, error))
        return false;
    if (before == ClientSessionPhase::AwaitingHello) {
        const auto payload = message.payload.toMap();
        const QJsonObject advertised = QJsonObject::fromVariantMap(
            payload.value(QStringLiteral("rules_bundle")).toMap());
        const QString mismatch = QSanRules::compatibilityError(advertised, m_identity, true);
        if (!mismatch.isEmpty())
            return reject(mismatch, error);
        ServerHelloPayload hello;
        if (!ServerHelloPayload::parse(message.payload, &hello, error))
            return false;
        m_state.setCardIdSpace(hello.cardCount);
        m_state.setConnectionValue(QStringLiteral("game_version"), hello.gameVersion);
        m_state.setConnectionValue(QStringLiteral("mod_name"), hello.modName);
        m_state.setConnectionValue(QStringLiteral("rules_bundle"), advertised.toVariantMap());
        return true;
    }
    if (before == ClientSessionPhase::AwaitingSignupReply) {
        SignupReplyPayload reply;
        if (!SignupReplyPayload::parse(message.payload, &reply, error))
            return false;
        if (!reply.accepted)
            return reject(QStringLiteral("stream_signup_rejected:" ) + reply.errorCode, error);
        m_state.setSelfName(reply.playerId);
        m_state.setConnectionValue(QStringLiteral("reconnected"), reply.reconnected);
        m_state.setConnectionValue(QStringLiteral("room_id"), reply.roomId);
        return true;
    }
    if (before == ClientSessionPhase::AwaitingSetup) {
        m_state.setSetup(message.payload.toMap());
        return true;
    }
    if (message.type == ProtocolMessageType::Request) {
        if (m_syncActive)
            return reject(QStringLiteral("stream_request_during_sync"), error);
        InteractionRequest request;
        if (!ProtocolInteractionRequestBuilder::build(message, m_state, &request, error))
            return false;
        m_request = message;
        m_interaction = std::move(request);
        m_hasRequest = true;
        return true;
    }
    if (message.type == ProtocolMessageType::Reply) {
        if (!m_outgoingRequests.contains(message.replyTo)
            || m_outgoingRequests.value(message.replyTo) != message.command)
            return reject(QStringLiteral("stream_reply_correlation"), error);
        m_outgoingRequests.remove(message.replyTo);
        return true;
    }
    if (message.type != ProtocolMessageType::Notification)
        return reject(QStringLiteral("stream_incoming_type"), error);
    if (message.command == S_COMMAND_CHECK_VERSION)
        return reject(QStringLiteral("stream_identity_replaced"), error);
    if (message.command == S_COMMAND_WARN)
        return reject(QStringLiteral("stream_server_warning"), error);
    return reduce(message, error);
}

bool ClientRulesIngress::outgoing(const ProtocolMessage &message, QString *error)
{
    if (message.source != ProtocolEndpoint::Client || message.messageId <= m_lastOutgoing)
        return reject(QStringLiteral("stream_outgoing_order"), error);
    ProtocolMessage expected;
    if (m_session.phase() == ClientSessionPhase::HelloAccepted) {
        if (message.command != S_COMMAND_SIGNUP || message.type != ProtocolMessageType::Request
            || message.destination != ProtocolEndpoint::Lobby)
            return reject(QStringLiteral("stream_signup_expected"), error);
        const QJsonObject identity = QJsonObject::fromVariantMap(
            message.payload.toMap().value(QStringLiteral("rules_bundle")).toMap());
        const QString mismatch = QSanRules::compatibilityError(m_identity, identity, true);
        if (!mismatch.isEmpty())
            return reject(mismatch, error);
        SignupRequestPayload signup;
        if (!SignupRequestPayload::parse(message.payload, &signup, error)
            || !m_session.makeSignupRequest(signup, &expected, error))
            return false;
        if (expected.messageId != message.messageId)
            return reject(QStringLiteral("stream_signup_id"), error);
    } else if (m_session.phase() == ClientSessionPhase::ReadyPending) {
        ReadyPayload ready;
        if (message.type != ProtocolMessageType::Notification || message.command != S_COMMAND_READY
            || message.destination != ProtocolEndpoint::Room
            || !ReadyPayload::parse(message.payload, &ready, error) || !ready.ready
            || !m_session.makeReadyNotification(&expected, error))
            return reject(QStringLiteral("stream_ready_expected"), error);
        if (expected.messageId != message.messageId)
            return reject(QStringLiteral("stream_ready_id"), error);
        m_state.setConnectionValue(QStringLiteral("state"), QStringLiteral("active"));
    } else {
        if (m_session.phase() != ClientSessionPhase::Active
            || message.destination != ProtocolEndpoint::Room)
            return reject(QStringLiteral("stream_outgoing_before_active"), error);
        if (message.type == ProtocolMessageType::Reply) {
            const auto *descriptor = m_hasRequest ? ProtocolPayloadRegistry::find(m_request) : nullptr;
            if (m_syncActive || descriptor == nullptr || message.replyTo != m_request.messageId
                || descriptor->replyCommand != message.command)
                return reject(QStringLiteral("stream_stale_reply"), error);
            clearRequest();
        } else if (message.type == ProtocolMessageType::Request) {
            const auto *descriptor = ProtocolPayloadRegistry::find(message);
            if (descriptor == nullptr || descriptor->replyCommand == 0 || m_outgoingRequests.size() >= 256)
                return reject(QStringLiteral("stream_pending_requests"), error);
            m_outgoingRequests.insert(message.messageId, descriptor->replyCommand);
        } else if (message.type != ProtocolMessageType::Notification) {
            return reject(QStringLiteral("stream_outgoing_type"), error);
        }
    }
    m_lastOutgoing = message.messageId;
    return true;
}

bool ClientRulesIngress::reduce(const ProtocolMessage &message, QString *error)
{
    StateSyncPayload sync;
    const bool synchronization = message.command == S_COMMAND_STATE_SYNC;
    if (synchronization && !StateSyncPayload::parse(message.payload, &sync, error))
        return false;
    if (synchronization && sync.phase == QLatin1String("begin")) {
        if (m_syncActive)
            return reject(QStringLiteral("stream_sync_overlap"), error);
        m_pending = m_state;
        m_pending.resetGameplayState();
        m_syncActive = true;
        m_syncId = sync.syncId;
        clearRequest();
    } else if (synchronization && (!m_syncActive || sync.syncId != m_syncId)) {
        return reject(QStringLiteral("stream_sync_mismatch"), error);
    }
    // A reducer may partially mutate its argument before reporting failure.
    // Only publish a copy after its complete typed reduction succeeds.
    ClientGameState candidate = m_syncActive ? m_pending : m_state;
    const auto result = ClientGameStateReducer::applyNotification(&candidate, message.command, message.payload);
    if (!result.success)
        return reject(QStringLiteral("stream_reduction:") + result.detail, error);
    if (m_syncActive) {
        m_pending = std::move(candidate);
        if (synchronization && sync.phase == QLatin1String("end")) {
            m_state = std::move(m_pending);
            m_pending.reset();
            m_syncActive = false;
            m_syncId.clear();
        }
    } else {
        m_state = std::move(candidate);
    }
    if (message.command == S_COMMAND_GAME_START || message.command == S_COMMAND_GAME_OVER
        || message.command == S_COMMAND_SWITCH_CONTEXT
        || (m_hasRequest && !m_state.hasPlayer(m_state.selfName())))
        clearRequest();
    return true;
}

bool ClientRulesIngress::prepareQuery(int generation, int revision, const QString &requestId,
                                      const QJsonObject &selection, QJsonObject *query,
                                      QString *error) const
{
    if (query != nullptr)
        *query = QJsonObject();
    if (error != nullptr)
        error->clear();
    if (query == nullptr)
        return reject(QStringLiteral("stream_query_output"), error);
    if (generation != m_generation || revision != m_revision)
        return reject(QStringLiteral("stream_stale_query"), error);
    if (m_failed || m_syncActive || m_session.phase() != ClientSessionPhase::Active)
        return reject(QStringLiteral("stream_not_queryable"), error);
    if (!m_hasRequest || requestId != QString::number(m_request.messageId))
        return reject(QStringLiteral("stream_no_matching_request"), error);
    *query = {{QStringLiteral("schema_version"), 1},
        {QStringLiteral("generation"), m_generation}, {QStringLiteral("revision"), m_revision},
        {QStringLiteral("request_id"), requestId}, {QStringLiteral("command"), m_request.command},
        {QStringLiteral("payload"), QJsonValue::fromVariant(m_request.payload)},
        {QStringLiteral("state"), snapshot(m_state)}, {QStringLiteral("selection"), selection}};
    return true;
}

QJsonObject ClientRulesIngress::status() const
{
    return {{QStringLiteral("generation"), m_generation}, {QStringLiteral("revision"), m_revision},
        {QStringLiteral("failed"), m_failed}, {QStringLiteral("synchronizing"), m_syncActive},
        {QStringLiteral("active"), !m_failed && m_session.phase() == ClientSessionPhase::Active},
        {QStringLiteral("request_id"), m_hasRequest ? QString::number(m_request.messageId) : QString()},
        {QStringLiteral("bundle_id"), m_identity.value(QStringLiteral("bundle_id"))}};
}

QJsonObject ClientRulesIngress::view() const
{
    // Deliberately never expose the uncommitted STATE_SYNC accumulator.
    return snapshot(m_state);
}
