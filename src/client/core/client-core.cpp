#include "client-core.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSet>

Q_LOGGING_CATEGORY(qsanClientCore, "qsan.client.core")

const int ClientCore::CompletedHistoryLimit = 32;

namespace {

// process 全局單調時鐘。用 static 係因為 ClientCore 可以有幾個 instance
// (測試會開多個),但佢哋應該讀同一條時間線。
qint64 monotonicNow()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

bool isEmptyAnswer(const InteractionResponse &response)
{
    switch (response.kind) {
    case InteractionResponseKind::Cancel:
        return true;
    case InteractionResponseKind::Option:
        return response.option.isEmpty();
    case InteractionResponseKind::Players:
        return response.players.isEmpty();
    case InteractionResponseKind::Cards:
        return response.cards.isEmpty() && response.cardText.isEmpty();
    case InteractionResponseKind::None:
        break;
    }
    return true;
}

InteractionResponseKind expectedKind(InteractionType type)
{
    switch (type) {
    case InteractionType::ChooseGeneral:
    case InteractionType::Choice:
    case InteractionType::SkillInvoke:
        return InteractionResponseKind::Option;
    case InteractionType::ChoosePlayer:
        return InteractionResponseKind::Players;
    case InteractionType::ResponseCard:
        return InteractionResponseKind::Cards;
    case InteractionType::None:
        break;
    }
    return InteractionResponseKind::None;
}

}  // namespace

ClientCore::ClientCore(QObject *parent)
    : QObject(parent)
    , m_clock(&monotonicNow)
{
}

ClientCore::~ClientCore()
{
    // core 死嗰陣唔通知 view:view 通常就係跟住一齊拆。
    m_view = nullptr;
}

void ClientCore::setClock(Clock clock)
{
    m_clock = clock ? clock : Clock(&monotonicNow);
}

qint64 ClientCore::now() const
{
    return m_clock ? m_clock() : monotonicNow();
}

void ClientCore::setView(IClientInteractionView *view)
{
    if (m_view == view)
        return;
    m_view = view;
    // 換 view 嗰陣如果有 request 未答,即刻餵佢:重連／換皮膚都唔應該
    // 令玩家對住一個空畫面等一個佢睇唔到嘅 request。
    if (m_view && hasActiveRequest())
        m_view->presentRequest(m_active);
}

void ClientCore::detachView()
{
    m_view = nullptr;
}

quint64 ClientCore::beginRequest(InteractionRequest request)
{
    if (!request.isValid()) {
        qCWarning(qsanClientCore) << "beginRequest ignored an invalid request";
        return 0;
    }

    if (hasActiveRequest())
        cancelActiveRequest(InteractionCancelReason::Superseded);

    if (request.requestId == 0)
        request.requestId = m_nextRequestId++;
    else if (request.requestId >= m_nextRequestId)
        m_nextRequestId = request.requestId + 1;

    request.deadlineMs = request.timeoutMs > 0 ? now() + request.timeoutMs : 0;

    m_active = request;
    ++m_startedCount;

    const quint64 requestId = m_active.requestId;
    emit requestStarted(requestId);
    if (m_view) {
        // 傳一份 copy 而唔係 m_active 嘅 reference。呈現係可以重入嘅:desktop
        // 嘅 setStatus() 會即刻叫 RoomScene::updateStatus(),而嗰度有幾條路
        // 會喺同一個 call stack 入面就答返呢個 request(例如 responding 狀態
        // 搵唔到可用嘅 view-as skill,就直接覆一個空答案)。答完之後 m_active
        // 已經清空,reference 就會指住一個唔同嘅 request。
        const InteractionRequest presented = m_active;
        m_view->presentRequest(presented);
    }
    return requestId;
}

bool ClientCore::hasActiveRequest() const
{
    return m_active.isValid();
}

bool ClientCore::hasActiveRequest(InteractionType type) const
{
    return m_active.isValid() && m_active.type == type;
}

quint64 ClientCore::activeRequestId() const
{
    return m_active.isValid() ? m_active.requestId : 0;
}

const ClientCore::CompletedRequest *ClientCore::findCompleted(quint64 requestId) const
{
    for (QList<CompletedRequest>::const_iterator it = m_completed.constBegin();
         it != m_completed.constEnd(); ++it) {
        if (it->requestId == requestId)
            return &(*it);
    }
    return nullptr;
}

void ClientCore::recordCompleted(quint64 requestId, CompletionKind kind)
{
    CompletedRequest entry;
    entry.requestId = requestId;
    entry.kind = kind;
    m_completed.append(entry);
    while (m_completed.size() > CompletedHistoryLimit)
        m_completed.removeFirst();
}

void ClientCore::clearActive()
{
    m_active = InteractionRequest();
}

