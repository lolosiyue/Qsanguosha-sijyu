#ifndef AI_STRATEGY_ROUND56_CONNECTION_CACHE_H
#define AI_STRATEGY_ROUND56_CONNECTION_CACHE_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;struct lua_TableObservation;
// Exact ordinary Choulve planner source cache operations. Not a snapshot seed.
QJsonObject queryAIRound56ConnectionCache(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
bool acceptAIRound56ConnectionEntry(Room&,lua_State*,int) noexcept;
bool allowAIRound56ConnectionCacheWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
void clearAIRound56ConnectionCache(Room&) noexcept;
#endif
