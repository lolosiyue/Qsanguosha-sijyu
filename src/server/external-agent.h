#ifndef QSAN_EXTERNAL_AGENT_H
#define QSAN_EXTERNAL_AGENT_H

#include "ai.h"
#include <QMutex>
#include <QWaitCondition>
#include <memory>

enum class ExternalAgentDisconnectPolicy { Pause, SmartAIFallback };

// A capability for exactly one seat. Adapters receive this object, never Room,
// Player, Lua, a registry of seats, or a callback running on the game worker.
// All adapter methods are thread safe and nonblocking. Authentication and wire
// serialization belong to the host adapter, outside the engine.
class ExternalAgentEndpoint
{
public:
    using DisconnectPolicy = ExternalAgentDisconnectPolicy;
    static constexpr auto Pause = DisconnectPolicy::Pause;
    static constexpr auto SmartAIFallback = DisconnectPolicy::SmartAIFallback;
    enum Outcome { Reply, Fallback, Cancelled };
    explicit ExternalAgentEndpoint(QString seat, DisconnectPolicy policy);
    bool pending(AIRequest &request) const;
    bool submit(const AIResult &result, QString *error = nullptr);
    void disconnect();
    void reconnect();
    void cancel(); // Cancels the session, never silently plays or passes.
    QString status() const;
    QString lastError() const;

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
    mutable QMutex m_mutex;
    QWaitCondition m_changed;
    AIRequest m_request;
    AIResult m_result;
    bool m_pending = false;
    bool m_submitted = false;
    bool m_connected = true;
    bool m_cancelled = false;
    QString m_error;
};

// Deterministic, deliberately conservative example adapter. No live engine
// access, Lua callbacks, providers, credentials, costs or implicit fallback.
AIResult mockExternalAgentAnswer(const AIRequest &request);

#endif
