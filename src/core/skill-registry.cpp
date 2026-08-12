#include "skill-registry.h"

#include "skill-instance-utils.h"

namespace {

template<typename T>
QList<const T *> castSkills(const QList<QPointer<Skill>> &skills)
{
    QList<const T *> result;
    foreach (const QPointer<Skill> &skill, skills)
        if (skill) result << dynamic_cast<const T *>(skill.data());
    result.removeAll(nullptr);
    return result;
}

}

bool SkillRegistry::add(const Skill *skill)
{
    if (!skill)
        return false;

    QWriteLocker locker(&m_lock);
    Skill *mutableSkill = const_cast<Skill *>(skill);
    const QString &name = skill->objectName();
    const bool replaced = m_skills.contains(name);

    if (replaced) {
        const QPointer<Skill> previous = m_skills.value(name);
        m_prohibitSkills.removeAll(previous);
        m_distanceSkills.removeAll(previous);
        m_maxCardsSkills.removeAll(previous);
        m_targetModSkills.removeAll(previous);
        m_invaliditySkills.removeAll(previous);
        m_globalTriggerSkills.removeAll(previous);
        m_attackRangeSkills.removeAll(previous);
        m_viewAsEquipSkills.removeAll(previous);
        m_cardLimitSkills.removeAll(previous);
        m_prohibitPindianSkills.removeAll(previous);
    }

    m_skills.insert(name, mutableSkill);
    if (dynamic_cast<const ProhibitSkill *>(skill)) m_prohibitSkills << mutableSkill;
    if (dynamic_cast<const DistanceSkill *>(skill)) m_distanceSkills << mutableSkill;
    if (dynamic_cast<const MaxCardsSkill *>(skill)) m_maxCardsSkills << mutableSkill;
    if (dynamic_cast<const TargetModSkill *>(skill)) m_targetModSkills << mutableSkill;
    if (dynamic_cast<const InvaliditySkill *>(skill)) m_invaliditySkills << mutableSkill;
    const TriggerSkill *trigger = dynamic_cast<const TriggerSkill *>(skill);
    if (trigger && trigger->isGlobal()) m_globalTriggerSkills << mutableSkill;
    if (dynamic_cast<const AttackRangeSkill *>(skill)) m_attackRangeSkills << mutableSkill;
    if (dynamic_cast<const ViewAsEquipSkill *>(skill)) m_viewAsEquipSkills << mutableSkill;
    if (dynamic_cast<const CardLimitSkill *>(skill)) m_cardLimitSkills << mutableSkill;
    if (dynamic_cast<const ProhibitPindianSkill *>(skill)) m_prohibitPindianSkills << mutableSkill;

    return replaced;
}

bool SkillRegistry::contains(const QString &skillName) const
{
    QReadLocker locker(&m_lock);
    return m_skills.contains(skillName);
}

const Skill *SkillRegistry::find(const QString &skillName) const
{
    QReadLocker locker(&m_lock);
    return m_skills.value(SkillInstanceUtils::baseName(skillName), nullptr);
}

const TriggerSkill *SkillRegistry::triggerSkill(const QString &skillName) const
{
    QReadLocker locker(&m_lock);
    const QPointer<Skill> skill = m_skills.value(SkillInstanceUtils::baseName(skillName));
    return skill ? qobject_cast<const TriggerSkill *>(skill.data()) : nullptr;
}

QStringList SkillRegistry::names() const
{
    QReadLocker locker(&m_lock);
    return m_skills.keys();
}

QList<const Skill *> SkillRegistry::allSkills() const
{
    QReadLocker locker(&m_lock);
    QList<const Skill *> result;
    // 遍歷 Hash 中的所有 QPointer
    foreach (const QPointer<Skill> &skill, m_skills.values())
        // QPointer 自動魔法：如果對象被 delete 了，isNull() 會變 true
        if (skill) result << skill.data();
    return result;
}

QList<const ProhibitSkill *> SkillRegistry::prohibitSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<ProhibitSkill>(m_prohibitSkills);
}

QList<const DistanceSkill *> SkillRegistry::distanceSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<DistanceSkill>(m_distanceSkills);
}

QList<const MaxCardsSkill *> SkillRegistry::maxCardsSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<MaxCardsSkill>(m_maxCardsSkills);
}

QList<const TargetModSkill *> SkillRegistry::targetModSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<TargetModSkill>(m_targetModSkills);
}

QList<const InvaliditySkill *> SkillRegistry::invaliditySkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<InvaliditySkill>(m_invaliditySkills);
}

QList<const TriggerSkill *> SkillRegistry::globalTriggerSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<TriggerSkill>(m_globalTriggerSkills);
}

QList<const AttackRangeSkill *> SkillRegistry::attackRangeSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<AttackRangeSkill>(m_attackRangeSkills);
}

QList<const ViewAsEquipSkill *> SkillRegistry::viewAsEquipSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<ViewAsEquipSkill>(m_viewAsEquipSkills);
}

QList<const CardLimitSkill *> SkillRegistry::cardLimitSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<CardLimitSkill>(m_cardLimitSkills);
}

QList<const ProhibitPindianSkill *> SkillRegistry::prohibitPindianSkills() const
{
    QReadLocker locker(&m_lock);
    return castSkills<ProhibitPindianSkill>(m_prohibitPindianSkills);
}
