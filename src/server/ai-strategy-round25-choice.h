#ifndef AI_STRATEGY_ROUND25_CHOICE_H
#define AI_STRATEGY_ROUND25_CHOICE_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
class Room; class ServerPlayer; class Card; struct AIRequest;
int aiRound25ChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &skill,const QString &prototype);
bool aiRound25ChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &spec);
void projectAIRound25ChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &context,
                                  const std::function<QJsonObject(const Card *)> &publicCard);
#endif
