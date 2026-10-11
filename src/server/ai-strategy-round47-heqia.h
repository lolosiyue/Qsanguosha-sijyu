#ifndef AI_STRATEGY_ROUND47_HEQIA_H
#define AI_STRATEGY_ROUND47_HEQIA_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <QList>
#include "structs.h"
#include <functional>
class Room;class ServerPlayer;class Card;struct AIRequest;struct lua_State;
// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAIRound45SlotCard(lua_State*,const Card*);
bool matchAIRound45SlotCard(lua_State*,int,const Card*);
bool pushAIRound45SlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAIRound45SlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAIRound47HeqiaSlot(const AIRequest&,ServerPlayer*,int,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AIRound47HeqiaSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAIRound47HeqiaSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,int*offered,QString*error=nullptr,AIRound47HeqiaSlotStatus*status=nullptr);
bool hasAIRound47HeqiaSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAIRound47HeqiaSlots(Room&);
bool registerAIRound47HeqiaSources(Room&,lua_State*,QString*error=nullptr);
std::unique_ptr<Card> constructAIRound47Heqia(const AIRequest&,ServerPlayer*,int,int,QString*error=nullptr);
struct AICardSpec;
QJsonObject issueAIRound47HeqiaReceipt(const AIRequest&,ServerPlayer*,const QString&,const QStringList&,const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIRound47HeqiaFirst(const AIRequest&,ServerPlayer*,const QList<int>&,const QStringList&,const std::function<bool(CardUseStruct&)>&);
bool hasAIRound47HeqiaReceipt(const AIRequest&,int);
bool applyAIRound47HeqiaReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
#endif
