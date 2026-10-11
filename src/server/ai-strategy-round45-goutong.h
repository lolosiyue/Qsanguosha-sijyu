#pragma once
#include <QJsonObject>
#include <QVariant>
class Player;class Room;class ServerPlayer;class LuaRuntime;struct AIRequest;struct lua_State;struct CardsMoveOneTimeStruct;
void projectAIRound45Goutong(AIRequest &,Room &,ServerPlayer *,const QVariant &);
QJsonObject writeAIRound45GoutongMove(Room &,const AIRequest &,ServerPlayer *);
QJsonObject readAIRound45GoutongMove(Room &,const AIRequest &,ServerPlayer *);
void clearAIRound45Goutong(Room &);
// Game-VM-only exact native copy; never registered as a Lua API.
bool pushAIRound45GoutongMove(lua_State *,const CardsMoveOneTimeStruct &);
bool readAIRound45GoutongEndpoints(lua_State *,int,Player *&,Player *&);
void clearAIRound45GoutongBindings(LuaRuntime &);
