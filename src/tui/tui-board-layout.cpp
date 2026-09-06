#include "tui-board-layout.h"

#include <QCoreApplication>

#include <algorithm>
#include <utility>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

// Below this the frame/separator accounting alone eats every row and column,
// leaving nothing for the room or the log; there is no useful smaller layout
// to fall back to, so the board declines to draw rather than tearing.
constexpr int MinRows = 18;
constexpr int MinCols = 60;
constexpr int LogColsMin = 22;
constexpr int LogColsMax = 34;
// The desktop client's own opponent-cell footprint (RoomScene's photo cells
// are laid out on roughly this proportion). Reusing it means the ring
// degrades at the same terminal sizes a player would recognise as "cramped"
// on the desktop, instead of a number invented fresh for the terminal.
constexpr int CellWidth = 20;
constexpr int CellHeight = 3;

int clampInt(int value, int lo, int hi)
{
    return std::max(lo, std::min(hi, value));
}

int ceilDiv(int numerator, int denominator)
{
    return (numerator + denominator - 1) / denominator;
}

// The ordered list of opponent-grid cells (row, col) that seats fill in seat
// order, for one page. Shape depends only on cellCols -- the degradation
// ladder from spec 3.6 -- and is identical on every page, so paging never
// changes which physical cell a given (seatOffset - 1) % capacity lands in.
//
// This reduces RoomScene::updateTable()'s s_regularSeatIndex (roomscene.cpp,
// ~line 1771) to the same three regions it ultimately buckets into. Per the
// diagram at roomscene.cpp:~1848 ("| 4 | table | 3 |", region 5 = 0+3, region
// 6 = 2+4) regions 3 and 5 sit on the RIGHT (x = col2, AlignRight) and 4 and
// 6 sit on the LEFT (x = pad, AlignLeft); 1 and 7 are the top row. Every row
// of s_regularSeatIndex opens with a right-side region, so on the desktop
// the downstream neighbour (seatOffset 1) sits at the player's right -- the
// walk below starts there for the same reason: someone who knows the desktop
// client should be able to tell at a glance who is downstream.
//
// Rows are interleaved right/top/left -- rather than exhausting one whole
// column before moving to the next -- so a handful of opponents in a tall
// grid still visits all three regions instead of piling straight down one
// column: with 4 opponents and cellRows = 10 an exhaust-one-column-first walk
// would place all 4 in the right column and never touch the top or the left,
// which is not a ring, just a list.
//
// The desktop instead grows the left/right column *counts* as the seat count
// rises, balancing them so no side gets too tall for a fixed-size photo on a
// canvas -- a constraint that does not apply to a scrolling character grid,
// so that balancing table is not reproduced verbatim here.
QVector<std::pair<int, int>> orderedGridCells(int cellCols, int cellRows)
{
    QVector<std::pair<int, int>> cells;
    if (cellCols >= 3) {
        for (int r = 0; r < cellRows; ++r) {
            cells.append({r, cellCols - 1}); // right column
            for (int c = 1; c < cellCols - 1; ++c)
                cells.append({r, c});        // top row(s), middle columns
            cells.append({r, 0});            // left column
        }
    } else if (cellCols == 2) {
        // No width left for a top row: alternate right/left starting from
        // the downstream neighbour on the right.
        for (int r = 0; r < cellRows; ++r) {
            cells.append({r, 1});
            cells.append({r, 0});
        }
    } else {
        // A single cell-column: just stack downward from the downstream
        // neighbour.
        for (int r = 0; r < cellRows; ++r)
            cells.append({r, 0});
    }
    return cells;
}

TuiRect gridCellRect(const std::pair<int, int> &cell, const TuiRect &room, int roomCols)
{
    TuiRect rect;
    rect.row = room.row + cell.first * CellHeight;
    rect.col = room.col + cell.second * CellWidth;
    rect.rows = CellHeight;
    rect.cols = std::max(1, std::min(CellWidth, roomCols - cell.second * CellWidth));
    return rect;
}

} // namespace

TuiBoardGeometry tuiComputeBoardGeometry(int rows, int cols, int playerCount, int handLines)
{
    TuiBoardGeometry geometry;

    if (rows < MinRows || cols < MinCols) {
        geometry.usable = false;
        geometry.unusableReason =
            tr("终端太小(需 60×18,当前 %1×%2),请放大窗口或用 --ui classic 启动")
                .arg(cols)
                .arg(rows);
        return geometry;
    }

    // Two outer frame lines plus one column separator between the room and
    // the log pane.
    const int logCols = clampInt(cols * 3 / 10, LogColsMin, LogColsMax);
    const int roomCols = cols - logCols - 3;
    const int handRows = clampInt(handLines, 1, 5);
    const int inputRows = 2; // one prompt line, one input line
    // Top border, room/hand separator, hand/input separator, bottom border.
    const int roomRows = rows - handRows - inputRows - 4;

    const int cellCols = roomCols / CellWidth;
    // The bottom three rows of the room pane are the player's own cell, not
    // an opponent slot, so they come off before dividing the rest into
    // opponent-sized (CellHeight-row) rows.
    const int cellRows = (roomRows - 3) / CellHeight;

    // Capacity is a function of how many opponent cells physically fit --
    // never of the player count. A 9-player game wedged into an 80x24
    // terminal takes exactly the same "does it fit, and if not, page" path a
    // 20-player game takes in the same terminal; there is deliberately no
    // branch anywhere in this function that asks "is this a lot of
    // players?". One cell is reserved for the central draw/discard pile --
    // except in the degenerate 1x1 grid (reachable at exactly 60x18 with
    // handLines >= 4), where max(1, ...) hands that single cell to a seat
    // instead: a page that seats nobody is worse than a page with no visible
    // pile marker.
    const int capacity = std::max(1, cellCols * cellRows - 1);

    geometry.usable = true;
    geometry.capacity = capacity;
    geometry.cellCols = cellCols;

    const int opponentCount = std::max(0, playerCount - 1);
    geometry.pageCount = std::max(1, ceilDiv(opponentCount, capacity));

    geometry.room = TuiRect{1, 1, roomRows, roomCols};
    geometry.log = TuiRect{1, roomCols + 2, rows - 2, logCols};
    geometry.hand = TuiRect{roomRows + 2, 1, handRows, roomCols};
    geometry.input = TuiRect{roomRows + handRows + 3, 1, inputRows, roomCols};
    // The player's own cell sits at the bottom of the room pane, the same
    // CellHeight the opponent grid above it uses.
    geometry.self = TuiRect{geometry.room.row + roomRows - CellHeight, geometry.room.col,
        CellHeight, roomCols};

    const QVector<std::pair<int, int>> gridCells = orderedGridCells(cellCols, cellRows);
    geometry.seatSlots.reserve(opponentCount);
    for (int idx = 0; idx < opponentCount; ++idx) {
        TuiSeatSlot slot;
        slot.seatOffset = idx + 1;
        slot.page = idx / capacity;
        const int posInPage = idx % capacity;
        slot.rect = gridCellRect(gridCells.at(posInPage), geometry.room, roomCols);
        geometry.seatSlots.append(slot);
    }

    return geometry;
}
