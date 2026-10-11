#ifndef AI_STRATEGY_ROUND23_CHOICE_H
#define AI_STRATEGY_ROUND23_CHOICE_H
#include <QString>
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest;
class ServerPlayer;
class Room;
class Card;
// Prototype catalogue admission only. Real scope, ordered materials and final
// provider feasibility remain checked by the existing sealed native binder.
int aiRound23ChoicePrototype(const AIRequest &, const ServerPlayer *,
                             const QString &skill, const QString &prototype);
bool aiRound23ChoiceConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
void projectAIRound23ChoiceContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);
#endif
