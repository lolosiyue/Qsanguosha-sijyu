#ifndef AI_STRATEGY_ROUND38_MANBING_H
#define AI_STRATEGY_ROUND38_MANBING_H
#include <QJsonObject>
#include <QString>
class Room;
class ServerPlayer;
struct AIRequest;
// Once per Room, before policies. Deliberately retained across AI VM rebuilds.
void initializeAIRound38Manbing(Room &);
// Existing legacy/fallback/external-policy entry invalidates tracked continuity.
void invalidateAIRound38Manbing(Room &);
// Optional exact native flag mutation hook, before its possible unchanged-native early return.
// Only manbingTo_<current-roster-name>; invalidates, never publishes a value.
void observeAIRound38ManbingFlag(Room &, const ServerPlayer *, const QString &);
// Host supplies the actual active request/viewer; optional write only sets true.
// No Player flags, gameplay revision, payment receipts or card authority change.
QJsonObject queryAIRound38Manbing(Room &, const AIRequest &, const ServerPlayer *,
                                 const QString &targetName, bool write);
#endif
