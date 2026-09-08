#include "tui-board-presenter.h"

#include "client-game-state.h"
#include "interaction-model.h"
#include "tui-renderer.h"
#include "tui-terminal.h"

#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

// The scrollback cap /log has always used (tui-application-controller.cpp's
// own log ring); the board's log pane borrows the same number so the two
// views of "recent output" never disagree about how much history survives.
constexpr int kScrollbackLimit = 200;

// The one player-shaped candidate a request names first, across the payload
// shapes that carry one at all. This only feeds auto-follow's "which page do
// I show" question -- the full candidate list a player can actually answer
// with still comes entirely from TuiInteractionView, unrelated to this.
QString firstCandidatePlayer(const InteractionRequest &request)
{
    if (const auto *players = request.payloadAs<PlayerInteractionPayload>())
        return players->selection.selectablePlayers.value(0);
    if (const auto *cards = request.payloadAs<CardInteractionPayload>()) {
        if (!cards->fixedTargets.isEmpty())
            return cards->fixedTargets.first();
        if (!cards->optionalTargets.isEmpty())
            return cards->optionalTargets.first();
    }
    if (const auto *gongxin = request.payloadAs<GongxinInteractionPayload>())
        return gongxin->targetPlayer;
    if (const auto *pindian = request.payloadAs<PindianInteractionPayload>())
        return pindian->opponent;
    if (const auto *yiji = request.payloadAs<YijiInteractionPayload>())
        return yiji->targetPlayers.value(0);
    return QString();
}

} // namespace

TuiBoardPresenter::TuiBoardPresenter(QSize size, TuiResolvers resolvers, TuiTerminal *terminal)
    : m_boardView(std::move(resolvers)), m_terminal(terminal), m_terminalReady(terminal == nullptr)
{
    setViewportSize(size);
    if (terminal != nullptr) {
        // spec §3.8: a resize rebuilds the grid and forces one full frame;
        // the diff resumes from there. Only wired when a real terminal is
        // present -- the test's null-terminal form has no resize signal to
        // listen for and drives setViewportSize() directly instead.
        QObject::connect(terminal, &TuiTerminal::resized, this, [this, terminal]() {
            setViewportSize(terminal->size());
        });
    }
}

// A cell grid has nowhere to put an escape sequence: TuiScreen::putText()
// substitutes a space for the ESC and keeps the rest, so a colour change
// arrives as a visible "[1;36m" in front of the text it was meant to colour.
// The board is the presenter that cannot render what the controller may hand
// it (TuiStreamPresenter can, and does), so the board is where colour is
// dropped -- not upstream, which would make --log-file and the controller's
// write path depend on the installed presenter.
QString TuiBoardPresenter::plainForGrid(const QString &text)
{
    return TuiRenderer::sanitize(text, text.size());
}

void TuiBoardPresenter::writeOutput(const QString &text)
{
    // Every non-dump message -- banners, connection/status lines, chat,
    // command feedback, and interaction prompts (spec §5.2's category 1) --
    // regardless of how many lines it happens to be. Interaction prompts
    // routinely run 4-20 lines, well past what an old line-count heuristic
    // here used to treat as "long enough for an overlay"; that heuristic
    // covered the board with a full-screen overlay on nearly every server
    // request instead of letting it flow into the log pane like this. Only
    // writeDump() (the six named long-dump commands) may open an overlay.
    const QStringList lines = plainForGrid(text).split(QLatin1Char('\n'));
    for (const QString &line : lines)
        appendScrollback(line);
    repaint();
}

void TuiBoardPresenter::writeDump(const QString &text)
{
    // spec §5.2: exactly the six long-dump commands (/players, /log, /hand,
    // /skills, /piles, /equip) take the whole screen rather than scrolling
    // the board out of view. The text itself is whatever TuiRenderer already
    // built for that command -- this class never generates or reformats it.
    toggleOverlay(plainForGrid(text));
}

void TuiBoardPresenter::writeError(const QString &text)
{
    // Takes the prompt row for one frame (repaint()'s notice handling) and
    // also joins the scrollback, so it survives past that one frame instead
    // of flashing and vanishing the moment something else redraws the prompt
    // row (spec §5.2: "同時進 scrollback，不會一閃即逝").
    // The prompt row is one row: a multi-line error collapses to a single
    // spaced line there and is elided like any other text. The scrollback
    // gets it split, one entry per line, the same as writeOutput() -- so a
    // long error stays readable in the log pane instead of becoming one
    // over-long entry that only its first screenful survives.
    const QStringList lines = plainForGrid(text).split(QLatin1Char('\n'));
    m_notice = lines.join(QLatin1Char(' '));
    for (const QString &line : lines)
        appendScrollback(line);
    repaint();
}

void TuiBoardPresenter::shutdown()
{
    // TuiTerminal is owned and restored by the controller (docs/tui-board-ui.md
    // §4.1's RAII contract runs on the terminal object itself, on every
    // unwind path); this presenter never entered raw mode or the alternate
    // screen on its own, so there is nothing of its own to give back here.
}

