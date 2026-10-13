#include "ai-strategy-round54-shebian.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-random.h"
#include <QJsonArray>
#include "ai.h"
#include "card.h"
#include "card-lifetime-manager.h"
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
struct State final:AIRoomLedger {
 explicit State(Room&r):AIRoomLedger(r),room(r){}
 ~State()override{for(auto id:{globals,space,players,cards,invokes})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1;bool valid=false,exhausted=false;
 const void*globals=nullptr,*space=nullptr,*players=nullptr,*cards=nullptr,*invokes=nullptr,*producer=nullptr;
 QPointer<ServerPlayer>owner;const SkillContext*context=nullptr;SkillInstanceRef activation,source;
 bool known[3]={false,false,false};int pending=-1;unsigned observed=0;
 void revoke()noexcept{for(bool&k:known)k=false;pending=-1;if(epoch==std::numeric_limits<quint64>::max())exhausted=true;else++epoch;}
};
State*state(Room&r,bool create=false){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return create?new State(r):nullptr;}
const char*keys[]={"olshebian_from","olshebian","olshebian_to"};
bool key(const lua_TableObservation&e,const char*s){return e.string_key&&e.string_key_length==std::strlen(s)&&!std::memcmp(e.string_key,s,e.string_key_length);}
int assign(lua_State*);
void observe(lua_State*L,const lua_TableObservation*event,void*ptr)noexcept{
 auto*s=static_cast<State*>(ptr);const auto&e=*event;if(!L||!s->L||G(L)!=G(s->L))return;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural){s->valid=false;s->revoke();if(e.event==LUA_TABLE_OBSERVER_FREE){for(auto*id:{&s->globals,&s->space,&s->players,&s->cards,&s->invokes})if(*id==e.table_identity)*id=nullptr;}return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if((e.table_identity==s->globals&&key(e,"sgs"))||(e.table_identity==s->space&&(key(e,"ai_skill_playerchosen")||key(e,"ai_skill_cardchosen")||key(e,"ai_skill_invoke")))
    ||(e.table_identity==s->invokes&&key(e,"olshebian"))){s->valid=false;s->revoke();return;}
 for(int n=0;n<3;++n)if(e.table_identity==(n==1?s->cards:s->players)&&key(e,keys[n])){
  const TValue*f=s2v(L->ci->func.p);
  if(s->pending==n&&ttislcf(f)&&fvalue(f)==assign&&s->observed==0){++s->observed;s->known[n]=false;}
  else s->revoke();
 }
}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&!s->exhausted
 &&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation&&!Config.value("EnableHegemony",false).toBool();}
