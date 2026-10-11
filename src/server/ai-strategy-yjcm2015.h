#ifndef AI_STRATEGY_YJCM2015_H
#define AI_STRATEGY_YJCM2015_H
#include <QJsonObject>
#include <functional>
struct AIWorldView;
class Room;
class ServerPlayer;
class Card;
// Owner tags and already-disclosed borrowed card identity only. The callback
// serializes the owner's public declared use, never an undisclosed foreign hand.
void projectAIYjcm2015(AIWorldView &, Room &, const ServerPlayer *,
                      const std::function<QJsonObject(const Card *)> &publicDecisionCard);
#endif
