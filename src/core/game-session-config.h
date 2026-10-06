#ifndef QSAN_GAME_SESSION_CONFIG_H
#define QSAN_GAME_SESSION_CONFIG_H

#include <QRandomGenerator>
#include <QSharedPointer>
#include <QString>
#include <QUuid>

namespace ScenarioWork { struct WorkLaunch; }

inline bool useJevHybrid50(const QString &mode, bool aiEnabled,
                           bool selected, bool takeover = false)
{
    return mode == QStringLiteral("50p") && aiEnabled && selected && !takeover;
}

struct GameSessionConfig
{
    GameSessionConfig()
        : seed(QRandomGenerator::system()->generate64()),
          hybridGameId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
    }

    explicit GameSessionConfig(quint64 seed)
        : seed(seed),
          hybridGameId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
    }

    quint64 seed;
    // Immutable room identity; never generated in an adapter or on reconnect.
    QString hybridGameId;
    bool takeover = false;
    QString takeoverSnapshotPath;
    QString takeoverSeatName;
    // Immutable work launch owned by this room; never stored in global Config.
    QSharedPointer<const ScenarioWork::WorkLaunch> workLaunch;
};

#endif
