#ifndef AI_STRATEGY_ROUND43_CHOICE_H
#define AI_STRATEGY_ROUND43_CHOICE_H
#include <QJsonObject>
#include <QString>
struct AIRequest;
class ServerPlayer;
int aiRound43ChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound43ChoiceDeclaration(const AIRequest &,const ServerPlayer *,const QJsonObject &);
bool aiRound43ChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
#endif
