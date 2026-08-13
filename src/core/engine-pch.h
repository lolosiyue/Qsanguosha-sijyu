#ifndef ENGINE_PCH_H
#define ENGINE_PCH_H

#include <QtCore>
#include <QtNetwork>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

#ifdef QSAN_ENGINE_BUILD
#include "game-rng.h"

inline int qrand()
{
    return qsanRandomBounded(RAND_MAX + 1);
}

inline void qsrand(uint seed)
{
    qsanSeedRandom(seed);
}
#endif

#endif
