#ifndef AI_STRATEGY_ROUND44_ROUTES_H
#define AI_STRATEGY_ROUND44_ROUTES_H
#include <QString>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound44RangeRoute(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,lua_State*,int);
// Pure finite admission predicate; grants no mutation or query authority by itself.
bool aiRound44RangeQuestion(const AIRequest&,const ServerPlayer*,const QString&);
#endif
