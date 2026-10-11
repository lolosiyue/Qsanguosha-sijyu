#pragma once
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool aiRound53TyfenwuConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIRound53TyfenwuSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIRound53TyfenwuSlots(Room&);
bool registerAIRound53TyfenwuSources(Room&,lua_State*,QString*error=nullptr);

class Card;
bool aiRound53TyfenwuRawProduct(const AIRequest&,ServerPlayer*,const QString&,const Card*);
