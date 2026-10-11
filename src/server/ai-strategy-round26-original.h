#ifndef AI_STRATEGY_ROUND26_ORIGINAL_H
#define AI_STRATEGY_ROUND26_ORIGINAL_H
#include <QJsonObject>
#include <functional>
class AIRequest; struct AIWorldView; class Room; class ServerPlayer; class Card; class QVariant;
int aiRound26OriginalPrototype(const AIRequest &, const ServerPlayer *, const QString &skill, const QString &prototype);
bool aiRound26OriginalConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &spec);
void projectAIRound26OriginalContext(AIRequest &, Room &, ServerPlayer *, const QVariant &, const std::function<QJsonObject(const Card *)> &publicCard);
void projectAIRound26OriginalWorld(AIWorldView &, Room &, ServerPlayer *);
#endif
