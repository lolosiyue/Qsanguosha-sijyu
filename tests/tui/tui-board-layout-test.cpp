#include "tui-board-layout.h"

#include <QCoreApplication>
#include <QSet>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

bool overlaps(const TuiRect &a, const TuiRect &b)
{
    return a.row < b.row + b.rows && b.row < a.row + a.rows
        && a.col < b.col + b.cols && b.col < a.col + a.cols;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // Below the floor the board refuses to draw rather than drawing a torn one.
    const TuiBoardGeometry tooSmall = tuiComputeBoardGeometry(14, 52, 5, 1);
    check(!tooSmall.usable, "a terminal under 60x18 is not drawn at all");
    check(tooSmall.unusableReason.contains(QStringLiteral("60")),
          "and the reason names the size the player needs");

    const TuiBoardGeometry wide = tuiComputeBoardGeometry(40, 120, 5, 1);
    check(wide.usable, "a large terminal is usable");
    check(wide.cellCols >= 3, "120 columns fit the full three region ring");
    check(wide.pageCount == 1, "five players fit on one page there");
    check(wide.seatSlots.size() == 4, "every opponent gets a slot");
    check(!overlaps(wide.room, wide.log) && !overlaps(wide.room, wide.hand)
          && !overlaps(wide.hand, wide.input),
          "panes never overlap");
    check(wide.log.cols >= 22 && wide.log.cols <= 34, "the log pane stays within its clamp");
    check(wide.self.row > wide.room.row, "self sits at the bottom of the room pane");

    // Regions must match the desktop, not just be internally consistent with
    // each other: every row of RoomScene's s_regularSeatIndex opens with a
    // right-side region, so the downstream neighbour sits at the player's
    // right there, and the ring must actually reach the top row instead of
    // piling every opponent down one column.
    {
        const TuiRect *firstRect = nullptr;
        const TuiRect *lastRect = nullptr;
        const int lastOffset = static_cast<int>(wide.seatSlots.size());
        QSet<int> columns;
        bool sawTopRow = false;
        for (const TuiSeatSlot &slot : wide.seatSlots) {
            if (slot.seatOffset == 1)
                firstRect = &slot.rect;
            if (slot.seatOffset == lastOffset)
                lastRect = &slot.rect;
            columns.insert(slot.rect.col);
            if (slot.rect.row == wide.room.row)
                sawTopRow = true;
        }
        const int midCol = wide.room.col + wide.room.cols / 2;
        check(firstRect != nullptr && firstRect->col > midCol,
              "the downstream neighbour sits to the player's right, matching the desktop");
        check(lastRect != nullptr && lastRect->col < midCol,
              "the last opponent wraps around to the player's left");
        check(sawTopRow, "the ring actually reaches the top row, not just one column");
        check(columns.size() > 1, "opponents are not all piled into a single column");
    }

    // A two-column size: no width for a top row, so the ladder alternates
    // right/left starting from the downstream neighbour on the right.
    const TuiBoardGeometry twoColumn = tuiComputeBoardGeometry(24, 70, 3, 1);
    check(twoColumn.cellCols == 2, "70 columns leave room for exactly two cell columns");
    {
        TuiRect firstRect;
        TuiRect secondRect;
        bool haveFirst = false;
        bool haveSecond = false;
        for (const TuiSeatSlot &slot : twoColumn.seatSlots) {
            if (slot.seatOffset == 1) {
                firstRect = slot.rect;
                haveFirst = true;
            }
            if (slot.seatOffset == 2) {
                secondRect = slot.rect;
                haveSecond = true;
            }
        }
        check(haveFirst && haveSecond && firstRect.col > secondRect.col,
              "the two-column alternation starts on the right");
    }

    // A tall one-column size: the stack runs top to bottom in seatOffset order.
    const TuiBoardGeometry stacked = tuiComputeBoardGeometry(30, 60, 5, 1);
    check(stacked.cellCols == 1, "60 columns leave room for a single cell column");
    check(stacked.capacity >= 4, "tall enough that all four opponents share one page");
    {
        int previousRow = -1;
        bool inOrder = true;
        for (int offset = 1; offset <= 4; ++offset) {
            for (const TuiSeatSlot &slot : stacked.seatSlots) {
                if (slot.seatOffset == offset) {
                    if (slot.rect.row <= previousRow)
                        inOrder = false;
                    previousRow = slot.rect.row;
                }
            }
        }
        check(inOrder, "the single-column stack runs top to bottom in seatOffset order");
    }

    // The floor case: one column of cells, so the ring degrades to a stack and
    // five players need more than one page.
    const TuiBoardGeometry floorSize = tuiComputeBoardGeometry(18, 60, 5, 1);
    check(floorSize.usable, "exactly 60x18 is usable");
    check(floorSize.cellCols == 1, "60 columns leave room for a single cell column");
    check(floorSize.pageCount > 1, "which means five players page");

    // Seat order is the property that survives every degradation step.
    for (int players : {2, 3, 5, 8, 10, 20}) {
        const TuiBoardGeometry geometry = tuiComputeBoardGeometry(40, 120, players, 1);
        check(geometry.seatSlots.size() == players - 1,
              "every opponent is placed exactly once at any player count");
        QSet<int> offsets;
        for (const TuiSeatSlot &slot : geometry.seatSlots)
            offsets.insert(slot.seatOffset);
        check(offsets.size() == players - 1, "no seat offset is duplicated or dropped");
        check(offsets.contains(1), "the player's downstream neighbour is always placed");
        for (const TuiSeatSlot &slot : geometry.seatSlots) {
            check(slot.page >= 0 && slot.page < geometry.pageCount,
                  "every slot lands on a real page");
        }
    }

    // 20p is not a special case: it is the same paging path a 9 player game
    // takes in an 80x24 terminal.
    // 120x40 fits 39 cells, so twenty players would sit on one page there --
    // the crowding only shows at a size a player actually has.
    const TuiBoardGeometry twenty = tuiComputeBoardGeometry(24, 80, 20, 1);
    const TuiBoardGeometry nineSmall = tuiComputeBoardGeometry(24, 80, 9, 1);
    check(twenty.pageCount > 1 && nineSmall.pageCount > 1,
          "both crowded cases page rather than degrade to a list");

    std::printf("[AUTOTEST] TUI_BOARD_LAYOUT_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
