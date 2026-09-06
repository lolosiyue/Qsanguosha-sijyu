#ifndef TUI_BOARD_VIEW_H
#define TUI_BOARD_VIEW_H

#include "tui-board-layout.h"
#include "tui-resolvers.h"

#include <QString>
#include <QStringList>

class ClientGameState;
class TuiScreen;

// The local view state a presenter owns on top of ClientGameState: which page
// of the seat ring is on screen, the chat/log scrollback tail, and the two
// input-area lines. None of this ever reaches the wire (design invariant 1 in
// docs/tui-board-ui.md) -- it only tells this class what to paint this frame.
struct TuiBoardViewState
{
    int page = 0;
    QStringList logLines;
    QString promptLine;
    QString inputLine;
    int inputCursorColumn = 0;
    // A transient writeError() message. When set it takes the prompt row for
    // one frame instead of adding a row the fixed-height input pane does not
    // have -- see the comment above drawInput() in the .cpp for why.
    QString notice;
};

// Paints one ClientGameState into a TuiScreen: room (seat ring or, before
// GAME_START, the waiting room), log, hand and input. This is the only layer
// that knows what a kingdom or a judge card is -- TuiBoardLayout upstream is
// pure geometry, and TuiScreen downstream is a character grid that has never
// heard of Sanguosha.
//
// Every other helper this needs (seat ordering, cell text, hand wrapping,
// frame drawing) is a free function in tui-board-view.cpp's anonymous
// namespace rather than a member here: none of it needs access to
// m_resolvers directly (it is threaded through as a parameter), and keeping
// it out of the header means a resolver-table change never forces every
// includer of this header to recompile.
class TuiBoardView
{
public:
    explicit TuiBoardView(TuiResolvers resolvers);

    void render(TuiScreen *screen, const ClientGameState &state,
                const TuiBoardViewState &view) const;

    // The layout render() would use for this state and viewport. Exposed so
    // TuiBoardPresenter can answer "how many pages are there" and "which page
    // is player X on" (auto-follow, docs/tui-board-ui.md §3.6) from the same
    // hand-line-count and seat-order math render() already owns, instead of
    // keeping a second copy that could silently drift from what actually gets
    // drawn.
    TuiBoardGeometry computeGeometry(const ClientGameState &state, int rows, int cols) const;

    // Which page (0-based) `name`'s seat lands on within `geometry` -- pass
    // the geometry this same view just computed for the same state. Returns
    // 0 before GAME_START (no ring exists yet), for the self player (the self
    // cell never pages), and for a name that never got a seat: there is
    // nothing to page to in any of those cases.
    int pageForPlayer(const ClientGameState &state, const TuiBoardGeometry &geometry,
                       const QString &name) const;

private:
    TuiResolvers m_resolvers;
};

#endif
