#include "ai-strategy-round44-focus.h"
#include "ai-strategy-round45-natural-event.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "lobject.h" // native-only immutable child Proto identity; no Lua-facing capability
#include <QHash>
#include <QPointer>
#include <QScopeGuard>
#include <limits>
#include <atomic>
#include "ai-strategy-round45-lua-inspect.h"

namespace {
char capturedKey;
std::atomic<quint64> sequence{0};
constexpr int filterRef=-1, pcallRef=-2, executionRef=-3;
int depth(lua_State*L){lua_Debug a{};int n=0;while(lua_getstack(L,n,&a))++n;return n;}
bool caller(lua_State*L,int level=1,int reference=filterRef){
 lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
 if(!lua_istable(L,-1)){lua_pop(L,2);return false;}
 lua_rawgeti(L,-1,reference);bool ok=lua_rawequal(L,-1,-3);lua_pop(L,3);return ok;
}
// lua_topointer returns the actual LClosure GC object for a verified Lua
// function (lapi.c). A new invocation creates a new closure, but its child
// Proto must be the immutable child of the exact captured filter closure.
// Source/line metadata alone would permit a same-name loaded impostor.
bool authenticBody(lua_State*L,int level){
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a)
    ||!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return false;
 const auto *body=static_cast<const LClosure *>(lua_topointer(L,-1));
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!lua_istable(L,-1))return false;
 lua_rawgeti(L,-1,filterRef);if(!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return false;
 const auto *filter=static_cast<const LClosure *>(lua_topointer(L,-1));
 for(int i=0;i<filter->p->sizep;++i)
  if(filter->p->p[i]==body->p&&body->p->linedefined==2491)return true;
 return false;
}
thread_local QHash<Room *,QList<const AIRound44FocusFrame *>> frames;
bool plain(lua_State *L,int i){if(!lua_istable(L,i))return false;if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;}
void rawfield(lua_State *L,int i,const char*k){i=lua_absindex(L,i);lua_pushstring(L,k);lua_rawget(L,i);}
bool source(lua_State *L,int i,int first,int last){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug info{};
 return lua_getinfo(L,">S",&info)&&QString::fromUtf8(info.source?info.source:"").endsWith("/ai/imagine-ai.lua")
  &&info.linedefined==first&&info.lastlinedefined==last;
}
bool filterSource(lua_State *L,int i){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug info{};
 return lua_getinfo(L,">S",&info)&&QString::fromUtf8(info.source?info.source:"").endsWith("/ai/smart-ai.lua")
  &&info.linedefined==2477&&info.lastlinedefined==2867;
}
bool currentCallback(lua_State *L,int event,int reference){
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 // The SWIG sgs namespace has a native metatable. Only raw fields are read;
 // the callback registry and event rows must still be plain tables.
 lua_pushglobaltable(L);rawfield(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 rawfield(L,-1,"ai_event_callback");if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,event);if(!plain(L,-1))return false;rawfield(L,-1,"general");
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,reference);return lua_rawequal(L,-1,-3);
}
}
struct AIRound44FocusFrame::Data {
 Room *room=nullptr;QPointer<ServerPlayer> actor;QString mark;const QVariant *data=nullptr;int event=0,filterDepth=0;quint64 serial=0;const void *filterBody=nullptr;bool valid=false,attempted=false;
};
struct AIRound44FocusAccess {
 static AIRound44FocusFrame::Data *current(Room *room){
  auto it=frames.constFind(room);return it==frames.cend()||it->isEmpty()?nullptr:it->last()->d.get();
 }
 static int enter(lua_State*L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));auto*f=current(room);
  if(f&&f->valid&&!f->filterDepth&&caller(L,3)&&caller(L,2,pcallRef)&&authenticBody(L,1)){
   lua_Debug a{};if(lua_getstack(L,1,&a)&&lua_getinfo(L,"f",&a)){
    f->filterBody=lua_topointer(L,-1);lua_pop(L,1);f->filterDepth=depth(L);
   }
  }
  return 0;
 }
 static bool bodyCaller(lua_State*L,const AIRound44FocusFrame::Data*f,int level=1){
  lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a))return false;
  const bool same=lua_topointer(L,-1)==f->filterBody;lua_pop(L,1);return same;
 }
 static int select(lua_State*L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));auto*f=current(room);
  if(lua_gettop(L)!=1||!lua_isfunction(L,1)){lua_pushnil(L);return 1;}
  if(!f||!f->valid||f->attempted||!f->filterDepth||f->filterDepth!=depth(L)||!bodyCaller(L,f)||!caller(L,2,pcallRef)||!caller(L,3)){
   lua_pushvalue(L,1);return 1;
  }
  lua_pushglobaltable(L);rawfield(L,-1,"pcall");lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
  if(!plain(L,-1)){lua_settop(L,1);lua_pushvalue(L,1);return 1;}
  lua_rawgeti(L,-1,pcallRef);const bool protectedCall=lua_rawequal(L,-1,-3);lua_settop(L,1);
  lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);lua_rawgeti(L,-1,f->event);
  const bool same=lua_rawequal(L,1,-1);lua_settop(L,1);
  if(!protectedCall||!same||!currentCallback(L,f->event,f->event)){lua_pushvalue(L,1);return 1;}
  lua_pushlightuserdata(L,room);lua_pushinteger(L,lua_Integer(f->serial));lua_pushvalue(L,1);
  lua_pushcclosure(L,&invoke,3);return 1;
 }
 static int invoke(lua_State *L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));
  auto *f=current(room);
  if(lua_gettop(L)!=3||!f||!f->valid||f->attempted||!f->actor
     ||f->serial!=quint64(lua_tointeger(L,lua_upvalueindex(2)))
     ||f->actor->getRoom()!=room||depth(L)!=f->filterDepth+1
     ||!caller(L,1,pcallRef)||!bodyCaller(L,f,2)||!caller(L,3,pcallRef)||!caller(L,4)){return luaL_error(L,"Imagine native callback scope expired");}
  lua_pushvalue(L,lua_upvalueindex(3));lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
  lua_rawgeti(L,-1,f->event);const bool same=lua_rawequal(L,-1,-3);lua_pop(L,3);
  if(!same||!currentCallback(L,f->event,f->event)){return luaL_error(L,"Imagine callback identity changed");}
  // No value supplied by Lua controls target, mark, count or current baseline.
  // Native frame is entered only for this genuine target filterEvent.
  // QVariant is the exact Lua-owned argument copied by LuaAI::filterEvent.
  // Earlier sibling mutations are observed here, not at frame creation.
  int amount;
  if(f->event==int(Damaged))amount=f->data->value<DamageStruct>().damage;
  else amount=f->data->value<RecoverStruct>().recover; // exact QVariant::toRecover value conversion
  const int before=f->actor->getMark(f->mark);
  const qint64 next=qint64(before)+qint64(amount);
  if(next<std::numeric_limits<int>::min()||next>std::numeric_limits<int>::max())
   return luaL_error(L,"Imagine Focus sum is outside native signed integer domain");
  f->attempted=true; // before native reentry: veto/throw is still a reached call.
  lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);lua_rawgeti(L,-1,executionRef);
  const lua_Integer executions=lua_tointeger(L,-1);lua_pop(L,1);
  lua_pushinteger(L,executions<std::numeric_limits<lua_Integer>::max()?executions+1:executions);lua_rawseti(L,-2,executionRef);lua_pop(L,1);
  room->setPlayerMark(f->actor,f->mark,int(next));
  return 0;
 }
};
AIRound44FocusFrame::AIRound44FocusFrame(Room&r,ServerPlayer*p,int event,const QVariant&v):d(new Data){
 d->room=&r;d->actor=p;d->event=event;d->data=&v;d->serial=++sequence;frames[&r].append(this);
 if(!p||p->getRoom()!=&r||!r.getAllPlayers(true).contains(p))return;
 if(event==int(Damaged)&&v.canConvert<DamageStruct>()){
  d->mark="FocusDamageCount";d->valid=true;
 }else if(event==int(HpRecover)&&v.canConvert<RecoverStruct>()){
  d->mark="FocusRecoverCount";d->valid=true;
 }
}
AIRound44FocusFrame::~AIRound44FocusFrame(){auto it=frames.find(d->room);if(it==frames.end())return;it->removeAll(this);if(it->isEmpty())frames.erase(it);}
void installAIRound44FocusCallbacks(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 // The SWIG sgs namespace has a native metatable. Only raw fields are read;
 // the callback registry and event rows must still be plain tables.
 lua_pushglobaltable(L);rawfield(L,-1,"sgs");if(!lua_istable(L,-1))return;
 rawfield(L,-1,"ai_event_callback");if(!plain(L,-1))return;
 const int callbacks=lua_gettop(L);lua_newtable(L);const int refs=lua_gettop(L);
 for(int event:{int(Damaged),int(HpRecover)}){
  lua_rawgeti(L,callbacks,event);if(!plain(L,-1)){lua_pop(L,1);continue;}
  rawfield(L,-1,"general");const int first=event==int(Damaged)?756:762;
  if(source(L,-1,first,first+5)){lua_pushvalue(L,-1);lua_rawseti(L,refs,event);}
  lua_pop(L,2);
 }
 lua_pushglobaltable(L);rawfield(L,-1,"SmartAI");
 if(lua_istable(L,-1)){
  rawfield(L,-1,"filterEvent");
  if(!lua_isfunction(L,-1)){lua_pop(L,1);rawfield(L,-1,"__instanceDict");if(lua_istable(L,-1))rawfield(L,-1,"filterEvent");}
  if(filterSource(L,-1)){lua_pushvalue(L,-1);lua_rawseti(L,refs,filterRef);}
 }
 lua_settop(L,refs);lua_pushglobaltable(L);rawfield(L,-1,"pcall");
 if(lua_iscfunction(L,-1)){lua_pushvalue(L,-1);lua_rawseti(L,refs,pcallRef);}
 lua_settop(L,refs);lua_pushvalue(L,refs);lua_rawsetp(L,LUA_REGISTRYINDEX,&capturedKey);
 lua_pushlightuserdata(L,&r);lua_pushcclosure(L,&AIRound44FocusAccess::select,1);
 lua_setglobal(L,"ai_imagine_focus_callback");
 lua_pushlightuserdata(L,&r);lua_pushcclosure(L,&AIRound44FocusAccess::enter,1);lua_setglobal(L,"ai_imagine_focus_enter");
}
bool hasAIRound44FocusCallbacks(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return false;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return false;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);rawfield(L,-1,"ai_imagine_focus_callback");
 if(lua_tocfunction(L,-1)!=&AIRound44FocusAccess::select)return false;
 if(!lua_getupvalue(L,-1,1)||lua_touserdata(L,-1)!=&r)return false;
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"ai_imagine_focus_enter");
 if(lua_tocfunction(L,-1)!=&AIRound44FocusAccess::enter)return false;
 if(!lua_getupvalue(L,-1,1)||lua_touserdata(L,-1)!=&r)return false;
 // Capability must attest the current dispatcher route, not merely a
 // retained old function that select/invoke would refuse to execute.
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"pcall");
 if(!lua_iscfunction(L,-1))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,pcallRef);if(!lua_rawequal(L,-1,-3))return false;
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"SmartAI");
 if(!lua_istable(L,-1))return false;
 rawfield(L,-1,"filterEvent");
 if(!lua_isfunction(L,-1)){
  lua_pop(L,1);rawfield(L,-1,"__instanceDict");
  if(!lua_istable(L,-1))return false;
  rawfield(L,-1,"filterEvent");
 }
 if(!lua_isfunction(L,-1))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,filterRef);if(!lua_rawequal(L,-1,-3))return false;
 return currentCallback(L,int(Damaged),int(Damaged))&&currentCallback(L,int(HpRecover),int(HpRecover));
}

