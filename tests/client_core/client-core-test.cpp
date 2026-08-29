// Client Architecture F1：ClientCore 的契約測試。
//
// 只 link Qt6::Core 同 qsanguosha_client_core：request model、response 驗證、
// exactly-once completion、cancel／timeout 同 view 生命週期全部唔應該要開
// QApplication、RoomScene 或者一局真遊戲先驗到。真正嘅 desktop 呈現由
// --local-response-ui runner 喺 GUI build 度驗。
#include "client-core.h"
#include "client-interaction-view.h"
#include "interaction-model.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        printf("PASS %s\n", what);
        return;
    }
    printf("FAIL %s\n", what);
    ++failures;
}

void checkEqual(const QByteArray &actual, const QByteArray &expected, const char *what)
{
    if (actual == expected) {
        printf("PASS %s\n", what);
        return;
    }
    printf("FAIL %s\n     expected: %s\n     actual:   %s\n", what,
        expected.constData(), actual.constData());
    ++failures;
}

void checkRejection(const InteractionValidation &validation, InteractionRejection expected,
    const char *what)
{
    if (validation.rejection == expected) {
        printf("PASS %s\n", what);
        return;
    }
    const QByteArray actualName = validation.reasonName().toUtf8();
    const QByteArray expectedName = interactionRejectionName(expected).toUtf8();
    printf("FAIL %s\n     expected: %s\n     actual:   %s (%s)\n", what,
        expectedName.constData(), actualName.constData(), validation.detail.toUtf8().constData());
    ++failures;
}

// ── 錄音 view ────────────────────────────────────────────────────────────
// Desktop adapter 契約嘅測試替身：記低 ClientCore 打過嚟嘅每一個 callback，
// 令「呈現一次／完成一次／拒絕唔會收檔」呢啲次序保證驗得到。
class RecordingView : public IClientInteractionView
{
public:
    struct Event
    {
        QString kind;
        quint64 requestId = 0;
        QString detail;
    };

    ~RecordingView() override
    {
        if (m_core != nullptr)
            m_core->detachView();
    }

    // view 死之前一定要同 core 解綁；呢個 helper 令測試唔使喺每個 case 重覆寫。
    void attachTo(ClientCore *core)
    {
        m_core = core;
        core->setView(this);
    }

    void presentRequest(const InteractionRequest &request) override
    {
        Event event;
        event.kind = QStringLiteral("present");
        event.requestId = request.requestId;
        event.detail = interactionTypeName(request.type);
        events.append(event);
    }

    void finishRequest(const InteractionRequest &request, const InteractionResponse &response) override
    {
        Event event;
        event.kind = QStringLiteral("finish");
        event.requestId = request.requestId;
        event.detail = interactionResponseKindName(response.kind);
        events.append(event);
    }

    void cancelRequest(const InteractionRequest &request, InteractionCancelReason reason) override
    {
        Event event;
        event.kind = QStringLiteral("cancel");
        event.requestId = request.requestId;
        event.detail = interactionCancelReasonName(reason);
        events.append(event);
    }

    void rejectResponse(const InteractionRequest &request, const InteractionResponse &,
        const InteractionValidation &validation) override
    {
        Event event;
        event.kind = QStringLiteral("reject");
        event.requestId = request.requestId;
        event.detail = validation.reasonName();
        events.append(event);
    }

    QString trace() const
    {
        QStringList parts;
        foreach (const Event &event, events)
            parts << QStringLiteral("%1(%2)").arg(event.kind, event.detail);
        return parts.join(QLatin1Char(','));
    }

    QList<Event> events;

private:
    ClientCore *m_core = nullptr;
};

// 數 signal。唔用 QSignalSpy：呢個測試刻意只 link Qt6::Core，多拉一個
// Qt6::Test 入嚟就會令「ClientCore 唔依賴 GUI」呢個 gate 冇咁鋒利。
class SignalCounter
{
public:
    template <typename Signal>
    SignalCounter(ClientCore *core, Signal signal)
    {
        QObject::connect(core, signal, core, [this]() { ++m_count; });
    }

    int count() const { return m_count; }

private:
    int m_count = 0;
};

// ── request builders ────────────────────────────────────────────────────

InteractionRequest chooseCardRequest()
{
    InteractionRequest request;
    request.requestId = 42;
    request.type = InteractionType::ResponseCard;
    request.responseSchema = InteractionResponseShape::Cards;
    CardInteractionPayload payload;
    payload.selection.enumerated = true;
    payload.selection.selectableCards = QList<int>() << 7 << 12;
    payload.selection.minSelection = 1;
    payload.selection.maxSelection = 1;
    request.payload = payload;
    return request;
}

InteractionRequest choiceRequest()
{
    InteractionRequest request;
    request.type = InteractionType::Choice;
    request.skillName = QStringLiteral("guhuo");
    request.responseSchema = InteractionResponseShape::Option;
    OptionInteractionPayload payload;
    payload.options << InteractionOption(QStringLiteral("yes"))
                    << InteractionOption(QStringLiteral("no"))
                    << InteractionOption(QStringLiteral("later"), QString(), false);
    request.payload = payload;
    return request;
}

InteractionRequest playerRequest()
{
    InteractionRequest request;
    request.type = InteractionType::ChoosePlayer;
    request.skillName = QStringLiteral("guicai");
    request.responseSchema = InteractionResponseShape::Players;
    PlayerInteractionPayload payload;
    payload.selection.selectablePlayers = QStringList() << QStringLiteral("sgs1")
        << QStringLiteral("sgs2") << QStringLiteral("sgs3");
    payload.selection.minSelection = 1;
    payload.selection.maxSelection = 2;
    request.payload = payload;
    return request;
}

// ── snapshot ────────────────────────────────────────────────────────────

