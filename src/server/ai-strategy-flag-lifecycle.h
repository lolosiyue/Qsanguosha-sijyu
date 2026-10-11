#ifndef AI_STRATEGY_FLAG_LIFECYCLE_H
#define AI_STRATEGY_FLAG_LIFECYCLE_H

struct AIWorldView;
class Room;
class ServerPlayer;

// Call only after the real Player::setFlags(".") has completed. This records
// even an empty native flag set: viewer-local strategy flags can still exist.
void recordAIStrategyFlagsCleared(Room &room, const ServerPlayer *player);
// Actual completed GameRule -PlayClear operation, including empty mark sets.
void recordAIStrategyPlayMarksCleared(Room &room, const ServerPlayer *player);
void recordAIStrategyTurnMarksCleared(Room &room, const ServerPlayer *player);
void recordAIStrategyAllMarksCleared(Room &room, const ServerPlayer *player);
// Exact native Lol2015 clear command also retires an isolated-only shadow.
void recordAIStrategyLolWEmptyCleared(Room &room, const ServerPlayer *player);
// Fresh Room ledger projection; do not cache it with the public board.
void projectAIStrategyFlagLifecycle(AIWorldView &world, Room &room);

#endif
