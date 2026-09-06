#ifndef TUI_BOARD_VIEW_H
#define TUI_BOARD_VIEW_H

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

private:
    TuiResolvers m_resolvers;
};

#endif
