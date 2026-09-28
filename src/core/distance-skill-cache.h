#ifndef QSAN_DISTANCE_SKILL_CACHE_H
#define QSAN_DISTANCE_SKILL_CACHE_H

#include "skill.h"
#include <QMutex>
#include <QPointer>
#include <QSet>

// Only definition pointers are memoized. No player state, callback results or
// distances belong here. Instances live with the engine or room definitions.
class DistanceSkillCache
{
public:
    struct Stamp {
        quint64 bootstrapVersion;
        quint64 roomVersion;
        QSet<QString> excludedBootstrapNames;
        bool operator==(const Stamp &other) const
        {
            return bootstrapVersion == other.bootstrapVersion
                && roomVersion == other.roomVersion
                && excludedBootstrapNames == other.excludedBootstrapNames;
        }
    };

    struct Snapshot {
        Snapshot(const QList<const DistanceSkill *> &value) : skills(value)
        {
            for (const DistanceSkill *skill : value) dependencies.append(skill);
        }
        QList<const DistanceSkill *> skills;
        // Also include excluded bootstrap entries and non-distance shadows:
        // their destruction/rename can change which definitions are merged.
        QList<const Skill *> dependencies;
    };

    template<typename ReadStamp, typename Build>
    QList<const DistanceSkill *> get(ReadStamp readStamp, Build build)
    {
        QMutexLocker locker(&m_mutex);
        const Stamp before = readStamp();
        if (m_valid && before == m_stamp && liveListMatches())
            return m_skills;

        m_valid = false;
        const Snapshot snapshot = build();
        QList<Dependency> live;
        live.reserve(snapshot.dependencies.size());
        QSet<const Skill *> seen;
        for (const Skill *skill : snapshot.dependencies) {
            if (seen.contains(skill)) continue;
            seen.insert(skill);
            live.append({const_cast<Skill *>(skill), skill->objectName()});
        }
        // Do not publish a fill spanning registration, replacement, renaming,
        // deletion or a change to the exact bootstrap exclusion set.
        if (before == readStamp()) {
            m_stamp = before;
            m_skills = snapshot.skills;
            m_live = live;
            m_valid = liveListMatches();
        }
        return snapshot.skills;
    }

private:
    struct Dependency {
        QPointer<Skill> skill;
        QString name;
    };

    bool liveListMatches() const
    {
        // Check all merge dependencies, including shadows absent from the
        // result. This also covers reentrant queries in an earlier QObject
        // destroyed/objectNameChanged observer, before our version slot runs.
        for (const Dependency &dependency : m_live) {
            const Skill *skill = dependency.skill.data();
            if (!skill || skill->objectName() != dependency.name) return false;
        }
        return true;
    }

    QMutex m_mutex;
    bool m_valid = false;
    Stamp m_stamp {0, 0, {}};
    QList<const DistanceSkill *> m_skills;
    QList<Dependency> m_live;
};

#endif
