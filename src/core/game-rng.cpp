#include "game-rng.h"

namespace {

thread_local GameRng *currentGameRng = nullptr;
thread_local GameRng fallbackRng;

}

GameRng::GameRng()
    : m_generator(), m_seed(QRandomGenerator::system()->generate64()), m_drawCount(0)
{
    const quint32 seedWords[] = { quint32(m_seed), quint32(m_seed >> 32) };
    m_generator = QRandomGenerator(seedWords);
}

void GameRng::seed(quint64 seed)
{
    const quint32 seedWords[] = { quint32(seed), quint32(seed >> 32) };
    m_generator = QRandomGenerator(seedWords);
    m_seed = seed;
    m_drawCount = 0;
}

quint32 GameRng::generate()
{
    ++m_drawCount;
    return m_generator.generate();
}

int GameRng::bounded(int upperExclusive)
{
    if (upperExclusive <= 0)
        return 0;

    // Count each raw generator draw. Rejection sampling makes discard(drawCount)
    // reproduce the same state even when a bound is not a power of two.
    const quint32 bound = static_cast<quint32>(upperExclusive);
    const quint32 threshold = static_cast<quint32>(-bound) % bound;
    quint32 value;
    do {
        value = generate();
    } while (value < threshold);
    return static_cast<int>(value % bound);
}

GameRng::State GameRng::exportState() const
{
    State state;
    state.seed = m_seed;
    state.drawCount = m_drawCount;
    state.algorithm = AlgorithmQsanRejectionV1;
    return state;
}

bool GameRng::restoreState(const State &state, QString *error)
{
    if (state.algorithm != AlgorithmQsanRejectionV1) {
        if (error)
            *error = QStringLiteral("unsupported game RNG algorithm");
        return false;
    }

    const quint32 seedWords[] = { quint32(state.seed), quint32(state.seed >> 32) };
    QRandomGenerator restored(seedWords);
    restored.discard(state.drawCount);
    m_generator = restored;
    m_seed = state.seed;
    m_drawCount = state.drawCount;
    return true;
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
    return fallbackRng.bounded(upperExclusive);
}

quint32 qsanRandomGenerate()
{
    return currentGameRng ? currentGameRng->generate() : fallbackRng.generate();
}

void qsanSeedRandom(quint64 seed)
{
    if (currentGameRng)
        currentGameRng->seed(seed);
    else
        fallbackRng.seed(seed);
}
