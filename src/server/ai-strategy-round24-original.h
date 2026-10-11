#ifndef AI_STRATEGY_ROUND24_ORIGINAL_H
#define AI_STRATEGY_ROUND24_ORIGINAL_H
#include <QJsonObject>
#include <functional>
class Room;
class Card;
class QVariant;
struct AIRequest;
class ServerPlayer;
// Finite Original HMiewu Play hypotheses only. Does not authorize payment.
int aiRound24OriginalPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound24OriginalConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAIRound24OriginalContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,const std::function<QJsonObject(const Card *)> &);
#endif
