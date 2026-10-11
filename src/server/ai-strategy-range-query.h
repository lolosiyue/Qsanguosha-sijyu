#ifndef AI_STRATEGY_RANGE_QUERY_H
#define AI_STRATEGY_RANGE_QUERY_H

#include "ai-owner-range-query-scope.h"
#include <QJsonObject>

class Room;
class ServerPlayer;
struct AIRequest;

// This authenticates a read-only hypothesis, never a skill action/payment.
bool validateAIStrategyOwnerRange(Room &, ServerPlayer *, const AIRequest &,
                                 const QString &flag, const QString &sourceKey,
                                 AIOwnerRangeQueryScope::Flag &kind);
// Caller must hold the authenticated scope. Only owner geometry is disclosed.
QJsonObject projectAIStrategyOwnerRange(Room &, ServerPlayer *, const QString &flag,
                                      const QString &sourceKey);

#endif
