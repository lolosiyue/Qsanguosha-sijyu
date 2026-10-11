#ifndef AI_STRATEGY_ROUND27_CHOICE_H
#define AI_STRATEGY_ROUND27_CHOICE_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
class Room; class ServerPlayer; class Card; struct AIRequest;
bool aiRound27ChoicePlanningQuestion(const AIRequest &,Room &,const ServerPlayer *);
QJsonObject aiRound27ChoicePlanningContext(const AIRequest &,Room &,const ServerPlayer *);
int aiRound27ChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound27ChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAIRound27ChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
#endif
