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
