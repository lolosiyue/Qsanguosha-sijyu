#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;class QVariant;struct AIRequest;struct lua_State;
void projectAIRound53Mingfa(AIRequest&,const QVariant&);
bool aiRound53MingfaConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53MingfaSlot(const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&,AIReadonlyCardLease lease={});
bool registerAIRound53MingfaSources(Room&,lua_State*,QString*error=nullptr);
void clearAIRound53MingfaSlots(Room&);
