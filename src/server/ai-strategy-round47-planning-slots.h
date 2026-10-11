#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
QJsonObject queryAIRound47OlClanSlot(const AIRequest&,ServerPlayer*,const QString&slot,const QString&operation,const QJsonObject&payload,AIReadonlyCardLease lease={});
void clearAIRound47OlClanSlots(Room&);
bool registerAIRound47OlClanSlotSources(Room&,lua_State*,QString*error=nullptr);
