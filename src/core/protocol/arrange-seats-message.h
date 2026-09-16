#ifndef ARRANGE_SEATS_MESSAGE_H
#define ARRANGE_SEATS_MESSAGE_H

#include <QStringList>
#include <QVariant>

// docs/focus-relation-protocol-decision.md 2.3: the seat ring carries its own
// play direction, so a reconnecting, spectating or late-joining client never
// has to infer it from the battle log.  Every S_COMMAND_ARRANGE_SEATS carries
// the flag, including the one at game start -- a replay seek replays from the
// first event, and only a value that is re-asserted there can be rewound.
struct ArrangeSeatsMessage
{
    static constexpr int SchemaVersion = 2;

    QStringList playerNames;
    bool playOrderReversed = false;

    QVariant toVariant() const;
};

#endif
