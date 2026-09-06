#include "client-game-state.h"
#include "interaction-model.h"
#include "tui-board-presenter.h"
#include "tui-line-editor.h"

#include "engine-bootstrap.h"
#include "engine.h"
#include "general.h"

#include <QCoreApplication>
#include <QSize>
#include <QString>
#include <QVariantList>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

// Mirrors TuiApplicationController's production wiring closely enough for a
// presenter test: real engine translation for names/kingdoms, matching
// tui-board-view-test.cpp's own testResolvers().
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

void addPlayer(ClientGameState *state, const QString &name, int seat, const QString &general,
              int hp, int maxHp)
{
    state->addPlayer(name);
    state->setPlayerValue(name, QStringLiteral("object_name"), name);
    state->setPlayerValue(name, QStringLiteral("seat"), seat);
    state->setPlayerValue(name, QStringLiteral("general"), general);
    state->setPlayerValue(name, QStringLiteral("hp"), hp);
    state->setPlayerValue(name, QStringLiteral("max_hp"), maxHp);
    state->setPlayerAlive(name, true);
}

// A 5-player game that fits on a single 80x24 page (same shape as
// tui-board-view-test.cpp's testBoard05pPlay, trimmed to what this file
// needs): self is sgs1, sgs2 is 曹操 (caocao).
ClientGameState fivePlayerState(const QString &currentPlayer)
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("05p")},
        {QStringLiteral("player_count"), 5}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("zhaoyun"), 4, 4);
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("caocao"), 4, 4);
    addPlayer(&state, QStringLiteral("sgs3"), 3, QStringLiteral("zhangfei"), 4, 4);
    addPlayer(&state, QStringLiteral("sgs4"), 4, QStringLiteral("diaochan"), 3, 3);
    addPlayer(&state, QStringLiteral("sgs5"), 5, QStringLiteral("sunquan"), 4, 4);
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2"), QStringLiteral("sgs3"),
                          QStringLiteral("sgs4"), QStringLiteral("sgs5")});
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 1);
    state.setGameValue(QStringLiteral("current_player"), currentPlayer);
    state.setGameValue(QStringLiteral("draw_pile_count"), 30);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList{});
    return state;
}

// A 9-player game at 80x24, which tui-board-view-test.cpp's own
// testBoard09pPage2() already established splits into two pages with sgs9
// (seatOffset 8) landing on page 1 -- reused here so this file's multi-page
// auto-follow check rests on already-verified geometry instead of a fresh
// guess about where the split falls.
ClientGameState ninePlayerState(const QString &currentPlayer)
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("09p")},
        {QStringLiteral("player_count"), 9}});
    state.setSelfName(QStringLiteral("sgs1"));
    const QStringList generals{QStringLiteral("zhaoyun"), QStringLiteral("caocao"),
        QStringLiteral("zhangfei"), QStringLiteral("diaochan"), QStringLiteral("sunquan"),
        QStringLiteral("guanyu"), QStringLiteral("zhouyu"), QStringLiteral("simayi"),
        QStringLiteral("lvbu")};
    QStringList names;
    for (int i = 1; i <= 9; ++i) {
        const QString name = QStringLiteral("sgs%1").arg(i);
        names << name;
        addPlayer(&state, name, i, generals.at(i - 1), 4, 4);
    }
    state.setPlayerNames(names);
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 1);
    state.setGameValue(QStringLiteral("current_player"), currentPlayer);
    state.setGameValue(QStringLiteral("draw_pile_count"), 60);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList{});
    return state;
}

InteractionRequest choosePlayerRequest(const QStringList &candidates)
{
    InteractionRequest request;
    request.type = InteractionType::ChoosePlayer;
    request.prompt = QStringLiteral("请选择目标");
    PlayerInteractionPayload payload;
    payload.selection.selectablePlayers = candidates;
    payload.selection.minSelection = 1;
    payload.selection.maxSelection = 1;
    request.payload = payload;
    return request;
}

