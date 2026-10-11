#ifndef AI_STRATEGY_ROUND27_ORIGINAL_H
#define AI_STRATEGY_ROUND27_ORIGINAL_H
#include <QString>
class AIRequest;
struct AIWorldView;
struct AIEventView;
class QVariant;
class Room;
class ServerPlayer;
int aiRound27OriginalPrototype(const AIRequest &, const ServerPlayer *, const QString &skill, const QString &prototype);
void projectAIRound27OriginalWorld(AIWorldView &, Room &, ServerPlayer *);
void recordAIRound27OriginalEvent(const AIWorldView &, Room &, ServerPlayer *viewer, ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);
void clearAIRound27OriginalViews(Room &);
#endif
