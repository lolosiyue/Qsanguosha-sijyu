#include "ai-strategy-round54-jijiang.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai.h"
#include "ai-runtime.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "settings.h"
#include "lua-runtime.h"
#include <QPointer>
#include <QJsonArray>
#include "ai-strategy-round56-async.h"
#include <limits>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
struct State;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
int operation(lua_State*);
struct State final:QObject {
 explicit State(Room&r):QObject(&r),room(r){}
 ~State()override{for(auto id:{globals,space,spaceMeta,filters,invokes,responses,plans,targets})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1;bool valid=false,exhausted=false,known=false,present=false;
 const void*globals=nullptr,*space=nullptr,*spaceMeta=nullptr,*filters=nullptr,*invokes=nullptr,*responses=nullptr,*producer=nullptr,*clearer=nullptr,*plans=nullptr,*planner=nullptr;
 QPointer<ServerPlayer>sourceWriter,targetWriter;
 QPointer<ServerPlayer>lord;const SkillContext*context=nullptr;SkillInstanceRef activation,source;
 const void*targets=nullptr;QPointer<ServerPlayer>targetOwner;QList<QPointer<ServerPlayer>>targetPlayers;
 bool targetsKnown=false,targetsPresent=false;quint64 targetEpoch=1,targetDecision=0,targetTransport=0;
 QString sourceWriterKind,targetWriterKind;
 bool pending=false;unsigned observed=0;const char*pendingKey=nullptr;const void*pendingTable=nullptr;
 void forgetTargets()noexcept{targetsKnown=false;if(targetEpoch==std::numeric_limits<quint64>::max())exhausted=true;else ++targetEpoch;}
 void detachTargets()noexcept{if(targets)lua_unobservetableidentity(L,targets,observe,this);targets=nullptr;targetPlayers.clear();}

 void revoke()noexcept{known=false;pending=false;if(epoch==std::numeric_limits<quint64>::max())exhausted=true;else ++epoch;}
};
State*state(Room&r,bool create=false){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return create?new State(r):nullptr;}
bool key(const lua_TableObservation&e,const char*s){return e.string_key&&e.string_key_length==std::strlen(s)&&!std::memcmp(e.string_key,s,e.string_key_length);}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&!s->exhausted&&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation&&!Config.value("EnableHegemony",false).toBool();}
QJsonObject no(const char*why){return{{"kind","ol_jijiang_state"},{"known",false},{"reason",why}};}
bool request(Room&r,ServerPlayer*p,const AIRequest&q){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
thread_local AIRound54JijiangScope::Data*active=nullptr;
thread_local AIRound54JijiangEvent::Data*eventFrame=nullptr;
thread_local AIRound54JijiangLiege::Data*liegeFrame=nullptr;
}
struct AIRound54JijiangScope::Data{State*s=nullptr;const SkillContext*ctx=nullptr;bool effect=false;Data*previous=nullptr;};
struct AIRound54JijiangLiege::Data{State*s=nullptr;QPointer<ServerPlayer>liege;Data*previous=nullptr;};
struct AIRound54JijiangEvent::Data{State*s=nullptr;int event=0;QPointer<ServerPlayer>actor;QString payload;bool invoke=false,response=false,read=false,sourceCleared=false,targetsCleared=false;Data*previous=nullptr;};
namespace {
bool frame(State*s,bool effect){return s&&active&&active->s==s&&active->effect==effect&&active->ctx==s->context&&s->lord&&live(s);}
bool roots(State*s){Stack st(s->L);auto*L=s->L;AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;AIRound45Inspect::field(L,-1,"sgs");if(lua_topointer(L,-1)!=s->space)return false;int ns=lua_absindex(L,-1);if(!matchAIRound45NativeNamespaceWrite(s->room,L,"sgs","oljijiangsource"))return false;AIRound45Inspect::field(L,ns,"ai_choicemade_filter");if(lua_topointer(L,-1)!=s->filters)return false;int fs=lua_absindex(L,-1);
 AIRound45Inspect::field(L,fs,"skillInvoke");if(lua_topointer(L,-1)!=s->invokes)return false;AIRound45Inspect::field(L,-1,"oljijiang");if(lua_topointer(L,-1)!=s->producer)return false;const char*up=lua_getupvalue(L,-1,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=s->globals)return false;lua_pop(L,1);lua_pop(L,2);
 AIRound45Inspect::field(L,fs,"cardResponded");if(lua_topointer(L,-1)!=s->responses)return false;AIRound45Inspect::field(L,-1,"@oljijiang-slash");if(lua_topointer(L,-1)!=s->clearer)return false;up=lua_getupvalue(L,-1,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=s->globals)return false;lua_pop(L,1);return lua_getupvalue(L,-1,2)==nullptr;
}
void observe(lua_State*L,const lua_TableObservation*event,void*ptr)noexcept{
 auto*s=static_cast<State*>(ptr);const auto&e=*event;if(!s->L||G(L)!=G(s->L))return;
 if(e.table_identity==s->targets){
  if(e.event==LUA_TABLE_OBSERVER_FREE){s->targets=nullptr;s->forgetTargets();return;}
  if(e.event==LUA_TABLE_OBSERVER_METATABLE){s->forgetTargets();return;}
  if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
  const CallInfo*ci=L->ci;while(ci&&!(ttislcf(s2v(ci->func.p))&&fvalue(s2v(ci->func.p))==operation))ci=ci->previous;
  if(s->pending&&s->pendingTable==e.table_identity&&ci&&s->observed==0){++s->observed;return;}
  // Includes equal writes through aliases and native/raw setters.
  s->forgetTargets();return;
 }
 if(e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE){s->forgetTargets();s->valid=false;s->revoke();if(e.event==LUA_TABLE_OBSERVER_FREE)for(auto*id:{&s->globals,&s->space,&s->spaceMeta,&s->filters,&s->invokes,&s->responses,&s->plans})if(*id==e.table_identity)*id=nullptr;return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if((e.table_identity==s->space&&key(e,"ai_skill_use_func"))||(e.table_identity==s->plans&&key(e,"OLJijiangCard"))){s->forgetTargets();return;}
 if(e.table_identity==s->spaceMeta){s->valid=false;s->revoke();return;}
 if((e.table_identity==s->globals&&key(e,"sgs"))||(e.table_identity==s->space&&key(e,"ai_choicemade_filter"))||(e.table_identity==s->filters&&(key(e,"skillInvoke")||key(e,"cardResponded")||key(e,"OLJijiangCard")))||(e.table_identity==s->invokes&&key(e,"oljijiang"))||(e.table_identity==s->responses&&key(e,"@oljijiang-slash"))){s->valid=false;s->revoke();return;}
 if(e.table_identity!=s->space||(!key(e,"oljijiangsource")&&!key(e,"oljijiangtarget")))return;
 const CallInfo*own=L->ci;while(own&&!(ttislcf(s2v(own->func.p))&&fvalue(s2v(own->func.p))==operation))own=own->previous;
 if(s->pending&&own&&s->pendingKey&&key(e,s->pendingKey)&&s->observed==0){++s->observed;return;}
 // The sole admitted legacy writer is the complete pinned unconditional
 // assignment reached by this exact real public invoke event. Namespace
 // __newindex is separately sealed; nearest Lua frame is the source closure.
 const CallInfo*ci=L->ci;while(ci&&!isLua(ci))ci=ci->previous;
 if(key(e,"oljijiangsource")&&eventFrame&&eventFrame->s==s&&eventFrame->invoke&&frame(s,false)&&eventFrame->actor==s->lord&&ci&&clLvalue(s2v(ci->func.p))==s->producer){s->known=true;s->present=true;s->sourceWriterKind="legacy_invoke";s->sourceWriter=eventFrame->actor;return;}
 if(key(e,"oljijiangtarget"))s->forgetTargets();else s->revoke();
}
struct Call{State*s;Room*r;ServerPlayer*p;const AIRequest*q;bool write=false;QString op;bool response=false;QString value;quint64 transport=0;QJsonObject out;const char*error=nullptr;};
int fail(lua_State*L,Call*w,const char*why){w->error=why;return luaL_error(L,"%s",why);}
bool writerRequest(Room&r,ServerPlayer*p,const AIRequest&q){
 return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&Sanguosha&&Sanguosha->currentRoom()==&r
  &&r.roomRuntime()&&r.roomRuntime()->ai().currentOLJijiangInvokeEvent(q,p);
}
bool responseRequest(State*s,Room&r,ServerPlayer*p,const AIRequest&q){
 return eventFrame&&eventFrame->s==s&&eventFrame->response&&eventFrame->actor==p
  &&frame(s,true)&&liegeFrame&&liegeFrame->s==s&&liegeFrame->liege==p
  &&r.roomRuntime()->ai().currentOLJijiangResponseEvent(q,p,eventFrame->payload);
}
bool plannerRoot(State*s,lua_State*L,bool append){
 Stack stack(L);AIRound45Inspect::globals(L);const int globals=lua_absindex(L,-1);
 AIRound45Inspect::field(L,globals,"sgs");AIRound45Inspect::field(L,-1,"ai_skill_use_func");
 if(lua_topointer(L,-1)!=s->plans)return false;
 AIRound45Inspect::field(L,-1,"OLJijiangCard");if(lua_topointer(L,-1)!=s->planner)return false;
 const char*up=lua_getupvalue(L,-1,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=s->globals)return false;
 lua_pop(L,1);if(lua_getupvalue(L,-1,2))return false;
 if(append){AIRound45Inspect::field(L,globals,"table");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;
  AIRound45Inspect::field(L,-1,"insert");if(!matchAIRound45NativeBinding(s->room,L,-1,"table.insert"))return false;}
 return true;
}
bool planningRequest(State*s,Room&r,ServerPlayer*p,const AIRequest&q){
 return s&&request(r,p,q)&&r.roomRuntime()->ai().currentOLJijiangPlanningRequest(q,p);
}
bool targetTable(State*s,lua_State*L,int ns){
 AIRound45Inspect::field(L,ns,"oljijiangtarget");const int at=lua_absindex(L,-1);
 auto*t=AIRound45Inspect::table(L,at);
 if(!s->targetsKnown||!s->targetsPresent||!s->targetTransport||s->targetTransport!=s->room.roomRuntime()->xufaTransportEpoch()||!t||t->metatable||lua_topointer(L,at)!=s->targets)return false;
 if(lua_rawlen(L,at)!=size_t(s->targetPlayers.size()))return false;
 for(int i=0;i<s->targetPlayers.size();++i){
  if(!s->targetPlayers[i]||s->targetPlayers[i]->getRoom()!=&s->room)return false;
  lua_rawgeti(L,at,i+1);const bool ok=matchAIRound45SlotPlayer(L,-1,s->targetPlayers[i]);lua_pop(L,1);if(!ok)return false;
 }
 // No sparse tail, hash alias cell or synthetic membership is accepted.
 lua_pushnil(L);int count=0;while(lua_next(L,at)){
  if(!lua_isinteger(L,-2)||lua_tointeger(L,-2)<1||lua_tointeger(L,-2)>s->targetPlayers.size())return false;
  ++count;lua_pop(L,1);
 }
 return count==s->targetPlayers.size();
}
QJsonObject targetResult(State*s){
 QJsonArray players;for(const auto&p:s->targetPlayers)players.append(p->objectName());
 return{{"kind","ol_jijiang_state"},{"known",true},{"present",s->targetsPresent},{"players",players},
  {"writer",s->targetOwner?s->targetOwner->objectName():QString()}};
}
bool current(const Call&w){
 if(w.op=="targets_reset"||w.op=="targets_append"||w.op=="targets_read"){
  auto*rt=w.r->roomRuntime();if(!rt||!w.transport||rt->xufaTransportEpoch()!=w.transport
   ||(w.op!="targets_reset"&&w.s->targetTransport!=w.transport))return false;
 }
 return live(w.s)&&(w.response?responseRequest(w.s,*w.r,w.p,*w.q):(w.write?writerRequest(*w.r,w.p,*w.q):request(*w.r,w.p,*w.q)))&&roots(w.s);}
int operation(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096)||!current(*w))return fail(L,w,"OLJijiang source frame unavailable");auto*s=w->s;
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");int ns=lua_absindex(L,-1);
 if(w->write){
  lua_pushliteral(L,"oljijiangsource");if(!pushAIRound45SlotPlayer(L,s->lord))return fail(L,w,"OLJijiang source Player binding unavailable");
  if(!current(*w)||!matchAIRound45NativeNamespaceWrite(*w->r,L,"sgs","oljijiangsource"))return fail(L,w,"OLJijiang namespace assignment changed");
  s->pending=true;s->pendingKey="oljijiangsource";s->pendingTable=nullptr;s->observed=0;lua_settable(L,ns);bool seen=s->pending&&s->observed==1;s->pending=false;
  if(!seen||!current(*w))return fail(L,w,"OLJijiang actual source assignment interrupted");s->known=true;s->present=true;s->sourceWriterKind="isolated_invoke";s->sourceWriter=w->p;
 }
 if(w->op=="targets_reset"||w->op=="targets_append"||w->op=="targets_read"){
  if(!planningRequest(s,*w->r,w->p,*w->q)||!plannerRoot(s,L,w->op=="targets_append"))return fail(L,w,"OLJijiang target owner question unavailable");
  if(w->op=="targets_reset"){
   if(!matchAIRound45NativeNamespaceWrite(*w->r,L,"sgs","oljijiangtarget"))return fail(L,w,"OLJijiang target namespace assignment changed");
   // This allocation is the translated source's actual fresh {}. Never adopt
   // an observed table or infer members from native selected targets/flags.
   s->forgetTargets();s->detachTargets();lua_pushliteral(L,"oljijiangtarget");lua_newtable(L);
   const void*born=lua_topointer(L,-1);
   if(!lua_observetableidentity(L,born,observe,s))return fail(L,w,"OLJijiang target table observer unavailable");
   s->targets=born;s->pending=true;s->pendingKey="oljijiangtarget";s->pendingTable=nullptr;s->observed=0;lua_settable(L,ns);
   const bool seen=s->pending&&s->observed==1;s->pending=false;
   if(!seen||!current(*w))return fail(L,w,"OLJijiang actual target reset interrupted");
   s->targetsKnown=true;s->targetsPresent=true;s->targetTransport=w->transport;s->targetOwner=w->p;s->targetDecision=w->q->decisionId;s->targetWriterKind="isolated_target_reset";s->targetWriter=w->p;
  }else{
   if(s->targetOwner!=w->p||!targetTable(s,L,ns))return fail(L,w,"OLJijiang target list continuity unavailable");
   if(w->op=="targets_append"){
    // One finite append is a real source prefix, not a replayed final plan.
    auto*p=w->r->findPlayerByObjectName(w->value,true);
    if(!p||!w->r->getAllPlayers(true).contains(p)||s->targetPlayers.size()>=1024)return fail(L,w,"OLJijiang target Player unavailable");
    if(!pushAIRound45SlotPlayer(L,p))return fail(L,w,"OLJijiang target Player binding unavailable");
    if(!s->targetsKnown||!current(*w))return fail(L,w,"OLJijiang target append preparation interrupted");
    s->pending=true;s->pendingKey=nullptr;s->pendingTable=s->targets;s->observed=0;
    lua_rawseti(L,-2,s->targetPlayers.size()+1);const bool seen=s->pending&&s->observed==1;s->pending=false;
    if(!seen||!current(*w)||!s->targetsKnown)return fail(L,w,"OLJijiang actual target append interrupted");
    s->targetPlayers.append(p);s->targetWriterKind="isolated_target_append";s->targetWriter=w->p;s->targetDecision=w->q->decisionId;
    if(s->targetEpoch==std::numeric_limits<quint64>::max()){s->exhausted=true;return fail(L,w,"OLJijiang target epoch exhausted");}++s->targetEpoch;
   }
  }
  lua_settop(L,ns);if(!targetTable(s,L,ns)||!current(*w))return fail(L,w,"OLJijiang target table changed during decode");
  w->out=targetResult(s);return 0;
 }
 if(w->op=="source_clear"||w->op=="targets_clear"){
  const bool target=w->op=="targets_clear";const char*name=target?"oljijiangtarget":"oljijiangsource";
  if(!matchAIRound45NativeNamespaceWrite(*w->r,L,"sgs",name))return fail(L,w,"OLJijiang clear namespace assignment changed");
  lua_pushstring(L,name);lua_pushnil(L);s->pending=true;s->pendingKey=name;s->pendingTable=nullptr;s->observed=0;lua_settable(L,ns);
  bool seen=s->pending&&s->observed==1;s->pending=false;
  if(!seen||!current(*w))return fail(L,w,"OLJijiang actual clear assignment interrupted");
  if(target){s->forgetTargets();s->detachTargets();s->targetsKnown=true;s->targetsPresent=false;s->targetWriterKind="response_target_clear";s->targetWriter=w->p;eventFrame->targetsCleared=true;}
  else{s->revoke();s->known=true;s->present=false;s->sourceWriterKind="response_source_clear";s->sourceWriter=w->p;eventFrame->sourceCleared=true;}
  w->out={{"kind","ol_jijiang_state"},{"known",true},{"present",false}};return 0;
 }
 if(!s->known)return fail(L,w,"OLJijiang public source continuity unavailable");
 if(!s->present&&!matchAIRound45NativeNamespaceRead(*w->r,L,"sgs","oljijiangsource"))
  return fail(L,w,"OLJijiang cleared source lookup changed");
 AIRound45Inspect::field(L,ns,"oljijiangsource");if(s->present?!matchAIRound45SlotPlayer(L,-1,s->lord):!lua_isnil(L,-1))return fail(L,w,"OLJijiang actual source no longer matches public writer");
 if(!current(*w)||!s->known)return fail(L,w,"OLJijiang source changed while decoding");
 if(w->response)eventFrame->read=true;
 w->out={{"kind","ol_jijiang_state"},{"known",true},{"present",s->present}};if(s->present)w->out.insert("player",s->lord->objectName());return 0;
}
}
bool initializeAIRound54Jijiang(Room&r,lua_State*L){
 auto*rt=r.roomRuntime();if(!rt||!L||L!=rt->lua().rawState()||!lua_checkstack(L,4096))return false;if(auto*s=state(r))return live(s)&&roots(s);if(Config.value("EnableHegemony",false).toBool())return false;Stack st(L);
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/ol_st_standard-ai.lua","cd1ff325355f4d7847522a73ce281f2044dee731eecf19be9ff9539059263dd7"))return false;
 AIRound45Inspect::globals(L);const void*g=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;const void*ns=lua_topointer(L,-1);if(!lua_getmetatable(L,-1))return false;const void*meta=lua_topointer(L,-1);lua_pop(L,1);
 if(!matchAIRound45NativeNamespaceWrite(r,L,"sgs","oljijiangsource"))return false;
 AIRound45Inspect::field(L,-1,"ai_choicemade_filter");int fs=lua_absindex(L,-1);auto*ft=AIRound45Inspect::table(L,fs);if(!ft||ft->metatable)return false;
 const void*ids[2],*funcs[2];const char*tabs[]={"skillInvoke","cardResponded"};const char*keys[]={"oljijiang","@oljijiang-slash"};const int first[]={37,87},last[]={41,96};
 for(int n=0;n<2;++n){AIRound45Inspect::field(L,fs,tabs[n]);auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;ids[n]=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,keys[n]);int f=lua_absindex(L,-1);if(!matchAIRound45PinnedFunction(r,L,f,"lua/ai/ol_st_standard-ai.lua",first[n],last[n]))return false;funcs[n]=lua_topointer(L,f);const char*up=lua_getupvalue(L,f,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=g)return false;lua_pop(L,1);if(lua_getupvalue(L,f,2))return false;lua_pop(L,2);}
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"ai_skill_use_func");
 auto*planTable=AIRound45Inspect::table(L,-1);if(!planTable||planTable->metatable)return false;const void*planId=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"OLJijiangCard");if(!matchAIRound45PinnedFunction(r,L,-1,"lua/ai/ol_st_standard-ai.lua",57,79))return false;const void*planFn=lua_topointer(L,-1);
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();s->producer=funcs[0];s->clearer=funcs[1];s->planner=planFn;
 if(lua_observetableidentity(L,planId,observe,s))s->plans=planId;
 const void*all[]={g,ns,meta,ft,ids[0],ids[1]};const void**out[]={&s->globals,&s->space,&s->spaceMeta,&s->filters,&s->invokes,&s->responses};for(int i=0;i<6;++i)if(lua_observetableidentity(L,all[i],observe,s))*out[i]=all[i];s->valid=s->globals&&s->space&&s->spaceMeta&&s->filters&&s->invokes&&s->responses&&s->plans;return s->valid;
}
AIRound54JijiangScope::AIRound54JijiangScope(Room&r,const SkillContext&ctx,int event,bool effect):d(new Data){d->previous=active;active=d;d->ctx=&ctx;d->effect=effect;auto*s=state(r);if(!live(s))return;
 if(event!=CardAsked||ctx.current_event!=(effect?EventSkillEffect:CardAsked)||!ctx.invoker||ctx.owner!=ctx.invoker||ctx.activationRef.key.skillName!="oljijiang")return;d->s=s;
 if(!effect){s->revoke();s->lord=ctx.invoker;s->context=&ctx;s->activation=ctx.activationRef;s->source=ctx.sourceRef;}
 else if(s->lord!=ctx.invoker||s->context!=&ctx||s->activation!=ctx.activationRef||s->source!=ctx.sourceRef)s->revoke();
}
AIRound54JijiangScope::~AIRound54JijiangScope(){if(d->effect&&d->s)d->s->revoke();active=d->previous;delete d;}
AIRound54JijiangLiege::AIRound54JijiangLiege(Room&r,ServerPlayer*p):d(new Data){d->previous=liegeFrame;liegeFrame=d;auto*s=state(r);if(frame(s,true)&&r.getAllPlayers(true).contains(p)){d->s=s;d->liege=p;}}
AIRound54JijiangLiege::~AIRound54JijiangLiege(){liegeFrame=d->previous;delete d;}
AIRound54JijiangEvent::AIRound54JijiangEvent(Room&r,int event,ServerPlayer*p,const QVariant&data):d(new Data){d->previous=eventFrame;eventFrame=d;d->event=event;d->actor=p;auto*s=state(r);if(!live(s))return;d->s=s;
 if(event!=ChoiceMade)return;d->payload=data.toString();d->invoke=d->payload=="skillInvoke:oljijiang:yes"&&frame(s,false)&&p==s->lord;
 const auto fields=d->payload.split(':');
 d->response=frame(s,true)&&liegeFrame&&liegeFrame->s==s&&liegeFrame->liege==p
  &&fields.size()>=5&&fields.at(0)=="cardResponded"&&fields.at(1)=="slash"
  &&fields.at(2)=="@oljijiang-slash"&&fields.at(3)==s->lord->objectName();
 if(!d->invoke&&!d->response)s->revoke();
}
AIRound54JijiangEvent::~AIRound54JijiangEvent(){eventFrame=d->previous;delete d;}
void enterAIRound54JijiangEvent(Room&r,int event,ServerPlayer*actor){
 // Both fixed global Player writer/clear dispatch roots, before any private
 // callback applicability. Other fixed-source card planners write targets only.
 if(auto*s=state(r)){
  // Full native trigger/notification preselection is still a required
  // premise. Do not retain a witness just because this selected callback did
  // not write; that would expose private branch applicability.
  s->forgetTargets();
  if(event==ChoiceMade||event==PreCardUsed)s->revoke();
  else if(event==CardAsked&&!(frame(s,true)&&liegeFrame&&liegeFrame->s==s&&liegeFrame->liege==actor))s->revoke();
 }
}
void enterAIRound54JijiangFilter(Room&r,int event,ServerPlayer*p,const QVariant&data){
 if(event!=ChoiceMade&&event!=PreCardUsed)return;auto*s=state(r);if(!s)return;
 if(!eventFrame||eventFrame->s!=s||eventFrame->event!=event||eventFrame->actor!=p||eventFrame->payload!=data.toString())s->revoke();
}
void clearAIRound54Jijiang(Room&r){delete state(r);}
QJsonObject queryAIRound54Jijiang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&value){
 const bool write=op=="source_write";
 auto*s=state(r);const bool response=live(s)&&responseRequest(s,r,p,q);
 if(!live(s)||!(response||(write?writerRequest(r,p,q):request(r,p,q))))return no("OLJijiang current ordinary source scope unavailable");
 if(response){
  if(!value.isEmpty())return no("OLJijiang response primitive accepts no value");
  if(op=="source_read"){
   if(!s->known)return no("OLJijiang response prior source continuity unavailable");
  }else if(op=="source_clear"){
   const auto fields=eventFrame->payload.split(':');
   // Preserve the original ~= empty-string branch, including literal '.'.
   bool clears=!fields.last().isEmpty();
   if(!clears&&s->present){const auto lieges=r.getLieges("shu",s->lord);clears=!lieges.isEmpty()&&lieges.last()==p;}
   if(!eventFrame->read||eventFrame->sourceCleared||!s->known||!clears)return no("OLJijiang source clear is not reached response continuation");
  }else if(op=="targets_clear"){
   if(!eventFrame->sourceCleared||eventFrame->targetsCleared||!s->known||s->present)return no("OLJijiang targets clear must follow actual source clear");
  }else return no("OLJijiang response operation unavailable");
 }else if(write){const auto e=q.choiceOptions.context.value("r40_oljijiang_event").toObject();if(!eventFrame||eventFrame->s!=s||!eventFrame->invoke||!frame(s,false)||p!=s->lord||value!=p->objectName()||e.value("writer")!="invoke"||e.value("actor")!=value)return no("OLJijiang source writer is not actual public invoke yes");}
 else if(op=="source_read"){
  if(!value.isEmpty()||!frame(s,true)||!liegeFrame||liegeFrame->s!=s||liegeFrame->liege!=p||!s->known||q.kind!=AIRequest::RespondCard||q.prompt!="@oljijiang-slash:"+s->lord->objectName()||q.pattern!="slash"||q.handlingMethod!=Card::MethodResponse)return no("OLJijiang source read is not current compiled liege question");
  if(q.choiceOptions.context.contains("r40_oljijiang_event"))return no("OLJijiang response filter continuation is not certified");
 }else if(op=="targets_reset"||op=="targets_append"||op=="targets_read"){
  if(!planningRequest(s,r,p,q)||(op!="targets_append"&&!value.isEmpty())||(op=="targets_append"&&value.isEmpty()))return no("OLJijiang target operation lacks exact owner question");
  if(op!="targets_reset"&&(!s->targetsKnown||s->targetOwner!=p))return no("OLJijiang target provenance unavailable");
 }else return no("OLJijiang generic prior-read or unproved continuation");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("OLJijiang gameplay VM unavailable");
 // The finite table thunk only creates a plain table and borrowed Player
 // bindings. It cannot dispatch a gameplay event or call an authored policy.
 // Async authority is the existing whole-bootstrap destructor/hook union.
 std::unique_ptr<AIRound56AsyncScope>targetAsync;
 if(op=="targets_reset"||op=="targets_append"||op=="targets_read"){
  targetAsync.reset(new AIRound56AsyncScope(r,L));
  if(!targetAsync->accepted())return no("OLJijiang target allocation source union unavailable");
 }
 Stack st(L);Call w{s,&r,p,&q,write,op,response,value,r.roomRuntime()->xufaTransportEpoch()};lua_pushcfunction(L,operation);lua_pushlightuserdata(L,&w);int status=LuaRuntime::protectedCall(L,1,0,0);s->pending=false;if(status!=LUA_OK)return no(w.error?w.error:"OLJijiang source operation failed");return w.out;
}

void observeAIRound54JijiangEntry(Room&r,lua_State*L,int nargs)noexcept{
 auto*s=state(r);if(!s||s->L!=L)return;
 const int fn=lua_gettop(L)-nargs;
 if(nargs==1&&fn>0&&lua_iscfunction(L,fn)&&lua_tocfunction(L,fn)==operation)return;
 // Called before any callback code, regardless of selected outcome. Existing
 // R54 source admission is unchanged; this new target ledger has no blanket
 // exemption from another feature's proof.
 s->forgetTargets();
}
