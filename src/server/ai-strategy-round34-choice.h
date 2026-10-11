#ifndef AI_STRATEGY_ROUND34_CHOICE_H
#define AI_STRATEGY_ROUND34_CHOICE_H
#include <QList>
struct AIWorldView;
class Room;
class Player;
class ServerPlayer;
// Call only after the corresponding actual public fixed-distance notification.
void observeAIRound34FixedDistance(Room &, Player *, const Player *, int distance,
                                  bool set, const QList<ServerPlayer *> &notifiedPlayers);
void projectAIRound34Choice(AIWorldView &, Room &, const ServerPlayer *);
#endif
