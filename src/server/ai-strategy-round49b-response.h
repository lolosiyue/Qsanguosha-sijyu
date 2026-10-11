#ifndef AI_STRATEGY_ROUND49B_RESPONSE_H
#define AI_STRATEGY_ROUND49B_RESPONSE_H
#include <QJsonObject>
struct AIRequest;
class ServerPlayer;
bool aiRound49bResponseConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
#endif
