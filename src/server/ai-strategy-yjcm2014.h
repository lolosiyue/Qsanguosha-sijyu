#ifndef AI_STRATEGY_YJCM2014_H
#define AI_STRATEGY_YJCM2014_H

#include <QJsonObject>
#include <functional>

struct AIWorldView;
class Card;
class Room;
class ServerPlayer;

// Owner-only named strategy reads, plus the public presence of a player AI.
// The descriptor callback must expose only the viewer's active use card.
void projectAIYjcm2014(AIWorldView &, Room &, const ServerPlayer *,
                      const std::function<QJsonObject(const Card *)> &);

#endif
