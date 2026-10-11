#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;class Card;struct AIRequest;
// Three fixed own game-VM scratch slots. The retained Card is observation only.
// Result contains current native serialization/ordered public target names;
// it never contains an action receipt or a replacement Card allocation.
QJsonObject queryAIRound46PlanningSlot(const AIRequest&,ServerPlayer*,const QString&slot,
 const QString&operation,const QJsonObject&payload,AIReadonlyCardLease lease={});
void clearAIRound46PlanningSlots(Room&);
// Exact attached view-as provider; paid/declaration bits do not grant payment.
int aiRound46PlanningPrototype(const AIRequest&,ServerPlayer*,const QString&,const QString&);

const Card*aiRound46PlanningGodMaterial(const AIRequest&,ServerPlayer*,int id);

struct lua_State;
bool registerAIRound46PlanningSlotSources(Room&,lua_State*,QString*error=nullptr);
