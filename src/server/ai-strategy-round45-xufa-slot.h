#ifndef AI_STRATEGY_ROUND45_XUFA_SLOT_H
#define AI_STRATEGY_ROUND45_XUFA_SLOT_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <QList>
#include <memory>
class Room;class ServerPlayer;class Card;struct AIRequest;
struct lua_State;
bool registerAIRound45XufaSlotSources(Room&,lua_State*,QString*error=nullptr);
QString writeAIRound45XufaSlot(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const QStringList&,const QJsonObject&,QString*error=nullptr);
struct AIRound45XufaSlotRead {
 enum Kind {Unknown,NilPlan,FalsePlan,EmptyCard,CardPlan};Kind kind=Unknown;
 AIReadonlyCardLease lease;QStringList targets;QString error;bool targetsMissing=false,targetsFalse=false;
};
AIRound45XufaSlotRead readAIRound45XufaSlot(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIRound45XufaPolicy(const AIRequest&,ServerPlayer*,
 const QString&,const QList<int>&,QString*error=nullptr);
void clearAIRound45XufaSlots(Room&);
#endif
