#ifndef AI_STRATEGY_ROUND36_PLANNING_H
#define AI_STRATEGY_ROUND36_PLANNING_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;
void initializeAIRound36KurouShadow(Room&);
void invalidateAIRound36KurouShadow(Room&);
QJsonObject readAIRound36KurouShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAIRound36KurouShadow(Room&,const AIRequest&,const ServerPlayer*,bool);
int aiRound36KurouPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);
#endif
