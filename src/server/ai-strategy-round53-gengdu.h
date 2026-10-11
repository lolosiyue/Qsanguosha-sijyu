#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound53GengduConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53GengduSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIRound53GengduSlots(Room&);
bool registerAIRound53GengduSources(Room&,lua_State*,QString*error=nullptr);
