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

// Arguments are rows then columns (unlike QSize); playerCount includes the local player. Below 60x18, usable is false.
TuiBoardGeometry tuiComputeBoardGeometry(int rows, int cols, int playerCount, int handLines);

#endif
