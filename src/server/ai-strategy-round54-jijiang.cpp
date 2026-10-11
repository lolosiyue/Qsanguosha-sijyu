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
 ~State()override{for(auto id:{globals,space,spaceMeta,filters,invokes,responses})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1;bool valid=false,exhausted=false,known=false,present=false;
 const void*globals=nullptr,*space=nullptr,*spaceMeta=nullptr,*filters=nullptr,*invokes=nullptr,*responses=nullptr,*producer=nullptr,*clearer=nullptr;
 QPointer<ServerPlayer>lord;const SkillContext*context=nullptr;SkillInstanceRef activation,source;
 bool pending=false;unsigned observed=0;
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
struct AIRound54JijiangEvent::Data{State*s=nullptr;int event=0;QPointer<ServerPlayer>actor;QString payload;bool invoke=false,response=false;Data*previous=nullptr;};
namespace {
bool frame(State*s,bool effect){return s&&active&&active->s==s&&active->effect==effect&&active->ctx==s->context&&s->lord&&live(s);}
bool roots(State*s){Stack st(s->L);auto*L=s->L;AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;AIRound45Inspect::field(L,-1,"sgs");if(lua_topointer(L,-1)!=s->space)return false;int ns=lua_absindex(L,-1);if(!matchAIRound45NativeNamespaceWrite(s->room,L,"sgs","oljijiangsource"))return false;AIRound45Inspect::field(L,ns,"ai_choicemade_filter");if(lua_topointer(L,-1)!=s->filters)return false;int fs=lua_absindex(L,-1);
 AIRound45Inspect::field(L,fs,"skillInvoke");if(lua_topointer(L,-1)!=s->invokes)return false;AIRound45Inspect::field(L,-1,"oljijiang");if(lua_topointer(L,-1)!=s->producer)return false;const char*up=lua_getupvalue(L,-1,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=s->globals)return false;lua_pop(L,1);lua_pop(L,2);
 AIRound45Inspect::field(L,fs,"cardResponded");if(lua_topointer(L,-1)!=s->responses)return false;AIRound45Inspect::field(L,-1,"@oljijiang-slash");return lua_topointer(L,-1)==s->clearer;
}
void observe(lua_State*L,const lua_TableObservation*event,void*ptr)noexcept{
 auto*s=static_cast<State*>(ptr);const auto&e=*event;if(!s->L||G(L)!=G(s->L))return;
 if(e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE){s->valid=false;s->revoke();if(e.event==LUA_TABLE_OBSERVER_FREE)for(auto*id:{&s->globals,&s->space,&s->spaceMeta,&s->filters,&s->invokes,&s->responses})if(*id==e.table_identity)*id=nullptr;return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if(e.table_identity==s->spaceMeta){s->valid=false;s->revoke();return;}
 if((e.table_identity==s->globals&&key(e,"sgs"))||(e.table_identity==s->space&&key(e,"ai_choicemade_filter"))||(e.table_identity==s->filters&&(key(e,"skillInvoke")||key(e,"cardResponded")||key(e,"OLJijiangCard")))||(e.table_identity==s->invokes&&key(e,"oljijiang"))||(e.table_identity==s->responses&&key(e,"@oljijiang-slash"))){s->valid=false;s->revoke();return;}
 if(e.table_identity!=s->space||!key(e,"oljijiangsource"))return;
 const CallInfo*own=L->ci;while(own&&!(ttislcf(s2v(own->func.p))&&fvalue(s2v(own->func.p))==operation))own=own->previous;
 if(s->pending&&own&&s->observed==0){++s->observed;s->known=false;return;}
 // The sole admitted legacy writer is the complete pinned unconditional
 // assignment reached by this exact real public invoke event. Namespace
 // __newindex is separately sealed; nearest Lua frame is the source closure.
 const CallInfo*ci=L->ci;while(ci&&!isLua(ci))ci=ci->previous;
 if(eventFrame&&eventFrame->s==s&&eventFrame->invoke&&frame(s,false)&&eventFrame->actor==s->lord&&ci&&clLvalue(s2v(ci->func.p))==s->producer){s->known=true;s->present=true;return;}
 s->revoke();
}
struct Call{State*s;Room*r;ServerPlayer*p;const AIRequest*q;bool write=false;QJsonObject out;const char*error=nullptr;};
int fail(lua_State*L,Call*w,const char*why){w->error=why;return luaL_error(L,"%s",why);}
bool writerRequest(Room&r,ServerPlayer*p,const AIRequest&q){
 return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&Sanguosha&&Sanguosha->currentRoom()==&r
  &&r.roomRuntime()&&r.roomRuntime()->ai().currentOLJijiangInvokeEvent(q,p);
}
bool current(const Call&w){return live(w.s)&&(w.write?writerRequest(*w.r,w.p,*w.q):request(*w.r,w.p,*w.q))&&roots(w.s);}
int operation(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096)||!current(*w))return fail(L,w,"OLJijiang source frame unavailable");auto*s=w->s;
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");int ns=lua_absindex(L,-1);
 if(w->write){
  lua_pushliteral(L,"oljijiangsource");if(!pushAIRound45SlotPlayer(L,s->lord))return fail(L,w,"OLJijiang source Player binding unavailable");
  if(!current(*w)||!matchAIRound45NativeNamespaceWrite(*w->r,L,"sgs","oljijiangsource"))return fail(L,w,"OLJijiang namespace assignment changed");
  s->pending=true;s->observed=0;lua_settable(L,ns);bool seen=s->pending&&s->observed==1;s->pending=false;
  if(!seen||!current(*w))return fail(L,w,"OLJijiang actual source assignment interrupted");s->known=true;s->present=true;
 }
 if(!s->known)return fail(L,w,"OLJijiang public source continuity unavailable");
 AIRound45Inspect::field(L,ns,"oljijiangsource");if(!s->present||!matchAIRound45SlotPlayer(L,-1,s->lord))return fail(L,w,"OLJijiang actual source no longer matches public writer");
 if(!current(*w)||!s->known)return fail(L,w,"OLJijiang source changed while decoding");
 w->out={{"kind","ol_jijiang_state"},{"known",true},{"present",true},{"player",s->lord->objectName()}};return 0;
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
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();s->producer=funcs[0];s->clearer=funcs[1];
 const void*all[]={g,ns,meta,ft,ids[0],ids[1]};const void**out[]={&s->globals,&s->space,&s->spaceMeta,&s->filters,&s->invokes,&s->responses};for(int i=0;i<6;++i)if(lua_observetableidentity(L,all[i],observe,s))*out[i]=all[i];s->valid=s->globals&&s->space&&s->spaceMeta&&s->filters&&s->invokes&&s->responses;return s->valid;
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
}
AIRound54JijiangEvent::~AIRound54JijiangEvent(){eventFrame=d->previous;delete d;}
void enterAIRound54JijiangEvent(Room&r,int event,ServerPlayer*actor){
 // Both fixed global Player writer/clear dispatch roots, before any private
 // callback applicability. Other fixed-source card planners write targets only.
 if(auto*s=state(r)){
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
 auto*s=state(r);if(!live(s)||!(write?writerRequest(r,p,q):request(r,p,q)))return no("OLJijiang current ordinary source scope unavailable");
 if(write){const auto e=q.choiceOptions.context.value("r40_oljijiang_event").toObject();if(!eventFrame||eventFrame->s!=s||!eventFrame->invoke||!frame(s,false)||p!=s->lord||value!=p->objectName()||e.value("writer")!="invoke"||e.value("actor")!=value)return no("OLJijiang source writer is not actual public invoke yes");}
 else if(op=="source_read"){
  if(!value.isEmpty()||!frame(s,true)||!liegeFrame||liegeFrame->s!=s||liegeFrame->liege!=p||!s->known||q.kind!=AIRequest::RespondCard||q.prompt!="@oljijiang-slash:"+s->lord->objectName()||q.pattern!="slash"||q.handlingMethod!=Card::MethodResponse)return no("OLJijiang source read is not current compiled liege question");
  if(q.choiceOptions.context.contains("r40_oljijiang_event"))return no("OLJijiang response filter continuation is not yet certified");
 }else return no("OLJijiang target list and response-clear continuation remain unsupported");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("OLJijiang gameplay VM unavailable");Stack st(L);Call w{s,&r,p,&q,write};lua_pushcfunction(L,operation);lua_pushlightuserdata(L,&w);int status=LuaRuntime::protectedCall(L,1,0,0);s->pending=false;if(status!=LUA_OK)return no(w.error?w.error:"OLJijiang source operation failed");return w.out;
}
