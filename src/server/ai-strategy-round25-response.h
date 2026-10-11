#ifndef AI_STRATEGY_ROUND25_RESPONSE_H
#define AI_STRATEGY_ROUND25_RESPONSE_H
#include <QJsonObject>
class Room; class ServerPlayer; struct AIRequest;
void projectAIStrategyRound25Response(AIRequest &request,Room &room,ServerPlayer *viewer,QJsonObject &context);
// Bit 0: paid prototype; bit 1: declared prototype. -1: not audited.
int aiRound25ResponsePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound25ResponseConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
#endif
