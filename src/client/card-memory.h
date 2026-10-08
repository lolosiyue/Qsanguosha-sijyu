#ifndef QSAN_CLIENT_CARD_MEMORY_H
#define QSAN_CLIENT_CARD_MEMORY_H

#include <algorithm>
#include <vector>

// Recipient-side observations only. No authoritative deck/hand is consulted.
namespace ClientCardMemory {
using Ids = std::vector<int>;
inline void erase(Ids &ids, int id) { ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end()); }
inline void insert(Ids &ids, int id) {
    if (id >= 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
}

struct Hand {
    Ids known;
    Ids uncertain;
    void clear() { known.clear(); uncertain.clear(); }
    void observe(int id) { insert(known, id); erase(uncertain, id); }
    void forget(int id) { erase(known, id); erase(uncertain, id); }
    void replace(const Ids &ids) { clear(); for (int id : ids) observe(id); }
    void lose(const Ids &ids, const Ids &visible, int remaining) {
        bool unknown = false;
        for (int id : ids) {
            if (id == -1) unknown = true;
            else if (id >= 0) forget(id);
        }
        if (unknown) {
            const Ids previous = known;
            for (int id : previous) {
                if (std::find(visible.begin(), visible.end(), id) == visible.end()) {
                    erase(known, id);
                    insert(uncertain, id);
                }
            }
        }
        if (remaining <= 0) clear();
        else if (static_cast<int>(known.size()) >= remaining) uncertain.clear();
    }
};

// Lists run inward from the respective edge: bottom[0] is bottommost.
// Canonical movement packets have no trustworthy edge/direction. Thus only
// explicit observed rearrangements establish order, never redacted IDs.
struct Deck {
    Ids top;
    Ids bottom;
    Ids pendingTop;
    Ids pendingBottom;
    bool pending = false;
    void discardPending() { pendingTop.clear(); pendingBottom.clear(); pending = false; }
    void clear() { top.clear(); bottom.clear(); discardPending(); }
    void stage(const Ids &ids, bool atTop) {
        Ids unique;
        for (int id : ids) {
            if (id < 0 || std::find(unique.begin(), unique.end(), id) != unique.end()) {
                clear(); return;
            }
            unique.push_back(id);
        }
        (atTop ? pendingTop : pendingBottom) = unique;
        pending = true;
    }
    void confirmCount(int count) {
        // Even an unchanged count may be an unannounced reorder/return.
        top.clear(); bottom.clear();
        bool valid = pending && count >= 0
            && pendingTop.size() + pendingBottom.size() <= static_cast<unsigned>(count);
        for (int id : pendingTop)
            if (std::find(pendingBottom.begin(), pendingBottom.end(), id) != pendingBottom.end()) valid = false;
        if (valid) { top = pendingTop; bottom = pendingBottom; }
        discardPending();
    }
    void lose(const Ids &ids) {
        discardPending();
        if (std::find(ids.begin(), ids.end(), -1) != ids.end()) { clear(); return; }
        for (int id : ids) { erase(top, id); erase(bottom, id); }
    }
};
}
#endif
