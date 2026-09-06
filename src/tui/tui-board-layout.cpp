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
// ~line 1771) to the same three regions it ultimately buckets into (3/5 ->
// left, 1/7 -> top, 4/6 -> right), walked in a fixed left/top/right order
// instead of the desktop's count-balancing table: the desktop grows the left
// and right columns as the seat count rises so no side gets too tall, which
// matters for a fixed-size photo on a canvas. In a scrolling character grid
// that balancing has no payoff -- a plain walk already satisfies the one
// property the layout actually needs to guarantee (every seatOffset placed
// exactly once, in order) -- so the simpler walk is what is implemented here.
QVector<std::pair<int, int>> orderedGridCells(int cellCols, int cellRows)
{
    QVector<std::pair<int, int>> cells;
    if (cellCols >= 3) {
        // Left column, downstream neighbour first (top to bottom); then the
        // top row(s) across the middle columns, left to right; then the
        // right column, top to bottom.
        for (int r = 0; r < cellRows; ++r)
            cells.append({r, 0});
        for (int r = 0; r < cellRows; ++r) {
            for (int c = 1; c < cellCols - 1; ++c)
                cells.append({r, c});
        }
        for (int r = 0; r < cellRows; ++r)
            cells.append({r, cellCols - 1});
    } else if (cellCols == 2) {
        // No width left for a top row: alternate left/right starting from
        // the downstream neighbour.
        for (int r = 0; r < cellRows; ++r) {
            cells.append({r, 0});
            cells.append({r, 1});
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
    // players?". One cell is reserved for the central draw/discard pile.
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
    geometry.slots.reserve(opponentCount);
    for (int idx = 0; idx < opponentCount; ++idx) {
        TuiSeatSlot slot;
        slot.seatOffset = idx + 1;
        slot.page = idx / capacity;
        const int posInPage = idx % capacity;
        slot.rect = gridCellRect(gridCells.at(posInPage), geometry.room, roomCols);
        geometry.slots.append(slot);
    }

    return geometry;
}
