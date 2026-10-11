#ifndef AI_STRATEGY_ROUND28_COMMON_H
#define AI_STRATEGY_ROUND28_COMMON_H
#include <QJsonValue>
#include <QString>
class Room;
class ServerPlayer;
// Invoke only after a successful broadcastProperty. `value` is the exact
// optional override passed to that broadcast (empty means native property).
void recordAIRound28PublicGeneralSlot(Room &room, ServerPlayer *player,
                                     const char *property, const QString &value);
// Undefined means no lawful proof. Never reads a foreign actual_general2.
QJsonValue aiRound28ShownAllGenerals(Room &room, ServerPlayer *viewer,
                                   ServerPlayer *player);
#endif
