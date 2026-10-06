#include "external-agent.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QSet>

ExternalAgentEndpoint::ExternalAgentEndpoint(QString seat, DisconnectPolicy policy,
                                             ProjectionPolicy projection)
    : m_seat(std::move(seat)), m_policy(policy), m_projection(projection) {}

bool ExternalAgentEndpoint::requiresWorldView(AIRequest::DecisionKind kind) const
{
    if (m_projection == ProjectionPolicy::Full) return true;
    // CardChosen derives its visible candidates from the projected board.
    // Activate/UseCard and Player(s)Chosen may go to JEV; all keep full views.
    // Unknown future categories conservatively retain the default contract.
    switch (kind) {
    case AIRequest::SkillInvoke: case AIRequest::Choice: case AIRequest::Suit:
    case AIRequest::Kingdom: case AIRequest::General: case AIRequest::Discard:
    case AIRequest::AmazingGrace: case AIRequest::Yiji: case AIRequest::RespondCard:
    case AIRequest::Guanxing: case AIRequest::TriggerOrder:
        return false;
    default:
        return true;
    }
}

bool ExternalAgentEndpoint::pending(AIRequest &request) const
{
    QMutexLocker lock(&m_mutex);
    if (m_hybridBounded && m_pending && m_requestTimer.elapsed() >= m_hybridDeadlineMs)
        return false;
    if (!m_pending || m_submitted || m_localRequested || !m_connected || m_cancelled) return false;
    request = m_request;
    return true;
}

bool ExternalAgentEndpoint::submit(const AIResult &result, QString *error)
{
    QMutexLocker lock(&m_mutex);
    if (m_hybridBounded && m_pending && m_requestTimer.elapsed() >= m_hybridDeadlineMs) {
        m_connected = false;
        m_submitted = false;
        m_error = QStringLiteral("adapter-timeout");
        if (error) *error = m_error;
        m_changed.wakeAll();
        return false;
    }
    QString reason;
    if (m_cancelled || !m_connected || !m_pending) reason = QStringLiteral("not-waiting");
    else if (m_submitted || m_localRequested) reason = QStringLiteral("duplicate");
    else if (result.decisionId != m_request.decisionId
             || result.stateRevision != m_request.stateRevision) reason = QStringLiteral("stale");
    else if (!validateShape(m_request, result)) reason = QStringLiteral("invalid-answer");
    if (!reason.isEmpty()) {
        m_error = reason;
        if (error) *error = reason;
        return false;
    }
    m_result = result;
    m_submitted = true;
    m_error.clear();
    if (error) error->clear();
    m_changed.wakeAll();
    return true; // queued; authoritative legality is checked on the room worker
}

bool ExternalAgentEndpoint::requestLocal(quint64 decisionId, quint64 stateRevision, QString *error)
{
    QMutexLocker lock(&m_mutex);
    if (m_hybridBounded && m_pending && m_requestTimer.elapsed() >= m_hybridDeadlineMs) {
        m_connected = false;
        m_localRequested = false;
        m_error = QStringLiteral("adapter-timeout");
        if (error) *error = m_error;
        m_changed.wakeAll();
        return false;
    }
    QString reason;
    if (m_cancelled || !m_connected || !m_pending) reason = QStringLiteral("not-waiting");
    else if (m_submitted || m_localRequested) reason = QStringLiteral("duplicate");
    else if (decisionId != m_request.decisionId || stateRevision != m_request.stateRevision)
        reason = QStringLiteral("stale");
    if (!reason.isEmpty()) {
        m_error = reason;
        if (error) *error = reason;
        return false;
    }
    m_localRequested = true;
    m_error.clear();
    if (error) error->clear();
    m_changed.wakeAll();
    return true;
}

void ExternalAgentEndpoint::begin(const AIRequest &request)
{
    QMutexLocker lock(&m_mutex);
    Q_ASSERT(request.viewerObjectName == m_seat);
    if (request.viewerObjectName != m_seat || m_pending) {
        m_cancelled = true;
        m_changed.wakeAll();
        return;
    }
    m_request = request;
    if (m_hybridBounded) m_requestTimer.start();
    m_pending = true;
    m_submitted = false;
    m_localRequested = false;
    m_result = AIResult();
    m_error.clear();
}