void TuiBoardPresenter::stateChanged(const ClientGameState &state)
{
    m_state = &state;
    const QString current = state.gameValue(QStringLiteral("current_player")).toString();
    if (current != m_lastCurrentPlayer) {
        m_lastCurrentPlayer = current;
        // A turn change reclaims the page from any manual flip. Paging's
        // whole risk is "the person to watch isn't on this page" (spec
        // §3.6), and that risk peaks exactly when someone new starts
        // acting -- so a turn change is one of the two moments (the other
        // being a new request, below) auto-follow is required to win.
        m_manualPage = false;
        if (!current.isEmpty())
            followPlayer(current);
    }
    // Several of these can arrive in one event-loop turn during a state-sync
    // burst; schedulePaint() coalesces them into one repaint rather than
    // redrawing per notification.
    schedulePaint();
}

void TuiBoardPresenter::interactionChanged(const InteractionRequest *request)
{
    m_viewState.promptLine = request != nullptr ? request->prompt : QString();
    if (request != nullptr) {
        // A new request is the other moment auto-follow overrides a manual
        // flip (see stateChanged()'s comment). A cleared request
        // (request == nullptr, answered/cancelled) deliberately does NOT
        // touch m_manualPage: the spec's suppression window is "until the
        // next turn change or the next request", not "until this request
        // ends".
        m_manualPage = false;
        followPlayer(firstCandidatePlayer(*request));
    }
    schedulePaint();
}

void TuiBoardPresenter::setCompleter(std::function<QString(const QString &, QStringList *)> completer)
{
    m_editor.setCompleter(std::move(completer));
}

void TuiBoardPresenter::setPage(int page)
{
    m_viewState.page = std::max(0, page);
    m_manualPage = true;
    repaint();
}

void TuiBoardPresenter::toggleOverlay(const QString &content)
{
    m_overlay = content;
    m_overlayScroll = 0;
    repaint();
}

void TuiBoardPresenter::setViewportSize(QSize size)
{
    // TuiScreen::resize is (rows, cols); QSize is (width, height) i.e.
    // (cols, rows) -- the opposite order. Getting this backwards silently
    // produces a plausible but transposed board (tui-board-layout.h's own
    // warning), so height (rows) is passed first, on purpose.
    m_screen.resize(std::max(0, size.height()), std::max(0, size.width()));
    repaint();
}

bool TuiBoardPresenter::handleKey(const TuiKeyEvent &event, QString *submitted)
{
    if (submitted != nullptr)
        submitted->clear();
    // A notice lives for one frame's worth of attention (spec §5.2: "下次輸入
    // 時清除") -- the next keystroke of any kind, overlay-consumed or not,
    // clears it.
    m_notice.clear();

    if (!m_overlay.isEmpty()) {
        // Esc/q/space close the overlay and are swallowed -- they are not
        // meaningful editor input on their own.
        if (event.key == TuiKey::Escape || event.text == QStringLiteral("q")
            || event.text == QStringLiteral(" ")) {
            toggleOverlay(QString());
            return true;
        }
        if (event.key == TuiKey::Up || event.key == TuiKey::Down
            || event.key == TuiKey::PageUp || event.key == TuiKey::PageDown) {
            scrollOverlay(event.key);
            return true;
        }
        // Anything else closes the overlay but is deliberately NOT consumed
        // here: it falls through to paging/the editor below. A player who
        // started typing the instant a dump happened to pop up must not
        // lose the first character of what they typed -- that is the one
        // failure mode a "swallow everything while an overlay is open" rule
        // would create.
        m_overlay.clear();
        m_overlayScroll = 0;
    }

    if (event.key == TuiKey::PageUp || event.key == TuiKey::PageDown) {
        setPage(m_viewState.page + (event.key == TuiKey::PageDown ? 1 : -1));
        return true;
    }

    const bool changed = m_editor.handle(event, submitted);
    repaint();
    return changed;
}

void TuiBoardPresenter::followPlayer(const QString &name)
{
    if (m_state == nullptr || name.isEmpty())
        return;
    const TuiBoardGeometry geometry =
        m_boardView.computeGeometry(*m_state, m_screen.rows(), m_screen.cols());
    m_viewState.page = m_boardView.pageForPlayer(*m_state, geometry, name);
}

void TuiBoardPresenter::schedulePaint()
{
    if (m_repaintPending)
        return;
    m_repaintPending = true;
    QTimer::singleShot(0, this, [this]() { repaint(); });
}

void TuiBoardPresenter::appendScrollback(const QString &text)
{
    m_scrollback.append(text);
    while (m_scrollback.size() > kScrollbackLimit)
        m_scrollback.removeFirst();
}

void TuiBoardPresenter::scrollOverlay(TuiKey key)
{
    const QStringList lines = m_overlay.split(QLatin1Char('\n'));
    // Leaves the last row for the close/scroll hint drawn in repaint().
    const int visibleRows = std::max(1, m_screen.rows() - 1);
    const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleRows);
    const int step = (key == TuiKey::PageUp || key == TuiKey::PageDown) ? visibleRows : 1;
    if (key == TuiKey::Up || key == TuiKey::PageUp)
        m_overlayScroll = std::max(0, m_overlayScroll - step);
    else
        m_overlayScroll = std::min(maxScroll, m_overlayScroll + step);
    repaint();
}

