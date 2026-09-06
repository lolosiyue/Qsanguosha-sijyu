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

class ClientGameState;
struct InteractionRequest;
class TuiTerminal;

// Assembles TuiBoardView + TuiScreen + TuiLineEditor into the one object the
// controller talks to once board mode is running: it repaints on
// stateChanged()/interactionChanged(), routes writeOutput()/writeError()
// between the log scrollback and a full-screen overlay, and turns decoded
// keys into either a view action (paging, scrolling, opening/closing the
// overlay) or a line-editor keystroke.
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

    void writeOutput(const QString &text) override;
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

private:
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
};

#endif
