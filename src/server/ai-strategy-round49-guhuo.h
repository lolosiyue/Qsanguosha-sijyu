#ifndef AI_STRATEGY_ROUND49_GUHUO_H
#define AI_STRATEGY_ROUND49_GUHUO_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
void projectAIRound49Guhuo(AIRequest&,Room&,ServerPlayer*);
bool aiRound49GuhuoConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif
