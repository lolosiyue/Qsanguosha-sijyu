#ifndef AI_STRATEGY_ROUND37_CHOICE_H
#define AI_STRATEGY_ROUND37_CHOICE_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;struct AIResult;
void captureAIRound37SD(Room &);
void beginAIRound37SD(Room &,const QString &viewer);
void invalidateAIRound37SD(Room &);
void invalidateAIRound37SDCard(Room &,int cardId);
QJsonObject queryAIRound37SD(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &);
bool resolveAIRound37SD(Room &,ServerPlayer *,const AIRequest &,AIResult &);
#endif
