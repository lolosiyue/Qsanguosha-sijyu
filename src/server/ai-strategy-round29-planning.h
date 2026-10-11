#ifndef AI_STRATEGY_ROUND29_PLANNING_H
#define AI_STRATEGY_ROUND29_PLANNING_H
#include <QString>
class Room;
class ServerPlayer;
struct AIRequest;
// Host must supply its current active decision/event request, never a Lua-built
// request. Only the answering owner's fixed valuation recursion bit is exposed.
bool readAIRound29UseValueFlag(Room &, const ServerPlayer *, const AIRequest &, bool &value, QString *error = nullptr);
bool writeAIRound29UseValueFlag(Room &, const ServerPlayer *, const AIRequest &, bool value, QString *error = nullptr);
// Call for exact +/-stack_overflow_UseValue commands before the service's
// absent-removal early return. This records the completed intent even when only
// an isolated shadow exists. Generic '.'/bury resets are tracked by clear epoch.
void synchronizeAIRound29UseValueFlag(Room &, const ServerPlayer *, const QString &command);
#endif
