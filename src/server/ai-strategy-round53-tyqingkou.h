#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound53TyqingkouConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53TyqingkouSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIRound53TyqingkouSlots(Room&);
bool registerAIRound53TyqingkouSources(Room&,lua_State*,QString*error=nullptr);
