#ifndef AI_STRATEGY_ROUND23_QIUXIN_H
#define AI_STRATEGY_ROUND23_QIUXIN_H
#include <QJsonObject>
#include <QString>
class Card;
class Room;
class ServerPlayer;
struct AIRequest;
// sourceKey MUST come from the runtime-owned readonly store for this exact
// card/request, never a Lua field. This grants no action/payment capability.
bool aiRound23QiuxinSource(const AIRequest &, const ServerPlayer *, const Card *,
                          const QString &sourceKey, QString *error = nullptr);
// Exact source targetFilter query on the retained original product. The caller
// holds normal game Lua/engine bindings; an outer owner-range scope is retained.
// Returns an error object for missing capability/input; never a guessed false.
QJsonObject aiRound23QiuxinTargetFilter(Room &, ServerPlayer *, const AIRequest &,
                                     const Card *, const QString &sourceKey,
                                     const QJsonObject &query);
#endif
