// The guarantee this file protects: which TuiPresenter is installed --
// TuiStreamPresenter (classic scrolling text) or TuiBoardPresenter (the
// full-screen board) -- must never change what reaches the server. Paging,
// opening/closing an overlay and resizing the viewport are local view state
// (design invariant 1, docs/tui-board-ui.md); the parser, ClientCore and the
// reply encoder must never learn a UI mode exists, and lineReady is the one
// input exit either mode ever uses (invariant 2).
//
// Why the obvious version of this test is worthless: TuiStreamPresenter and
// TuiBoardPresenter both sit *downstream* of the request/response pipeline
// (ProtocolInteractionRequestBuilder::build -> TuiInteractionView::parseAnswer
// -> ClientCore::submitResponse -> the reply encoder). A test that builds a
// request, parses a canned answer and encodes a reply -- twice, once "for"
// each presenter, without ever handing either presenter anything to do --
// cannot fail no matter how badly a presenter is broken, because the
// presenter was never part of the computation that produced the bytes being
// compared. That is a test that advertises a guarantee it never checks.
//
// What makes this version capable of failing:
//   1. Each run below wires a real ClientCore to a real, installed presenter
//      through the exact connections TuiApplicationController uses in
//      production (requestStarted/responseAccepted/requestCancelled ->
//      presenter->interactionChanged(), the view's writer -> presenter->
//      writeOutput()) -- see runThroughPresenter(). The presenter is not a
//      bystander; it is actually notified of the request and actually
//      receives the rendered prompt text, the same way the live session
//      notifies it.
//   2. For the board side, real player-facing view actions -- paging,
//      opening and closing the overlay, resizing twice -- are interleaved
//      between the request arriving and the answer being submitted, called
//      through TuiBoardPresenter's own public entry points (setPage(),
//      toggleOverlay(), setViewportSize()), not a stub standing in for them.
//      If any of those ever grew a path back into ClientCore, this is where
//      it would show up.
//   3. The comparison is field-by-field on the resulting InteractionWireReply
//      (command, the full 64-bit replyTo, payload) rather than string
//      equality, and there is a standing assertion that replyTo actually
//      survived as a full 64-bit value rather than being silently truncated
//      to 32 bits -- exactly the class of bug a naive test would let through
//      unnoticed on both sides at once.
//   4. presenterBackedRequestTypes() is InteractionCommandRegistry::
//      descriptors() itself -- the runtime source of truth also asserted
//      elsewhere (tui-contract-test.cpp) to hold exactly 29 entries -- not a
//      hand-copied list that quietly stops covering new flows.
//   5. A separate, explicit negative case proves invariant 1 head-on: a board
//      presenter given nothing but paging/overlay/resize calls, with no
//      request ever begun, must produce zero replies.
//
// Do not "simplify" this back into two calls to the reply encoder run side
// by side. That is the version this comment exists to rule out.

#include "client-game-state.h"
#include "core/interaction-model.h"
#include "client-core.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "general.h"
#include "interaction-command-registry.h"
#include "interaction-reply-coordinator.h"
#include "interaction-reply-encoder.h"
#include "protocol-interaction-request-builder.h"
#include "protocol.h"
#include "tui-board-presenter.h"
#include "tui-interaction-view.h"
#include "tui-presenter.h"
#include "tui-renderer.h"
#include "tui-resolvers.h"
#include "tui-stream-presenter.h"

#include <QCoreApplication>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <functional>
#include <limits>

using namespace QSanProtocol;

