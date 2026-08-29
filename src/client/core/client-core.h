#ifndef CLIENT_CORE_H
#define CLIENT_CORE_H

// ClientCore:protocol 同 UI 之間嘅中間層。
//
//   Protocol / Client
//           ↓  beginRequest(InteractionRequest)
//       ClientCore
//           ↓  IClientInteractionView::presentRequest()
//       DesktopInteractionView / TextClient / Android / WASM
//           ↓  submitResponse(InteractionResponse)
//       ClientCore  ← 驗證 + exactly-once
//           ↓  responseAccepted()
//   Protocol / Client → replyToServer()
//
// ClientCore 只 link Qt Core。佢唔認識 QWidget、QGraphicsItem、QDialog、
// QQuickItem、Dashboard、RoomScene,亦唔認識 engine 嘅 Card／Player／Skill。
//
// 佢負責:
//   - 當前 request 同 correlation ID
//   - 可選卡牌／玩家／option、min-max、cancelable、timeout／deadline
//   - skill/context metadata
//   - response 驗證
//   - exactly-once completion guard
//
// 佢唔負責:規則。「呢張牌配唔配到 pattern」係 server 嘅事;ClientCore 只
// 執行 server 喺 request 入面已經寫低嘅約束。

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
    // 單調毫秒時鐘。預設係 process 開機以嚟嘅 QElapsedTimer,測試可以換走。
    typedef std::function<qint64()> Clock;

    explicit ClientCore(QObject *parent = nullptr);
    ~ClientCore() override;

    void setClock(Clock clock);
    qint64 now() const;
    void setCardEligibilityProvider(const ICardEligibilityProvider *provider);
    const ICardEligibilityProvider *cardEligibilityProvider() const { return m_cardEligibilityProvider; }

    ClientGameState *state() { return &m_state; }
    const ClientGameState *state() const { return &m_state; }

    // View 唔屬於 ClientCore。View 死之前一定要 detachView():core 會保住
    // pending request(佢先係真相),只係停止再通知。
    void setView(IClientInteractionView *view);
    IClientInteractionView *view() const { return m_view; }
    void detachView();

    // 開一個新 request。requestId 若為 0 就自動編號並寫返落 request。
    // 上一個未完成嘅 request 會以 Superseded 取消,唔會送任何 reply。
    quint64 beginRequest(InteractionRequest request);

    bool hasActiveRequest() const;
    bool hasActiveRequest(InteractionType type) const;
    const InteractionRequest &activeRequest() const { return m_active; }
    quint64 activeRequestId() const;

    // 淨係驗,唔改狀態。
    InteractionValidation validate(const InteractionResponse &response) const;
    // 驗 + 完成。被接納嘅答案會令 request 收檔,再答一次係 AlreadyCompleted。
    InteractionValidation submitResponse(const InteractionResponse &response);

    void cancelActiveRequest(InteractionCancelReason reason);
    // 過咗死線就取消,回傳有冇取消到。冇死線／未到期／冇 request 都回 false。
    bool expireIfDue();

    // 診斷:snapshot、smoke report 同測試會讀。
    quint64 acceptedCount() const { return m_acceptedCount; }
    quint64 rejectedCount() const { return m_rejectedCount; }
    quint64 cancelledCount() const { return m_cancelledCount; }
    quint64 startedCount() const { return m_startedCount; }
    QJsonObject toJson() const;

    // duplicate／stale reply 嘅偵測窗。夠深去接住任何合理嘅 double click,
    // 又唔會無限增長。
    static const int CompletedHistoryLimit;

signals:
    void requestStarted(quint64 requestId);
    void responseAccepted(quint64 requestId);
    // rejection／reason 用 int 過 signal:queued connection 唔使為 enum class
    // 註冊 metatype。
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
