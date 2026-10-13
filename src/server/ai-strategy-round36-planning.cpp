#include "ai-strategy-round36-planning.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai-strategy-round27-choice.h"
#include "ai-strategy-round28-choice.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>
namespace {
struct Row{qint64 epoch=0;bool value=false;bool known=false;};
class Shadow final:public AIRoomLedger{public:explicit Shadow(Room&r):AIRoomLedger(r){}bool initialized=false;bool baselineKnown=true;QHash<const ServerPlayer*,Row> owners;};
Shadow*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Shadow*>(c))return l;return new Shadow(r);}
QJsonObject fail(const char*why){return{{"kind","original_kurou_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool valid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))&&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiRound35OriginalQuestion(q);}
qint64 epoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
Row&row(Room&r,const ServerPlayer*p){auto*l=ledger(r);const auto e=epoch(r,p);auto it=l->owners.find(p);if(it==l->owners.end()){
 QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);}, Qt::DirectConnection);
 // The initialization hook executes before policies. Never seed a late unknown
 // row from mutable native state after an uninstrumented legacy policy.
 const bool baseline=l->initialized&&l->baselineKnown;
 return l->owners.insert(p,Row{e,baseline?p->hasFlag("Kurou_toDie"):false,baseline}).value();
 }if(it->epoch!=e)*it=Row{e,false,true};return it.value();}
QJsonObject answer(const Row&r){return r.known?QJsonObject{{"kind","original_kurou_shadow"},{"known",true},{"value",r.value}}:fail("untracked legacy policy interrupted owner Kurou flag continuity");}
bool planning(Room&r,const AIRequest&q,const ServerPlayer*p){if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY)return true;
 if(!q.worldView.strategyFacts.contains("readonly_play_context"))return false;
 return aiRound23OlClanPlanningQuestion(q,r,p)||aiRound27ChoicePlanningQuestion(q,r,p)||aiRound28ChoicePlanningQuestion(q,r,p);
}
}
void initializeAIRound36KurouShadow(Room&r){auto*l=ledger(r);if(l->initialized)return;l->initialized=true;for(auto*p:r.getAllPlayers(true))row(r,p);}
void invalidateAIRound36KurouShadow(Room&r){auto*l=ledger(r);l->baselineKnown=false;for(auto it=l->owners.begin();it!=l->owners.end();++it)it->known=false;}
QJsonObject readAIRound36KurouShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!valid(r,q,p))return fail("Original owner/request revision mismatch");return answer(row(r,p));}
QJsonObject writeAIRound36KurouShadow(Room&r,const AIRequest&q,const ServerPlayer*p,bool v){if(!valid(r,q,p)||!planning(r,q,p)||!p->hasSkill("heg_kurou"))return fail("Original Kurou fill writer scope mismatch");auto&x=row(r,p);x.value=v;x.known=true;return answer(x);}
int aiRound36KurouPrototype(const AIRequest&q,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!p||!p->getRoom()||!valid(*p->getRoom(),q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||skill!="heg_kurou"||prototype!="ActiveSkillCard"||!p->hasSkill(skill))return -1;
 // Fixed effective heg-common38-54, not raw Card::Parse. bit1 permits the
 // source's material-bearing hypothesis. Current native skill refs, ordered
 // canSelectCard/cardSelectionFeasible/createCard and final payment validation
 // remain downstream. No legacy HKurouCard prototype is invented.
 return 1;
}
