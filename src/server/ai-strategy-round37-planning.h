#ifndef AI_STRATEGY_ROUND37_PLANNING_H
#define AI_STRATEGY_ROUND37_PLANNING_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
void initializeAIRound37CrossbowShadow(Room&);
void invalidateAIRound37CrossbowShadow(Room&);
QJsonObject readAIRound37CrossbowShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAIRound37CrossbowShadow(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&source);
#endif
