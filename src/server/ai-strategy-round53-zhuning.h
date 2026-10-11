#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound53ZhuningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53ZhuningSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIRound53ZhuningSlots(Room&);
bool registerAIRound53ZhuningSources(Room&,lua_State*,QString*error=nullptr);
