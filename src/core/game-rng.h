#ifndef QSAN_GAME_RNG_H
#define QSAN_GAME_RNG_H

#include <QRandomGenerator>

class GameRng
{
public:
    GameRng();

    void seed(quint64 seed);
    int bounded(int upperExclusive);

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
    QRandomGenerator m_generator;
};

int qsanRandomBounded(int upperExclusive);
void qsanSeedRandom(quint64 seed);

#endif
