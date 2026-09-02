#ifndef _AI_PROBE_H
#define _AI_PROBE_H

// 診斷插樁: 量測單次 AI 決策裡「技能有效性 / 距離修正 / 卡牌限制」的呼叫次數與耗時。
// 預設完全關閉 (只有一個 thread_local bool 的分支), 以 QSAN_AI_PROBE=1 開啟,
// QSAN_AI_PROBE_MS=<毫秒> 設定只回報超過該耗時的決策 (預設 200)。

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

// 計次 + 計時的 RAII; 探針關閉時只剩一個 bool 判斷。
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
// 記錄一次 CardLimitSkill::limitPattern 的耗時, 以技能名與卡名分別累計。
void recordLimitPattern(const QString &skillName, const QString &cardName, qint64 nanos);
QString report();

}

#endif
