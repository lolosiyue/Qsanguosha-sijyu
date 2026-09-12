#ifndef CLIENT_CORE_H
#define CLIENT_CORE_H

// ClientCore: the middle layer between protocol and UI.
//
//   Protocol / Client
//           ↓  beginRequest(InteractionRequest)
//       ClientCore
//           ↓  IClientInteractionView::presentRequest()
//       DesktopInteractionView / TextClient / Android / WASM
//           ↓  submitResponse(InteractionResponse)
//       ClientCore  ← validation + exactly-once
//           ↓  responseAccepted()
//   Protocol / Client → replyToServer()
//
// ClientCore links only Qt Core. It knows nothing about QWidget, QGraphicsItem, QDialog,
// QQuickItem, Dashboard or RoomScene, nor about the engine's Card/Player/Skill.
//
// It is responsible for:
//   - the current request and its correlation ID
//   - selectable cards/players/options, min-max, cancelable, timeout/deadline
//   - skill/context metadata
//   - response validation
//   - exactly-once completion guard
//
// It is NOT responsible for rules. "Does this card match the pattern" is the server's
// business; ClientCore only enforces the constraints the server has already written
// into the request.

#include "client-game-state.h"
#include "card-eligibility-provider.h"
#include "client-interaction-view.h"
#include "interaction-model.h"

#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

class ClientCore : public QObject
{
    Q_OBJECT

public:
    // Monotonic millisecond clock. Defaults to a QElapsedTimer started at process boot;
    // tests may swap it.
    typedef std::function<qint64()> Clock;

    explicit ClientCore(QObject *parent = nullptr);
    ~ClientCore() override;

    void setClock(Clock clock);
    qint64 now() const;
    void setCardEligibilityProvider(const ICardEligibilityProvider *provider);
    const ICardEligibilityProvider *cardEligibilityProvider() const { return m_cardEligibilityProvider; }

    ClientGameState *state() { return &m_state; }
    const ClientGameState *state() const { return &m_state; }

    // The view is not owned by ClientCore. detachView() must be called before the view dies:
    // the core keeps the pending request (that is the source of truth) and merely stops
    // notifying.
    void setView(IClientInteractionView *view);
    IClientInteractionView *view() const { return m_view; }
    void detachView();

    // Opens a new request. If requestId is 0 it is auto-numbered and written back into the
    // request. The previous unfinished request is cancelled as Superseded; no reply is sent
    // for it.
    quint64 beginRequest(InteractionRequest request);

    bool hasActiveRequest() const;
    bool hasActiveRequest(InteractionType type) const;
    const InteractionRequest &activeRequest() const { return m_active; }
    quint64 activeRequestId() const;

    // Validate only; no state change.
    InteractionValidation validate(const InteractionResponse &response) const;
    // Validate + complete. An accepted answer finalizes the request; answering again yields
    // AlreadyCompleted.
    InteractionValidation submitResponse(const InteractionResponse &response);

    void cancelActiveRequest(InteractionCancelReason reason);
    // Cancels past the deadline and returns whether the cancellation happened. Returns false
    // when there is no deadline, the deadline has not passed, or there is no request.
    bool expireIfDue();

    // 診斷:snapshot、smoke report 同測試會讀。
    quint64 acceptedCount() const { return m_acceptedCount; }
    quint64 rejectedCount() const { return m_rejectedCount; }
    quint64 cancelledCount() const { return m_cancelledCount; }
    quint64 startedCount() const { return m_startedCount; }
    QJsonObject toJson() const;

    // Detection window for duplicate/stale replies. Deep enough to absorb any reasonable
    // double click, yet it never grows without bound.
    static const int CompletedHistoryLimit;

signals:
    void requestStarted(quint64 requestId);
    void responseAccepted(quint64 requestId);
    // rejection/reason cross signals as int: queued connections then need no metatype
    // registration for the enum class.
    void responseRejected(quint64 requestId, int rejection);
    void requestCancelled(quint64 requestId, int reason);

private:
    enum class CompletionKind
    {
        Answered,
        Cancelled,
        Expired
    };

    struct CompletedRequest
    {
        quint64 requestId = 0;
        CompletionKind kind = CompletionKind::Answered;
    };

    InteractionValidation validateAgainst(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateOption(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validatePlayers(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateCards(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateAssignment(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateRearrangement(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateDistribution(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateGeneralArrangement(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation validateCustom(const InteractionRequest &request,
        const InteractionResponse &response) const;
    InteractionValidation rejectionForCompleted(quint64 requestId) const;
    const CompletedRequest *findCompleted(quint64 requestId) const;
    void recordCompleted(quint64 requestId, CompletionKind kind);
    void clearActive();
    void enrichEligibilityHints(InteractionRequest &request) const;
    void scheduleDeadlineTimer();

    ClientGameState m_state;
    IClientInteractionView *m_view = nullptr;
    Clock m_clock;
    const ICardEligibilityProvider *m_cardEligibilityProvider = nullptr;
    QTimer m_deadlineTimer;
    InteractionRequest m_active;
    QList<CompletedRequest> m_completed;
    quint64 m_nextRequestId = 1;
    quint64 m_startedCount = 0;
    quint64 m_acceptedCount = 0;
    quint64 m_rejectedCount = 0;
    quint64 m_cancelledCount = 0;
};

#endif
