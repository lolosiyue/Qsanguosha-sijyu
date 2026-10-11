#include "ai-strategy-round45-poxi-slot.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QSet>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
constexpr const char *slot = "poxi_target";
struct Stack {lua_State *L;int top;~Stack(){lua_settop(L,top);}};
QJsonObject unknown(const char *reason){return{{"known",false},{"reason",QString::fromLatin1(reason)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&aiRound44RangeRequestCurrent(r,q,p);
}
bool sourceScope(const AIRequest&q,const ServerPlayer*p){
 const auto exact=[&](const AiSkillActionContext&a){return a.isValid()
  &&a.activationRef==a.sourceRef&&a.getActivationOwner()==p->objectName()
  &&a.getActivationSkillName()=="poxi";};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;
 return false;
}
bool publicPlayer(const AIRequest&q,const ServerPlayer*p){
 if(!p)return false;
 if(q.worldView.self.objectName==p->objectName())return true;
 for(const auto&v:q.worldView.players)if(v.objectName==p->objectName())return true;
 return false;
}
// Implement the exact table-valued __index chain, never call authored getters.
// A missing raw slot is NOT automatically nil: inherited fields are observed.
bool pushField(lua_State*L,int self){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;
  seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);
  AIRound45Inspect::field(L,row,slot);
  if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);
  if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}
  AIRound45Inspect::field(L,-1,"__index");
  if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false;
 }
 return false;
}
bool ordinaryAssignment(lua_State*L,int self){
 AIRound45Inspect::field(L,self,slot);const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(present)return true; // Lua does not consult __newindex for a present raw key.
 if(!lua_getmetatable(L,self))return true;
 AIRound45Inspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);
 return absent; // Do not bypass a function OR table-valued assignment handler.
}
struct Write {Room*r;const AIRequest*q;ServerPlayer*owner;ServerPlayer*target;const char*failure=nullptr;};
int assignmentError(lua_State*L,Write*w,const char*reason){
 // Only our fixed native failure codes cross back to isolated Lua. Never copy
 // arbitrary authored Lua/GC error strings, userdata or private payloads.
 w->failure=reason;return luaL_error(L,"%s",reason);
}
// Only this finite native thunk runs in the game VM; never a legacy strategy.
int assign(lua_State*L){
 auto*w=static_cast<Write*>(lua_touserdata(L,1));
 // luaD_precall creates a fresh C CallInfo with LUA_MINSTACK. The caller's
 // reserve is not this frame's inspect capacity; reserve inside the thunk.
 if(!lua_checkstack(L,2048))return assignmentError(L,w,"Poxi assignment frame capacity unavailable");
 lua_pushstring(L,slot);const int key=lua_absindex(L,-1);
 if(w->target){if(!pushAIRound45SlotPlayer(L,w->target))return assignmentError(L,w,"Poxi Player binding unavailable");}
 else lua_pushnil(L);
 const int value=lua_absindex(L,-1);
 // Any allocation/finalizer or protected-entry observer ran BEFORE this exact
 // self/request reauthentication. The setter itself cannot call __newindex.
 if(!current(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi current request changed before assignment");
 if(!pushAIRound45ActualAISelf(*w->r,w->owner,L))
  return assignmentError(L,w,"Poxi actual LuaAI receiver authentication failed before assignment");
 const int self=lua_absindex(L,-1);
 if(!ordinaryAssignment(L,self)||!current(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi source assignment metamethod is not certified");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 // Preserve any reached assignment on later error, like the original body.
 if(!current(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi current request changed after assignment");
 if(!pushAIRound45ActualAISelf(*w->r,w->owner,L)||!lua_rawequal(L,self,-1))
  return assignmentError(L,w,"Poxi actual LuaAI receiver authentication failed after assignment");
 AIRound45Inspect::field(L,self,slot);
 if(w->target?!matchAIRound45SlotPlayer(L,-1,w->target):!lua_isnil(L,-1))
  return assignmentError(L,w,"Poxi actual assigned slot changed");
 return 0;
}
}
QJsonObject queryAIRound45PoxiSlot(Room&r,const AIRequest&q,ServerPlayer*p,
 const QString&op,const QString&name){
 if(!current(r,q,p))return unknown("Poxi slot requires current native owner request");
 const bool write=op=="write";
 if(write){
  if(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty()
   ||p->getPhase()!=Player::Play||!sourceScope(q,p)||!current(r,q,p))
   return unknown("Poxi writer requires current own Play source");
 }else if(op!="read"||!name.isEmpty()||q.kind!=AIRequest::UseCard||q.pattern!="@@poxi")
  return unknown("Poxi reader requires exact current response question");
 ServerPlayer*target=nullptr;
 if(write&&!name.isEmpty()){
  target=r.findPlayerByObjectName(name,true);
  if(!target||!publicPlayer(q,target))return unknown("Poxi assigned Player is not current public roster");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);lua_State*L=vm.state();
 if(!L||(!Sanguosha||Sanguosha->currentRoom()!=&r)||!lua_checkstack(L,2048))return unknown("Poxi game VM binding unavailable");
 Stack restore{L,lua_gettop(L)};
 if(write){
  Write w{&r,&q,p,target};lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.failure?w.failure:"Poxi protected source assignment failed outside fixed native guards");
  return current(r,q,p)?QJsonObject{{"known",true}}:unknown("Poxi source assignment changed current request");
 }
 if(!current(r,q,p)||!pushAIRound45ActualAISelf(r,p,L))return unknown("Poxi actual own LuaAI self unavailable");
 const int self=lua_absindex(L,-1);
 if(!pushField(L,self))return unknown("Poxi source slot requires an unproved dynamic lookup");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)))
  return{{"known",true},{"value",false}};
 // Only exact live roster identities; never derive this value from current
 // prompt target, poxiForAI, notified hand, or saved JSON/name memory.
 for(auto*actual:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,-1,actual)){
  if(!publicPlayer(q,actual)||!current(r,q,p))return unknown("Poxi actual slot Player is not current public roster");
  // Source next invokes this actual saved userdata's getCards("h"). Matching
  // only its pointer/type cannot justify substituting a native projection if
  // authored Lua replaced the SWIG metatable/class method dispatch.
  if(!matchAIRound45NativeObjectMethods(r,L,-1,"ServerPlayer",{"getCards","objectName"}))
   return unknown("Poxi actual Player source method binding changed");
  return{{"known",true},{"value",actual->objectName()}};
 }
 return unknown("Poxi source slot is not nil or an authenticated roster Player");
}
