#include "player-ui-state-builder.h"

#include "engine.h"
#include "room.h"
#include "serverplayer.h"

PlayerUIState PlayerUIStateBuilder::build(const ServerPlayer &player, const Room &room)
{
    PlayerUIState state;
    state.handMax = player.getMaxCards();

    foreach (const MaxCardsSkill *mc_skill, Sanguosha->getMaxCardsSkills()) {
        if (!mc_skill || mc_skill->objectName() == "gamerulemaxcards") continue;

        foreach (const SkillUIContribution &c,
                 Sanguosha->listMaxCardsSkillContributions(mc_skill, &player)) {
            if (c.isFixed) {
                state.maxCardsSkills << QString("%1^F%2^%3")
                                            .arg(mc_skill->objectName()).arg(c.value).arg(c.holderName);
            } else if (c.value != 0) {
                state.maxCardsSkills << QString("%1^%2^%3")
                                            .arg(mc_skill->objectName()).arg(c.value).arg(c.holderName);
            }
        }
    }
    state.maxCardsSkills.removeDuplicates();

    const QList<ServerPlayer *> siblings = room.getOtherPlayers(const_cast<ServerPlayer *>(&player));
    if (!siblings.isEmpty()) {
        foreach (const Skill *skill, player.getSkills(true, false)) {
            const DistanceSkill *dist_skill = qobject_cast<const DistanceSkill *>(skill);
            if (!dist_skill) continue;
            int off_val = Sanguosha->contributionOfDistanceSkill(dist_skill, &player, siblings.first());
            if (off_val < 0) {
                state.offensiveDistance += off_val;
                state.offensiveSkills << dist_skill->objectName();
            }
            int def_val = Sanguosha->contributionOfDistanceSkill(dist_skill, siblings.first(), &player);
            if (def_val > 0) {
                state.defensiveDistance += def_val;
                state.defensiveSkills << dist_skill->objectName();
            }
        }
    }

    foreach (const Skill *skill, player.getSkills(true, false)) {
        const ViewAsEquipSkill *vaes = qobject_cast<const ViewAsEquipSkill *>(skill);
        if (vaes) {
            QString cns = vaes->viewAsEquip(&player);
            if (!cns.isEmpty()) {
                foreach (const QString &eq, cns.split(",", Qt::SkipEmptyParts))
                    state.viewAsEquipSkills << QString("%1^%2").arg(eq).arg(vaes->objectName());
            }
        }
    }

    return state;
}
