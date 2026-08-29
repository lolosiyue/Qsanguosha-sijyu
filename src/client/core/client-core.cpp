#include "client-core.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSet>

#include <limits>

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
    case InteractionResponseKind::Option: {
        const InteractionResponse::OptionData *value
            = response.payloadAs<InteractionResponse::OptionData>();
        return value == nullptr || value->value.isEmpty();
    }
    case InteractionResponseKind::Players: {
        const InteractionResponse::PlayerSelectionData *value
            = response.payloadAs<InteractionResponse::PlayerSelectionData>();
        return value == nullptr || value->names.isEmpty();
    }
    case InteractionResponseKind::Cards: {
        const InteractionResponse::CardSelectionData *value
            = response.payloadAs<InteractionResponse::CardSelectionData>();
        return value == nullptr || (value->cardIds.isEmpty() && value->cardText.isEmpty());
    }
    case InteractionResponseKind::Assignment: {
        const InteractionResponse::AssignmentData *value
            = response.payloadAs<InteractionResponse::AssignmentData>();
        return value == nullptr || value->names.isEmpty();
    }
    case InteractionResponseKind::Rearrangement: {
        const InteractionResponse::RearrangementData *value
            = response.payloadAs<InteractionResponse::RearrangementData>();
        return value == nullptr || (value->first.isEmpty() && value->second.isEmpty());
    }
    case InteractionResponseKind::Distribution: {
        const InteractionResponse::DistributionData *value
            = response.payloadAs<InteractionResponse::DistributionData>();
        return value == nullptr || value->cards.isEmpty() || value->target.isEmpty();
    }
    case InteractionResponseKind::GeneralArrangement: {
        const InteractionResponse::GeneralArrangementData *value
            = response.payloadAs<InteractionResponse::GeneralArrangementData>();
        return value == nullptr || value->generalNames.isEmpty();
    }
    case InteractionResponseKind::Custom:
        return response.payloadAs<InteractionResponse::CustomData>() == nullptr;
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

void sanitizeRequestMetadata(InteractionRequest &request)
{
    static const QString eligibilityDiagnostic
        = QStringLiteral("eligibility_diagnostic");
    const QStringList keys = request.metadata.keys();
    for (const QString &key : keys) {
        if (key == eligibilityDiagnostic)
            continue;
        qCWarning(qsanClientCore).noquote()
            << "discarding non-diagnostic interaction metadata key:" << key;
        request.metadata.remove(key);
    }
}

InteractionResponseKind expectedKind(InteractionResponseShape shape)
{
    switch (shape) {
    case InteractionResponseShape::Option: return InteractionResponseKind::Option;
    case InteractionResponseShape::Players: return InteractionResponseKind::Players;
    case InteractionResponseShape::Cards: return InteractionResponseKind::Cards;
    case InteractionResponseShape::Assignment: return InteractionResponseKind::Assignment;
    case InteractionResponseShape::Rearrangement: return InteractionResponseKind::Rearrangement;
    case InteractionResponseShape::Distribution: return InteractionResponseKind::Distribution;
    case InteractionResponseShape::GeneralArrangement: return InteractionResponseKind::GeneralArrangement;
    case InteractionResponseShape::Custom: return InteractionResponseKind::Custom;
    case InteractionResponseShape::None: break;
    }
    return InteractionResponseKind::None;
}

}  // namespace

ClientCore::ClientCore(QObject *parent)
    : QObject(parent)
    , m_clock(&monotonicNow)
{
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, [this]() {
        if (!expireIfDue() && hasActiveRequest())
            scheduleDeadlineTimer();
    });
}

ClientCore::~ClientCore()
{
    // core 死嗰陣唔通知 view:view 通常就係跟住一齊拆。
    m_view = nullptr;
}

void ClientCore::setClock(Clock clock)
{
    m_clock = clock ? clock : Clock(&monotonicNow);
    scheduleDeadlineTimer();
}

void ClientCore::setCardEligibilityProvider(const ICardEligibilityProvider *provider)
{
    m_cardEligibilityProvider = provider;
}

qint64 ClientCore::now() const
{
    return m_clock ? m_clock() : monotonicNow();
}

