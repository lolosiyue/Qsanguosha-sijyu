#ifndef CLIENT_TARGET_TIP_H
#define CLIENT_TARGET_TIP_H

#include "card.h"
#include "clientplayer.h"
#include "engine.h"
#include "hegemony-mode.h"
#include "skill.h"

namespace ClientRules {

// This boundary accepts ClientPlayer only: it must never inspect a server room.
// Deliberately use ownership metadata rather than hasSkill/isSkillValid, which
// can run Lua invalidity callbacks. Concealed providers fail closed.
inline bool targetTipInstanceVisible(const ClientPlayer *player, const ClientPlayer *viewer,
                                     const SkillInstance &instance)
{
    const SkillInstance *root = &instance;
    QList<SkillInstanceKey> seen;
    // Exact parent provenance, never a same-name skill on a different root.
    while (root->source == SourceHelper) {
        if (seen.contains(root->key()) || seen.size() >= 32) return false;
        seen.append(root->key());
        SkillInstanceKey parent = root->parent;
        if (root->parentRef.isValid()) {
            if (root->parentRef.ownerObjectName != player->objectName()) return false;
            parent = root->parentRef.key;
        }
        if (!parent.isValid() || parent.instanceID <= 0) return false;
        root = player->findSkillInstance(parent.skillName, parent.instanceID);
        if (!root) return false;
    }
    if (!root->visible || !Sanguosha) return false;
    const Skill *rootSkill = Sanguosha->getSkill(root->skillName);
    if (!rootSkill || !rootSkill->isVisibleForPlayer(player)) return false;
    if (player != viewer && rootSkill->isHideSkill()) return false;
    if (player == viewer || !HegemonyMode::enabled()) return true;
    if (root->bindHead == 1) return player->hasShownGeneral();
    if (root->bindHead == 2) return player->hasShownGeneral2();
    // Missing innate provenance cannot establish disclosure to another viewer.
    return root->source == SourceAcquired && !root->parentRef.isValid();
}

inline QStringList targetTipVisibleSkills(const ClientPlayer *player, const ClientPlayer *viewer)
{
    QStringList result;
    if (!player || !viewer) return result;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        if (!targetTipInstanceVisible(player, viewer, instance)) continue;
        if (!result.contains(instance.skillName)) result.append(instance.skillName);
    }
    return result;
}

inline QString targetTip(const Card *card, const QList<const Player *> &selected,
                         const ClientPlayer *source, const ClientPlayer *candidate,
                         const ClientPlayer *viewer, bool selectable)
{
    if (!Sanguosha || !card || !source || !candidate || !viewer) return {};
    TargetTipQuery query;
    query.viewer = viewer->objectName();
    query.source = source->objectName();
    query.candidate = candidate->objectName();
    query.cardName = card->objectName();
    query.cardType = card->getType();
    query.cardColor = card->getColorString();
    // A controlled player's selection is already visible through the dashboard.
    query.selectedCards = card->getSubcards();
    if (query.selectedCards.isEmpty() && card->getEffectiveId() >= 0)
        query.selectedCards.append(card->getEffectiveId());
    for (const Player *player : selected)
        if (player) query.selectedTargets.append(player->objectName());
    query.sourceHp = source->getHp();
    query.candidateHp = candidate->getHp();
    query.candidateMaxHp = candidate->getMaxHp();
    query.selectable = selectable;
    query.sourceSkills = targetTipVisibleSkills(source, viewer);
    query.candidateSkills = targetTipVisibleSkills(candidate, viewer);
    QStringList tips;
    const auto append = [&query, &tips](const QString &name, const QString &owner) {
        const Skill *skill = Sanguosha->getSkill(name);
        if (!skill) return;
        query.owner = owner;
        const QString tip = skill->targetTip(query);
        if (!tip.isEmpty() && !tips.contains(tip) && tips.size() < 3) tips.append(tip);
    };
    const QString cardSkill = card->getSkillName();
    if (query.sourceSkills.contains(cardSkill)) append(cardSkill, QStringLiteral("card"));
    for (const QString &name : query.sourceSkills) append(name, QStringLiteral("source"));
    for (const QString &name : query.candidateSkills) append(name, QStringLiteral("candidate"));
    return tips.join(QStringLiteral(" · "));
}

} // namespace ClientRules
#endif
