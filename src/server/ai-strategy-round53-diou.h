#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound53DiouConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53DiouSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIRound53DiouSlots(Room&);
bool registerAIRound53DiouSources(Room&,lua_State*,QString*error=nullptr);

#include <functional>
#include <QStringList>
struct CardUseStruct;struct AICardSpec;
QJsonObject issueAIRound53DiouReceipt(const AIRequest&,ServerPlayer*,const QJsonObject&,const std::function<bool(CardUseStruct&)>&);
bool hasAIRound53DiouReceipt(const AIRequest&,int);
bool applyAIRound53DiouReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
