#include "ai-strategy-round47-xufa-event-scope.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-xufa-filter.h"
#include "ai-strategy-round45-natural-event.h"
#include "ai-strategy-choice-continuity.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariant>
#include <cstring>
extern "C" {
#include "lua.h"
}
namespace {thread_local QHash<Room*,QList<const AIRound47XufaEventScope*>> events;}
struct AIRound47XufaEventScope::Data {
 Room*room=nullptr;int event=0;QPointer<ServerPlayer>actor,writer;
 const QVariant*payload=nullptr;quint64 writeSerial=0;bool valid=false,completed=false;
};
struct AIRound47XufaEventAccess {
 static AIRound47XufaEventScope::Data*top(Room&r){auto i=events.constFind(&r);return i==events.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
};
AIRound47XufaEventScope::AIRound47XufaEventScope(Room&r,int e,ServerPlayer*p,const QVariant&v):d(new Data){
 d->room=&r;d->event=e;d->actor=p;d->payload=&v;d->writer=currentAIRound47XufaContinuityWriter(r);d->writeSerial=currentAIRound47XufaWriteSerial(r);
 // Nested/uncovered events never inherit an outer grant. There is no value
 // restoration, including when the exact same scalar is subsequently written.
 const bool nested=AIRound47XufaEventAccess::top(r)!=nullptr;
 events[&r].append(this);
 try {
  // The complete pre-filter TriggerSkill and event snapshot enable unions
  // are not yet admitted. Keep this native event grant closed until those
  // fixed-source prerequisites are connected BEFORE private applicability.
  const bool completeNativeEventPreflight=false;
  if(completeNativeEventPreflight&&!nested&&d->writer&&matchesAIRound47XufaMoveEvent(r,e,p,v)){
   auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
   d->valid=L&&lua_checkstack(L,8192)&&proveAIRound47XufaCorrectionUnion(r,L)
       &&d->writeSerial&&currentAIRound47XufaWriteSerial(r)==d->writeSerial&&currentAIRound47XufaContinuityWriter(r)==d->writer;
  }
 }catch(...){d->valid=false;}
 if(!d->valid)interruptAIRound47XufaContinuity(r);
}
AIRound47XufaEventScope::~AIRound47XufaEventScope(){
 auto&r=*d->room;if(!d->valid||!d->completed)interruptAIRound47XufaContinuity(r);
 auto i=events.find(&r);if(i!=events.end()){i->removeAll(this);if(i->isEmpty())events.erase(i);}
}
bool pendingAIRound47XufaEvent(Room&r){
 auto*d=AIRound47XufaEventAccess::top(r);return d&&d->valid&&!d->completed&&d->writer
  &&d->writeSerial&&currentAIRound47XufaWriteSerial(r)==d->writeSerial&&currentAIRound47XufaContinuityWriter(r)==d->writer;
}
bool prepareAIRound47XufaFilterCallback(Room&r,const char*m){
 return m&&!std::strcmp(m,"filterEvent")&&pendingAIRound47XufaEvent(r);
}
void completeAIRound47XufaEventProof(Room&r,lua_State*L,int nargs){
 auto*d=AIRound47XufaEventAccess::top(r);
 // Called ONLY after the common prefix+actual-self+full family/event callback
 // graph has passed in the sole protected-entry observer. This is not itself
 // a graph verifier and is never exported to Lua.
 if(!d||!d->valid||d->completed||!d->writer||!d->writeSerial||currentAIRound47XufaWriteSerial(r)!=d->writeSerial||currentAIRound47XufaContinuityWriter(r)!=d->writer)return;
 if(matchesAIRound47NativeFilterDispatch(r,L,nargs,d->event,d->actor,d->payload))d->completed=true;
}
