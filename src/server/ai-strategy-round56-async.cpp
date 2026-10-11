#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "lua-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include <QHash>
#include <QSet>
#include <QObject>
#include <QPointer>
namespace {
struct State final : QObject {
 explicit State(Room &r) : QObject(&r), room(&r) {}
 QPointer<Room> room;
 lua_State *lua = nullptr;
 quint64 generation = 0;
 quint64 hostEpoch = 0;
 QSet<const Table*> protectedTables;
 bool attempted = false, valid = false, freshLuaAIOrigin = false, gameplayVariantOrigin = false;
};
QHash<lua_State *,QPointer<State>> states;
thread_local AIRound56AsyncScope *active = nullptr;
State *find(lua_State *L) noexcept {
 if(!L)return nullptr;
 return states.value(G(L)->mainthread,nullptr);
}
bool hook(lua_State *L) noexcept {
 return !lua_gethook(L) || LuaRuntime::InstructionBudgetScope::matches(L)
     || matchesAIRound56OriginalConstructorHook(L);
}
bool finalizer(lua_State *L,int f,int o,bool fresh,bool variants) noexcept {
 return aiRound56NativeFinalizerNonwriter(L,f,o,fresh,variants)
     || luaQsgsStandardIoFinalizer(L,f,o)
     || luaQsgsStandardLoadlibFinalizer(L,f,o);
}
bool seed(State&s,lua_State *L,QString *why) {
 const int top=lua_gettop(L);
 if(!AIRound45Inspect::capacity(L,8)){if(why)*why="async metadata stack capacity";return false;}
 const auto no=[&](const char*s){lua_settop(L,top);if(why)*why=QString::fromLatin1(s);return false;};
 if(!luaQsgsStandardWarningSink(L))return no("unknown Lua warning callback");
 if(!aiRound56RegisteredFinalizerUniverse(L))return no("unproved registered allocation finalizer");
 AIRound45Inspect::field(L,LUA_REGISTRYINDEX,"SWIG");
 if(const auto*t=AIRound45Inspect::table(L,-1))s.protectedTables.insert(t);
 lua_settop(L,top);
 auto*g=G(L);
 if(ttistable(&g->l_registry))s.protectedTables.insert(hvalue(&g->l_registry));
 if(!hook(g->mainthread))return no("unproved main thread hook");
 for(auto*first:{g->allgc,g->finobj,g->tobefnz,g->fixedgc})for(auto*o=first;o;o=o->next){
  if(o->tt==LUA_VTHREAD&&!hook(gco2th(o)))return no("unproved thread hook");
  Table*mt=o->tt==LUA_VUSERDATA?gco2u(o)->metatable:o->tt==LUA_VTABLE?gco2t(o)->metatable:nullptr;
  if(mt)s.protectedTables.insert(mt);
  if(o->tt==LUA_VTABLE){
   // Also protect registered allocation metatables not yet used by an object.
   sethvalue2s(L,L->top.p,gco2t(o));++L->top.p;
   AIRound45Inspect::field(L,-1,"__gc");
   if(!lua_isnil(L,-1))s.protectedTables.insert(gco2t(o));
   lua_settop(L,top);
  }
 }
 for(auto*first:{g->finobj,g->tobefnz})for(auto*o=first;o;o=o->next){
  Table*mt=o->tt==LUA_VUSERDATA?gco2u(o)->metatable:o->tt==LUA_VTABLE?gco2t(o)->metatable:nullptr;
  if(!mt)continue;
  if(o->tt==LUA_VTABLE)s.protectedTables.insert(gco2t(o));
  sethvalue2s(L,L->top.p,mt);++L->top.p;
  AIRound45Inspect::field(L,-1,"__gc");
  if(!lua_isnil(L,-1)){
   setgcovalue(L,s2v(L->top.p),o);++L->top.p;
   // Fresh owned LuaAI is deliberately not admitted at bootstrap. Its later
   // allocation has a separate exact constructor/capture-origin obligation.
   if(!finalizer(L,-2,-1,false,s.gameplayVariantOrigin))return no("unproved bootstrap finalizer effect");
  }
  lua_settop(L,top);
 }
 return true;
}
}
AIRound56BootstrapSourceProof::AIRound56BootstrapSourceProof(
 Room&r,lua_State*L,quint64 generation,quint64 serial,Current current,bool gameplayVariants)
 :m_roomLifetime(&r),m_room(&r),m_lua(L),m_generation(generation),m_serial(serial),
  m_current(current),m_gameplayVariants(gameplayVariants){}
