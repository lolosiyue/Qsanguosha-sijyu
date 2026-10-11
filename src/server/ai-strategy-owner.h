#ifndef AI_STRATEGY_OWNER_H
#define AI_STRATEGY_OWNER_H

struct AIWorldView;
class QJsonObject;
class QString;
class Room;
class ServerPlayer;

// Pure-value strategy metadata. The caller establishes the room's Engine/Lua
// runtime binding; private facts are emitted only for world.self's own viewer.
void projectAIStrategyOwner(AIWorldView &, Room &, ServerPlayer *viewer);
void projectAIStrategySkills(AIWorldView &, Room &, ServerPlayer *viewer);

// Decision-only, bounded hypothetical Slash descriptors; never action tickets.
void projectAIStrategySlashProbes(AIWorldView &world, Room &room, ServerPlayer *viewer);

// Branch-time public-deck shuffle RNG consumption; dispatcher authorizes scope.
QJsonObject consumeAIStrategyDeckCountShuffle(Room &, ServerPlayer *viewer);

// The two fixed chat patterns only; returns known-none with found=false.
QJsonObject sampleAIStrategyPatternLogName(Room &, ServerPlayer *, const QString &pattern, bool derivative = false);

#endif
