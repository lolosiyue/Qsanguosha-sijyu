#include "player-ui-state-builder.h"

#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill-instance-utils.h"

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

    buildSkillDescriptions(state, player, room);
    return state;
}

void PlayerUIStateBuilder::buildSkillDescriptions(PlayerUIState &state,
                                                 const ServerPlayer &player, const Room &room)
{
    state.skillUsage.clear();
    state.skillValidity.clear();
    state.skillEffects.clear();
    if (!player.isAlive()) return;

    QMap<QString, QStringList> counters;
    for (const SkillInstance &instance : player.getSkillInstances()) {
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        if (!skill || !instance.visible || !skill->isVisible()) continue;
        const QString key = SkillInstanceUtils::formatName(instance.skillName, instance.instanceID);
        // The public validity cache follows the same visibility rule as instance sync.
        if (instance.source != SourceHelper)
            state.skillValidity.insert(key, !player.isSkillInvalid(skill, instance.instanceID));
        QVariantMap usage = room.describeSkillUsage(const_cast<ServerPlayer *>(&player), instance);
        if (const auto *amountSkill = dynamic_cast<const AmountSkillV2 *>(skill))
            usage.insert("base_amount", amountSkill->getBaseAmount());
        const QString counter = usage.value("counter").toString();
        if (!counter.isEmpty()) counters[counter] << key;
        state.skillUsage.insert(key, usage);
    }
    for (auto it = counters.cbegin(); it != counters.cend(); ++it) {
        for (const QString &key : it.value()) {
            QVariantMap usage = state.skillUsage.value(key).toMap();
            usage.insert("shared", usage.value("shared").toBool() || it.value().size() > 1);
            state.skillUsage.insert(key, usage);
        }
    }

    // These are recorded effects, not additional owned skills. No source/expiry is inferred.
    for (const QString &record : player.getTag("SkillInvalidityRecords").toStringList()) {
        const QStringList parts = record.split('|');
        if (parts.size() < 3) continue;
        QString skillName;
        const int id = SkillInstanceUtils::parseName(parts.at(0), skillName);
        if (skillName != "all" && (id > 0 ? !player.hasSkillInstance(skillName, id)
                                          : player.getSkillInstanceIds(skillName).isEmpty())) continue;
        state.skillEffects << QVariantMap{{"kind", "invalidity"}, {"target_skill", parts.at(0)},
            {"source_player", parts.at(1)}, {"reason", parts.at(2)}, {"target", player.objectName()}};
    }
    for (const QVariant &value : player.getCardLimitationDetails()) {
        QVariantMap effect = value.toMap();
        effect.insert("kind", "card_limit");
        effect.insert("target", player.objectName());
        state.skillEffects << effect;
    }
    const QVariantMap effects = player.getTag("SkillEffectDescriptions").toMap();
    for (auto it = effects.cbegin(); it != effects.cend(); ++it) {
        QVariantMap effect = it.value().toMap();
        const QString activeMark = effect.value("active_mark").toString();
        if (!activeMark.isEmpty() && player.getMark(activeMark) <= 0) continue;
        effect.insert("kind", "declared");
        effect.insert("id", it.key());
        effect.insert("target", player.objectName());
        state.skillEffects << effect;
    }
}
