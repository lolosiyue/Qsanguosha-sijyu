#ifndef AI_STRATEGY_ROUND22_CHOICE_H
#define AI_STRATEGY_ROUND22_CHOICE_H
#include <QJsonObject>
#include <QString>
#include <functional>
struct AIRequest;class Room;class ServerPlayer;class Card;
int aiRound22ChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
void projectAIRound22ChoiceContext(AIRequest &,Room &,ServerPlayer *,QJsonObject &,
    const std::function<QJsonObject(const Card *)> &);
#endif
