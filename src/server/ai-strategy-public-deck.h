#ifndef AI_STRATEGY_ROUND39_PUBLIC_DECK_H
#define AI_STRATEGY_ROUND39_PUBLIC_DECK_H
#include <QJsonObject>
class Room;
class ServerPlayer;
struct AIRequest;
// Two source call sites only. No caller supplied card IDs, filter, or seed.
// Shuffles the configured printed deck once on the gameplay RNG at call time.
QJsonObject resolveAIRound39PublicDeck(Room &, ServerPlayer *, const AIRequest &, const QJsonObject &);
#endif
