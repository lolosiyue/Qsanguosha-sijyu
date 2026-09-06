#ifndef TUI_BOARD_LAYOUT_H
#define TUI_BOARD_LAYOUT_H

#include "tui-screen.h"

#include <QString>
#include <QVector>

// One opponent's on-screen cell. seatOffset counts seats downstream from the
// local player -- 1 is the player's immediate downstream neighbour, 2 the one
// after that, and so on all the way around the table. That ordering is the
// only thing tuiComputeBoardGeometry() guarantees about a slot; which
// physical rect a given offset lands in changes with cellCols (see below).
struct TuiSeatSlot
{
    int seatOffset = 0;
    int page = 0;
    TuiRect rect;
};

// A complete plan for one frame of the board: where every pane goes, how many
// opponents fit on a page, and which page/cell each of them lands in. This is
// pure data -- tui-board-layout.cpp never touches a TuiScreen, a socket, or
// any global state -- which is what makes it cheap to test at every terminal
// size and player count instead of only a handful of "supported" ones.
struct TuiBoardGeometry
{
    bool usable = false;
    QString unusableReason;
    TuiRect room;
    TuiRect log;
    TuiRect hand;
    TuiRect input;
    TuiRect self;
    int capacity = 0;
    int pageCount = 1;
    int cellCols = 0;
    // Not `slots`: that bare word is Qt's own keyword macro (qtmetamacros.h),
    // and undefining it to allow a member of that name breaks every QObject
    // declared later in the same translation unit that uses bare
    // "slots:"/"signals:" syntax -- do not rename this back.
    QVector<TuiSeatSlot> seatSlots;
};

// Lays out the board for a terminal of `rows` rows by `cols` columns.
//
// NOTE: this is (rows, cols) -- rows FIRST -- the opposite order from
// QSize(width, height), i.e. QSize's own (cols, rows). A caller building this
// from a QSize must pass size.height() then size.width(); passing the QSize's
// natural order transposes the whole board.
//
// `handLines` is how many lines the caller wants reserved for the player's
// hand pane (clamped to [1, 5] inside); `playerCount` includes the local
// player, so a two-player game passes 2. Below 60x18 the board refuses to lay
// out at all (`usable` is false and `unusableReason` says why) rather than
// returning a geometry that would draw a torn table -- the caller keeps
// running and asks again once the terminal is resized.
TuiBoardGeometry tuiComputeBoardGeometry(int rows, int cols, int playerCount, int handLines);

#endif
