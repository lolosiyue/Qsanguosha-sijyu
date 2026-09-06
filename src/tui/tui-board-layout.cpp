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

// The ordered list of opponent-grid cells (row, col) that the `n` seats on
// one page fill, in seat order. Shape depends on cellCols -- the degradation
// ladder from spec 3.6 -- and, for the ring (cellCols >= 3), on `n` itself:
// see below for why the split cannot be a fixed function of the grid alone.
//
// This reduces RoomScene::updateTable()'s s_regularSeatIndex (roomscene.cpp,
// ~line 1771) to the same three regions it ultimately buckets into. Per the
// diagram at roomscene.cpp:~1848 ("| 4 | table | 3 |", region 5 = 0+3, region
// 6 = 2+4) regions 3 and 5 sit on the RIGHT (x = col2, AlignRight) and 4 and
// 6 sit on the LEFT (x = pad, AlignLeft); 1 and 7 are the top row.
//
// The WITHIN-region direction also comes from the desktop, not just the
// region membership: RoomScene::updateTable() (roomscene.cpp:~1949) appends
// left-column (4/6) seats in seat order but PREPENDS top-row and right-column
// (1/7, 3/5) seats, and _dispersePhotos then lays each region's list out
// left-to-right (top) or top-to-bottom (sides) in list order. Net effect: the
// right column reads bottom-to-top, the top row reads right-to-left, and the
// left column reads top-to-bottom -- one continuous counter-clockwise ring
// starting at the player's right. That is the one thing this function must
// reproduce exactly, because it is the whole mechanism by which a player who
// knows the desktop client can tell at a glance who is downstream; how many
// seats each region gets is not (the desktop grows the side columns to keep
// a fixed-size photo from getting too tall on a canvas, which does not apply
// to a scrolling character grid, so that growth table is not reproduced).
//
// The side/top split is computed from `n`, the actual population of this
// page, rather than from cellRows*cellCols: a fixed split sized for a full
// page (up to `capacity` seats) would let a handful of opponents exhaust the
// right column's full height before ever reaching the top or the left --
// visually a list, not a ring. Growing the sides by roughly a quarter of `n`
// keeps a small page's ring proportioned to what is actually on it.
QVector<std::pair<int, int>> orderedGridCells(int cellCols, int cellRows, int n)
{
    QVector<std::pair<int, int>> cells;
    if (cellCols >= 3) {
        const int topWidth = cellCols - 2; // middle columns available per row
        int side = std::min({cellRows, (n + 3) / 4, n / 2});
        int top = n - 2 * side;
        // A tall, narrow grid (few middle columns, many seats) can still
        // overflow the top strip even with both sides at their maximum
        // height; hand the sides more of the population until it fits, which
        // capacity() guarantees is possible by the time side == cellRows.
        while (top > topWidth * cellRows && side < cellRows) {
            ++side;
            top = n - 2 * side;
        }

        // Right column, bottom to top -- nearest the player first.
        for (int i = 0; i < side; ++i)
            cells.append({cellRows - 1 - i, cellCols - 1});

        // Top row(s), right to left, filling one row before starting the
        // next.
        int placed = 0;
        for (int r = 0; placed < top; ++r) {
            for (int c = topWidth; c >= 1 && placed < top; --c, ++placed)
                cells.append({r, c});
        }

        // Left column, top to bottom.
        for (int i = 0; i < side; ++i)
            cells.append({i, 0});
    } else if (cellCols == 2) {
        // No width left for a top row: alternate right/left starting from
        // the downstream neighbour on the right. (This is the TUI's own
        // degradation, with no desktop equivalent, so it keeps the spec's
        // original top-to-bottom alternation rather than the ring's
        // bottom-to-top reversal.)
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

    // Each page gets its own ring, sized to that page's own population (see
    // orderedGridCells) rather than one grid-wide list reused via modulo --
    // every page before the last is full (`capacity` seats), but the last
    // page's ring is proportioned to however many seats actually land on it.
    geometry.seatSlots.reserve(opponentCount);
    int remaining = opponentCount;
    int seatOffset = 1;
    for (int page = 0; remaining > 0; ++page) {
        const int pageSize = std::min(capacity, remaining);
        const QVector<std::pair<int, int>> gridCells = orderedGridCells(cellCols, cellRows, pageSize);
        for (int i = 0; i < pageSize; ++i) {
            TuiSeatSlot slot;
            slot.seatOffset = seatOffset++;
            slot.page = page;
            slot.rect = gridCellRect(gridCells.at(i), geometry.room, roomCols);
            geometry.seatSlots.append(slot);
        }
        remaining -= pageSize;
    }

    return geometry;
}
