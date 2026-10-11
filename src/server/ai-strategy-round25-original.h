#ifndef AI_STRATEGY_ROUND25_ORIGINAL_H
#define AI_STRATEGY_ROUND25_ORIGINAL_H
#include <QJsonObject>
#include <functional>
class Room;class Card;class QVariant;class ServerPlayer;
struct AIRequest;struct AIWorldView;
int aiRound25OriginalPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound25OriginalConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAIRound25OriginalContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,const std::function<QJsonObject(const Card *)> &);
void projectAIRound25OriginalWorld(AIWorldView &,Room &,ServerPlayer *);
#endif
