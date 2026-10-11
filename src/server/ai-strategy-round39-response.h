#ifndef AI_STRATEGY_ROUND39_RESPONSE_H
#define AI_STRATEGY_ROUND39_RESPONSE_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Finite source-timed reference/parse observations. No ticket or hidden lookup.
QJsonObject queryAIRound39GodYouhua(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);
bool aiRound39GodYouhuaConstruction(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);
#endif
