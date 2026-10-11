#ifndef AI_STRATEGY_ROUND26_CHOICE_H
#define AI_STRATEGY_ROUND26_CHOICE_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
class Room;class ServerPlayer;class Card;struct AIRequest;struct AIResult;
int aiRound26ChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &skill,const QString &prototype);
bool aiRound26ChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &spec);
void projectAIRound26ChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &context,
 const std::function<QJsonObject(const Card *)> &publicCard);
bool stageAIRound26ChoiceFlags(Room &,const AIRequest &,const AIResult &,bool commit,QString *error);
// Invoke only after the actual public flag broadcast.
void observeAIRound26ChoiceFlag(Room &,ServerPlayer *,const QString &flag);
#endif
