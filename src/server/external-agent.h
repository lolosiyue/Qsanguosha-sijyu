#ifndef QSAN_EXTERNAL_AGENT_H
#define QSAN_EXTERNAL_AGENT_H

#include "ai.h"
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <memory>

enum class ExternalAgentDisconnectPolicy { Pause, SmartAIFallback };
// Explicit host preference for questions that the hybrid adapter answers locally.
// This changes snapshot work only; it grants no additional authority.
enum class ExternalAgentProjectionPolicy { Full, LocalDecisions };

// A capability for exactly one seat. Adapters receive this object, never Room,
// Player, Lua, a registry of seats, or a callback running on the game worker.
// All adapter methods are thread safe and nonblocking. Authentication and wire
// serialization belong to the host adapter, outside the engine.
class ExternalAgentEndpoint
{
public:
    using DisconnectPolicy = ExternalAgentDisconnectPolicy;
    using ProjectionPolicy = ExternalAgentProjectionPolicy;
    static constexpr auto Pause = DisconnectPolicy::Pause;
    static constexpr auto SmartAIFallback = DisconnectPolicy::SmartAIFallback;
    enum Outcome { Reply, Local, Fallback, Cancelled };
    explicit ExternalAgentEndpoint(QString seat, DisconnectPolicy policy,
                                   ProjectionPolicy projection = ProjectionPolicy::Full);
    bool requiresWorldView(AIRequest::DecisionKind kind) const;
    bool pending(AIRequest &request) const;
    bool submit(const AIResult &result, QString *error = nullptr);
    // Resolve only this pending decision through the seat's native SmartAI.
    // Unlike disconnect fallback, this leaves the capability connected.
    bool requestLocal(quint64 decisionId, quint64 stateRevision, QString *error = nullptr);
    void disconnect();
    void reconnect();
    void cancel(); // Cancels the session, never silently plays or passes.
    QString status() const;
    QString lastError() const;
    // Enabled only by the opted-in 50P room manager before the first decision.
    void enableBoundedHybrid(int deadlineMs = 30000);
    bool boundedHybrid() const { return m_hybridBounded; }

    static bool validateShape(const AIRequest &request, const AIResult &result);

private:
    friend class AiDecisionCoordinator;
    friend struct RoomTestAccess;
    // Authority-side lifecycle. Called only by the room's decision coordinator.
    void begin(const AIRequest &request);
    Outcome awaitReply(AIResult &result);
    void reject(const QString &reason);
    void finish();

    const QString m_seat;
    const DisconnectPolicy m_policy;
    const ProjectionPolicy m_projection;
    mutable QMutex m_mutex;
    QWaitCondition m_changed;
    AIRequest m_request;
    AIResult m_result;
    bool m_pending = false;
    bool m_submitted = false;
    bool m_localRequested = false;
    bool m_connected = true;
    bool m_cancelled = false;
    bool m_hybridBounded = false;
    int m_hybridDeadlineMs = 30000;
    QElapsedTimer m_requestTimer;
    QString m_error;
};

// Deterministic, deliberately conservative example adapter. No live engine
// access, Lua callbacks, providers, credentials, costs or implicit fallback.
AIResult mockExternalAgentAnswer(const AIRequest &request);

#endif
