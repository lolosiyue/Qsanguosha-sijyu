#include "ai-strategy-round36-choice.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>
namespace {
struct Row{qint64 epoch=0;bool value=false;bool known=true;};
class Shadow final:public AIRoomLedger{public:explicit Shadow(Room&r):AIRoomLedger(r){}QHash<const ServerPlayer*,Row> owners;bool baselineKnown=true;};
Shadow* ledger(Room&r,bool create=true){for(auto*c:r.children())if(auto*l=dynamic_cast<Shadow*>(c))return l;return create?new Shadow(r):nullptr;}
QJsonObject fail(const char*why){return{{"kind","original_guanxing_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool valid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))
 &&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiRound35OriginalQuestion(q);}
qint64 epoch(Room&r,const ServerPlayer*p){AIWorldView fresh;fresh.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(fresh,r);
 return qint64(fresh.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
Row& row(Room&r,const AIRequest&q,const ServerPlayer*p){auto*l=ledger(r);auto it=l->owners.find(p);const qint64 e=epoch(r,p);
 if(it==l->owners.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);}, Qt::DirectConnection);
  // Initial migration is lawful only while no legacy/external policy entry
  // has interrupted this ledger's baseline, including before the first read.
  if(!l->baselineKnown)return l->owners.insert(p,Row{e,false,false}).value();
  // Preserve an uninterrupted R35 owner shadow; otherwise use lawful own flag.
  auto prior=q.worldView.modePolicy.value("strategy_flags").toObject().value(p->objectName()).toObject().value("willSkipPlayPhase");
  const bool value=prior.isBool()?prior.toBool():p->hasFlag("willSkipPlayPhase");
  return l->owners.insert(p,Row{e,value,true}).value();
 }
 if(it->epoch!=e)*it=Row{e,false,true};
 return it.value();
}
QJsonObject answer(const Row&r){return r.known?QJsonObject{{"kind","original_guanxing_shadow"},{"known",true},{"value",r.value}}
 :fail("legacy policy interrupted viewer shadow continuity");}
}
QJsonObject readAIRound36GuanxingShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!valid(r,q,p))return fail("Original owner/request revision mismatch");return answer(row(r,q,p));}
QJsonObject writeAIRound36GuanxingShadow(Room&r,const AIRequest&q,const ServerPlayer*p,bool value){
 if(!valid(r,q,p)||q.kind!=AIRequest::Guanxing||q.choiceOptions.question!="askForGuanxing"
  ||q.worldView.strategyFacts.value("original_guanxing_flag_write")!=QJsonValue(true))return fail("Original Guanxing writer scope mismatch");
 auto&v=row(r,q,p);v.value=value;v.known=true;
 // Pure Room-owned strategy state: no Player write/revision/payment/event
 // transaction or Lua VM state. Reached writes survive errors and VM reset.
 return answer(v);
}
void invalidateAIRound36GuanxingShadow(Room&r){
 auto*l=ledger(r);l->baselineKnown=false;
 for(auto it=l->owners.begin();it!=l->owners.end();++it)it->known=false;
 // Seed public clear epochs, not flag values. A later first read must not
 // resurrect a stale R35 mode-policy value. A real clear after this entry
 // reestablishes false, even if no read/write existed before the entry.
 for(const auto*p:r.getAllPlayers(true)){
  if(!l->owners.contains(p))QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);}, Qt::DirectConnection);
  l->owners.insert(p,Row{epoch(r,p),false,false});
 }
}
