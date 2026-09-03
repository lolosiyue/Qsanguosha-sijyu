#ifndef _SKILL_SET_GENERATION_H
#define _SKILL_SET_GENERATION_H

// 全域技能集合世代計數。任何 Player 的技能集合變動時遞增, 與 Player::skill_set_changed
// 訊號同步發出。用途是讓「全場擁有哪些技能」這類跨玩家的衍生快取知道自己過期了 ——
// 這種集合每回合只變動幾次, 但每次 AI 決策會被查詢數萬次。

#include <QAtomicInteger>

namespace SkillSet {

inline QAtomicInteger<quint64> &generationCounter()
{
    static QAtomicInteger<quint64> counter(1);
    return counter;
}

inline quint64 generation()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return generationCounter().loadRelaxed();
#else
    return generationCounter().load();
#endif
}

inline void bump()
{
    generationCounter().fetchAndAddRelaxed(1);
}

}

#endif