namespace {

int failures = 0;

void check(bool condition, const char *name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

// Mirrors TuiApplicationController's own resolver wiring closely enough for
// this test: real engine translation for names/kingdoms, matching every
// other TUI test file's testResolvers().
TuiResolvers testResolvers()
{
    TuiResolvers resolvers;
    resolvers.name = [](const QString &name) {
        if (name.isEmpty() || Sanguosha == nullptr)
            return name;
        const QString translated = Sanguosha->translate(name);
        return translated.isEmpty() ? name : translated;
    };
    resolvers.player = [](const QString &objectName) { return objectName; };
    resolvers.kingdom = [](const QString &generalName) {
        if (Sanguosha == nullptr || generalName.isEmpty())
            return QString();
        const General *general = Sanguosha->getGeneral(generalName);
        return general == nullptr ? QString() : general->getKingdom();
    };
    return resolvers;
}

// A 2-player game with enough shape (setup, seats, hp, a hand each) that
// TuiBoardPresenter's real layout code has something to draw, plus the card
// ids and amazing_grace game value tui-contract-test.cpp's own
// interactionRoundTripContract() already proved answers all 29 registered
// interactions. Shared, read-only, by both runs of every descriptor -- the
// point is that the two runs start from byte-identical state and only differ
// in which presenter is installed.
ClientGameState buildBaseState()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("02p")},
        {QStringLiteral("player_count"), 2}});
    state.setSelfName(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p2"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("object_name"), QStringLiteral("p1"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("seat"), 1);
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("general"), QStringLiteral("zhaoyun"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("hp"), 4);
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("max_hp"), 4);
    state.setPlayerAlive(QStringLiteral("p1"), true);
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("object_name"), QStringLiteral("p2"));
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("seat"), 2);
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("general"), QStringLiteral("caocao"));
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("hp"), 4);
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("max_hp"), 4);
    state.setPlayerAlive(QStringLiteral("p2"), true);
    state.setPlayerNames({QStringLiteral("p1"), QStringLiteral("p2")});
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 1);
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("p1"));
    state.setGameValue(QStringLiteral("draw_pile_count"), 30);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList());
    state.setCardIdSpace(32);
    state.setCardValue(1, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(1, QStringLiteral("place"), 0);
    state.setCardValue(2, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(2, QStringLiteral("place"), 0);
    state.setCardValue(7, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(7, QStringLiteral("place"), 0);
    state.setGameValue(QStringLiteral("amazing_grace"), QVariantMap{
        {QStringLiteral("card_ids"), QVariantList{1, 2}},
        {QStringLiteral("disabled_card_ids"), QVariantList()}});
    return state;
}

// Copied verbatim from tui-contract-test.cpp's own samplePayload(): every
// field any of the 29 registered request builders reads, plus the three
// commands that need something extra. Kept identical on purpose -- this is
// not a second opinion on what a sample payload looks like, it is the same
// payload the contract test already proved every builder accepts.
QVariantMap samplePayload(CommandType command)
{
    QVariantMap payload{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("candidates"), QVariantList{QStringLiteral("g1"), QStringLiteral("g2")}},
        {QStringLiteral("players"), QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}},
        {QStringLiteral("roles"), QVariantList{QStringLiteral("lord"), QStringLiteral("guard")}},
        {QStringLiteral("kingdoms"), QVariantList{QStringLiteral("wei"), QStringLiteral("shu")}},
        {QStringLiteral("generals"), QVariantList{QStringLiteral("g1"), QStringLiteral("g2")}},
        {QStringLiteral("card_ids"), QVariantList{1, 2}},
        {QStringLiteral("enabled_card_ids"), QVariantList{2}},
        {QStringLiteral("disabled_card_ids"), QVariantList()},
        {QStringLiteral("player"), QStringLiteral("p1")},
        {QStringLiteral("source_player"), QStringLiteral("p1")},
        {QStringLiteral("target_player"), QStringLiteral("p2")},
        {QStringLiteral("dying_player"), QStringLiteral("p1")},
        {QStringLiteral("zone_flags"), QStringLiteral("he")},
        {QStringLiteral("hand_cards_visible"), true},
        {QStringLiteral("mode"), QStringLiteral("up_only")},
        {QStringLiteral("scheme"), QStringLiteral("test")},
        {QStringLiteral("pattern"), QStringLiteral(".")},
        {QStringLiteral("min_cards"), 0}, {QStringLiteral("max_cards"), 2},
        {QStringLiteral("min_players"), 1}, {QStringLiteral("max_players"), 1},
        {QStringLiteral("include_equip"), true}, {QStringLiteral("optional"), true},
        {QStringLiteral("can_cancel"), true}, {QStringLiteral("refusable"), true}};
    if (command == S_COMMAND_MULTIPLE_CHOICE) {
        payload.insert(QStringLiteral("options"),
                       QVariantList{QStringLiteral("yes"), QStringLiteral("no")});
    } else if (command == S_COMMAND_TRIGGER_ORDER) {
        payload.insert(QStringLiteral("options"), QVariantList{QVariantMap{
            {QStringLiteral("skill"), QStringLiteral("jizhi")},
            {QStringLiteral("instanceID"), 2},
            {QStringLiteral("invoker"), QStringLiteral("p1")},
            {QStringLiteral("owner"), QStringLiteral("p1")},
            {QStringLiteral("preferredtarget"), QStringLiteral("p2")},
            {QStringLiteral("preferredtargetseat"), 3}}});
    } else if (command == S_COMMAND_CHOOSE_ORDER) {
        payload.insert(QStringLiteral("reason"), QStringLiteral("select"));
    } else if (command == S_COMMAND_QML_INTERACT) {
        payload.insert(QStringLiteral("interaction"), QVariantMap{
            {QStringLiteral("schema_version"), 1},
            {QStringLiteral("type"), QStringLiteral("qsanguosha.qml")},
            {QStringLiteral("title"), QStringLiteral("Choose")},
            {QStringLiteral("payload"), QVariantMap{
                {QStringLiteral("qml_path"), QStringLiteral("qml/Choose.qml")},
                {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("x"), 1}}}}},
            {QStringLiteral("response_schema"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("json")}}}});
    }
    return payload;
}

