#ifndef AI_STRATEGY_ROUND45_NATURAL_FILTER_H
#define AI_STRATEGY_ROUND45_NATURAL_FILTER_H
#include <QString>
class Room;struct lua_State;
// Compile-only source registration at the trusted bootstrap boundary.
bool registerAIRound45NaturalFilterSources(Room &,lua_State *,QString *reason=nullptr);
// Whole finite binding/effect graph, never an execution/branch trace. A missing
// native leaf, callback sibling, finalizer premise or family proof fails closed.
bool certifyAIRound45NaturalFilterGraph(Room &,lua_State *,int nargs,QString *reason=nullptr);
#endif
