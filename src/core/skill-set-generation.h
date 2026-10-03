#ifndef _SKILL_SET_GENERATION_H
#define _SKILL_SET_GENERATION_H

// Global generation counter for the skill set. Incremented whenever any Player's skills change, alongside Player::skill_set_changed.
// Cross-player derived caches, such as the set of skills held across the room, use it to detect stale results;
// the set changes only a few times per turn but may be queried tens of thousands of times per AI decision.

#include <QAtomicInteger>

namespace SkillSet {

inline QAtomicInteger<quint64> &generationCounter()
{
    static QAtomicInteger<quint64> counter(1);
    return counter;
}

inline quint64 generation()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return generationCounter().loadRelaxed();
#else
    return generationCounter().load();
#endif
}

inline void bump()
{
    generationCounter().fetchAndAddRelaxed(1);
}

}

#endif
