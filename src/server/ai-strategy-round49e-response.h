#ifndef AI_STRATEGY_ROUND49E_RESPONSE_H
#define AI_STRATEGY_ROUND49E_RESPONSE_H
#include <QJsonObject>
struct AIRequest;class ServerPlayer;
bool aiRound49ePanqinEvaluation(const AIRequest &,const ServerPlayer *,const QJsonObject &);
#endif
