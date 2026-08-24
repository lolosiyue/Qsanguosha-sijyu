#ifndef ENGINE_PCH_H
#define ENGINE_PCH_H

#include <QtCore>
#include <QtNetwork>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <memory>
#include <utility>

#ifdef QSAN_ENGINE_BUILD
#include "game-rng.h"

inline int qrand()
{
#if RAND_MAX == INT_MAX
    return qsanRandomBounded(RAND_MAX);
#else
    return qsanRandomBounded(RAND_MAX + 1);
#endif
}

inline void qsrand(uint seed)
{
    qsanSeedRandom(seed);
}
#endif

#endif
