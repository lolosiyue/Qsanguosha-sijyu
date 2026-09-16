#ifndef SEAT_RING_TABLE_H
#define SEAT_RING_TABLE_H

// The seat ring regions -- the project-wide norm docs/ui-roadmap.md 2.1 refers to.
//
// Each row maps seat order to one of the eight U-shaped table regions around the
// player. Index 0 is the seat next to the player and the order continues around the
// ring, so the row is what makes "the one on your left" mean the same thing in every
// shell. For the regular table the row index is opponent count - 1, which covers 1..19
// opponents; past 20 players the ring is undefined and a shell must degrade (2.1).
// Rows are ragged -- entries past the opponent count are zero padding and never read.
//
// Shells must not re-invent this ordering:
//   - Qt desktop reads these arrays directly (src/ui/room-layout-engine.cpp).
//   - The web shell keeps a TypeScript copy in web/src/ui-seat-layout.ts, held to
//     these values by web/scripts/check-seat-ring-sync.mjs.
//   - The TUI takes only the ring direction from it (src/tui/tui-board-layout.cpp).
// A change here is a change to every shell's seat order; update the copies with it.

namespace SeatRingTable
{
const int regularSeatRegions[][20] = {
    { 1 }, { 5, 6 }, { 5, 1, 6 }, { 3, 1, 1, 4 }, { 3, 1, 1, 1, 4 },
    { 5, 5, 1, 1, 6, 6 }, { 5, 5, 1, 1, 1, 6, 6 },
    { 3, 3, 7, 7, 7, 7, 4, 4 }, { 3, 3, 7, 7, 7, 7, 7, 4, 4 },
    { 3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4 },
    { 3, 3, 3, 7, 7, 7, 7, 7, 7, 4, 4, 4 },
    { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 },
    { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 },
    { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 },
    { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 },
    { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 },
    { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 },
    { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 },
    { 3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4, 4 }
};
const int hulaoSeatRegions[4][3] = {
    { 1, 1, 1 }, { 3, 3, 1 }, { 3, 1, 4 }, { 1, 4, 4 }
};
const int threeVThreeSeatRegions[3][5] = {
    { 3, 1, 1, 1, 4 }, { 1, 1, 1, 4, 4 }, { 3, 3, 1, 1, 1 }
};
}

#endif