// Pumps the event loop once so a schedulePaint()'d QTimer::singleShot(0, ...)
// fires. stateChanged()/interactionChanged() defer their repaint on purpose
// (coalescing several notifications from one event-loop turn into a single
// repaint), so a test that wants to observe the effect has to give that timer
// a turn to run -- production always does, just by continuing to spin its own
// event loop.
void pumpEvents()
{
    QCoreApplication::processEvents();
}

void testAutoFollowAndOverlay()
{
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());

    // 1. Auto-follow: the player whose turn it is is always on screen, even
    // after a manual flip parked the view somewhere else first.
    presenter.setPage(1);
    ClientGameState turnState = fivePlayerState(QStringLiteral("sgs2"));
    presenter.stateChanged(turnState);
    pumpEvents();
    check(presenter.screenText().contains(QString::fromUtf8("曹操")),
          "a turn change pages to whoever is acting");

    // 2. A manual flip holds, but only until the next turn or request.
    presenter.setPage(2);
    check(presenter.page() == 2, "a manual flip is respected");
    const InteractionRequest request =
        choosePlayerRequest({QStringLiteral("sgs3"), QStringLiteral("sgs4")});
    presenter.interactionChanged(&request);
    pumpEvents();
    const QString firstCandidateName = QString::fromUtf8("张飞"); // zhangfei == sgs3
    check(presenter.page() != 2 || presenter.screenText().contains(firstCandidateName),
          "a new request pages to its first candidate");
    presenter.interactionChanged(nullptr);
    pumpEvents();

    // 3. Long dumps do not scroll the board away.
    const QString longPlayersDump = QStringLiteral(
        "座位=1 曹操\n座位=2 张飞\n座位=3 貂蝉\n座位=4 孙权");
    presenter.writeOutput(longPlayersDump);
    check(presenter.screenText().contains(QString::fromUtf8("座位=")),
          "a long dump opens as an overlay");
    presenter.toggleOverlay(QString());
    check(presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "closing the overlay puts the board back");

    // 4. Short messages land in the log pane instead.
    presenter.writeOutput(QString::fromUtf8("时语 打出【闪】"));
    check(presenter.screenText().contains(QString::fromUtf8("打出")),
          "a short message joins the log scrollback");
}

void testAutoFollowAcrossPages()
{
    // Same fixture and viewport tui-board-view-test.cpp's testBoard09pPage2()
    // already proved lands sgs9 on page 1 (0-based) -- this exercises that
    // TuiBoardPresenter's own auto-follow (via TuiBoardView::pageForPlayer())
    // agrees with what render() actually draws there, not just the lenient
    // either/or the first test above allows.
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    ClientGameState state = ninePlayerState(QStringLiteral("sgs9"));
    presenter.stateChanged(state);
    pumpEvents();
    check(presenter.page() == 1, "a turn change pages to the seat's actual page, not just page 0");
    check(presenter.screenText().contains(QStringLiteral("‹2/")),
          "the followed-to page renders its own page indicator");
    check(presenter.screenText().contains(QString::fromUtf8("吕布")), // lvbu == sgs9
          "the acting player's cell is visible after auto-follow");
}

void testWriteErrorNotice()
{
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    ClientGameState state = fivePlayerState(QStringLiteral("sgs1"));
    presenter.stateChanged(state);
    pumpEvents();

    presenter.writeError(QString::fromUtf8("连接已断开"));
    // Both halves of spec §5.2's writeError() rule land on the same screen at
    // once (the prompt-row notice and the scrollback copy), so a single
    // contains() cannot tell them apart -- this only pins that the message
    // reaches the screen at all.
    check(presenter.screenText().contains(QString::fromUtf8("连接已断开")),
          "writeError() puts the notice on screen");

    // The prompt-row notice is transient (spec §5.2's "下次輸入時清除"), but
    // the scrollback copy is not ("不會一閃即逝") -- so the next keystroke is
    // checked by what it does, not by the message vanishing from the screen:
    // a printable key still reaches the editor rather than being swallowed by
    // notice-clearing. "@" does not otherwise appear anywhere the board
    // draws (kingdom codes, hearts, seat numbers, box-drawing glyphs), so its
    // presence on screen can only come from the input line.
    TuiKeyEvent charEvent{TuiKey::Char, QStringLiteral("@")};
    QString submitted;
    presenter.handleKey(charEvent, &submitted);
    check(submitted.isEmpty(), "a plain character does not submit a line");
    check(presenter.screenText().contains(QStringLiteral("@")),
          "the keystroke that clears the notice still reaches the line editor");
}

