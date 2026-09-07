#ifndef TUI_BOARD_PRESENTER_H
#define TUI_BOARD_PRESENTER_H

#include "tui-board-view.h"
#include "tui-line-editor.h"
#include "tui-presenter.h"
#include "tui-resolvers.h"
#include "tui-screen.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>

class ClientGameState;
struct InteractionRequest;
class TuiTerminal;

// Assembles TuiBoardView + TuiScreen + TuiLineEditor into the one object the
// controller talks to once board mode is running: it repaints on
// stateChanged()/interactionChanged(), sends writeOutput() to the log
// scrollback and writeDump() to a full-screen overlay (spec §5.2 -- only the
// six long-dump commands ever call writeDump(); everything else, interaction
// prompts included, is writeOutput() and never opens an overlay no matter how
// many lines it is), and turns decoded keys into either a view action
// (paging, scrolling, opening/closing the overlay) or a line-editor
// keystroke.
//
// Design invariant 1 (docs/tui-board-ui.md): a view action never touches the
// wire. That is not a convention this class has to remember to honour --
// there is no member here that names ClientLiveSession, ClientCore, or
// anything that could send a protocol message, so setPage()/toggleOverlay()/
// handleKey()'s paging branches have no path to one even by accident. The
// only way anything this class does ever reaches the server is the completed
// line handed back through handleKey()'s `submitted` out-parameter, which the
// controller feeds through the exact same handleInputLine() classic mode
// always used (invariant 2) -- this class never calls into ClientCore or the
// session itself.
class TuiBoardPresenter final : public QObject, public TuiPresenter
{
    Q_OBJECT

public:
    // `terminal` is optional: null means paint into this presenter's own
    // TuiScreen and never touch a real terminal or install a resize
    // connection -- how the unit test drives this class without a tty.
    // Production passes the real TuiTerminal so a SIGWINCH-driven resize()
    // signal keeps the viewport in sync (see the constructor body).
    explicit TuiBoardPresenter(QSize size, TuiResolvers resolvers, TuiTerminal *terminal = nullptr);

    // Called by the controller once TuiTerminal::enter() has actually put the
    // terminal into the alternate screen (see start() in
    // tui-application-controller.cpp). Before this runs, flushToTerminal()
    // holds every frame instead of writing it: this presenter is constructed,
    // and paints its first frame, well before enter() has run, and writing
    // that frame straight to the terminal's raw fd at that point would land on
    // the user's real (primary) screen instead of the alternate one -- an
    // ordering bug, not a cosmetic one, since those bytes would then persist
    // in the shell's scrollback after the alternate screen is later left. If
    // enter() never succeeds, this is never called, and nothing this
    // presenter draws ever reaches the terminal. Safe to call more than once;
    // only the first call does anything. No-op when constructed without a
    // real terminal (the unit test's form): flushToTerminal() already never
    // touches a null m_terminal, so there is nothing here to hold back.
    void terminalEntered();

    void writeOutput(const QString &text) override;
    void writeDump(const QString &text) override;
    void writeError(const QString &text) override;
    void shutdown() override;
    void stateChanged(const ClientGameState &state) override;
    void interactionChanged(const InteractionRequest *request) override;

