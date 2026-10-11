#ifndef AI_STRATEGY_ROUND51_NOTIFIED_COSTS_H
#define AI_STRATEGY_ROUND51_NOTIFIED_COSTS_H
#include "structs.h"
#include <QJsonObject>
#include <functional>
class Room;class ServerPlayer;class Card;struct AIRequest;struct AICardSpec;
// Exact compiled Sushou question callsite. No Lua exposure; the lexical scope
// authenticates recipient/current foreign owner, order and ambient game depth.
const Card* askAIRound51Sushou(Room&,ServerPlayer*,ServerPlayer*,const QList<int>&,const QString&);
QJsonObject issueAIRound51NotifiedCosts(const AIRequest&,ServerPlayer*,const QString&,const std::function<bool(CardUseStruct&)>&);
bool hasAIRound51NotifiedCosts(const AIRequest&,int);
bool applyAIRound51NotifiedCosts(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
#endif
