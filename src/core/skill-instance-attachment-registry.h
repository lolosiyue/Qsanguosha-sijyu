#ifndef SKILL_INSTANCE_ATTACHMENT_REGISTRY_H
#define SKILL_INSTANCE_ATTACHMENT_REGISTRY_H

#include "skill-instance-types.h"

#include <QMap>

// Room owns cross-player lifecycle links; Player remains the owner of its instance data.
class SkillInstanceAttachmentRegistry {
public:
    bool attach(const SkillInstanceRef &parent, const SkillInstanceRef &child)
    {
        if (!parent.isValid() || !child.isValid() || parent == child) return false;
        if (m_parentByChild.contains(child)) return m_parentByChild.value(child) == parent;
        if (wouldCreateCycle(parent, child)) return false;
        m_parentByChild.insert(child, parent);
        m_childrenByParent[parent].insert(child, true);
        return true;
    }

    QList<SkillInstanceRef> detach(const SkillInstanceRef &root)
    {
        QList<SkillInstanceRef> result;
        collectPostOrder(root, result);
        foreach (const SkillInstanceRef &ref, result) {
            const SkillInstanceRef parent = m_parentByChild.take(ref);
            if (parent.isValid()) {
                QMap<SkillInstanceRef, bool> &siblings = m_childrenByParent[parent];
                siblings.remove(ref);
                if (siblings.isEmpty()) m_childrenByParent.remove(parent);
            }
            m_childrenByParent.remove(ref);
        }
        return result;
    }

    QList<SkillInstanceRef> childrenOf(const SkillInstanceRef &parent) const { return m_childrenByParent.value(parent).keys(); }
    bool contains(const SkillInstanceRef &child) const { return m_parentByChild.contains(child); }

private:
    bool wouldCreateCycle(const SkillInstanceRef &parent, const SkillInstanceRef &child) const
    {
        SkillInstanceRef cursor = parent;
        while (m_parentByChild.contains(cursor)) {
            if (cursor == child) return true;
            cursor = m_parentByChild.value(cursor);
        }
        return cursor == child;
    }

    void collectPostOrder(const SkillInstanceRef &root, QList<SkillInstanceRef> &result) const
    {
        foreach (const SkillInstanceRef &child, childrenOf(root)) collectPostOrder(child, result);
        result << root;
    }

    QMap<SkillInstanceRef, QMap<SkillInstanceRef, bool> > m_childrenByParent;
    QMap<SkillInstanceRef, SkillInstanceRef> m_parentByChild;
};

#endif // SKILL_INSTANCE_ATTACHMENT_REGISTRY_H
