#include "../../src/client/card-memory.h"
#include <cassert>
#include <iostream>
using namespace ClientCardMemory;
int main()
{
    Hand hand;
    hand.replace({1, 2, 3});
    hand.lose({-1}, {3}, 3);
    assert(hand.known == Ids({3}));
    assert(hand.uncertain == Ids({1, 2}));
    hand.lose({1, -1}, {3}, 2);
    assert(hand.known == Ids({3}) && hand.uncertain == Ids({2}));
    hand.observe(2);
    hand.lose({}, {}, 2);
    assert(hand.uncertain.empty());
    hand.lose({-1}, {}, 1);
    assert(hand.known.empty() && hand.uncertain.size() == 2);
    hand.forget(2); // observed in somebody else's zone
    assert(hand.uncertain == Ids({3}));
    hand.lose({-1}, {}, 0);
    assert(hand.known.empty() && hand.uncertain.empty());
    hand.replace({4, 4, -1});
    assert(hand.known == Ids({4}));
    hand.lose({-1, 4}, {}, 1); // batch ordering must not resurrect a visible loss
    assert(hand.uncertain.empty());

    Deck deck;
    deck.stage({1, 2}, true);
    deck.stage({4, 3}, false);
    assert(deck.top.empty()); // observations wait for deck commit
    deck.confirmCount(10);
    assert(deck.top == Ids({1, 2}) && deck.bottom == Ids({4, 3}));
    deck.lose({1, 3});
    assert(deck.top == Ids({2}) && deck.bottom == Ids({4}));
    deck.lose({-1});
    assert(deck.top.empty() && deck.bottom.empty());
    deck.stage({5}, true);
    deck.confirmCount(5);
    deck.confirmCount(5); // count-only reorder even when size did not change
    assert(deck.top.empty());
    deck.stage({5}, true);
    deck.discardPending(); // unrelated protocol message
    deck.confirmCount(5);
    assert(deck.top.empty());
    deck.stage({5}, true);
    deck.stage({5}, false);
    deck.confirmCount(5);
    assert(deck.top.empty() && deck.bottom.empty());
    deck.stage({1, 2}, true);
    deck.confirmCount(1);
    assert(deck.top.empty());
    deck.stage({1, -1}, true);
    deck.confirmCount(4);
    assert(deck.top.empty());
    deck.stage({1}, true);
    deck.confirmCount(4);
    deck.clear(); // shuffle / insertion / snapshot / context reset
    assert(deck.top.empty() && deck.bottom.empty());
    std::cout << "card memory contract passed\n";
}
