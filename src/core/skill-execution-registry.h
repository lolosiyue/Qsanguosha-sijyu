#ifndef SKILL_EXECUTION_REGISTRY_H
#define SKILL_EXECUTION_REGISTRY_H

#include <QMap>
#include <QSharedPointer>
#include <QVariant>

enum SkillExecutionResult {
    SkillExecutionNoResult,
    SkillExecutionCompleted,
    SkillExecutionEffectSkipped,
    SkillExecutionPayFailed,
    SkillExecutionInvalidTargetUpdate
};

class SkillExecutionRegistry
{
public:
    struct Entry {
        Entry(qint64 id, const QVariant &data) : executionID(id), backingData(data), finished(false), result(SkillExecutionNoResult) {}
        qint64 executionID;
        QVariant backingData;
        QVariant immutableContextData;
        QVariant contextData;
        bool finished;
        SkillExecutionResult result;
    };

    class Guard {
    public:
        Guard() : registry(nullptr), id(0) {}
        Guard(SkillExecutionRegistry *registry, qint64 id) : registry(registry), id(id) {}
        Guard(const Guard &) = delete;
        Guard &operator =(const Guard &) = delete;
        Guard(Guard &&other) : registry(other.registry), id(other.id) { other.registry = nullptr; }
        Guard &operator =(Guard &&other) {
            if (this != &other) {
                if (registry) registry->remove(id);
                registry = other.registry;
                id = other.id;
                other.registry = nullptr;
            }
            return *this;
        }
        ~Guard() { if (registry) registry->remove(id); }
        Entry *get() const { return registry ? registry->find(id) : nullptr; }
        qint64 executionID() const { return id; }
        bool finish(SkillExecutionResult result) { return registry && registry->finish(id, result); }
    private:
        SkillExecutionRegistry *registry;
        qint64 id;
    };

    SkillExecutionRegistry() : m_nextID(1) {}
    Guard begin(const QVariant &backingData = QVariant()) {
        qint64 id = m_nextID++;
        m_entries.insert(id, QSharedPointer<Entry>(new Entry(id, backingData)));
        return Guard(this, id);
    }
    Entry *find(qint64 id) const { return m_entries.value(id).data(); }
    int size() const { return m_entries.size(); }
private:
    bool finish(qint64 id, SkillExecutionResult result) {
        Entry *entry = find(id);
        if (!entry || entry->finished) return false;
        entry->finished = true;
        entry->result = result;
        return true;
    }
    void remove(qint64 id) { m_entries.remove(id); }
    qint64 m_nextID;
    QMap<qint64, QSharedPointer<Entry> > m_entries;
};

#endif
