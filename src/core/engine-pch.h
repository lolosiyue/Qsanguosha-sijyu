#ifndef ENGINE_PCH_H
#define ENGINE_PCH_H

#include <QtCore>
#include <QtNetwork>

#include <algorithm>
#include <memory>
#include <utility>

#if defined(QSAN_ENGINE_BUILD) || defined(QSAN_ENGINE_TEST_BUILD)
#include "game-rng.h"
#endif

#endif
