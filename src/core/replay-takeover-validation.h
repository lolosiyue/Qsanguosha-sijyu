#ifndef QSAN_REPLAY_TAKEOVER_VALIDATION_H
#define QSAN_REPLAY_TAKEOVER_VALIDATION_H
#include <QString>
bool validateReplayTakeover(const QString &replayPath, const QString &snapshotPath,
                           const QString &seatName, QString *error);
#endif
