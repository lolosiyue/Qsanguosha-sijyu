#ifndef QSAN_GAME_RNG_H
#define QSAN_GAME_RNG_H

#include <QRandomGenerator>

#include <QString>
#include <memory>

class GameRng
{
public:
    // QRandomGenerator raw stream plus QSan's counted rejection sampler.
    static constexpr quint32 AlgorithmQsanRejectionV1 = 1;

    // State is deliberately independent of QRandomGenerator's private layout.
    struct State
    {
        quint64 seed = 0;
        quint64 drawCount = 0;
        quint32 algorithm = AlgorithmQsanRejectionV1;

        bool isValid() const { return algorithm == AlgorithmQsanRejectionV1; }
    };

    GameRng();
    GameRng(const GameRng &) = delete;
    GameRng &operator=(const GameRng &) = delete;
    GameRng(GameRng &&) = delete;
    GameRng &operator=(GameRng &&) = delete;

    void seed(quint64 seed);
    quint32 generate();
    int bounded(int upperExclusive);

    State exportState() const;
    bool restoreState(const State &state, QString *error = nullptr);

    // Exchanges the already prepared deterministic stream without replacing
    // this GameRng object (other runtime components may hold its address).
    // The caller must serialize access and prepare the target stream first.
    void swapState(GameRng &prepared) noexcept;

    // Short aliases keep callers from depending on the serialization name.
    State state() const { return exportState(); }
    bool restore(const State &state, QString *error = nullptr)
    {
        return restoreState(state, error);
    }

    static GameRng *current();
    static void setCurrent(GameRng *rng);

    class Binding
    {
    public:
        explicit Binding(GameRng &rng);
        ~Binding();

        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;

    private:
        GameRng *m_previous;
    };

private:
    // Keep the public GameRng object's address stable while allowing an
    // allocation-free pointer swap at a coordinated world publication point.
    std::unique_ptr<QRandomGenerator> m_generator;
    quint64 m_seed;
    quint64 m_drawCount;
};

int qsanRandomBounded(int upperExclusive);
quint32 qsanRandomGenerate();
void qsanSeedRandom(quint64 seed);

#endif