InteractionValidation ClientCore::rejectionForCompleted(quint64 requestId) const
{
    const CompletedRequest *entry = findCompleted(requestId);
    if (entry == nullptr)
        return InteractionValidation::ok();

    switch (entry->kind) {
    case CompletionKind::Answered:
        return InteractionValidation::fail(InteractionRejection::AlreadyCompleted,
            QStringLiteral("request %1 was already answered").arg(requestId));
    case CompletionKind::Cancelled:
        return InteractionValidation::fail(InteractionRejection::RequestCancelled,
            QStringLiteral("request %1 was cancelled").arg(requestId));
    case CompletionKind::Expired:
        return InteractionValidation::fail(InteractionRejection::RequestExpired,
            QStringLiteral("request %1 expired").arg(requestId));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validate(const InteractionResponse &response) const
{
    if (!hasActiveRequest()) {
        const InteractionValidation completed = rejectionForCompleted(response.requestId);
        if (!completed.accepted())
            return completed;
        return InteractionValidation::fail(InteractionRejection::NoActiveRequest,
            QStringLiteral("no interaction is awaiting a reply"));
    }

    if (response.requestId != m_active.requestId) {
        const InteractionValidation completed = rejectionForCompleted(response.requestId);
        if (!completed.accepted())
            return completed;
        return InteractionValidation::fail(InteractionRejection::RequestIdMismatch,
            QStringLiteral("reply targets request %1 but %2 is active")
                .arg(response.requestId).arg(m_active.requestId));
    }

    if (m_active.deadlineMs > 0 && now() > m_active.deadlineMs) {
        return InteractionValidation::fail(InteractionRejection::RequestExpired,
            QStringLiteral("request %1 expired").arg(m_active.requestId));
    }

    return validateAgainst(m_active, response);
}

InteractionValidation ClientCore::validateAgainst(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    if (response.kind == InteractionResponseKind::None) {
        return InteractionValidation::fail(InteractionRejection::KindMismatch,
            QStringLiteral("reply carries no answer"));
    }

    // 空答案一律當「取消」處理,無論 view 送 Cancel 定送一個空 selection:
    // 唔准取消嘅 request 唔可以就咁跳過。
    if (isEmptyAnswer(response) && request.minSelection() > 0) {
        if (!request.cancelable) {
            return InteractionValidation::fail(InteractionRejection::NotCancelable,
                QStringLiteral("request %1 does not accept an empty reply").arg(request.requestId));
        }
        return InteractionValidation::ok();
    }

    if (response.kind == InteractionResponseKind::Cancel) {
        if (!request.cancelable && request.minSelection() > 0) {
            return InteractionValidation::fail(InteractionRejection::NotCancelable,
                QStringLiteral("request %1 is not cancelable").arg(request.requestId));
        }
        return InteractionValidation::ok();
    }

    const InteractionResponseKind wanted = expectedKind(request.type);
    if (wanted != InteractionResponseKind::None && response.kind != wanted) {
        return InteractionValidation::fail(InteractionRejection::KindMismatch,
            QStringLiteral("request %1 expects %2 but the reply is %3")
                .arg(request.requestId)
                .arg(interactionResponseKindName(wanted))
                .arg(interactionResponseKindName(response.kind)));
    }

    switch (response.kind) {
    case InteractionResponseKind::Option:
        return validateOption(request, response);
    case InteractionResponseKind::Players:
        return validatePlayers(request, response);
    case InteractionResponseKind::Cards:
        return validateCards(request, response);
    case InteractionResponseKind::Cancel:
    case InteractionResponseKind::None:
        break;
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateOption(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const InteractionOption *option = request.option(response.option);
    if (option == nullptr) {
        // 非枚舉 request（例如 FreeChoose 之下嘅 choose general）冇清單以外
        // 嘅約束,但答案本身仍然唔可以係空。
        if (!request.optionsEnumerated)
            return InteractionValidation::ok();
        return InteractionValidation::fail(InteractionRejection::UnknownOption,
            QStringLiteral("'%1' is not an option of request %2")
                .arg(response.option).arg(request.requestId));
    }
    if (!option->enabled) {
        return InteractionValidation::fail(InteractionRejection::DisabledOption,
            QStringLiteral("option '%1' of request %2 is disabled")
                .arg(response.option).arg(request.requestId));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validatePlayers(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const PlayerSelectionState &selection = request.players;

    QSet<QString> seen;
    foreach (const QString &name, response.players) {
        if (seen.contains(name)) {
            return InteractionValidation::fail(InteractionRejection::DuplicatePlayer,
                QStringLiteral("player '%1' was selected twice").arg(name));
        }
        seen.insert(name);
        if (!selection.selectablePlayers.contains(name)) {
            return InteractionValidation::fail(InteractionRejection::UnknownPlayer,
                QStringLiteral("player '%1' is not selectable for request %2")
                    .arg(name).arg(request.requestId));
        }
    }

    const int count = response.players.size();
    if (count < selection.minSelection
        || (selection.maxSelection > 0 && count > selection.maxSelection)) {
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("request %1 wants %2..%3 players but the reply has %4")
                .arg(request.requestId).arg(selection.minSelection)
                .arg(selection.maxSelection).arg(count));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateCards(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const CardSelectionState &selection = request.cards;

    QSet<int> seen;
    foreach (int cardId, response.cards) {
        if (seen.contains(cardId)) {
            return InteractionValidation::fail(InteractionRejection::DuplicateCard,
                QStringLiteral("card %1 was selected twice").arg(cardId));
        }
        seen.insert(cardId);

        // 值域檢查對 enumerated 同非 enumerated 都成立:呢個唔係規則判斷,
        // 而係「client 由頭到尾都未見過呢個 id」。
        if (!m_state.isKnownCardId(cardId)) {
            return InteractionValidation::fail(InteractionRejection::UnknownCard,
                QStringLiteral("card id %1 is outside the client card id space").arg(cardId));
        }
        if (selection.disabledCards.contains(cardId)) {
            return InteractionValidation::fail(InteractionRejection::DisabledCard,
                QStringLiteral("card %1 is disabled for request %2")
                    .arg(cardId).arg(request.requestId));
        }
        if (selection.enumerated && !selection.selectableCards.contains(cardId)) {
            return InteractionValidation::fail(InteractionRejection::UnknownCard,
                QStringLiteral("card %1 is not selectable for request %2")
                    .arg(cardId).arg(request.requestId));
        }
    }

    // virtual card 冇實 id,但一定有 cardText。數量檢查對佢嚟講係「一張牌」。
    const int count = response.cards.isEmpty() && !response.cardText.isEmpty()
        ? 1 : response.cards.size();
    if (count < selection.minSelection
        || (selection.maxSelection > 0 && count > selection.maxSelection)) {
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("request %1 wants %2..%3 cards but the reply has %4")
                .arg(request.requestId).arg(selection.minSelection)
                .arg(selection.maxSelection).arg(count));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::submitResponse(const InteractionResponse &response)
{
    const InteractionValidation validation = validate(response);
    if (!validation.accepted()) {
        ++m_rejectedCount;
        qCWarning(qsanClientCore).noquote()
            << "rejected reply:" << validation.reasonName() << validation.detail;
        emit responseRejected(response.requestId, static_cast<int>(validation.rejection));
        if (m_view && hasActiveRequest() && response.requestId == m_active.requestId)
            m_view->rejectResponse(m_active, response, validation);
        return validation;
    }

    // Exactly-once:先收檔,再通知。任何 handler 喺呢一刻再 submit 同一個
    // request 都只會攞到 AlreadyCompleted。
    const InteractionRequest finished = m_active;
    recordCompleted(finished.requestId, CompletionKind::Answered);
    clearActive();
    ++m_acceptedCount;

    emit responseAccepted(finished.requestId);
    if (m_view)
        m_view->finishRequest(finished, response);
    return validation;
}

void ClientCore::cancelActiveRequest(InteractionCancelReason reason)
{
    if (!hasActiveRequest())
        return;

    const InteractionRequest cancelled = m_active;
    recordCompleted(cancelled.requestId,
        reason == InteractionCancelReason::Expired ? CompletionKind::Expired : CompletionKind::Cancelled);
    clearActive();
    ++m_cancelledCount;

    emit requestCancelled(cancelled.requestId, static_cast<int>(reason));
    if (m_view)
        m_view->cancelRequest(cancelled, reason);
}

bool ClientCore::expireIfDue()
{
    if (!hasActiveRequest() || m_active.deadlineMs <= 0)
        return false;
    if (now() <= m_active.deadlineMs)
        return false;
    cancelActiveRequest(InteractionCancelReason::Expired);
    return true;
}

QJsonObject ClientCore::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("state"), m_state.toJson());
    object.insert(QStringLiteral("started"), static_cast<qint64>(m_startedCount));
    object.insert(QStringLiteral("accepted"), static_cast<qint64>(m_acceptedCount));
    object.insert(QStringLiteral("rejected"), static_cast<qint64>(m_rejectedCount));
    object.insert(QStringLiteral("cancelled"), static_cast<qint64>(m_cancelledCount));
    object.insert(QStringLiteral("has_view"), m_view != nullptr);
    if (hasActiveRequest())
        object.insert(QStringLiteral("active_request"), m_active.toJson());
    return object;
}