bool roots(State*s){Stack st(s->L);auto*L=s->L;AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=s->space)return false;int ns=lua_absindex(L,-1);
 const char*names[]={"ai_skill_playerchosen","ai_skill_cardchosen","ai_skill_invoke"};const void*ids[]={s->players,s->cards,s->invokes};
 for(int n=0;n<3;++n){AIRound45Inspect::field(L,ns,names[n]);auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable||lua_topointer(L,-1)!=ids[n])return false;lua_pop(L,1);}
 AIRound45Inspect::field(L,ns,"ai_skill_invoke");AIRound45Inspect::field(L,-1,"olshebian");return lua_topointer(L,-1)==s->producer;
}
bool request(Room&r,ServerPlayer*p,const AIRequest&q){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&q.decisionId
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
QJsonObject no(const char*why){return{{"known",false},{"reason",QString::fromLatin1(why)}};}
thread_local AIRound54ShebianScope::Data*active=nullptr;
}
struct AIRound54ShebianScope::Data {State*s=nullptr;const SkillContext*ctx=nullptr;quint64 epoch=0;bool effect=false,invokeEntered=false;Data*previous=nullptr;};
namespace {
bool frame(State*s,ServerPlayer*p,bool effect){return active&&active->s==s&&active->epoch==s->epoch&&active->effect==effect
 &&s->owner==p&&s->context==active->ctx&&live(s);}
struct Call{State*s;Room*r;ServerPlayer*p;const AIRequest*q;int key=-1;bool write=false,consume=false;QPointer<ServerPlayer>target;int id=-1;QJsonObject out;const char*error=nullptr;quint64 epoch=0;};
int fail(lua_State*L,Call*w,const char*why){if(w)w->error=why;return luaL_error(L,"%s",why);}
bool current(const Call&w){return request(*w.r,w.p,*w.q)&&frame(w.s,w.p,!w.write)&&w.epoch==w.s->epoch&&roots(w.s);}
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Shebian source frame capacity unavailable");
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!w||!current(*w))return fail(L,w,"Shebian native operation changed");
 auto*s=w->s;AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,w->key==1?"ai_skill_cardchosen":"ai_skill_playerchosen");int table=lua_absindex(L,-1);
 if(w->write){
  lua_pushstring(L,keys[w->key]);
  if(w->key==1)lua_pushinteger(L,w->id);
  else if(!w->target||!pushAIRound45SlotPlayer(L,w->target))return fail(L,w,"Shebian source Player binding unavailable");
 }else{
  if(!s->known[w->key])return fail(L,w,"Shebian actual registry writer is not this operation");
  AIRound45Inspect::field(L,table,keys[w->key]);
  if(lua_isnil(L,-1))w->out={{"known",true},{"present",false}};
  else if(w->key==1){
   if(lua_type(L,-1)!=LUA_TNUMBER)return fail(L,w,"Shebian actual card slot changed type");
   double n=lua_tonumber(L,-1);if(n<0||n>2147483647.0||n!=int(n))return fail(L,w,"Shebian actual card slot is not integral");
   // This is the same known own prefix, not a fresh foreign-card lookup.
   w->out={{"known",true},{"present",true},{"value",n}};
  }else{
   ServerPlayer*actual=nullptr;for(auto*p:w->r->getAllPlayers(true))if(matchAIRound45SlotPlayer(L,-1,p)){actual=p;break;}
   if(!actual)return fail(L,w,"Shebian actual target is not a current roster Player");
   w->out={{"known",true},{"present",true},{"value",actual->objectName()}};
  }
  if(!w->consume||w->out.value("present")!=true){if(!current(*w)||!s->known[w->key])return fail(L,w,"Shebian reader changed during decoding");return 0;}
  lua_pushstring(L,keys[w->key]);lua_pushnil(L);
 }
 if(!current(*w)||(w->target&&!w->r->getAllPlayers(true).contains(w->target)))return fail(L,w,"Shebian source changed before assignment");
 s->pending=w->key;s->observed=0;lua_settable(L,table);const bool observed=s->observed==1&&s->pending==w->key;s->pending=-1;
 if(!observed||!current(*w))return fail(L,w,"Shebian assignment was interrupted");
 s->known[w->key]=true;if(w->write)w->out={{"known",true},{"recorded",true}};return 0;
}
}
bool initializeAIRound54Shebian(Room&r,lua_State*L){
 auto*rt=r.roomRuntime();if(!rt||!L||L!=rt->lua().rawState()||!lua_checkstack(L,4096))return false;
 if(auto*s=state(r))return live(s)&&roots(s);
 if(Config.value("EnableHegemony",false).toBool())return false;
 Stack st(L);
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/ol_st_standard-ai.lua","cd1ff325355f4d7847522a73ce281f2044dee731eecf19be9ff9539059263dd7"))return false;
 AIRound45Inspect::globals(L);const void*g=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;int ns=lua_absindex(L,-1);const void*space=lua_topointer(L,ns);
 const char*names[]={"ai_skill_playerchosen","ai_skill_cardchosen","ai_skill_invoke"};const void*ids[3];
 for(int n=0;n<3;++n){AIRound45Inspect::field(L,ns,names[n]);auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;ids[n]=lua_topointer(L,-1);lua_pop(L,1);}
 AIRound45Inspect::field(L,ns,"ai_skill_invoke");AIRound45Inspect::field(L,-1,"olshebian");int fn=lua_absindex(L,-1);
 if(!matchAIRound45PinnedFunction(r,L,fn,"lua/ai/ol_st_standard-ai.lua",662,671))return false;
 const char*up=lua_getupvalue(L,fn,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=g)return false;lua_pop(L,1);if(lua_getupvalue(L,fn,2))return false;
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();s->producer=lua_topointer(L,fn);
 const void**dest[]={&s->globals,&s->space,&s->players,&s->cards,&s->invokes};const void*all[]={g,space,ids[0],ids[1],ids[2]};
 for(int n=0;n<5;++n)if(lua_observetableidentity(L,all[n],observe,s))*dest[n]=all[n];
 s->valid=s->globals&&s->space&&s->players&&s->cards&&s->invokes;return s->valid;
}
AIRound54ShebianScope::AIRound54ShebianScope(Room&r,const SkillContext&ctx,bool effect):d(new Data){
 d->previous=active;active=d;d->ctx=&ctx;d->effect=effect;
 auto*s=state(r);if(!s||!live(s))return;d->s=s;
 if(!effect){
  s->revoke();s->owner=ctx.invoker;s->context=&ctx;s->activation=ctx.activationRef;s->source=ctx.sourceRef;
 }else if(s->owner!=ctx.invoker||s->context!=&ctx||s->activation!=ctx.activationRef||s->source!=ctx.sourceRef){s->revoke();return;}
 if(!ctx.invoker||ctx.owner!=ctx.invoker||ctx.activationRef.key.skillName!="olshebian")return;
 d->epoch=s->epoch;
}
AIRound54ShebianScope::~AIRound54ShebianScope(){if(d->effect&&d->s)d->s->revoke();active=d->previous;delete d;}
void invalidateAIRound54Shebian(Room&r){if(auto*s=state(r))s->revoke();}
void clearAIRound54Shebian(Room&r){delete state(r);}
void enterAIRound54ShebianInquiry(Room&r,ServerPlayer*p,const QString&kind,const QString&reason){
 auto*s=state(r);if(!s)return;
 if((kind=="invoke"&&reason=="olshebian")||(kind=="player_chosen"&&(reason=="olshebian_from"||reason=="olshebian_to"))||(kind=="card_chosen"&&reason=="olshebian")){
  if(!active||active->s!=s||s->owner!=p||active->epoch!=s->epoch
     ||(kind=="invoke"?active->effect:!active->effect)){s->revoke();return;}
  // The compiled cost issues one invoke inquiry. A recursive same-owner
  // inquiry (including a direct legacy fallback) is not that source entry.
  if(kind=="invoke"){if(active->invokeEntered){s->revoke();return;}active->invokeEntered=true;}
 }
}
QJsonObject queryAIRound54Shebian(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto*s=state(r);if(!live(s)||!request(r,p,q))return no("Shebian source operation unavailable");
 const QString key=input.value("key").toString(),op=input.value("op").toString();int n=-1;for(int i=0;i<3;++i)if(key==keys[i])n=i;
 if(n<0)return no("Shebian key outside fixed family");const bool write=op=="record",consume=op=="consume";
 if(write){if(q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="olshebian"||input.size()!=3||!input.value("row").isObject())return no("Shebian writer question differs");}
 else if((op!="read"&&!consume)||input.size()!=2||(consume!=(n==1))||q.choiceOptions.reason!=key||(n==1?q.kind!=AIRequest::CardChosen:q.kind!=AIRequest::PlayerChosen))return no("Shebian reader question differs");
 if(!frame(s,p,!write))return no("Shebian current compiled cost/effect scope unavailable");
 Call w{s,&r,p,&q,n,write,consume};w.epoch=s->epoch;
 if(write){const auto v=input.value("row").toObject();if(v.size()!=3||v.value("known")!=true||v.value("present")!=true)return no("Shebian writer shape differs");
  if(n==1){if(!v.value("value").isDouble())return no("Shebian ID shape differs");const double x=v.value("value").toDouble();w.id=v.value("value").toInt(-1);if(w.id<0||x!=w.id)return no("Shebian ID is not integral");
   auto*holder=r.getCardOwner(w.id);if(!holder||!r.getAllPlayers(true).contains(holder)||r.getCardPlace(w.id)!=Player::PlaceEquip)return no("Shebian source is not current equipment");
   const Card*c=nullptr;for(auto*e:holder->getEquips())if(e->getEffectiveId()==w.id)c=e;bool seen=false;
   auto inspect=[&](const AIPlayerView&v){if(v.objectName==holder->objectName())for(const auto&e:v.equips)if(e.effectiveId==w.id&&e.nativeIdentity&&globalCardLifetimeManager().isLive(e.nativeIdentity)&&e.nativeIdentity->address==c)seen=true;};
   inspect(q.worldView.self);for(const auto&v:q.worldView.players)inspect(v);if(!c||!seen)return no("Shebian source equipment lacks public current identity");
  }else{if(!v.value("value").isString())return no("Shebian Player shape differs");w.target=r.findPlayerByObjectName(v.value("value").toString(),true);if(!w.target)return no("Shebian Player outside room");}
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding game(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("Shebian game stack unavailable");Stack st(L);
 lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,1,0,0);s->pending=-1;
 if(status!=LUA_OK)return no(w.error?w.error:"Shebian protected source operation failed");return w.out;
}

// smart-ai.lua:3431 actual Self prefix, not a copied local observation.
namespace {
struct OrdinaryDisabledWrite{Room*r;ServerPlayer*p;const AIRequest*q;QJsonArray ids;const char*error=nullptr;};
int ordinaryDisabledFail(lua_State*L,OrdinaryDisabledWrite*w,const char*why){w->error=why;return luaL_error(L,"%s",why);}
int assignOrdinaryDisabled(lua_State*L){
 auto*w=static_cast<OrdinaryDisabledWrite*>(lua_touserdata(L,1));
 if(!lua_checkstack(L,4096))return ordinaryDisabledFail(L,w,"ordinary disabled list frame capacity unavailable");
 // SWIG QVariant::toIntList is exactly toList()/toInt() in source order.
 // Construct the same native list type; no gameplay userdata leaves this VM.
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"IntList");
 if(!matchAIRound47NativeScratchConstructor(*w->r,L,-1,"IntList"))return ordinaryDisabledFail(L,w,"ordinary disabled IntList constructor changed");lua_call(L,0,1);int value=lua_absindex(L,-1);
 if(!matchAIRound45NativeObjectMethods(*w->r,L,value,"IntList",{"append","contains"}))return ordinaryDisabledFail(L,w,"ordinary disabled IntList methods changed");
 for(const auto&v:w->ids){lua_getfield(L,value,"append");lua_pushvalue(L,value);lua_pushinteger(L,v.toInt());lua_call(L,2,0);}
 if(!request(*w->r,w->p,*w->q)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return ordinaryDisabledFail(L,w,"ordinary disabled actual Self unavailable");int self=lua_absindex(L,-1);
 AIRound45Inspect::field(L,self,"disabled_ids");bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,self)){AIRound45Inspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);if(!absent)return ordinaryDisabledFail(L,w,"ordinary disabled assignment handler changed");}
 if(!request(*w->r,w->p,*w->q))return ordinaryDisabledFail(L,w,"ordinary disabled request changed before assignment");
 lua_pushliteral(L,"disabled_ids");lua_pushvalue(L,value);lua_settable(L,self);
 if(!request(*w->r,w->p,*w->q)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return ordinaryDisabledFail(L,w,"ordinary disabled actual Self changed after assignment");
 AIRound45Inspect::field(L,self,"disabled_ids");if(!lua_rawequal(L,value,-1))return ordinaryDisabledFail(L,w,"ordinary disabled assignment changed");return 0;
}
}
QJsonObject refreshAIRound54OrdinaryCardChoice(Room&r,ServerPlayer*p,const AIRequest&q){
 const auto bad=[](const char*why){return QJsonObject{{"error",why},{"key","askForCardChosen.disabled_ids"}};};
 if(!r.roomRuntime()||r.roomRuntime()->isClosing()||!request(r,p,q))return bad("ordinary card-choice request unavailable");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return bad("ordinary card-choice gameplay VM unavailable");Stack st(L);
 const auto result=refreshAIStrategyDiscardPreferences(r,p);if(result.contains("error"))return result;
 OrdinaryDisabledWrite w{&r,p,&q,result.value("disabled_ids").toArray()};
 lua_pushcfunction(L,assignOrdinaryDisabled);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return bad(w.error?w.error:"ordinary disabled protected assignment failed");
 return result;
}