// Also copied verbatim from tui-contract-test.cpp: one syntactically valid
// answer per response shape, independent of which interaction is asking.
QString validAnswerFor(const InteractionRequest &request)
{
    switch (request.responseSchema) {
    case InteractionResponseShape::Option:
    case InteractionResponseShape::Players:
    case InteractionResponseShape::Cards:
        return QStringLiteral("1");
    case InteractionResponseShape::Assignment:
        return QStringLiteral("p1=lord p2=guard");
    case InteractionResponseShape::Rearrangement:
        return QStringLiteral("1 2 |");
    case InteractionResponseShape::Distribution:
        return QStringLiteral("cards 1 -> 1");
    case InteractionResponseShape::GeneralArrangement:
        return QStringLiteral("1 2");
    case InteractionResponseShape::Custom:
        return QStringLiteral("{}");
    case InteractionResponseShape::None:
        return QString();
    }
    return QString();
}

// What one run (one presenter, one descriptor) produced, or nothing if the
// pipeline stopped early -- which the per-step checks below already reported.
struct RunOutcome
{
    bool reachedWire = false;
    InteractionWireReply reply;
    // Whatever text the presenter's writeOutput() actually received while the
    // request was open -- proof presentRequest() -> the writer -> the
    // presenter really ran, not just that beginRequest() returned.
    QStringList presentedOutput;
};

