#ifndef AI_STRATEGY_ROUND44_RESPONSE_H
#define AI_STRATEGY_ROUND44_RESPONSE_H
#include "ai.h"
class Room; class ServerPlayer;
// Called only after the actual recipient's notification has been sent.
void recordAIRound44PoxiNotification(Room &,ServerPlayer *,const QList<int>&,
    const QString &reason,bool active);
void projectAIRound44LeiExchange(AIRequest &,Room &,ServerPlayer *);
int aiRound44LeiPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool authorizeAIRound44PoxiEvaluation(const AIRequest &,int cardId,QJsonObject *face=nullptr);
bool authorizeAIRound44LeiPayment(const AIRequest &,Room &,ServerPlayer *,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,const QList<int>&,
    QList<int> *foreignIds=nullptr);
#endif
