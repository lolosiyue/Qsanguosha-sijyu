#ifndef AI_STRATEGY_ROUND49_EXTRA_RESPONSE_H
#define AI_STRATEGY_ROUND49_EXTRA_RESPONSE_H
#include <QJsonObject>
struct AIRequest; class ServerPlayer; class Room;
void projectAIRound49HTMS(AIRequest &,Room &,ServerPlayer *);
bool aiRound49HTMSConstruction(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &);
#endif
