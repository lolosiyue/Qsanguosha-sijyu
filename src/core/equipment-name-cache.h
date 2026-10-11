#ifndef EQUIPMENT_NAME_CACHE_H
#define EQUIPMENT_NAME_CACHE_H
#include <QMutex>
#include <QStringList>

// An owned snapshot is the only read interface. The mutex covers construction
// of that snapshot and first-nonempty publication, including every empty check.
// Instances must be constructed before publication to readers (Player's caches
// are namespace objects). No reliance on local-static guards or 64-bit atomics.
class EquipmentNameCache final
{
public:
    QStringList snapshot() const
    {
        QMutexLocker locker(&m_mutex);
        return m_names;
    }

    template<typename Collect> QStringList getOrPopulate(Collect collect)
    {
        const QStringList current = snapshot();
        if (!current.isEmpty()) return current;
        // Registry/name observation and any reentrant work happen without the
        // cache mutex. This lock does not protect QObject trees or card lifetime.
        const QStringList candidate = collect();
        QMutexLocker locker(&m_mutex);
        // A recursive or competing call may already have published. First
        // nonempty publication wins; empty candidates never disable retries.
        if (m_names.isEmpty()) m_names = candidate;
        return m_names;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_names;
};
#endif