quint64 aiRound44FocusExecutionSerial(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return 0;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return 0;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return 0;
 lua_rawgeti(L,-1,executionRef);return quint64(lua_tointeger(L,-1));
}

// No authored function, semantic read hook, metamethod or Lua GC is called.
// The central observer provides preallocated Lua stack space before entry.
// A pre-entry checker must separately seal the full filterEvent graph.
bool aiRound44FocusContinuityNoop(Room&r,lua_State*L,int argumentCount) {
 AIRound45NaturalFilterProof proof;
 if(!readAIRound45NaturalFilterEntry(r,L,argumentCount,&proof)
    ||proof.event==int(Damaged)||proof.event==int(HpRecover))return false;
 auto *f=AIRound44FocusAccess::current(&r);
 if(!f||f->valid||f->event!=proof.event||!f->actor
    ||!matchesAIRound45NaturalFilterPayload(r,L,argumentCount,f->data,f->actor))return false;
 return aiRound56FocusBindings(r,L);
}
bool aiRound56FocusBindings(Room&r,lua_State*L) noexcept {
 if(!AIRound45Inspect::capacity(L,4))return false;
 const int top=lua_gettop(L);
 const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 AIRound45Inspect::globals(L);
 if(!AIRound45Inspect::table(L,-1))return false;
 auto exact=[&](const char *name,lua_CFunction function){
  AIRound45Inspect::field(L,-1,name);
  const TValue *v=AIRound45Inspect::value(L,-1);
  bool ok=false;
  if(ttisCclosure(v)){
   const CClosure *c=clCvalue(v);
   ok=c->f==function&&c->nupvalues==1&&ttislightuserdata(&c->upvalue[0])
      &&pvalue(&c->upvalue[0])==&r;
  }
  lua_pop(L,1);return ok;
 };
 return exact("ai_imagine_focus_enter",&AIRound44FocusAccess::enter)
     &&exact("ai_imagine_focus_callback",&AIRound44FocusAccess::select);
}
