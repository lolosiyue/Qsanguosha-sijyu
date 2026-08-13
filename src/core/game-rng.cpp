#include "game-rng.h"

namespace {

thread_local GameRng *currentGameRng = nullptr;
thread_local QRandomGenerator fallbackRng;

}

GameRng::GameRng()
    : m_generator(QRandomGenerator::securelySeeded())
{
}

void GameRng::seed(quint32 seed)
{
    m_generator = QRandomGenerator(seed);
}

int GameRng::bounded(int upperExclusive)
{
    return upperExclusive > 0 ? m_generator.bounded(upperExclusive) : 0;
}

GameRng *GameRng::current()
{
    return currentGameRng;
}

void GameRng::setCurrent(GameRng *rng)
{
    currentGameRng = rng;
}

GameRng::Binding::Binding(GameRng &rng)
    : m_previous(currentGameRng)
{
    currentGameRng = &rng;
}

GameRng::Binding::~Binding()
{
    currentGameRng = m_previous;
}

int qsanRandomBounded(int upperExclusive)
{
    if (currentGameRng)
        return currentGameRng->bounded(upperExclusive);
    return upperExclusive > 0 ? fallbackRng.bounded(upperExclusive) : 0;
}

void qsanSeedRandom(quint32 seed)
{
    if (currentGameRng)
        currentGameRng->seed(seed);
    else
        fallbackRng = QRandomGenerator(seed);
}