void testRequestSnapshot()
{
    // 任務書指定嘅 deterministic snapshot 形狀。key 排序由 QJsonObject 保證，
    // 所以同一個 request 喺任何平台都出同一串 bytes。
    InteractionRequest request = chooseCardRequest();
    checkEqual(request.toSnapshot(),
        QByteArray("{\"cancelable\":false,\"max\":1,\"min\":1,"
                   "\"payload\":{\"card_text_allowed\":false,\"max\":1,\"min\":1,"
                   "\"selectable_cards\":[7,12],\"virtual_card_allowed\":false},"
                   "\"request_id\":42,\"response_schema\":\"cards\","
                   "\"type\":\"response_card\"}"),
        "choose card request snapshot is deterministic");

    InteractionRequest choice = choiceRequest();
    choice.requestId = 7;
    choice.command = 24;
    choice.cancelable = true;
    choice.timeoutMs = 15000;
    checkEqual(choice.toSnapshot(),
        QByteArray("{\"cancelable\":true,\"command\":24,\"max\":1,\"min\":0,"
                   "\"payload\":{\"enumerated\":true,\"options\":[{\"value\":\"yes\"},"
                   "{\"value\":\"no\"},{\"enabled\":false,\"value\":\"later\"}]},"
                   "\"request_id\":7,\"response_schema\":\"option\",\"skill\":\"guhuo\","
                   "\"timeout_ms\":15000,\"type\":\"choice\"}"),
        "choice request snapshot carries options and timeout");

    InteractionRequest players = playerRequest();
    players.requestId = 9;
    checkEqual(players.toSnapshot(),
        QByteArray("{\"cancelable\":false,\"max\":2,\"min\":1,"
                   "\"payload\":{\"max\":2,\"min\":1,\"selectable_players\":[\"sgs1\","
                   "\"sgs2\",\"sgs3\"]},\"request_id\":9,\"response_schema\":\"players\","
                   "\"skill\":\"guicai\",\"type\":\"choose_player\"}"),
        "player request snapshot carries the selectable roster");

    // 同一個 request 序列化兩次一定出同一串 bytes。
    checkEqual(players.toSnapshot(), players.toSnapshot(), "snapshot is stable across calls");

    // 非枚舉 option（FreeChoose 之下嘅 choose general）喺 snapshot 度睇得出。
    InteractionRequest general;
    general.requestId = 3;
    general.type = InteractionType::ChooseGeneral;
    general.responseSchema = InteractionResponseShape::Option;
    OptionInteractionPayload generalPayload;
    generalPayload.enumerated = false;
    generalPayload.options << InteractionOption(QStringLiteral("zhangfei"));
    general.payload = generalPayload;
    checkEqual(general.toSnapshot(),
        QByteArray("{\"cancelable\":false,\"max\":1,\"min\":1,"
                   "\"payload\":{\"enumerated\":false,\"options\":[{\"value\":\"zhangfei\"}]},"
                   "\"request_id\":3,\"response_schema\":\"option\","
                   "\"type\":\"choose_general\"}"),
        "choose general snapshot marks its options advisory");

    checkEqual(InteractionResponse::makeCards(42, QList<int>() << 7).toSnapshot(),
        QByteArray("{\"kind\":\"cards\",\"payload\":{\"cards\":[7]},\"request_id\":42}"),
        "response snapshot is deterministic");

    check(interactionTypeFromName(QStringLiteral("choose_player")) == InteractionType::ChoosePlayer,
        "interaction type names round-trip");
}

// ── request id 同 correlation ────────────────────────────────────────────

void testRequestIdentity()
{
    ClientCore core;
    RecordingView view;
    view.attachTo(&core);

    InteractionRequest first = choiceRequest();
    const quint64 firstId = core.beginRequest(first);
    check(firstId == 1, "the first auto-numbered request id is 1");
    check(core.activeRequestId() == firstId, "the request becomes active");
    check(core.hasActiveRequest(InteractionType::Choice), "the active request keeps its type");
    check(!core.hasActiveRequest(InteractionType::ChoosePlayer),
        "a type query does not match a different type");

    // 上一個 request 未答就嚟第二個：舊嗰個以 superseded 收檔，唔會送任何答案。
    const quint64 secondId = core.beginRequest(playerRequest());
    check(secondId == 2, "request ids keep increasing");
    check(view.trace() == QLatin1String("present(choice),cancel(superseded),present(choose_player)"),
        "a new request supersedes the pending one");

    // 舊 id 嘅遲到答案唔會被當成新 request 嘅答案。
    checkRejection(core.submitResponse(InteractionResponse::makePlayers(firstId,
            QStringList() << QStringLiteral("sgs1"))),
        InteractionRejection::RequestCancelled, "a reply to a superseded request is rejected");

    // 明確編號嘅 request 會推高之後嘅自動編號。
    ClientCore explicitCore;
    InteractionRequest numbered = choiceRequest();
    numbered.requestId = 100;
    explicitCore.beginRequest(numbered);
    explicitCore.submitResponse(InteractionResponse::makeOption(100, QStringLiteral("yes")));
    check(explicitCore.beginRequest(choiceRequest()) == 101,
        "auto numbering continues past an explicit request id");
}

// ── 驗證 ────────────────────────────────────────────────────────────────

