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

// Preserve desktop counter-clockwise seat order; size the side/top split to the player count.
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

    // Capacity follows available cells. Reserve one for the central pile unless a 1x1 grid would seat nobody.
    const int capacity = std::max(1, cellCols * cellRows - 1);

    geometry.usable = true;
    geometry.capacity = capacity;
    geometry.cellCols = cellCols;

    const int opponentCount = std::max(0, playerCount - 1);
    geometry.pageCount = std::max(1, ceilDiv(opponentCount, capacity));

    geometry.room = TuiRect{1, 1, roomRows, roomCols};
    // Bound the log pane to roomRows so it cannot overlap the hand and input panes.
    geometry.log = TuiRect{1, roomCols + 2, roomRows, logCols};
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
