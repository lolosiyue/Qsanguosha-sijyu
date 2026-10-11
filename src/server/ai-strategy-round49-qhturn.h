#ifndef AI_STRATEGY_ROUND49_QHTURN_H
#define AI_STRATEGY_ROUND49_QHTURN_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
bool aiRound49QhTurnConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif
