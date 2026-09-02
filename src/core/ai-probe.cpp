#include "ai-probe.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QStringList>

namespace {

struct LimitEntry {
    qint64 calls = 0;
    qint64 nanos = 0;
};

struct ProbeState {
    qint64 counts[AiProbe::Slot_Count] = {0};
    qint64 nanos[AiProbe::Slot_Count] = {0};
    QHash<QString, LimitEntry> bySkill;
    QHash<QString, LimitEntry> byCard;
};

thread_local ProbeState g_state;

const char *const kSlotNames[AiProbe::Slot_Count] = {
    "hasSkill", "getSkillList", "isSkillInvalid", "correctSkillValidity",
    "distanceTo", "correctDistance", "isCardLimited", "getDistanceSkills", "distIter", "distLegacy", "distV2", "limitOwnerFilter", "limitOwnerRebuild"
};

QString topOf(const QHash<QString, LimitEntry> &table, int limit)
{
    QList<QString> keys = table.keys();
    std::sort(keys.begin(), keys.end(), [&table](const QString &a, const QString &b) {
        return table.value(a).nanos > table.value(b).nanos;
    });
    QStringList parts;
    for (int i = 0; i < keys.size() && i < limit; ++i) {
        const LimitEntry &e = table.value(keys.at(i));
        parts << QString("%1(%2x,%3ms)").arg(keys.at(i)).arg(e.calls).arg(e.nanos / 1000000);
    }
    return parts.join(" ");
}

}

namespace AiProbe {

bool enabled()
{
    static const bool on = !qgetenv("QSAN_AI_PROBE").isEmpty()
                           && qgetenv("QSAN_AI_PROBE") != "0";
    return on;
}

int reportThresholdMs()
{
    static const int ms = qgetenv("QSAN_AI_PROBE_MS").isEmpty()
        ? 200 : qgetenv("QSAN_AI_PROBE_MS").toInt();
    return ms;
}

void reset()
{
    for (int i = 0; i < Slot_Count; ++i) {
        g_state.counts[i] = 0;
        g_state.nanos[i] = 0;
    }
    g_state.bySkill.clear();
    g_state.byCard.clear();
}

void bump(Slot slot)
{
    ++g_state.counts[slot];
}

void addNanos(Slot slot, qint64 nanos)
{
    g_state.nanos[slot] += nanos;
}

void recordLimitPattern(const QString &skillName, const QString &cardName, qint64 nanos)
{
    LimitEntry &s = g_state.bySkill[skillName];
    ++s.calls;
    s.nanos += nanos;
    if (!cardName.isEmpty()) {
        LimitEntry &c = g_state.byCard[cardName];
        ++c.calls;
        c.nanos += nanos;
    }
}

QString report()
{
    QStringList parts;
    for (int i = 0; i < Slot_Count; ++i) {
        if (g_state.nanos[i] > 0)
            parts << QString("%1=%2/%3ms").arg(kSlotNames[i])
                     .arg(g_state.counts[i]).arg(g_state.nanos[i] / 1000000);
        else
            parts << QString("%1=%2").arg(kSlotNames[i]).arg(g_state.counts[i]);
    }
    QString out = parts.join(" ");
    const QString skills = topOf(g_state.bySkill, 5);
    if (!skills.isEmpty()) out += " | limitPattern_top: " + skills;
    const QString cards = topOf(g_state.byCard, 5);
    if (!cards.isEmpty()) out += " | card_top: " + cards;
    return out;
}

}
