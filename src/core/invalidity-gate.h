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
    const QString prefix = rule->property("InvalidityMarkPrefix").toString();
    if (!prefix.isEmpty()
        && (!player || !skill || player->getMark(prefix + skill->objectName()) < 1))
        return false;

    const QString currentSkill = rule->property("InvalidityCurrentSiblingSkill").toString();
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
