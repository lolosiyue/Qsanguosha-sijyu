#include "ai-strategy-round56-clone-scope.h"
#include "ai-strategy-round56-clone-proof.h"
#include "ai-strategy-round56-maxcards.h"
#include <cstdio>
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-projection.h"
#include <QScopeGuard>
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
extern bool matchAIRound56NativePlayer(lua_State*,int,const ServerPlayer*);
namespace {thread_local AIRound56CloneScope*active=nullptr;}
struct AIRound56CloneScope::Data {
 Room&r;ServerPlayer*p;lua_State*L;AIRound56CloneScope*previous=nullptr;
 std::unique_ptr<AIRound56OwnerCardRulesScope>identityRules;
 std::unique_ptr<AIRound56AsyncScope>async;
 const void*function=nullptr;quint64 generation=0,transport=0;int depth=0;
 bool ready=false,entered=false,invalid=false;
 Data(Room&room,ServerPlayer*player,lua_State*state):r(room),p(player),L(state){}
};
AIRound56CloneScope::AIRound56CloneScope(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 d->previous=active;active=this;
 // A failed preflight retires the shared source prerequisites BEFORE the
 // caller allocates its first SWIG argument. Original CloneAI still runs.
 const auto failedPreallocation=qScopeGuard([&]{
  if(!accepted())retireAIRound56Projection(r,L);
 });
 auto*rt=r.roomRuntime();
 if(!p||p->getRoom()!=&r||!rt||!L||!lua_checkstack(L,8192))return;
 try{
  QString why;
  if(!proveAIRound56CloneSource(r,L,&why)){
   if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
    std::fprintf(stderr,"R56_CLONE_SOURCE_REFUSAL reason=%s\n",qPrintable(why));std::fflush(stderr);
   }
   return;
  }
  if(r.getMode()=="04p"){
   d->identityRules.reset(new AIRound56OwnerCardRulesScope(r,p,L));
   if(!d->identityRules->accepted())return;
  }
  d->generation=rt->lua().generation();d->transport=rt->xufaTransportEpoch();
  d->depth=rt->lua().protectedCallDepth();
  const int top=lua_gettop(L);AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"CloneAI");
  d->function=lua_topointer(L,-1);lua_settop(L,top);
  d->async.reset(new AIRound56AsyncScope(r,L,AIRound56AsyncScope::FreshLuaAI));
  d->ready=d->function&&d->transport&&d->async->accepted();
 }catch(...){d->invalid=true;}
}
AIRound56CloneScope::~AIRound56CloneScope(){active=d->previous;}
bool AIRound56CloneScope::accepted()const noexcept{
 auto*rt=d->r.roomRuntime();return d->ready&&!d->invalid&&(!d->identityRules||d->identityRules->accepted())&&rt&&!rt->isClosing()
  &&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&hasAIRound56CloneInputLineage(d->r,d->L)&&hasAIRound56AsyncUniverse(d->r,d->L);
}
bool inAIRound56CloneScope(const Room&r,lua_State*L)noexcept{
 return active&&&active->d->r==&r&&active->d->L==L&&active->accepted();
}
bool acceptAIRound56CloneEntry(Room&r,lua_State*L,int n)noexcept{
 if(!active||&active->d->r!=&r||active->d->L!=L)return false;
 auto&x=*active->d;auto*rt=r.roomRuntime();
 if(active->accepted()&&!x.entered&&n==1&&lua_gettop(L)>=2
  &&rt->lua().protectedCallDepth()==x.depth&&lua_topointer(L,-2)==x.function
  &&matchAIRound56NativePlayer(L,-1,x.p)){x.entered=true;return true;}
 x.invalid=true;retireAIRound56Projection(r,L);return false;
}