void ClientCore::enrichEligibilityHints(InteractionRequest &request) const
{
    if (m_cardEligibilityProvider == nullptr)
        return;
    CardInteractionPayload *payload = std::get_if<CardInteractionPayload>(&request.payload);
    if (payload == nullptr || payload->selection.enumerated)
        return;

    const CardEligibilityResult result = m_cardEligibilityProvider->resolve(request);
    payload->suggestedCards = result.suggestedCards;
    payload->suggestedDisabledCards = result.suggestedDisabledCards;
    if (!result.diagnostic.isEmpty())
        request.metadata.insert(QStringLiteral("eligibility_diagnostic"), result.diagnostic);
}

void ClientCore::scheduleDeadlineTimer()
{
    m_deadlineTimer.stop();
    if (!hasActiveRequest() || m_active.deadlineMs <= 0)
        return;

    const qint64 remaining = qMax<qint64>(1, m_active.deadlineMs - now() + 1);
    const int interval = static_cast<int>(qMin<qint64>(remaining,
        std::numeric_limits<int>::max()));
    m_deadlineTimer.start(interval);
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

    sanitizeRequestMetadata(request);
    enrichEligibilityHints(request);
    request.deadlineMs = request.timeoutMs > 0 ? now() + request.timeoutMs : 0;

    m_active = request;
    scheduleDeadlineTimer();
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
    m_deadlineTimer.stop();
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

    if (response.serverSerial != 0 && response.serverSerial != m_active.serverSerial) {
        return InteractionValidation::fail(InteractionRejection::ServerSerialMismatch,
            QStringLiteral("reply targets server serial %1 but %2 is active")
                .arg(response.serverSerial).arg(m_active.serverSerial));
    }
    if (response.command != 0 && response.command != m_active.command) {
        return InteractionValidation::fail(InteractionRejection::CommandMismatch,
            QStringLiteral("reply targets command %1 but %2 is active")
                .arg(response.command).arg(m_active.command));
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

    InteractionResponseKind wanted = expectedKind(request.responseSchema);
    if (wanted == InteractionResponseKind::None)
        wanted = expectedKind(request.type);
    if (wanted != InteractionResponseKind::None && response.kind != wanted) {
        return InteractionValidation::fail(InteractionRejection::KindMismatch,
            QStringLiteral("request %1 expects %2 but the reply is %3")
                .arg(request.requestId)
                .arg(interactionResponseKindName(wanted))
                .arg(interactionResponseKindName(response.kind)));
    }

    using Validator = InteractionValidation (ClientCore::*)(
        const InteractionRequest &, const InteractionResponse &) const;
    struct ValidatorEntry
    {
        InteractionResponseKind kind;
        Validator validator;
    };
    static const ValidatorEntry validators[] = {
        { InteractionResponseKind::Option, &ClientCore::validateOption },
        { InteractionResponseKind::Players, &ClientCore::validatePlayers },
        { InteractionResponseKind::Cards, &ClientCore::validateCards },
        { InteractionResponseKind::Assignment, &ClientCore::validateAssignment },
        { InteractionResponseKind::Rearrangement, &ClientCore::validateRearrangement },
        { InteractionResponseKind::Distribution, &ClientCore::validateDistribution },
        { InteractionResponseKind::GeneralArrangement,
            &ClientCore::validateGeneralArrangement },
        { InteractionResponseKind::Custom, &ClientCore::validateCustom },
    };
    for (const ValidatorEntry &entry : validators) {
        if (entry.kind == response.kind)
            return (this->*entry.validator)(request, response);
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateOption(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const InteractionResponse::OptionData *answer
        = response.payloadAs<InteractionResponse::OptionData>();
    if (answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
	if (const TriggerOrderInteractionPayload *trigger
		= request.payloadAs<TriggerOrderInteractionPayload>()) {
		for (const TriggerOrderOption &option : trigger->options) {
			if (option.responseValue == answer->value)
				return InteractionValidation::ok();
		}
		return InteractionValidation::fail(InteractionRejection::UnknownOption,
			QStringLiteral("'%1' is not a trigger option of request %2")
				.arg(answer->value).arg(request.requestId));
	}
    const InteractionOption *option = request.option(answer->value);
    const OptionInteractionPayload *typed = request.payloadAs<OptionInteractionPayload>();
    const bool enumerated = typed == nullptr || typed->enumerated;
    if (option == nullptr) {
        // 非枚舉 request（例如 FreeChoose 之下嘅 choose general）冇清單以外
        // 嘅約束,但答案本身仍然唔可以係空。
        if (!enumerated)
            return InteractionValidation::ok();
        return InteractionValidation::fail(InteractionRejection::UnknownOption,
            QStringLiteral("'%1' is not an option of request %2")
                .arg(answer->value).arg(request.requestId));
    }
    if (!option->enabled) {
        return InteractionValidation::fail(InteractionRejection::DisabledOption,
            QStringLiteral("option '%1' of request %2 is disabled")
                .arg(answer->value).arg(request.requestId));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validatePlayers(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const PlayerInteractionPayload *typed = request.payloadAs<PlayerInteractionPayload>();
    const InteractionResponse::PlayerSelectionData *answer
        = response.payloadAs<InteractionResponse::PlayerSelectionData>();
    if (typed == nullptr || answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
    const PlayerSelectionState &selection = typed->selection;

    QSet<QString> seen;
    foreach (const QString &name, answer->names) {
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

    const int count = answer->names.size();
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
    const CardInteractionPayload *typed = request.payloadAs<CardInteractionPayload>();
    const PindianInteractionPayload *pindian = request.payloadAs<PindianInteractionPayload>();
    const GongxinInteractionPayload *gongxin = request.payloadAs<GongxinInteractionPayload>();
    const AmazingGraceInteractionPayload *amazingGrace
        = request.payloadAs<AmazingGraceInteractionPayload>();
    const InteractionResponse::CardSelectionData *answer
        = response.payloadAs<InteractionResponse::CardSelectionData>();
    if (answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
    CardSelectionState gongxinSelection;
    if (gongxin != nullptr) {
        gongxinSelection.enumerated = true;
        gongxinSelection.selectableCards = gongxin->selectableCards;
        gongxinSelection.minSelection = 0;
        gongxinSelection.maxSelection = 1;
    }
    if (typed == nullptr && pindian == nullptr && gongxin == nullptr && amazingGrace == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
    const CardSelectionState &selection = typed != nullptr ? typed->selection
        : (pindian != nullptr ? pindian->selection
            : (gongxin != nullptr ? gongxinSelection : amazingGrace->selection));
	if (amazingGrace != nullptr && !amazingGrace->selectable
		&& !answer->cardIds.isEmpty()) {
		return InteractionValidation::fail(InteractionRejection::DisabledCard,
			QStringLiteral("Amazing Grace selection is disabled"));
	}

    QSet<int> seen;
    foreach (int cardId, answer->cardIds) {
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
    if (!answer->cardText.isEmpty() && typed != nullptr && !typed->cardTextAllowed) {
        return InteractionValidation::fail(InteractionRejection::MalformedResponse,
            QStringLiteral("request %1 does not allow card text").arg(request.requestId));
    }
    const int count = answer->cardIds.isEmpty() && !answer->cardText.isEmpty()
        ? 1 : answer->cardIds.size();
    if (count < selection.minSelection
        || (selection.maxSelection > 0 && count > selection.maxSelection)) {
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("request %1 wants %2..%3 cards but the reply has %4")
                .arg(request.requestId).arg(selection.minSelection)
                .arg(selection.maxSelection).arg(count));
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateAssignment(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const RoleAssignmentInteractionPayload *payload
        = request.payloadAs<RoleAssignmentInteractionPayload>();
    const InteractionResponse::AssignmentData *answer
        = response.payloadAs<InteractionResponse::AssignmentData>();
    if (payload == nullptr || answer == nullptr || answer->names.size() != answer->values.size()) {
        return InteractionValidation::fail(InteractionRejection::MalformedResponse,
            QStringLiteral("assignment response does not match its request schema"));
    }

    QSet<QString> seen;
    for (int i = 0; i < answer->names.size(); ++i) {
        const QString &name = answer->names.at(i);
        const QString &value = answer->values.at(i);
        if (seen.contains(name))
            return InteractionValidation::fail(InteractionRejection::DuplicatePlayer, name);
        seen.insert(name);
        if (!payload->playerNames.isEmpty() && !payload->playerNames.contains(name))
            return InteractionValidation::fail(InteractionRejection::UnknownPlayer, name);
        if (!payload->roles.isEmpty() && !payload->roles.contains(value))
            return InteractionValidation::fail(InteractionRejection::UnknownOption, value);
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateRearrangement(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const RearrangeCardsInteractionPayload *payload
        = request.payloadAs<RearrangeCardsInteractionPayload>();
    const InteractionResponse::RearrangementData *answer
        = response.payloadAs<InteractionResponse::RearrangementData>();
    if (payload == nullptr || answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);

    QList<int> combined = answer->first;
    combined.append(answer->second);
    QSet<int> actual;
    for (int cardId : combined) {
        if (actual.contains(cardId))
            return InteractionValidation::fail(InteractionRejection::DuplicateCard,
                QString::number(cardId));
        actual.insert(cardId);
    }
    QSet<int> expected;
    for (int cardId : payload->cardIds)
        expected.insert(cardId);
    if (actual != expected)
        return InteractionValidation::fail(InteractionRejection::UnknownCard,
            QStringLiteral("rearrangement must contain every requested card exactly once"));
    if (payload->mode == RearrangementMode::UpOnly && !answer->second.isEmpty())
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("up-only rearrangement cannot contain bottom cards"));
    if (payload->mode == RearrangementMode::DownOnly && !answer->first.isEmpty())
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("down-only rearrangement cannot contain top cards"));
    if (answer->first.size() < payload->minTop
        || (payload->maxTop > 0 && answer->first.size() > payload->maxTop)
        || answer->second.size() < payload->minBottom
        || (payload->maxBottom > 0 && answer->second.size() > payload->maxBottom)) {
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange);
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateDistribution(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const YijiInteractionPayload *payload = request.payloadAs<YijiInteractionPayload>();
    const InteractionResponse::DistributionData *answer
        = response.payloadAs<InteractionResponse::DistributionData>();
    if (payload == nullptr || answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
    if (!payload->targetPlayers.contains(answer->target))
        return InteractionValidation::fail(InteractionRejection::UnknownPlayer, answer->target);

    QSet<int> seen;
    for (int cardId : answer->cards) {
        if (seen.contains(cardId))
            return InteractionValidation::fail(InteractionRejection::DuplicateCard,
                QString::number(cardId));
        seen.insert(cardId);
        if (!payload->cardIds.contains(cardId))
            return InteractionValidation::fail(InteractionRejection::UnknownCard,
                QString::number(cardId));
    }
    const int effectiveMax = payload->remainingCount > 0
        ? qMin(payload->maxCards, payload->remainingCount) : payload->maxCards;
    if (answer->cards.size() < payload->minCards
        || (effectiveMax > 0 && answer->cards.size() > effectiveMax)) {
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange);
    }
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateGeneralArrangement(
    const InteractionRequest &request, const InteractionResponse &response) const
{
    const ArrangeGeneralsInteractionPayload *payload
        = request.payloadAs<ArrangeGeneralsInteractionPayload>();
    const InteractionResponse::GeneralArrangementData *answer
        = response.payloadAs<InteractionResponse::GeneralArrangementData>();
    if (payload == nullptr || answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);

    QSet<QString> seen;
    for (const QString &name : answer->generalNames) {
        if (seen.contains(name))
            return InteractionValidation::fail(InteractionRejection::DuplicateGeneral, name);
        seen.insert(name);
        if (!payload->generalNames.contains(name))
            return InteractionValidation::fail(InteractionRejection::UnknownGeneral, name);
    }
    if (answer->generalNames.size() != payload->slotCount)
        return InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange);
    return InteractionValidation::ok();
}

InteractionValidation ClientCore::validateCustom(const InteractionRequest &request,
    const InteractionResponse &response) const
{
    const CustomInteractionPayload *payload = request.payloadAs<CustomInteractionPayload>();
    const InteractionResponse::CustomData *answer
        = response.payloadAs<InteractionResponse::CustomData>();
    if (payload == nullptr || answer == nullptr)
        return InteractionValidation::fail(InteractionRejection::MalformedResponse);
    if (answer->schemaVersion != payload->schemaVersion || answer->typeName != payload->typeName) {
        return InteractionValidation::fail(InteractionRejection::MalformedResponse,
            QStringLiteral("custom response schema does not match the active request"));
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
