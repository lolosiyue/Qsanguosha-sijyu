#ifndef ENGINE_PCH_H
#define ENGINE_PCH_H

#include <QtCore>
#include <QtNetwork>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

#ifdef QSAN_ENGINE_BUILD
inline QRandomGenerator &qsanRng()
{
    static QRandomGenerator generator(*QRandomGenerator::system());
    return generator;
}

inline int qrand()
{
    return int(qsanRng().bounded(uint(RAND_MAX) + 1u));
}

inline void qsrand(uint seed)
{
    qsanRng().seed(seed);
}
#endif

#endif
