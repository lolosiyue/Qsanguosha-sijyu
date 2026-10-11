#ifndef AI_STRATEGY_ROUND53_PINGJIAN_H
#define AI_STRATEGY_ROUND53_PINGJIAN_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct AIWorldView;struct lua_State;
bool initializeAIRound53Pingjian(Room&,lua_State*);
void clearAIRound53Pingjian(Room&);
void projectAIRound53Pingjian(AIWorldView&,Room&,ServerPlayer*);
QJsonObject queryAIRound53PingjianCatalog(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
#endif