void testOverlayKeyPriority()
{
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    ClientGameState state = fivePlayerState(QStringLiteral("sgs1"));
    presenter.stateChanged(state);
    pumpEvents();

    presenter.writeOutput(QStringLiteral("line1\nline2\nline3\nline4"));
    check(presenter.screenText().contains(QStringLiteral("line1")),
          "the overlay is open with its content on screen");

    // Esc closes and swallows -- nothing reaches the editor.
    QString submitted;
    TuiKeyEvent escape{TuiKey::Escape, QString()};
    const bool escHandled = presenter.handleKey(escape, &submitted);
    check(escHandled, "Esc is consumed while the overlay is open");
    check(!presenter.screenText().contains(QStringLiteral("line1")),
          "Esc closed the overlay");

    // Re-open, then close it with a printable key that must still reach the
    // editor -- the player's first typed character is never lost.
    presenter.writeOutput(QStringLiteral("line1\nline2\nline3\nline4"));
    TuiKeyEvent letter{TuiKey::Char, QStringLiteral("x")};
    presenter.handleKey(letter, &submitted);
    check(!presenter.screenText().contains(QStringLiteral("line1")),
          "a printable key closes the overlay");
    check(presenter.screenText().contains(QStringLiteral("x")),
          "and the same key still reaches the line editor instead of being eaten");
}

void testOverlayScrollKeys()
{
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    ClientGameState state = fivePlayerState(QStringLiteral("sgs1"));
    presenter.stateChanged(state);
    pumpEvents();

    // More lines than an 80x24 screen's overlay can show at once (visible
    // rows = screen rows - 1, the last row being the scroll/close hint), so
    // scrolling actually moves what's on screen instead of being a no-op
    // that would pass whether or not scrolling was wired up at all.
    // Zero-padded to two digits so no line's marker is a substring of
    // another's (unpadded "OVERLAYLINE1" would also match "OVERLAYLINE10"
    // through "OVERLAYLINE19", which would still be on screen after a
    // one-line scroll and make the very check this test exists for pass
    // vacuously).
    QStringList lines;
    for (int i = 1; i <= 30; ++i)
        lines << QStringLiteral("OVERLAYLINE%1").arg(i, 2, 10, QLatin1Char('0'));
    presenter.writeOutput(lines.join(QLatin1Char('\n')));
    check(presenter.screenText().contains(QStringLiteral("OVERLAYLINE01"))
              && !presenter.screenText().contains(QStringLiteral("OVERLAYLINE30")),
          "the overlay opens scrolled to the top, taller than one screen");

    QString submitted;
    auto sendKey = [&](TuiKey key) { return presenter.handleKey(TuiKeyEvent{key, QString()}, &submitted); };

    check(sendKey(TuiKey::Down), "Down is consumed while the overlay is open");
    check(!presenter.screenText().contains(QStringLiteral("OVERLAYLINE01")),
          "Down scrolled line 1 off the top");
    check(presenter.screenText().contains(QStringLiteral("OVERLAYLINE02")),
          "Down still shows line 2 (scrolled by one, not by a full page)");
    check(!presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "Down did not close the overlay back to the board");

    check(sendKey(TuiKey::Up), "Up is consumed while the overlay is open");
    check(presenter.screenText().contains(QStringLiteral("OVERLAYLINE01")),
          "Up scrolled back up to line 1");
    check(!presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "Up did not close the overlay back to the board");

    check(sendKey(TuiKey::PageDown), "PageDown is consumed while the overlay is open");
    check(!presenter.screenText().contains(QStringLiteral("OVERLAYLINE01")),
          "PageDown scrolled past the first line");
    check(presenter.screenText().contains(QStringLiteral("OVERLAYLINE30")),
          "PageDown reaches the last line (30 lines, 23 visible -> max scroll 7)");
    check(!presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "PageDown did not close the overlay back to the board");

    check(sendKey(TuiKey::PageUp), "PageUp is consumed while the overlay is open");
    check(presenter.screenText().contains(QStringLiteral("OVERLAYLINE01")),
          "PageUp scrolled back to the top");
    check(!presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "PageUp did not close the overlay back to the board");

    // None of the four scroll keys reached the line editor: close the
    // overlay and submit, and the line must be empty -- an arrow/page key
    // wrongly routed to TuiLineEditor::handle() would still not insert text
    // (Up/Down browse an empty history, PageUp/PageDown are no-ops there
    // too), so the real proof is that Enter has nothing to submit at all.
    check(sendKey(TuiKey::Escape), "Esc closes the overlay after the scroll checks");
    presenter.handleKey(TuiKeyEvent{TuiKey::Enter, QString()}, &submitted);
    check(submitted.isEmpty(),
          "none of the four scroll keys reached the line editor's buffer");
}

