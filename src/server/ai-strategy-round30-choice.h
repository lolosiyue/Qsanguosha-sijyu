#ifndef AI_STRATEGY_ROUND30_CHOICE_H
#define AI_STRATEGY_ROUND30_CHOICE_H
#include <QJsonObject>
class Room;
class ServerPlayer;
struct AIWorldView;
struct AIRequest;
// Only already-disclosed general identities (or the viewer's own identities)
// authorize looking up the exact General description used by donor policy.
void projectAIRound30TargetRecommend(AIWorldView &, Room &, const ServerPlayer *);
// Capture only after configured scripts and Original overrides, before policy.
void captureAIRound30TargetCache(Room &);
// Exact source filterEvent/invalidate_qlist_cache boundary, not every recordEvent.
void resetAIRound30TargetCache(Room &);
// Legacy/external policy can mutate a VM-global cache without lawful provenance.
void invalidateAIRound30TargetCache(Room &);
QJsonObject queryAIRound30TargetCache(Room &, const AIRequest &, ServerPlayer *, const QJsonObject &);
#endif
