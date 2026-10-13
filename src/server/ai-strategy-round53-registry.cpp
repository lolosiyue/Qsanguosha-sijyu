#include "ai-strategy-round53-registry.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "ai-runtime.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QPointer>
#include <QByteArray>
#include <limits>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack {lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
class State;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
class State final:public AIRoomLedger {
public:
 explicit State(Room&r):AIRoomLedger(r),room(r){}
 ~State()override {
  // FREE removes each identity before reuse. Normal runtime clear is before
  // Lua closes; no dead table address is passed to the observer API.
  for(auto id:{globals,space,choices})if(id)lua_unobservetableidentity(L,id,observe,this);
 }
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1,stamp=0,transport=0;
 const void*globals=nullptr,*space=nullptr,*choices=nullptr;
 bool valid=false,coverage=false,exhausted=false,pending=false,expected=false;
 unsigned writes=0;
 QPointer<ServerPlayer>writer;
 void revoke()noexcept {stamp=0;if(epoch==std::numeric_limits<quint64>::max())exhausted=true;else ++epoch;}
};
State*state(Room&r,bool create=false){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return create?new State(r):nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
int assign(lua_State*);
void observe(lua_State*L,const lua_TableObservation*ep,void*ud)noexcept {
 auto*s=static_cast<State*>(ud);const auto&e=*ep;
 // Coroutines share table identities and provenance with their global state.
 // The exact assigning C frame below is still checked on the calling thread.
 if(!L||!s->L||G(L)!=G(s->L))return;
 const bool root=e.table_identity==s->globals,ns=e.table_identity==s->space,cell=e.table_identity==s->choices;
 if(!root&&!ns&&!cell)return;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&((root&&key(e,"sgs"))||(ns&&key(e,"ai_skill_choice"))))){
  s->valid=false;s->revoke();
  if(e.event==LUA_TABLE_OBSERVER_FREE){if(root)s->globals=nullptr;if(ns)s->space=nullptr;if(cell)s->choices=nullptr;}
  return;
 }
 if(cell&&e.event==LUA_TABLE_OBSERVER_WRITE&&key(e,"xufa")){
  // Every write, including equal value/nil/restore, changes provenance. READ
  // does not revoke: the actual source value is synchronized, not shadowed.
  const TValue*f=s2v(L->ci->func.p);
  const bool own=s->pending&&s->expected&&ttislcf(f)&&fvalue(f)==assign;
  s->revoke();
  if(own&&s->writes==0)s->writes=1;else s->pending=false;
 }
}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&!s->exhausted
 &&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation;}