void testRepaintCoalescing()
{
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    // Constructing the presenter already repaints once (setViewportSize()
    // forces a full frame), so this measures the delta rather than an
    // absolute count.
    const int before = presenter.repaintCountForTest();

    ClientGameState first = fivePlayerState(QStringLiteral("sgs2"));
    ClientGameState second = fivePlayerState(QStringLiteral("sgs3"));
    const InteractionRequest request = choosePlayerRequest({QStringLiteral("sgs4")});

    // Three notifications inside one event-loop turn -- no pumpEvents()
    // between them -- must still coalesce into exactly one repaint. This is
    // schedulePaint()'s whole reason to exist (a pending flag plus a single
    // QTimer::singleShot(0, ...)); a presenter that repainted synchronously
    // on every notification would leave this at 3, not 1.
    presenter.stateChanged(first);
    presenter.stateChanged(second);
    presenter.interactionChanged(&request);
    pumpEvents();

    check(presenter.repaintCountForTest() == before + 1,
          "three notifications in one event-loop turn coalesce into exactly one repaint");
}

void testPagingNeverTouchesTheEditorLine()
{
    // Invariant 1's structural half: a view action must not be able to
    // submit anything. PageUp/PageDown are consumed as paging and must never
    // produce a submitted line, no matter what the editor currently holds.
    TuiBoardPresenter presenter(QSize(80, 24), testResolvers());
    ClientGameState state = fivePlayerState(QStringLiteral("sgs1"));
    presenter.stateChanged(state);
    pumpEvents();

    QString submitted;
    TuiKeyEvent pageDown{TuiKey::PageDown, QString()};
    const bool handled = presenter.handleKey(pageDown, &submitted);
    check(handled, "PageDown is consumed by paging");
    check(submitted.isEmpty(), "paging never produces a submitted line");
    check(presenter.page() == 1, "PageDown actually moved the page");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        std::printf("[FAIL] engine initialization failed: %s\n", qPrintable(error));
        return 1;
    }

    testAutoFollowAndOverlay();
    testAutoFollowAcrossPages();
    testWriteErrorNotice();
    testOverlayKeyPriority();
    testOverlayScrollKeys();
    testRepaintCoalescing();
    testPagingNeverTouchesTheEditorLine();

    std::printf("[AUTOTEST] TUI_BOARD_PRESENTER_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
