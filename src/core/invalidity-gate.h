#ifndef INVALIDITY_GATE_H
#define INVALIDITY_GATE_H

#include "player.h"
#include "skill.h"

// Opt-in necessary conditions for a pure invalidity callback. Read live state
// on every query: marks, flags, death and skill attachment can change inside a
// single trigger dispatch. A possible invalidation still runs the Lua callback.
inline bool invalidityCallbackMayReject(const InvaliditySkill *rule,
                                        const Player *player, const Skill *skill)
{
    // Fixed per-player prerequisites do not depend on the queried skill name.
    const QString &mark = rule->gateMark();
    if (!mark.isEmpty() && (!player || player->getMark(mark) < 1))
        return false;

    const QString &prefix = rule->gateMarkPrefix();
    if (!prefix.isEmpty()
        && (!player || !skill || player->getMark(prefix + skill->objectName()) < 1))
        return false;

    const QString &currentSkill = rule->gateCurrentSiblingSkill();
    if (!currentSkill.isEmpty() && player) {
        // Match getAliveSiblings() order and its exclusion of self, including
        // the first-flag-wins behavior if several players have CurrentPlayer.
        for (const Player *sibling : player->getAliveSiblings()) {
            if (sibling->hasFlag("CurrentPlayer"))
                return sibling->hasSkill(currentSkill, true);
        }
        return false;
    }
    return true;
}

#endif