bool current(Room&r,ServerPlayer*p,const AIRequest&q){return p&&p->getRoom()==&r&&q.decisionId
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRound44RangeRequestCurrent(r,q,p)
 &&r.roomRuntime()->ai().currentXufaChoiceRequest(q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool roots(State*s){Stack st(s->L);auto*L=s->L;AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=s->space)return false;
 AIRound45Inspect::field(L,-1,"ai_skill_choice");auto*t=AIRound45Inspect::table(L,-1);
 return t&&!t->metatable&&lua_topointer(L,-1)==s->choices;}
QJsonObject unknown(){return{{"known",false}};}
bool disclosedName(Room&r,ServerPlayer*p,const AIRequest&q,const QString&name){
 // Exact current own source HE/xufa-pile faces only. A printed ID by itself is
 // not a permit to look up a hidden current card or a foreign physical face.
 auto test=[&](const AICardView&v){if(v.objectName!=name||!v.nativeIdentity||!globalCardLifetimeManager().isLive(v.nativeIdentity))return false;
  const Card*c=r.getCard(v.effectiveId);if(!c||v.nativeIdentity->address!=c||c->isVirtualCard())return false;
  if(!p->handCards().contains(v.effectiveId)&&!p->getPile("xufa").contains(v.effectiveId)){
   bool equip=false;for(auto*e:p->getEquips())if(e==c)equip=true;if(!equip)return false;
  }
  return v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)
   &&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard()&&c->objectName()==name&&c->isNDTrick();
 };
 for(const auto&v:q.worldView.handCards)if(test(v))return true;
 for(const auto&v:q.worldView.self.equips)if(test(v))return true;
 for(const auto&pile:q.worldView.self.piles)for(const auto&v:pile.cards)if(test(v))return true;
 return false;
}
struct Write {State*s;Room*r;ServerPlayer*p;const AIRequest*q;QByteArray value;quint64 before=0,transport=0;bool reached=false;};
int assign(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"R53 registry assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 if(!w||!live(w->s)||!current(*w->r,w->p,*w->q)||!roots(w->s)
  ||lua_topointer(L,1)!=w->s->choices)
  return luaL_error(L,"R53 registry assignment context changed");
 // Entry revocation belongs to the reached prefix before this new source
 // assignment. Capture once here, before allocation; reads and continuation
 // never resample an interrupted source's transport generation.
 w->transport=w->r->roomRuntime()->xufaTransportEpoch();
 if(!w->transport)return luaL_error(L,"R53 source transport unavailable");
 // Any Lua allocation stays inside this game-VM protected C frame. No error
 // may jump from the game VM through the outer isolated host frame.
 lua_pushlstring(L,w->value.constData(),size_t(w->value.size()));
 lua_pushliteral(L,"xufa");lua_pushvalue(L,-2);
 if(!live(w->s)||w->r->roomRuntime()->xufaTransportEpoch()!=w->transport
  ||!current(*w->r,w->p,*w->q)||!roots(w->s))
  return luaL_error(L,"R53 registry assignment changed during allocation");
 // All allocation / protected-entry observers before this point belong to the
 // reached prefix. Arm only at the exact ordinary source assignment.
 w->s->revoke();w->before=w->s->epoch;w->s->pending=true;w->s->expected=true;w->s->writes=0;
 lua_settable(L,1); // equivalent source assignment; plain registry was proved

 w->s->expected=false;w->reached=true;return 0;
}
}
bool initializeAIRound53Registry(Room&r,lua_State*L,QString*error){
 auto fail=[&](const char*s){if(error)*error=QString::fromLatin1(s);return false;};
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||!L||L!=rt->lua().rawState()||!rt->lua().isCurrentThreadOwner())return fail("R53 source game VM unavailable");
 if(!lua_checkstack(L,32))return fail("R53 source roots stack unavailable");Stack st(L);
 if(auto*old=state(r)){if(live(old)&&roots(old)){if(error)error->clear();return true;}return fail("R53 source roots changed; no recapture after replacement");}
 AIRound45Inspect::globals(L);const void*g=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("R53 source namespace unavailable");const void*ns=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"ai_skill_choice");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return fail("R53 source choice registry is not plain");const void*c=lua_topointer(L,-1);
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();
 // Record only successfully registered identities so partial failure is safe.
 if(lua_observetableidentity(L,g,observe,s))s->globals=g;
 if(lua_observetableidentity(L,ns,observe,s))s->space=ns;
 if(lua_observetableidentity(L,c,observe,s))s->choices=c;
 s->valid=s->globals&&s->space&&s->choices;
 if(!s->valid)return fail("R53 complete source table observers unavailable");
 // No static absent/default grant here: source state is not public merely
 // because bootstrap can read it. Only a reached authorized own write grants.
 if(error)error->clear();return true;
}
void clearAIRound53Registry(Room&r){delete state(r);}
void retireAIRound53Registry(Room&r)noexcept{
 if(auto*s=state(r)){s->pending=false;s->revoke();}
}
ServerPlayer*currentAIRound53XufaWriter(Room&r)noexcept{
 auto*s=state(r);
 return live(s)&&s->coverage&&s->stamp&&s->stamp==s->epoch&&s->writer
  &&s->writer->getRoom()==&r&&s->transport&&s->transport==r.roomRuntime()->xufaTransportEpoch()
  ?s->writer.data():nullptr;
}
quint64 currentAIRound53XufaSerial(Room&r)noexcept{
 auto*s=state(r);return currentAIRound53XufaWriter(r)&&s?s->stamp:0;
}
void enableAIRound53RegistryEntryCoverage(Room&r,bool complete){auto*s=state(r);if(!s)return;s->revoke();s->coverage=complete;}
void enterAIRound53RegistryPolicy(Room&r,ServerPlayer*actor,bool reentrant){auto*s=state(r);if(!s)return;
 if(reentrant||!actor||actor->getRoom()!=&r||(s->writer&&s->writer!=actor)){s->pending=false;s->revoke();}
}
QJsonObject writeAIRound53XufaChoice(Room&r,ServerPlayer*p,const AIRequest&q,const QString&name){
 auto*s=state(r);if(!live(s)||!current(r,p,q)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!p->hasSkill("xufa")||!current(r,p,q)||!disclosedName(r,p,q,name))return unknown();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,512)||!live(s))return unknown();Stack st(L);
 if(!roots(s))return unknown();
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"ai_skill_choice");const int registry=lua_absindex(L,-1);
 const auto bytes=name.toUtf8();Write w{s,&r,p,&q,bytes};
 lua_pushcfunction(L,assign);lua_pushvalue(L,registry);lua_pushlightuserdata(L,&w);
 const int status=LuaRuntime::protectedCall(L,2,0,0);
 const bool own=w.reached&&s->pending&&s->writes==1&&w.before<std::numeric_limits<quint64>::max()&&s->epoch==w.before+1;
 s->pending=false;s->expected=false;
 if(status!=LUA_OK||!own||!live(s)||!w.transport||rt->xufaTransportEpoch()!=w.transport
  ||!current(r,p,q)||!roots(s))return unknown();
 AIRound45Inspect::field(L,registry,"xufa");size_t length=0;const char*value=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&length):nullptr;
 if(!value||length!=size_t(bytes.size())||std::memcmp(value,bytes.constData(),length))return unknown();
 s->writer=p;s->stamp=s->epoch;s->transport=w.transport;
 // Successful actual prefix is independent of whether later continuity has a
 // complete entry proof. No value copy is retained in native or isolated VM.
 return{{"known",true},{"recorded",true}};
}
QJsonObject readAIRound53XufaChoice(Room&r,ServerPlayer*p,const AIRequest&q){
 auto*s=state(r);if(currentAIRound53XufaWriter(r)!=p||!p||!current(r,p,q)
  ||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="xufa")return unknown();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,64))return unknown();Stack st(L);
 if(!roots(s))return unknown();const auto stamp=s->stamp;
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"ai_skill_choice");AIRound45Inspect::field(L,-1,"xufa");
 if(lua_type(L,-1)!=LUA_TSTRING)return unknown();size_t n=0;const char*text=lua_tolstring(L,-1,&n);
 const QString value=QString::fromUtf8(text,int(n));
 if(currentAIRound53XufaWriter(r)!=p||s->stamp!=stamp||s->epoch!=stamp||!current(r,p,q))return unknown();
 return{{"known",true},{"present",true},{"value",value}};
}