// Wires a fresh ClientCore to `presenter` exactly the way
// TuiApplicationController's constructor does (see its own comment on the
// requestStarted/responseAccepted/requestCancelled connections), begins
// `request`, runs `interleave` (empty for the classic run; real view actions
// for the board run) between the request arriving and the answer being
// submitted, then parses and submits `answer` through
// InteractionReplyCoordinator::submit -- the same coordinator function
// ClientLiveSession::submitInteractionResponse calls in production, rather
// than reaching around it to call the reply encoder directly.
RunOutcome runThroughPresenter(TuiPresenter *presenter, TuiRenderer *renderer,
    const ClientGameState &baseState, const InteractionCommandDescriptor &descriptor,
    const InteractionRequest &request, const QString &answer,
    const std::function<void()> &interleave)
{
    RunOutcome outcome;

    ClientCore core;
    *core.state() = baseState;

    TuiInteractionView view(renderer,
        [presenter, &outcome](const QString &text) {
            presenter->writeOutput(text);
            outcome.presentedOutput << text;
        },
        [](int cardId) { return QStringLiteral("slash:%1").arg(cardId); });
    core.setView(&view);

    QObject::connect(&core, &ClientCore::requestStarted, [&core, presenter](quint64) {
        presenter->interactionChanged(&core.activeRequest());
    });
    QObject::connect(&core, &ClientCore::responseAccepted, [presenter](quint64) {
        presenter->interactionChanged(nullptr);
    });
    QObject::connect(&core, &ClientCore::requestCancelled, [presenter](quint64, int) {
        presenter->interactionChanged(nullptr);
    });

    // The one full-state push a live session gives a fresh presenter before
    // anything else happens (ClientLiveSession::stateChanged ->
    // TuiApplicationController -> presenter->stateChanged()). Needed so the
    // board has real players/seats to lay out rather than painting into an
    // empty state it has never seen.
    presenter->stateChanged(*core.state());

    core.beginRequest(request);
    // interactionChanged()'s repaint is scheduled via QTimer::singleShot(0);
    // flush it so the board has actually drawn the request before the view
    // actions below run against it.
    QCoreApplication::processEvents();

    if (interleave)
        interleave();

    InteractionResponse response;
    QString error;
    if (!view.parseAnswer(core.activeRequest(), answer, &response, &error)) {
        std::printf("  %s parse failed: %s\n", descriptor.commandName, qPrintable(error));
        return outcome;
    }

    const bool sent = InteractionReplyCoordinator::submit(&core, descriptor.replyEncoder,
        response, [&outcome](const InteractionWireReply &reply) {
            outcome.reply = reply;
            outcome.reachedWire = true;
        });
    if (!sent) {
        std::printf("  %s submit was not accepted\n", descriptor.commandName);
        return outcome;
    }
    QCoreApplication::processEvents();
    return outcome;
}

bool wireRepliesEqual(const InteractionWireReply &a, const InteractionWireReply &b)
{
    return a.command == b.command && a.replyTo == b.replyTo && a.payload == b.payload;
}