    // A manual flip: sets the page and marks it manual (see handleKey()'s
    // paging branch and the auto-follow comments on stateChanged()/
    // interactionChanged()). Not clamped to the last-known page count --
    // the count can change (players join, hand size changes) between one
    // repaint and the next, and a page briefly beyond the end just renders
    // its ring empty rather than erroring; TuiBoardView already tolerates
    // that (see tui-board-layout.cpp's own page-per-slot bookkeeping).
    void setPage(int page);
    int page() const { return m_viewState.page; }
    // The page count as of the last repaint. Display-only (the room title's
    // "‹n/N›"); nothing here gates on it, for the reason setPage() explains.
    int pageCount() const { return m_pageCount; }
    // Opens a full-screen scrollable overlay showing `content`, or closes the
    // current one when `content` is empty. `content` is exactly what the
    // controller already builds through TuiRenderer for /players, /log,
    // /hand, /skills, /piles and /equip (spec §5.2) -- this class never
    // builds that text itself.
    void toggleOverlay(const QString &content);
    // Forwards to the embedded TuiLineEditor's own setCompleter() (spec
    // §5.1: "Tab 補全，直接重用現有 m_completer"). The controller installs the
    // exact same completer function on TuiInput for classic mode; board mode
    // never assembles lines through TuiInput at all (raw mode hands this
    // presenter individual keys instead, see tui-application-controller.cpp's
    // handleBoardKeyEvents()), so without this call board mode's line editor
    // never learns about completion and Tab does nothing there.
    void setCompleter(std::function<QString(const QString &, QStringList *)> completer);
    // Rebuilds the character grid for a new terminal size and forces one
    // full frame; TuiScreen's own diff resumes after (spec §3.8).
    void setViewportSize(QSize size);
    // Consumes one decoded key. Overlay scrolling/closing and page-flipping
    // are handled here and never reach the line editor; everything else is
    // handed to it. `*submitted` is set (matching TuiLineEditor::handle) when
    // Enter just completed a line -- the controller's only job with that
    // value is to feed it through the existing lineReady-shaped path.
    bool handleKey(const TuiKeyEvent &event, QString *submitted);
    // Test-only accessor: the stripped-of-attributes frame, equivalent to
    // TuiScreen::toPlainText(). Production never reads this; it writes the
    // frame straight to the terminal instead (see flushToTerminal()).
    QString screenText() const { return m_screen.toPlainText(); }
    // Test-only accessor: how many times repaint() has actually run. Exists
    // solely so a test can prove stateChanged()/interactionChanged()'s
    // pending-flag coalescing does something -- several calls within one
    // event-loop turn, then one pump, should move this by exactly one, not
    // by however many notifications were sent.
    int repaintCountForTest() const { return m_repaintCount; }

private:
    // Colour and control bytes a cell grid cannot hold, removed on the way in.
    static QString plainForGrid(const QString &text);

    void schedulePaint();
    void repaint();
    void flushToTerminal();
    void appendScrollback(const QString &text);
    void scrollOverlay(TuiKey key);
    void followPlayer(const QString &name);

    // TuiBoardView keeps its own copy of the resolver table; this presenter
    // never needs one of its own since every call that reads it goes through
    // m_boardView (computeGeometry(), pageForPlayer(), render()).
    TuiBoardView m_boardView;
    TuiScreen m_screen;
    TuiLineEditor m_editor;
    // Non-owning: the controller owns the real terminal (RAII per
    // docs/tui-board-ui.md §4.1) and outlives this presenter.
    TuiTerminal *m_terminal = nullptr;

    TuiBoardViewState m_viewState;
    // The last state handed to stateChanged(), kept only to repaint from --
    // this presenter never mutates it and never reaches into it for
    // anything wire-related. Non-owning: ClientCore, which really owns the
    // ClientGameState, outlives this presenter (see the ordering note on
    // TuiApplicationController::m_presenter).
    const ClientGameState *m_state = nullptr;
    QString m_lastCurrentPlayer;
    // Set by setPage(), cleared by the next turn change or interaction
    // request -- see stateChanged()/interactionChanged() for why it is only
    // ever cleared there and nowhere else (docs/tui-board-ui.md §3.6: "手動
    // 翻頁壓住自動跟隨，但只壓到下一次回合轉換或下一個請求為止").
    bool m_manualPage = false;
    int m_pageCount = 1;

    // Empty means no overlay is open.
    QString m_overlay;
    int m_overlayScroll = 0;

    // The log pane's scrollback, capped the same as the existing /log's
    // 200-line history so the two never disagree about how much is kept.
    QStringList m_scrollback;
    // A transient writeError() message; see repaint()'s comment on why it
    // borrows the prompt row instead of getting one of its own, and
    // handleKey() for where it gets cleared.
    QString m_notice;

    bool m_repaintPending = false;
    // Backs repaintCountForTest() above.
    int m_repaintCount = 0;

    // Gates flushToTerminal() until terminalEntered() releases it (see that
    // method's comment). Starts true when there is no real terminal to guard
    // against (m_terminal == nullptr, the unit test's construction form), so
    // that form's behaviour is unchanged.
    bool m_terminalReady;
};

#endif
