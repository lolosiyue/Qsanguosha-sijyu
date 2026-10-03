#ifndef _AI_PROBE_H
#define _AI_PROBE_H

// Optional probe for skill validity, distance modifiers and card-limit calls
// during one AI decision. Disabled by default; QSAN_AI_PROBE=1 enables it.
// QSAN_AI_PROBE_MS=<milliseconds> sets the reporting threshold (default 200).

#include <QElapsedTimer>
#include <QString>

namespace AiProbe {

enum Slot {
    Slot_hasSkill = 0,
    Slot_getSkillList,
    Slot_isSkillInvalid,
    Slot_correctSkillValidity,
    Slot_distanceTo,
    Slot_correctDistance,
    Slot_isCardLimited,
    Slot_getDistanceSkills,
    Slot_distIter,
    Slot_distLegacy,
    Slot_distV2,
    Slot_limitOwnerFilter,
    Slot_limitOwnerRebuild,
    Slot_Count
};

bool enabled();
int reportThresholdMs();

void reset();
void bump(Slot slot);
void addNanos(Slot slot, qint64 nanos);

// Count and time a scope; disabled probes add only one thread-local bool check.
class ScopedProbe
{
public:
    explicit ScopedProbe(Slot slot) : m_slot(slot), m_on(enabled())
    {
        if (m_on) {
            bump(slot);
            m_timer.start();
        }
    }
    ~ScopedProbe()
    {
        if (m_on) addNanos(m_slot, m_timer.nsecsElapsed());
    }

private:
    Slot m_slot;
    bool m_on;
    QElapsedTimer m_timer;
};
// Record CardLimitSkill::limitPattern time, grouped by skill and card name.
void recordLimitPattern(const QString &skillName, const QString &cardName, qint64 nanos);
QString report();

}

#endif