void TuiBoardPresenter::repaint()
{
    m_repaintPending = false;
    ++m_repaintCount;

    if (m_state != nullptr) {
        const TuiBoardGeometry geometry =
            m_boardView.computeGeometry(*m_state, m_screen.rows(), m_screen.cols());
        m_pageCount = geometry.pageCount;
    }

    if (!m_overlay.isEmpty()) {
        // The overlay's content is exactly what writeOutput() was handed
        // (TuiRenderer's own layout, spec §5.2's "classic 的排版原封不動") --
        // this only slices it to the viewport and adds a scroll/close hint.
        m_screen.clear();
        const QStringList lines = m_overlay.split(QLatin1Char('\n'));
        const int rows = m_screen.rows();
        const int cols = m_screen.cols();
        const int visibleRows = std::max(1, rows - 1);
        for (int i = 0; i < visibleRows && (m_overlayScroll + i) < lines.size(); ++i)
            m_screen.putText(i, 0, tuiPadTo(lines.at(m_overlayScroll + i), cols));
        if (rows > 0) {
            m_screen.putText(rows - 1, 0,
                tuiPadTo(QStringLiteral("↑↓/PgUp/PgDn 滚动   Esc/q/空格 关闭"), cols),
                TuiAttr::Dim);
        }
    } else if (m_state != nullptr) {
        // The notice takes the prompt row for one frame instead of getting a
        // row of its own -- the input pane is exactly two rows (spec §3.2)
        // and neither is spare.
        m_viewState.notice = m_notice;
        m_viewState.inputLine = m_editor.text();
        m_viewState.inputCursorColumn = m_editor.cursorColumn();
        m_viewState.logLines = m_scrollback;
        m_boardView.render(&m_screen, *m_state, m_viewState);
    } else {
        // No game state has arrived yet: construction, setViewportSize()
        // firing before the first stateChanged(), or (the case that actually
        // matters to a player) a connection that never gets that far at all
        // -- `qsanguosha_tui --ui board --port 1` fails before ClientCore
        // ever sees a state push. Blanking the screen here used to mean that
        // failure rendered zero non-space characters: the alternate screen
        // came up, nothing was ever drawn into it, and the player saw a
        // plain empty terminal with no clue anything had gone wrong, even
        // though writeError()/writeOutput() had already put a real message
        // in m_notice/m_scrollback by this point.
        //
        // Render through the exact same path used once a real state exists,
        // against a placeholder empty ClientGameState instead of skipping
        // straight to screen.clear(): TuiBoardView treats an empty state
        // exactly like a game that has not started yet (§3.3's waiting
        // room), which is a reasonable enough thing to show, and it comes
        // with the frame drawn and the log/notice panes wired up for free --
        // so a startup error still lands somewhere the player can see it.
        ClientGameState empty;
        m_viewState.notice = m_notice;
        m_viewState.inputLine = m_editor.text();
        m_viewState.inputCursorColumn = m_editor.cursorColumn();
        m_viewState.logLines = m_scrollback;
        m_boardView.render(&m_screen, empty, m_viewState);
    }

    if (m_terminal != nullptr)
        flushToTerminal();
}

void TuiBoardPresenter::terminalEntered()
{
    if (m_terminalReady)
        return;
    m_terminalReady = true;
    // The frame(s) painted between construction and this call were computed
    // (setViewportSize()'s forced-full-repaint flag, and any state that
    // arrived since) but never flushed -- see flushToTerminal() below, which
    // returns before calling TuiScreen::flush() while not ready specifically
    // so that flag survives untouched until now.
    //
    // schedulePaint() rather than a direct repaint() call: this function
    // runs synchronously inside TuiApplicationController::start(), before
    // QCoreApplication::exec() has started the event loop. A direct
    // repaint() here would do this presenter's first real terminal write
    // (and, transitively, whatever a resize's TuiTerminal::resized()
    // connection or ClientLiveSession's own startup does after it) from
    // that pre-exec() context instead of from inside the event loop like
    // every later repaint. QTimer::singleShot(0, ...) still fires as soon
    // as exec() starts, so the frame appears immediately either way; it
    // just does so from the same place every other repaint runs from.
    schedulePaint();
}

void TuiBoardPresenter::flushToTerminal()
{
    if (!m_terminalReady) {
        // Hold the frame -- see terminalEntered(). Must return before
        // TuiScreen::flush() below: flush() unconditionally commits its diff
        // state (m_previous = m_current), so calling it here and merely
        // discarding the string would make the *next* real flush() -- once
        // terminalEntered() runs -- see no change and emit nothing, silently
        // losing the held frame.
        return;
    }
    const QString diff = m_screen.flush();
    if (diff.isEmpty())
        return;
    // The terminal owns the descriptor and platform byte-writing contract.
    // In particular, _write's text-mode CRLF conversion corrupts VT frames.
    m_terminal->write(diff.toUtf8());
}
