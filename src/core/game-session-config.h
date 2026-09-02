#ifndef QSAN_GAME_SESSION_CONFIG_H
#define QSAN_GAME_SESSION_CONFIG_H

#include <QRandomGenerator>
#include <QString>

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
    bool takeover = false;
    QString takeoverSnapshotPath;
    QString takeoverSeatName;
};

#endif
