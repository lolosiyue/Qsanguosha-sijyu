#ifndef QSAN_GAME_SESSION_CONFIG_H
#define QSAN_GAME_SESSION_CONFIG_H

#include <QRandomGenerator>

struct GameSessionConfig
{
    GameSessionConfig()
        : seed(QRandomGenerator::system()->generate64())
    {
    }

    explicit GameSessionConfig(quint64 seed)
        : seed(seed)
    {
    }

    quint64 seed;
};

#endif