ExternalAgentEndpoint::Outcome ExternalAgentEndpoint::awaitReply(AIResult &result)
{
    QMutexLocker lock(&m_mutex);
    // Opt-in room adapters must never hold a game worker forever if their
    // process or provider stalls. Other external sessions retain their policy.
    while (!m_cancelled) {
        if (m_hybridBounded && m_requestTimer.elapsed() >= m_hybridDeadlineMs) {
            m_connected = false;
            m_submitted = false;
            m_localRequested = false;
            m_error = QStringLiteral("adapter-timeout");
            return Fallback;
        }
        // An accepted local route is atomic even if its socket then disconnects.
        if (m_localRequested) return Local;
        if (!m_connected && m_policy == SmartAIFallback) return Fallback;
        if (m_connected && m_submitted) { result = m_result; return Reply; }
        if (m_hybridBounded) {
            const qint64 remaining = m_hybridDeadlineMs - m_requestTimer.elapsed();
            if (remaining <= 0) {
                m_connected = false;
                m_submitted = false;
                m_error = QStringLiteral("adapter-timeout");
                return Fallback;
            }
            m_changed.wait(&m_mutex, static_cast<unsigned long>(remaining));
        } else m_changed.wait(&m_mutex);
    }
    return Cancelled;
}

void ExternalAgentEndpoint::enableBoundedHybrid(int deadlineMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_policy == SmartAIFallback && !m_pending && deadlineMs >= 1
        && deadlineMs <= 30000) {
        m_hybridDeadlineMs = deadlineMs;
        m_hybridBounded = true;
    }
}

void ExternalAgentEndpoint::reject(const QString &reason)
{
    QMutexLocker lock(&m_mutex);
    m_submitted = false;
    m_localRequested = false;
    m_result = AIResult();
    m_error = reason;
}

void ExternalAgentEndpoint::finish()
{
    QMutexLocker lock(&m_mutex);
    m_pending = m_submitted = m_localRequested = false;
    m_request = AIRequest();
    m_result = AIResult();
    if (!m_cancelled) m_error.clear();
}

void ExternalAgentEndpoint::disconnect()
{
    QMutexLocker lock(&m_mutex);
    m_connected = false;
    m_submitted = false;
    m_changed.wakeAll();
}

void ExternalAgentEndpoint::reconnect()
{
    QMutexLocker lock(&m_mutex);
    m_connected = true;
    m_changed.wakeAll();
}

void ExternalAgentEndpoint::cancel()
{
    QMutexLocker lock(&m_mutex);
    m_cancelled = true;
    m_changed.wakeAll();
}

QString ExternalAgentEndpoint::status() const
{
    QMutexLocker lock(&m_mutex);
    if (m_cancelled) return QStringLiteral("cancelled");
    if (m_localRequested) return QStringLiteral("local-queued");
    if (!m_connected) return m_policy == Pause ? QStringLiteral("paused-disconnected")
                                             : QStringLiteral("smart-ai-fallback");
    if (m_submitted) return QStringLiteral("validating");
    return m_pending ? QStringLiteral("waiting-no-clock") : QStringLiteral("idle");
}

QString ExternalAgentEndpoint::lastError() const
{
    QMutexLocker lock(&m_mutex);
    return m_error;
}

template<class T> static bool selection(const QList<T> &picked, const QList<T> &offered,
                                        int min, int max)
{
    if (picked.size() < min || picked.size() > max) return false;
    QSet<T> seen;
    for (const auto &v : picked) {
        if (!offered.contains(v) || seen.contains(v)) return false;
        seen.insert(v);
    }
    return true;
}

