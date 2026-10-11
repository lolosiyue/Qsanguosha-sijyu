#ifndef AI_STRATEGY_ROUND45_FAQI_SLOT_H
#define AI_STRATEGY_ROUND45_FAQI_SLOT_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <QList>
class Room;class ServerPlayer;class Card;struct AIRequest;struct lua_State;
// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAIRound45SlotCard(lua_State*,const Card*);
bool matchAIRound45SlotCard(lua_State*,int,const Card*);
bool pushAIRound45SlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAIRound45SlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAIRound45FaqiSlot(const AIRequest&,ServerPlayer*,int,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AIRound45FaqiSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAIRound45FaqiSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,int*offered,QString*error=nullptr,AIRound45FaqiSlotStatus*status=nullptr);
bool hasAIRound45FaqiSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAIRound45FaqiSlots(Room&);
#endif
