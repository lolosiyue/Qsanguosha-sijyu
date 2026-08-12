#ifndef _SKILL_REGISTRY_H
#define _SKILL_REGISTRY_H

#include "skill.h"

#include <QPointer>
#include <QReadWriteLock>

class SkillRegistry
{
public:
    bool add(const Skill *skill);
    bool contains(const QString &skillName) const;

    const Skill *find(const QString &skillName) const;
    const TriggerSkill *triggerSkill(const QString &skillName) const;
    QStringList names() const;
    QList<const Skill *> allSkills() const;

    QList<const ProhibitSkill *> prohibitSkills() const;
    QList<const DistanceSkill *> distanceSkills() const;
    QList<const MaxCardsSkill *> maxCardsSkills() const;
    QList<const TargetModSkill *> targetModSkills() const;
    QList<const InvaliditySkill *> invaliditySkills() const;
    QList<const TriggerSkill *> globalTriggerSkills() const;
    QList<const AttackRangeSkill *> attackRangeSkills() const;
    QList<const ViewAsEquipSkill *> viewAsEquipSkills() const;
    QList<const CardLimitSkill *> cardLimitSkills() const;
    QList<const ProhibitPindianSkill *> prohibitPindianSkills() const;

private:
    mutable QReadWriteLock m_lock;
    QHash<QString, QPointer<Skill>> m_skills;
    QList<QPointer<Skill>> m_prohibitSkills;
    QList<QPointer<Skill>> m_distanceSkills;
    QList<QPointer<Skill>> m_maxCardsSkills;
    QList<QPointer<Skill>> m_targetModSkills;
    QList<QPointer<Skill>> m_invaliditySkills;
    QList<QPointer<Skill>> m_globalTriggerSkills;
    QList<QPointer<Skill>> m_attackRangeSkills;
    QList<QPointer<Skill>> m_viewAsEquipSkills;
    QList<QPointer<Skill>> m_cardLimitSkills;
    QList<QPointer<Skill>> m_prohibitPindianSkills;
};

#endif
