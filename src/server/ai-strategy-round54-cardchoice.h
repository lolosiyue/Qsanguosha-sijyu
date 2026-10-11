#ifndef AI_STRATEGY_ROUND54_CARDCHOICE_H
#define AI_STRATEGY_ROUND54_CARDCHOICE_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool registerAIRound54CardChoiceSource(Room&,lua_State*);
QJsonObject queryAIRound54CardChoicePrefix(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
#endif