bool initializeAIRound56AsyncUniverse(Room&,lua_State*,QString*why){
 if(why)*why="complete bootstrap source-union token required";
 return false;
}
bool initializeAIRound56AsyncUniverse(Room&r,lua_State*L,
                                     AIRound56BootstrapSourceProof&proof,QString*why){
 if(why)why->clear();
 auto*rt=r.roomRuntime();
 if(!L||!rt||rt->lua().state()!=L||proof.m_consumed||proof.m_room!=&r
    ||proof.m_roomLifetime.data()!=static_cast<QObject*>(&r)
    ||proof.m_lua!=L||proof.m_generation!=rt->lua().generation()
    ||!proof.m_serial||!proof.m_current||!proof.m_current(r,L,proof.m_serial)){
  if(why)*why="complete bootstrap source-union proof absent or retired";
  return false;
 }
 proof.m_consumed=true;
 State*s=find(L);if(s&&s->attempted){if(why)*why="async bootstrap already attempted";return false;}
 if(!s){s=new State(r);s->lua=L;states.insert(L,s);}
 s->attempted=true;s->generation=r.roomRuntime()->lua().generation();
 s->hostEpoch=aiRound56ServerMessageSinkEpoch();
 s->gameplayVariantOrigin=proof.m_gameplayVariants;
 s->valid=seed(*s,L,why);return s->valid;
}
bool hasAIRound56AsyncUniverse(const Room&r,lua_State*L) noexcept {
 // Room exposes its read-only runtime lookup through a nonconst override.
 auto*rt=const_cast<Room&>(r).roomRuntime();
 auto*s=find(L);return s&&s->valid&&s->room==&r&&s->lua==L
  &&rt&&rt->lua().generation()==s->generation
  &&s->hostEpoch==aiRound56ServerMessageSinkEpoch()
  &&proveAIRound56ServerLogEffects(r);
}
bool isAIRound56AsyncProtectedTable(const Room&r,lua_State*L,int index) noexcept {
 auto*s=find(L);
 if(!hasAIRound56AsyncUniverse(r,L)||!s)return true;
 const Table*t=AIRound45Inspect::table(L,index);
 return t&&s->protectedTables.contains(t);
}
void retireAIRound56AsyncUniverse(lua_State*L) noexcept {if(auto*s=find(L))s->valid=false;}
AIRound56AsyncScope::AIRound56AsyncScope(Room&r,lua_State*L,Products products)
 :m_previous(active),m_lua(L){
 if(!hasAIRound56AsyncUniverse(r,L))return;
 m_state=find(L);auto*s=static_cast<State*>(m_state);
 // This extends the SOURCE-PROVED allocation universe before allocation;
 // it does not inspect, adopt or sample a privately selected existing object.
 if(int(products)&int(FreshLuaAI))s->freshLuaAIOrigin=true;
 if(int(products)&int(GameplayVariants))s->gameplayVariantOrigin=true;
 m_active=true;active=this;
}
AIRound56AsyncScope::~AIRound56AsyncScope(){if(m_active)active=m_previous;}
void observeAIRound56AsyncProtectedEntry(lua_State*L) noexcept {
 auto*s=find(L);
 if(!s)return;
 if(!active||active->m_state!=s||active->m_lua!=L)s->valid=false;
}
bool acceptAIRound56AsyncEntry(lua_State*L,int kind) noexcept {
 auto*s=find(L);
 if(!s||!s->valid)return false;
 if(!active||active->m_state!=s||active->m_lua!=L){s->valid=false;return false;}
 if(!s->room||!hasAIRound56AsyncUniverse(*s->room,L)){s->valid=false;return false;}
 if(kind==4){if(hook(L))return true;s->valid=false;return false;}
 if(kind!=3||lua_gettop(L)<2||!luaQsgsStandardWarningSink(L)){
  s->valid=false;return false;
 }
 // Defensive verification only: authorization came from the entire seeded
 // universe + allocation graph before selection, never this observed branch.
 if(finalizer(L,-2,-1,s->freshLuaAIOrigin,s->gameplayVariantOrigin))return true;
 s->valid=false;return false;
}
