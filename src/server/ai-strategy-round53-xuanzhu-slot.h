#ifndef AI_STRATEGY_ROUND53_XUANZHU_SLOT_H
#define AI_STRATEGY_ROUND53_XUANZHU_SLOT_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <memory>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
bool registerAIRound53XuanzhuSlotSources(Room&,lua_State*,QString*error=nullptr);
void clearAIRound53XuanzhuSlots(Room&);
QString writeAIRound53XuanzhuSlot(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
struct AIRound53XuanzhuSlotRead{
 enum Kind{Unknown,NilPlan,BooleanPlan,TargetsError,Targets,CardError,Card};Kind kind=Unknown;
 QString error,name;QStringList targets;AIReadonlyCardLease lease;bool targetsNil=false,targetsBoolean=false,cardNil=false;
};
AIRound53XuanzhuSlotRead readAIRound53XuanzhuSlot(const AIRequest&,ServerPlayer*,bool includeCard);
std::unique_ptr<Card> constructAIRound53XuanzhuPolicy(const AIRequest&,ServerPlayer*,const QString&,QString*error=nullptr);
#endif
