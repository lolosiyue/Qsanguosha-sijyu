#ifndef AI_STRATEGY_ROUND42_RESPONSE_H
#define AI_STRATEGY_ROUND42_RESPONSE_H
#include <QString>
#include <QList>
#include <memory>
class Card;class Room;class ServerPlayer;struct AIRequest;
int aiRound42ResponsePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
std::unique_ptr<Card> aiRound42LimuEvaluation(const AIRequest &,Room &,ServerPlayer *,const QString &,const QList<int> &,QString *);
#endif
