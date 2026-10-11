#ifndef AI_STRATEGY_ROUND22_OLCLAN_OBSERVATIONS_H
#define AI_STRATEGY_ROUND22_OLCLAN_OBSERVATIONS_H
#include <QJsonObject>
#include <QVariant>
#include <QString>
#include <functional>
struct AIRequest;
class Room;
class ServerPlayer;
class Card;
// Exact fixed olClan questions only; an owner tag never grants a hidden face.
void projectAIStrategyRound22OlClanObservations(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
// Source prototype metadata only; issued scope and final native payment remain required.
int aiRound22OlClanObservationPrototype(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QString &prototype);
#endif