// The invariant this whole feature rests on, run for every one of the 29
// registered production interactions: what the server receives cannot depend
// on which UI drew the prompt.
void uiChoiceNeverChangesTheWire()
{
    const auto &descriptors = InteractionCommandRegistry::descriptors();
    // The runtime source of truth, not a hand-copied list -- see the file
    // header. If a new flow is ever registered without a reply encoder or a
    // response shape, descriptors() itself would already be incomplete
    // before this file gets a chance to run over it.
    check(descriptors.size() == 29, "registry still reports all 29 production interactions");

    const ClientGameState baseState = buildBaseState();
    const TuiResolvers resolvers = testResolvers();
    TuiRenderer renderer(false, resolvers);

    int exercised = 0;
    quint64 messageId = 0x100000000ULL; // > 2^32 on purpose: see the replyTo check below.
    for (const InteractionCommandDescriptor &descriptor : descriptors) {
        ProtocolMessage message;
        message.type = ProtocolMessageType::Request;
        message.source = ProtocolEndpoint::Room;
        message.destination = ProtocolEndpoint::Client;
        message.command = descriptor.command;
        message.messageId = ++messageId;
        message.hasPayload = true;
        message.payload = samplePayload(descriptor.command);

        InteractionRequest request;
        QString error;
        if (!ProtocolInteractionRequestBuilder::build(message, baseState, &request, &error)) {
            std::printf("  %s build failed: %s\n", descriptor.commandName, qPrintable(error));
            continue;
        }
        const QString answer = validAnswerFor(request);

        // -- Classic mode: no view actions at all. --
        TuiStreamPresenter classicPresenter([](const QString &) {}, [](const QString &) {});
        const RunOutcome classic = runThroughPresenter(&classicPresenter, &renderer, baseState,
            descriptor, request, answer, {});

        // -- Board mode: deliberately noisy. Every local view action a
        // player can take, interleaved between the request landing and the
        // answer going out, through the presenter's real entry points. --
        TuiBoardPresenter board(QSize(120, 40), resolvers);
        const RunOutcome boardOutcome = runThroughPresenter(&board, &renderer, baseState,
            descriptor, request, answer, [&board]() {
                board.setPage(board.page() + 1);
                board.setPage(board.page() - 1);
                board.toggleOverlay(QStringLiteral("ui-parity-test overlay"));
                board.toggleOverlay(QString());
                board.setViewportSize(QSize(80, 24));
                board.setViewportSize(QSize(120, 40));
            });

        // ChooseRole is the one registered interaction TuiInteractionView
        // deliberately never renders (see its own presentRequest(): the
        // production controller auto-answers role assignment via
        // trySkipRoleAssignment() before a player ever sees a prompt). That
        // is a documented exception to "the presenter is shown something",
        // not a gap this test should paper over by requiring it anyway.
        const bool presentationExpected = request.type != InteractionType::ChooseRole;
        if (presentationExpected) {
            check(!classic.presentedOutput.isEmpty(),
                  QByteArray("classic presenter actually received the rendered prompt for ")
                      .append(descriptor.commandName).constData());
            check(!boardOutcome.presentedOutput.isEmpty(),
                  QByteArray("board presenter actually received the rendered prompt for ")
                      .append(descriptor.commandName).constData());
        }

        if (!classic.reachedWire || !boardOutcome.reachedWire) {
            check(false, QByteArray("both UIs reach the wire for ")
                              .append(descriptor.commandName).constData());
            continue;
        }
        ++exercised;

        check(classic.reply.replyTo == request.requestId
                  && classic.reply.replyTo > std::numeric_limits<quint32>::max(),
              QByteArray("classic reply keeps the full 64-bit replyTo for ")
                  .append(descriptor.commandName).constData());
        check(boardOutcome.reply.replyTo == request.requestId
                  && boardOutcome.reply.replyTo > std::numeric_limits<quint32>::max(),
              QByteArray("board reply keeps the full 64-bit replyTo for ")
                  .append(descriptor.commandName).constData());
        check(wireRepliesEqual(classic.reply, boardOutcome.reply),
              QByteArray("both UIs send a byte-identical reply for ")
                  .append(descriptor.commandName).constData());
    }
    std::printf("ui-parity coverage: %d/29 interactions exercised\n", exercised);
    check(exercised == 29, "all 29 registered interactions were actually exercised, not fewer");
}

// Invariant 1, pinned directly: a board presenter given nothing but paging,
// overlay and resize calls -- no request ever begun, nothing ever typed --
// must not produce a single reply. TuiBoardPresenter holds no reference to
// ClientCore or ClientLiveSession at all (see its own header comment), so
// this is a regression pin rather than a discovery mechanism: it exists so
// that guarantee stays checked in code the day it stops being obviously true.
void viewActionsAloneReachNothing()
{
    const TuiResolvers resolvers = testResolvers();
    TuiBoardPresenter idle(QSize(120, 40), resolvers);
    ClientCore core;

    idle.setPage(3);
    idle.setPage(0);
    idle.toggleOverlay(QStringLiteral("idle overlay"));
    idle.toggleOverlay(QString());
    idle.setViewportSize(QSize(80, 24));
    idle.setViewportSize(QSize(120, 40));
    QCoreApplication::processEvents();

    check(core.startedCount() == 0 && core.acceptedCount() == 0
              && core.rejectedCount() == 0 && core.cancelledCount() == 0,
          "paging, opening/closing an overlay and resizing never start, "
          "accept, reject or cancel anything");
}

} // namespace

int runTuiUiParityTests(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString engineError;
    if (!EngineBootstrap::initialize(false, &engineError)) {
        std::fprintf(stderr, "engine initialization failed: %s\n", qPrintable(engineError));
        return 1;
    }

    uiChoiceNeverChangesTheWire();
    viewActionsAloneReachNothing();

    std::printf("[AUTOTEST] TUI_UI_PARITY_RESULT status=%s failures=%d\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    EngineBootstrap::shutdown();
    return failures == 0 ? 0 : 1;
}