bool ExternalAgentEndpoint::validateShape(const AIRequest &q, const AIResult &r)
{
    const auto &a = r.action;
    const auto &o = q.choiceOptions;
    if (!r.handled || !r.errorCode.isEmpty() || !a.legacyCardString.isEmpty()
        || a.userString.size() > 65536 || a.selectedCardIds.size() > 2048
        || a.bottomCardIds.size() > 2048 || a.selectedTargetNames.size() > 64
        || a.cardSpec.subcardIds.size() > 2048 || a.cardSpec.name.size() > 65536
        || a.cardSpec.skillName.size() > 65536) return false;
    for (const auto &name : a.selectedTargetNames) if (name.size() > 65536) return false;
    for (const auto &ref : {a.skillActionContext.activationRef, a.skillActionContext.sourceRef})
        if (ref.ownerObjectName.size() > 65536 || ref.key.skillName.size() > 65536) return false;
    if (r.kind == AIResult::Pass)
        return (q.kind == AIRequest::Activate || q.kind == AIRequest::UseCard || o.optional)
            && a.useCardId < 0 && !a.hasCardSpec && !a.hasSkillActionContext
            && a.selectedCardIds.isEmpty() && a.selectedTargetNames.isEmpty()
            && a.bottomCardIds.isEmpty() && a.userString.isEmpty();
    if (q.kind == AIRequest::Activate || q.kind == AIRequest::UseCard)
        return r.kind == AIResult::UseCard; // full engine authority checks follow
    if (r.kind != AIResult::Answer || a.useCardId >= 0 || a.hasSkillActionContext) return false;
    if (q.kind == AIRequest::RespondCard)
        return a.userString.isEmpty() && a.selectedTargetNames.isEmpty()
            && a.bottomCardIds.isEmpty() && (a.hasCardSpec
                ? a.selectedCardIds.isEmpty()
                : selection(a.selectedCardIds, o.cardIds, 1, 1));
    if (a.hasCardSpec) return false;
    if (q.kind == AIRequest::Guanxing)
        return a.userString.isEmpty() && a.selectedTargetNames.isEmpty()
            && selection(a.selectedCardIds + a.bottomCardIds, o.cardIds, o.cardIds.size(), o.cardIds.size())
            && (o.defaultChoice != QStringLiteral("1") || a.bottomCardIds.isEmpty())
            && (o.defaultChoice != QStringLiteral("2") || a.selectedCardIds.isEmpty());
    if (!a.bottomCardIds.isEmpty()) return false;
    switch (q.kind) {
    case AIRequest::Discard:
    case AIRequest::AmazingGrace:
    case AIRequest::CardChosen:
        return a.userString.isEmpty() && a.selectedTargetNames.isEmpty()
            && selection(a.selectedCardIds, o.cardIds, o.minCount, o.maxCount);
    case AIRequest::PlayerChosen:
    case AIRequest::PlayersChosen:
        return a.userString.isEmpty() && a.selectedCardIds.isEmpty()
            && selection(a.selectedTargetNames, o.playerNames, o.minCount, o.maxCount);
    case AIRequest::Yiji:
        return a.userString.isEmpty() && selection(a.selectedCardIds, o.cardIds, 1, 1)
            && selection(a.selectedTargetNames, o.playerNames, 1, 1);
    default:
        return a.selectedCardIds.isEmpty() && a.selectedTargetNames.isEmpty()
            && o.choices.contains(a.userString);
    }
}

AIResult mockExternalAgentAnswer(const AIRequest &q)
{
    AIResult r;
    r.handled = true;
    r.decisionId = q.decisionId;
    r.stateRevision = q.stateRevision;
    const auto &o = q.choiceOptions;
    if (q.kind == AIRequest::Activate || q.kind == AIRequest::UseCard || o.optional) return r;
    r.kind = AIResult::Answer;
    if (q.kind == AIRequest::Guanxing) {
        if (o.defaultChoice == QStringLiteral("2")) r.action.bottomCardIds = o.cardIds;
        else r.action.selectedCardIds = o.cardIds;
    } else if (q.kind == AIRequest::PlayerChosen || q.kind == AIRequest::PlayersChosen) {
        r.action.selectedTargetNames = o.playerNames.mid(0, o.minCount);
    } else if (!o.cardIds.isEmpty()) {
        r.action.selectedCardIds = o.cardIds.mid(0, o.minCount);
    } else if (!o.choices.isEmpty()) {
        r.action.userString = o.choices.first();
    } else {
        r.handled = false;
        r.errorCode = QStringLiteral("mock-unsupported");
    }
    return r;
}
