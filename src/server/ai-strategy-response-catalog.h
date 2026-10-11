#ifndef AI_STRATEGY_RESPONSE_CATALOG_H
#define AI_STRATEGY_RESPONSE_CATALOG_H
#include <QJsonObject>
class Room;
class ServerPlayer;
struct AIRequest;
// Executes only the three audited public-catalog calls at source call time.
// It consumes the same gameplay shuffle as the donor; it never reads live piles.
QJsonObject resolveAIResponseCatalogue(Room &, ServerPlayer *, const AIRequest &, const QJsonObject &);
#endif