void testOptionValidation()
{
    ClientCore core;
    const quint64 id = core.beginRequest(choiceRequest());

    checkRejection(core.validate(InteractionResponse::makeOption(id, QStringLiteral("maybe"))),
        InteractionRejection::UnknownOption, "an option that was never offered is rejected");
    checkRejection(core.validate(InteractionResponse::makeOption(id, QStringLiteral("later"))),
        InteractionRejection::DisabledOption, "a disabled option is rejected");
    checkRejection(core.validate(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1"))),
        InteractionRejection::KindMismatch, "a player answer to a choice request is rejected");
    check(core.validate(InteractionResponse::makeOption(id, QStringLiteral("yes"))).accepted(),
        "an offered option is accepted");
    check(core.hasActiveRequest(), "validate() alone does not complete the request");

    // 非枚舉 request 收清單以外嘅答案，但仍然唔收空答案。
    ClientCore freeCore;
    InteractionRequest general;
    general.type = InteractionType::ChooseGeneral;
    general.responseSchema = InteractionResponseShape::Option;
    OptionInteractionPayload generalPayload;
    generalPayload.enumerated = false;
    generalPayload.options << InteractionOption(QStringLiteral("zhangfei"));
    general.payload = generalPayload;
    const quint64 generalId = freeCore.beginRequest(general);
    check(freeCore.validate(InteractionResponse::makeOption(generalId,
            QStringLiteral("caocao"))).accepted(),
        "an unlisted general is accepted when the options are advisory");
    checkRejection(freeCore.validate(InteractionResponse::makeOption(generalId, QString())),
        InteractionRejection::NotCancelable, "an empty answer to a mandatory request is rejected");
}

void testPlayerValidation()
{
    ClientCore core;
    const quint64 id = core.beginRequest(playerRequest());

    checkRejection(core.validate(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs9"))),
        InteractionRejection::UnknownPlayer, "a player outside the roster is rejected");
    checkRejection(core.validate(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs1"))),
        InteractionRejection::DuplicatePlayer, "the same player cannot be picked twice");
    checkRejection(core.validate(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2")
                          << QStringLiteral("sgs3"))),
        InteractionRejection::SelectionCountOutOfRange, "picking more players than max is rejected");
    checkRejection(core.validate(InteractionResponse::makePlayers(id, QStringList())),
        InteractionRejection::NotCancelable, "an empty pick is rejected when min is 1");
    check(core.validate(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2"))).accepted(),
        "a legal player selection is accepted");

    // cancelable（min == 0）嘅 request 收得起空答案。
    ClientCore optionalCore;
    InteractionRequest optional = playerRequest();
    std::get<PlayerInteractionPayload>(optional.payload).selection.minSelection = 0;
    optional.cancelable = true;
    const quint64 optionalId = optionalCore.beginRequest(optional);
    check(optionalCore.validate(InteractionResponse::makePlayers(optionalId, QStringList())).accepted(),
        "an empty pick is accepted when the request is cancelable");
    check(optionalCore.validate(InteractionResponse::makeCancel(optionalId)).accepted(),
        "an explicit cancel is accepted when the request is cancelable");
}

void testCardValidation()
{
    ClientCore core;
    core.state()->setCardIdSpace(160);
    const quint64 id = core.beginRequest(chooseCardRequest());

    checkRejection(core.validate(InteractionResponse::makeCards(id, QList<int>() << 13)),
        InteractionRejection::UnknownCard, "a card outside the selectable set is rejected");
    checkRejection(core.validate(InteractionResponse::makeCards(id, QList<int>() << 900)),
        InteractionRejection::UnknownCard, "a card id outside the id space is rejected");
    checkRejection(core.validate(InteractionResponse::makeCards(id, QList<int>() << 7 << 12)),
        InteractionRejection::SelectionCountOutOfRange, "selecting more cards than max is rejected");
    checkRejection(core.validate(InteractionResponse::makeCards(id, QList<int>())),
        InteractionRejection::NotCancelable, "an empty card reply is rejected when min is 1");
    check(core.validate(InteractionResponse::makeCards(id, QList<int>() << 12)).accepted(),
        "a selectable card is accepted");

    // disabled 清單贏過 selectable 清單。
    ClientCore disabledCore;
    InteractionRequest disabled = chooseCardRequest();
    CardInteractionPayload &disabledPayload
        = std::get<CardInteractionPayload>(disabled.payload);
    disabledPayload.selection.selectableCards << 13;
    disabledPayload.selection.disabledCards << 13;
    const quint64 disabledId = disabledCore.beginRequest(disabled);
    checkRejection(disabledCore.validate(InteractionResponse::makeCards(disabledId, QList<int>() << 13)),
        InteractionRejection::DisabledCard, "a disabled card is rejected even when listed");

    // 非枚舉（pattern 配對）嘅 response card：成員資格由 server 判，但值域、
    // 數量同取消權仍然由 ClientCore 執行。
    ClientCore patternCore;
    patternCore.state()->setCardIdSpace(160);
    InteractionRequest pattern;
    pattern.type = InteractionType::ResponseCard;
    pattern.responseSchema = InteractionResponseShape::Cards;
    CardInteractionPayload patternPayload;
    patternPayload.selection.enumerated = false;
    patternPayload.selection.pattern = QStringLiteral("jink");
    patternPayload.selection.minSelection = 1;
    patternPayload.selection.maxSelection = 1;
    patternPayload.cardTextAllowed = true;
    pattern.payload = patternPayload;
    const quint64 patternId = patternCore.beginRequest(pattern);
    check(patternCore.validate(InteractionResponse::makeCards(patternId, QList<int>() << 88)).accepted(),
        "a pattern-matched request does not police card membership");
    checkRejection(patternCore.validate(InteractionResponse::makeCards(patternId, QList<int>() << -5)),
        InteractionRejection::UnknownCard, "a negative card id is rejected even without a card list");
    checkRejection(patternCore.validate(InteractionResponse::makeCards(patternId, QList<int>() << 7 << 7)),
        InteractionRejection::DuplicateCard, "the same card cannot be submitted twice");
    // virtual card 冇實 id，但一定有 toString()。
    check(patternCore.validate(InteractionResponse::makeCards(patternId, QList<int>(),
            QStringLiteral("jink:qiaobian[spade:7]="))).accepted(),
        "a virtual card with no concrete id counts as one card");
}

// ── exactly-once ────────────────────────────────────────────────────────

void testExactlyOnceCompletion()
{
    ClientCore core;
    RecordingView view;
    view.attachTo(&core);
    SignalCounter accepted(&core, &ClientCore::responseAccepted);

    const quint64 id = core.beginRequest(choiceRequest());
    check(core.submitResponse(InteractionResponse::makeOption(id, QStringLiteral("yes"))).accepted(),
        "the first answer is accepted");
    check(!core.hasActiveRequest(), "an accepted answer completes the request");

    checkRejection(core.submitResponse(InteractionResponse::makeOption(id, QStringLiteral("no"))),
        InteractionRejection::AlreadyCompleted, "a duplicate answer is rejected");
    checkRejection(core.submitResponse(InteractionResponse::makeOption(id, QStringLiteral("yes"))),
        InteractionRejection::AlreadyCompleted, "replaying the identical answer is also rejected");
    checkRejection(core.submitResponse(InteractionResponse::makeOption(0, QStringLiteral("yes"))),
        InteractionRejection::NoActiveRequest, "answering with no request pending is rejected");

    check(accepted.count() == 1, "responseAccepted fires exactly once");
    check(core.acceptedCount() == 1, "exactly one answer is counted");
    check(core.rejectedCount() == 3, "every later answer is counted as rejected");
    check(view.trace() == QLatin1String("present(choice),finish(option)"),
        "the view is told about the completion exactly once");

    // 被拒嘅答案唔會收檔：request 仲喺度等一個好答案。
    ClientCore retryCore;
    RecordingView retryView;
    retryView.attachTo(&retryCore);
    const quint64 retryId = retryCore.beginRequest(choiceRequest());
    retryCore.submitResponse(InteractionResponse::makeOption(retryId, QStringLiteral("maybe")));
    check(retryCore.hasActiveRequest(), "a rejected answer leaves the request pending");
    check(retryCore.submitResponse(InteractionResponse::makeOption(retryId,
            QStringLiteral("yes"))).accepted(),
        "a valid answer still lands after a rejected one");
    check(retryView.trace() == QLatin1String("present(choice),reject(unknown_option),finish(option)"),
        "the view sees the rejection before the completion");

    // id 對唔上嘅答案唔會完成 active request。
    ClientCore mismatchCore;
    const quint64 mismatchId = mismatchCore.beginRequest(choiceRequest());
    checkRejection(mismatchCore.submitResponse(InteractionResponse::makeOption(mismatchId + 500,
            QStringLiteral("yes"))),
        InteractionRejection::RequestIdMismatch, "an answer with the wrong request id is rejected");
    check(mismatchCore.hasActiveRequest(), "a mismatched id leaves the request pending");
}

// ── cancel／timeout ──────────────────────────────────────────────────────

void testCancelAndTimeout()
{
    ClientCore core;
    RecordingView view;
    view.attachTo(&core);
    SignalCounter cancelled(&core, &ClientCore::requestCancelled);

    const quint64 id = core.beginRequest(playerRequest());
    core.cancelActiveRequest(InteractionCancelReason::Abandoned);
    check(!core.hasActiveRequest(), "cancelling clears the active request");
    check(cancelled.count() == 1, "requestCancelled fires once");
    checkRejection(core.submitResponse(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1"))),
        InteractionRejection::RequestCancelled, "answering a cancelled request is rejected");
    check(view.trace() == QLatin1String("present(choose_player),cancel(abandoned)"),
        "the view is told once about the cancellation");
    core.cancelActiveRequest(InteractionCancelReason::Abandoned);
    check(cancelled.count() == 1, "cancelling twice is a no-op");

    // 注入時鐘：死線係測試得到嘅純數值，唔使真係等。
    qint64 clock = 0;
    ClientCore timed;
    timed.setClock([&clock]() { return clock; });
    RecordingView timedView;
    timedView.attachTo(&timed);

    InteractionRequest request = playerRequest();
    request.timeoutMs = 15000;
    const quint64 timedId = timed.beginRequest(request);
    check(timed.activeRequest().deadlineMs == 15000, "the deadline is derived from the timeout");

    clock = 14999;
    check(!timed.expireIfDue(), "a request does not expire before its deadline");
    check(timed.validate(InteractionResponse::makePlayers(timedId,
            QStringList() << QStringLiteral("sgs1"))).accepted(),
        "an answer just before the deadline is accepted");

    clock = 15001;
    checkRejection(timed.submitResponse(InteractionResponse::makePlayers(timedId,
            QStringList() << QStringLiteral("sgs1"))),
        InteractionRejection::RequestExpired, "an answer after the deadline is rejected");
    check(timed.hasActiveRequest(), "a late answer alone does not retire the request");
    check(timed.expireIfDue(), "expireIfDue() retires an overdue request");
    checkRejection(timed.submitResponse(InteractionResponse::makePlayers(timedId,
            QStringList() << QStringLiteral("sgs1"))),
        InteractionRejection::RequestExpired, "the expired request stays expired");
    check(timedView.trace() == QLatin1String("present(choose_player),reject(request_expired),cancel(expired)"),
        "the view sees the expiry as a cancellation");

    // 冇 timeout 就冇死線。
    ClientCore untimed;
    untimed.setClock([]() { return Q_INT64_C(1) << 40; });
    untimed.beginRequest(playerRequest());
    check(untimed.activeRequest().deadlineMs == 0, "a request without a timeout has no deadline");
    check(!untimed.expireIfDue(), "a request without a deadline never expires");
}

// ── view 生命週期 ────────────────────────────────────────────────────────

void testViewLifecycle()
{
    ClientCore core;
    quint64 id = 0;
    {
        RecordingView view;
        view.attachTo(&core);
        id = core.beginRequest(playerRequest());
        check(view.events.size() == 1, "attaching a view before the request presents it once");
    }
    // view 喺 request 未答之前死咗：core 先係真相，request 仲喺度。
    check(core.view() == nullptr, "a destroyed view detaches itself");
    check(core.hasActiveRequest(), "the request survives the view");
    check(core.submitResponse(InteractionResponse::makePlayers(id,
            QStringList() << QStringLiteral("sgs1"))).accepted(),
        "an answer still lands with no view attached");

    // 後嚟先接上嘅 view 會即刻收到未答嘅 request，唔會對住空畫面等。
    ClientCore lateCore;
    lateCore.beginRequest(choiceRequest());
    RecordingView lateView;
    lateView.attachTo(&lateCore);
    check(lateView.trace() == QLatin1String("present(choice)"),
        "attaching a view mid-request replays the pending request");

    // 冇 view 嘅 core 一樣行得。
    ClientCore headless;
    const quint64 headlessId = headless.beginRequest(choiceRequest());
    check(headless.submitResponse(InteractionResponse::makeOption(headlessId,
            QStringLiteral("yes"))).accepted(),
        "a headless core completes requests");
}

// ── desktop adapter 契約 ─────────────────────────────────────────────────

void testViewContract()
{
    // Desktop adapter（DesktopInteractionView）靠呢啲保證：
    //   1. 每個 request 恰好 present 一次；
    //   2. 每個 request 恰好收檔一次，唔係 finish 就係 cancel，冇兩者皆有；
    //   3. reject 唔算收檔。
    ClientCore core;
    RecordingView view;
    view.attachTo(&core);

    const quint64 first = core.beginRequest(choiceRequest());
    core.submitResponse(InteractionResponse::makeOption(first, QStringLiteral("later")));
    core.submitResponse(InteractionResponse::makeOption(first, QStringLiteral("yes")));
    const quint64 second = core.beginRequest(playerRequest());
    core.cancelActiveRequest(InteractionCancelReason::Disconnected);

    int presented = 0;
    int settled = 0;
    foreach (const RecordingView::Event &event, view.events) {
        if (event.kind == QLatin1String("present"))
            ++presented;
        else if (event.kind != QLatin1String("reject"))
            ++settled;
    }
    check(presented == 2, "each request is presented exactly once");
    check(settled == 2, "each request is settled exactly once");
    check(view.events.first().requestId == first && view.events.last().requestId == second,
        "view callbacks carry the request they belong to");
    check(core.startedCount() == 2 && core.acceptedCount() == 1
            && core.rejectedCount() == 1 && core.cancelledCount() == 1,
        "the core counters match the observed traffic");

    const QJsonObject json = core.toJson();
    check(json.value(QStringLiteral("accepted")).toInt() == 1
            && json.value(QStringLiteral("has_view")).toBool()
            && !json.contains(QStringLiteral("active_request")),
        "the core diagnostic snapshot reports the settled state");
}

// ── 重入 ────────────────────────────────────────────────────────────────

// Desktop 嘅 presentRequest() 會 setStatus()，而 RoomScene::updateStatus() 有
// 幾條路會喺同一個 call stack 入面就答返呢個 request（例如 responding 狀態搵
// 唔到可用嘅 view-as skill，就即刻覆一個空答案）。呢個 view 就係模擬嗰種
// 「present 途中已經答咗」嘅行為。
class SelfAnsweringView : public IClientInteractionView
{
public:
    SelfAnsweringView(ClientCore *core)
        : m_core(core)
    {
        core->setView(this);
    }

    ~SelfAnsweringView() override { m_core->detachView(); }

    void presentRequest(const InteractionRequest &request) override
    {
        ++presented;
        presentedId = request.requestId;
        if (!m_answerOnce)
            return;
        m_answerOnce = false;
        accepted = m_core->submitResponse(
            InteractionResponse::makeOption(request.requestId, QStringLiteral("yes"))).accepted();
        // 答完之後仲用得返個 request：ClientCore 一定要傳 copy，唔可以傳
        // m_active 嘅 reference（嗰陣 m_active 已經清空）。
        stillReadable = request.requestId == presentedId
            && request.type != InteractionType::None;
    }

    void finishRequest(const InteractionRequest &, const InteractionResponse &) override
    {
        ++finished;
    }

    void cancelRequest(const InteractionRequest &, InteractionCancelReason) override {}

    void rejectResponse(const InteractionRequest &, const InteractionResponse &,
        const InteractionValidation &) override {}

    int presented = 0;
    int finished = 0;
    quint64 presentedId = 0;
    bool accepted = false;
    bool stillReadable = false;

private:
    ClientCore *m_core;
    bool m_answerOnce = true;
};

void testReentrantAnswer()
{
    ClientCore core;
    SelfAnsweringView view(&core);

    const quint64 id = core.beginRequest(choiceRequest());
    check(view.accepted, "a view can answer from inside presentRequest()");
    check(view.stillReadable, "the presented request stays readable after a reentrant answer");
    check(id != 0 && id == view.presentedId,
        "beginRequest() still reports the id it started, not the cleared one");
    check(!core.hasActiveRequest(), "the reentrant answer completed the request");
    check(view.presented == 1 && view.finished == 1,
        "a reentrant answer still presents and finishes exactly once");
    checkRejection(core.submitResponse(InteractionResponse::makeOption(id, QStringLiteral("no"))),
        InteractionRejection::AlreadyCompleted,
        "the exactly-once guard holds across a reentrant answer");
}

// ── 遊戲狀態 ────────────────────────────────────────────────────────────

void testGameState()
{
    ClientCore core;
    ClientGameState *state = core.state();
    state->setSelfName(QStringLiteral("sgs1"));
    state->setPlayerNames(QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2"));
    state->setPlayerAlive(QStringLiteral("sgs2"), false);
    state->setCardIdSpace(160);

    check(state->hasPlayer(QStringLiteral("sgs2")), "the roster tracks known players");
    check(!state->isPlayerAlive(QStringLiteral("sgs2")), "the roster tracks deaths");
    check(state->isPlayerAlive(QStringLiteral("sgs1")), "unknown liveness defaults to alive");
    check(state->isKnownCardId(159) && !state->isKnownCardId(160) && !state->isKnownCardId(-1),
        "the card id space bounds valid ids");

    state->addPlayer(QStringLiteral("sgs3"));
    state->removePlayer(QStringLiteral("sgs2"));
    check(state->playerNames() == (QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs3")),
        "players can join and leave");

    // 未知值域就唔攔：寧可唔驗，都唔可以攔錯一個合法回覆。
    ClientGameState unbounded;
    check(unbounded.isKnownCardId(9999), "an unknown card id space accepts any non-negative id");

    state->reset();
    check(state->playerNames().isEmpty() && state->selfName().isEmpty(),
        "reset clears the client game state");
}

class FakeEligibilityProvider : public ICardEligibilityProvider
{
public:
    CardEligibilityResult resolve(const InteractionRequest &) const override
    {
        CardEligibilityResult result;
        result.suggestedCards << 7 << 8;
        result.suggestedDisabledCards << 9;
        result.diagnostic = QStringLiteral("fake-provider");
        return result;
    }
};

void testStructuredModels()
{
    ClientCore roleCore;
    InteractionRequest role;
    role.type = InteractionType::ChooseRole;
    role.command = 91;
    role.serverSerial = 17;
    role.responseSchema = InteractionResponseShape::Assignment;
    role.payload = RoleAssignmentInteractionPayload {
        QStringLiteral("standard"),
        QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2"),
        QStringList() << QStringLiteral("lord") << QStringLiteral("rebel") };
    const quint64 roleId = roleCore.beginRequest(role);
    InteractionResponse wrongCommand = InteractionResponse::makeAssignment(roleId,
        QStringList() << QStringLiteral("sgs1"), QStringList() << QStringLiteral("lord"));
    wrongCommand.command = 92;
    checkRejection(roleCore.validate(wrongCommand), InteractionRejection::CommandMismatch,
        "a response for a different command is rejected");
    InteractionResponse wrongSerial = wrongCommand;
    wrongSerial.command = 91;
    wrongSerial.serverSerial = 18;
    checkRejection(roleCore.validate(wrongSerial), InteractionRejection::ServerSerialMismatch,
        "a response for a different server serial is rejected");
    InteractionResponse roleAnswer = InteractionResponse::makeAssignment(roleId,
        QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2"),
        QStringList() << QStringLiteral("lord") << QStringLiteral("rebel"));
    roleAnswer.command = 91;
    roleAnswer.serverSerial = 17;
    check(roleCore.submitResponse(roleAnswer).accepted(),
        "a command-correlated role assignment is accepted");

    ClientCore rearrangeCore;
    InteractionRequest rearrange;
    rearrange.type = InteractionType::SkillGuanxing;
    rearrange.responseSchema = InteractionResponseShape::Rearrangement;
    RearrangeCardsInteractionPayload rearrangePayload;
    rearrangePayload.cardIds = QList<int>() << 1 << 2 << 3;
    rearrangePayload.maxTop = 3;
    rearrangePayload.maxBottom = 3;
    rearrange.payload = rearrangePayload;
    const quint64 rearrangeId = rearrangeCore.beginRequest(rearrange);
    check(rearrangeCore.validate(InteractionResponse::makeRearrangement(rearrangeId,
            QList<int>() << 3 << 1, QList<int>() << 2)).accepted(),
        "a Guanxing rearrangement that preserves every card is accepted");
    checkRejection(rearrangeCore.validate(InteractionResponse::makeRearrangement(rearrangeId,
            QList<int>() << 1 << 1, QList<int>() << 2)),
        InteractionRejection::DuplicateCard,
        "a Guanxing rearrangement cannot duplicate cards");

    ClientCore yijiCore;
    InteractionRequest yiji;
    yiji.type = InteractionType::SkillYiji;
    yiji.responseSchema = InteractionResponseShape::Distribution;
    yiji.payload = YijiInteractionPayload {
        QList<int>() << 4 << 5, QStringList() << QStringLiteral("sgs2"), 1, 2, 2 };
    const quint64 yijiId = yijiCore.beginRequest(yiji);
    check(yijiCore.validate(InteractionResponse::makeDistribution(yijiId,
            QList<int>() << 4, QStringLiteral("sgs2"))).accepted(),
        "a typed Yiji distribution is accepted");
    checkRejection(yijiCore.validate(InteractionResponse::makeDistribution(yijiId,
            QList<int>() << 4, QStringLiteral("sgs9"))),
        InteractionRejection::UnknownPlayer,
        "a Yiji distribution cannot target an unlisted player");

    ClientCore customCore;
    InteractionRequest custom;
    custom.type = InteractionType::QmlInteract;
    custom.responseSchema = InteractionResponseShape::Custom;
    CustomInteractionPayload customPayload;
    customPayload.schemaVersion = 2;
    customPayload.typeName = QStringLiteral("extension.pick");
    custom.payload = customPayload;
    const quint64 customId = customCore.beginRequest(custom);
    checkRejection(customCore.validate(InteractionResponse::makeCustom(customId, 1,
            QStringLiteral("extension.pick"), QJsonObject())),
        InteractionRejection::MalformedResponse,
        "a custom response with the wrong schema version is rejected");
}

void testEligibilityHints()
{
    ClientCore core;
    core.state()->setCardIdSpace(100);
    FakeEligibilityProvider provider;
    core.setCardEligibilityProvider(&provider);

    InteractionRequest request;
    request.type = InteractionType::ResponseCard;
    request.responseSchema = InteractionResponseShape::Cards;
    CardInteractionPayload payload;
    payload.selection.pattern = QStringLiteral("jink");
    payload.selection.minSelection = 1;
    payload.selection.maxSelection = 1;
    request.payload = payload;
    const quint64 id = core.beginRequest(request);
    const CardInteractionPayload *active
        = core.activeRequest().payloadAs<CardInteractionPayload>();
    check(active != nullptr && active->suggestedCards == (QList<int>() << 7 << 8),
        "an injected eligibility provider enriches presentation hints");
    check(active != nullptr && !active->selection.enumerated,
        "provider hints never become authoritative membership constraints");
    check(core.validate(InteractionResponse::makeCards(id, QList<int>() << 10)).accepted(),
        "a card outside provider hints remains valid for a pattern request");
}

void testSpecialInteractionSemantics()
{
    ClientCore guanxingCore;
    RearrangeCardsInteractionPayload upOnly;
    upOnly.cardIds = QList<int>() << 1 << 2;
    upOnly.mode = RearrangementMode::UpOnly;
    upOnly.minTop = upOnly.maxTop = 2;
    InteractionRequest upRequest;
    upRequest.type = InteractionType::SkillGuanxing;
    upRequest.responseSchema = InteractionResponseShape::Rearrangement;
    upRequest.payload = upOnly;
    const quint64 upId = guanxingCore.beginRequest(upRequest);
    check(guanxingCore.validate(InteractionResponse::makeRearrangement(upId,
            QList<int>() << 2 << 1, QList<int>())).accepted(),
        "Guanxing up-only accepts every card on top");
    checkRejection(guanxingCore.validate(InteractionResponse::makeRearrangement(upId,
            QList<int>() << 1, QList<int>() << 2)),
        InteractionRejection::SelectionCountOutOfRange,
        "Guanxing up-only rejects bottom cards");
    checkRejection(guanxingCore.validate(InteractionResponse::makeRearrangement(upId,
            QList<int>() << 1, QList<int>())),
        InteractionRejection::UnknownCard,
        "Guanxing rejects a missing card");

    ClientCore downCore;
    RearrangeCardsInteractionPayload downOnly = upOnly;
    downOnly.mode = RearrangementMode::DownOnly;
    downOnly.minTop = downOnly.maxTop = 0;
    downOnly.minBottom = downOnly.maxBottom = 2;
    InteractionRequest downRequest = upRequest;
    downRequest.payload = downOnly;
    const quint64 downId = downCore.beginRequest(downRequest);
    check(downCore.validate(InteractionResponse::makeRearrangement(downId,
            QList<int>(), QList<int>() << 1 << 2)).accepted(),
        "Guanxing down-only accepts every card on bottom");

    ClientCore bothCore;
    RearrangeCardsInteractionPayload both = upOnly;
    both.mode = RearrangementMode::BothSides;
    both.minTop = both.minBottom = 0;
    both.maxTop = both.maxBottom = 2;
    InteractionRequest bothRequest = upRequest;
    bothRequest.payload = both;
    const quint64 bothId = bothCore.beginRequest(bothRequest);
    check(bothCore.validate(InteractionResponse::makeRearrangement(bothId,
            QList<int>() << 1, QList<int>() << 2)).accepted(),
        "Guanxing both-sides accepts a split arrangement");

    ClientCore yijiCore;
    InteractionRequest yiji;
    yiji.type = InteractionType::SkillYiji;
    yiji.responseSchema = InteractionResponseShape::Distribution;
    yiji.payload = YijiInteractionPayload { QList<int>() << 4 << 5,
        QStringList() << QStringLiteral("sgs2"), 1, 2, 1 };
    const quint64 yijiId = yijiCore.beginRequest(yiji);
    checkRejection(yijiCore.validate(InteractionResponse::makeDistribution(yijiId,
            QList<int>() << 4 << 5, QStringLiteral("sgs2"))),
        InteractionRejection::SelectionCountOutOfRange,
        "Yiji cannot distribute more cards than remain");
    checkRejection(yijiCore.validate(InteractionResponse::makeDistribution(yijiId,
            QList<int>() << 9, QStringLiteral("sgs2"))),
        InteractionRejection::UnknownCard,
        "Yiji rejects a card outside the offered set");

    ClientCore gongxinCore;
    gongxinCore.state()->setCardIdSpace(20);
    InteractionRequest gongxin;
    gongxin.type = InteractionType::SkillGongxin;
    gongxin.responseSchema = InteractionResponseShape::Cards;
    gongxin.cancelable = true;
    gongxin.payload = GongxinInteractionPayload { QStringLiteral("sgs2"),
        QList<int>() << 5 << 6, QList<int>() << 5, false };
    const quint64 gongxinId = gongxinCore.beginRequest(gongxin);
    check(gongxinCore.validate(InteractionResponse::makeCards(gongxinId,
            QList<int>() << 5)).accepted(),
        "Gongxin accepts an explicitly selectable visible card");
    checkRejection(gongxinCore.validate(InteractionResponse::makeCards(gongxinId,
            QList<int>() << 6)), InteractionRejection::UnknownCard,
        "Gongxin does not treat every visible card as selectable");

    ClientCore pindianCore;
    pindianCore.state()->setCardIdSpace(20);
    PindianInteractionPayload pindianPayload;
    pindianPayload.opponent = QStringLiteral("sgs2");
    pindianPayload.selection.minSelection = 1;
    pindianPayload.selection.maxSelection = 1;
    pindianPayload.hiddenUntilResolved = true;
    InteractionRequest pindian;
    pindian.type = InteractionType::Pindian;
    pindian.responseSchema = InteractionResponseShape::Cards;
    pindian.payload = pindianPayload;
    const quint64 pindianId = pindianCore.beginRequest(pindian);
    check(pindianCore.validate(InteractionResponse::makeCards(pindianId,
            QList<int>() << 7)).accepted(),
        "Pindian accepts exactly one known card");
    checkRejection(pindianCore.validate(InteractionResponse::makeCards(pindianId,
            QList<int>() << 7 << 8)), InteractionRejection::SelectionCountOutOfRange,
        "Pindian rejects multiple cards");

    ClientCore agCore;
    agCore.state()->setCardIdSpace(20);
    AmazingGraceInteractionPayload agPayload;
    agPayload.selection.enumerated = true;
    agPayload.selection.selectableCards = QList<int>() << 7 << 8 << 9;
    agPayload.selection.disabledCards = QList<int>() << 8 << 9;
    agPayload.selection.minSelection = 1;
    agPayload.selection.maxSelection = 1;
    agPayload.takenCards = QList<int>() << 9;
    InteractionRequest ag;
    ag.type = InteractionType::AmazingGrace;
    ag.responseSchema = InteractionResponseShape::Cards;
    ag.payload = agPayload;
    const quint64 agId = agCore.beginRequest(ag);
    check(agCore.validate(InteractionResponse::makeCards(agId,
            QList<int>() << 7)).accepted(),
        "Amazing Grace accepts an available card");
    checkRejection(agCore.validate(InteractionResponse::makeCards(agId,
            QList<int>() << 8)), InteractionRejection::DisabledCard,
        "Amazing Grace rejects a disabled card");
    checkRejection(agCore.validate(InteractionResponse::makeCards(agId,
            QList<int>() << 9)), InteractionRejection::DisabledCard,
        "Amazing Grace rejects a taken card");

    ClientCore arrangeCore;
    ArrangeGeneralsInteractionPayload arrangePayload;
    arrangePayload.generalNames = QStringList() << QStringLiteral("caocao")
        << QStringLiteral("liubei") << QStringLiteral("sunquan");
    arrangePayload.slotCount = 3;
    InteractionRequest arrange;
    arrange.type = InteractionType::ArrangeGeneral;
    arrange.responseSchema = InteractionResponseShape::GeneralArrangement;
    arrange.payload = arrangePayload;
    const quint64 arrangeId = arrangeCore.beginRequest(arrange);
    check(arrangeCore.validate(InteractionResponse::makeGeneralArrangement(arrangeId,
            QStringList() << QStringLiteral("sunquan") << QStringLiteral("caocao")
                          << QStringLiteral("liubei"))).accepted(),
        "general arrangement accepts a complete permutation");
    checkRejection(arrangeCore.validate(InteractionResponse::makeGeneralArrangement(arrangeId,
            QStringList() << QStringLiteral("caocao") << QStringLiteral("caocao")
                          << QStringLiteral("liubei"))), InteractionRejection::DuplicateGeneral,
        "general arrangement rejects duplicate generals");
    checkRejection(arrangeCore.validate(InteractionResponse::makeGeneralArrangement(arrangeId,
            QStringList() << QStringLiteral("caocao") << QStringLiteral("liubei")
                          << QStringLiteral("zhouyu"))), InteractionRejection::UnknownGeneral,
        "general arrangement rejects unknown generals");

    ClientCore role3v3Core;
    OptionInteractionPayload role3v3Payload;
    role3v3Payload.scheme = QStringLiteral("2013");
    role3v3Payload.options << InteractionOption(QStringLiteral("leader"))
                          << InteractionOption(QStringLiteral("guard"));
    InteractionRequest role3v3;
    role3v3.type = InteractionType::ChooseRole3v3;
    role3v3.responseSchema = InteractionResponseShape::Option;
    role3v3.payload = role3v3Payload;
    const quint64 role3v3Id = role3v3Core.beginRequest(role3v3);
    check(role3v3Core.validate(InteractionResponse::makeOption(role3v3Id,
            QStringLiteral("leader"))).accepted(),
        "3v3 role choice uses the canonical option model");

    ClientCore triggerCore;
    TriggerOrderOption triggerOption;
    triggerOption.responseValue = QStringLiteral("jianxiong:sgs1:sgs1");
    TriggerOrderInteractionPayload triggerPayload;
    triggerPayload.options << triggerOption;
    InteractionRequest trigger;
    trigger.type = InteractionType::TriggerOrder;
    trigger.responseSchema = InteractionResponseShape::Option;
    trigger.payload = triggerPayload;
    const quint64 triggerId = triggerCore.beginRequest(trigger);
    check(triggerCore.validate(InteractionResponse::makeOption(triggerId,
            triggerOption.responseValue)).accepted(),
        "trigger order validates its typed response value");
}

void testMetadataAllowlist()
{
    ClientCore core;
    InteractionRequest request = choiceRequest();
    request.metadata.insert(QStringLiteral("eligibility_diagnostic"),
        QStringLiteral("fixture"));
    request.metadata.insert(QStringLiteral("gameplay_constraint"), 42);
    core.beginRequest(request);
    check(core.activeRequest().metadata.size() == 1
            && core.activeRequest().metadata.value(
                QStringLiteral("eligibility_diagnostic")).toString()
                == QLatin1String("fixture"),
        "request metadata keeps only diagnostic allowlist keys");
}

void testProductionDeadlineTimer()
{
    ClientCore core;
    InteractionRequest request = choiceRequest();
    request.timeoutMs = 5;

    QEventLoop loop;
    bool expired = false;
    QObject::connect(&core, &ClientCore::requestCancelled, &loop,
        [&expired, &loop](quint64, int reason) {
            expired = reason == static_cast<int>(InteractionCancelReason::Expired);
            loop.quit();
        });
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    core.beginRequest(request);
    loop.exec();
    check(expired && !core.hasActiveRequest(),
        "the production QTimer expires a pending request without polling");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    testRequestSnapshot();
    testRequestIdentity();
    testOptionValidation();
    testPlayerValidation();
    testCardValidation();
    testExactlyOnceCompletion();
    testCancelAndTimeout();
    testViewLifecycle();
    testViewContract();
    testReentrantAnswer();
    testGameState();
    testStructuredModels();
    testEligibilityHints();
    testSpecialInteractionSemantics();
    testMetadataAllowlist();
    testProductionDeadlineTimer();

    if (failures > 0) {
        printf("%d ClientCore contract check(s) failed\n", failures);
        return 1;
    }
    printf("ClientCore contract OK\n");
    return 0;
}
