#ifndef AI_STRATEGY_ROUND24_FEIDIY_H
#define AI_STRATEGY_ROUND24_FEIDIY_H
#include <QJsonObject>
#include <QString>
#include <functional>
struct AIRequest;
class Card;
class Room;
class ServerPlayer;
// The source key must come from the runtime-owned readonly recipe store.
bool aiRound24FeiDIYSource(const AIRequest &, const ServerPlayer *, const Card *,
    const QString &sourceKey, QString *error = nullptr);
// Exact constructor tuple and request admission; no payment rights.
int aiRound24FeiDIYExtraTarget(const AIRequest &, const ServerPlayer *, const QJsonObject &spec);
QJsonObject aiRound24FeiDIYTargetFilter(Room &, ServerPlayer *, const AIRequest &,
    const Card *, const QString &sourceKey, const QJsonObject &query);
// Only the two previously absent response prototypes. Existing seven Play
// prototypes remain in the audited construction catalogue.
int aiRound24FeiDIYPrototype(const AIRequest &, const ServerPlayer *, const QString &skill,
    const QString &prototype);
void projectAIRound24FeiDIYContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicCard);
#endif
