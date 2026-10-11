#ifndef AI_STRATEGY_ROUND36_CHOICE_H
#define AI_STRATEGY_ROUND36_CHOICE_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
QJsonObject readAIRound36GuanxingShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAIRound36GuanxingShadow(Room&,const AIRequest&,const ServerPlayer*,bool);
void invalidateAIRound36GuanxingShadow(Room&);
#endif
