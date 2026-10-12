#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-playloop-filter.h"
#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-async.h"
#include <cstdio>
#ifdef __linux__
#include <execinfo.h>
#endif
#include "ai-strategy-round56-connection-cache.h"
#include "ai-strategy-round53-targets.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-bootstrap-source.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round54-priority.h"
#include "ai-strategy-round54-shebian.h"
#include "ai-strategy-round54-jijiang.h"
#include "ai-strategy-round53-pingjian.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round47-xufa-event-scope.h"
#include "ai-strategy-round47-xufa-query-scope.h"
#include "ai-strategy-round45-continuity.h"
#include "ai-strategy-round37-response-init.h"
#include "ai-strategy-round56-clone-scope.h"
#include "ai-strategy-round53-registry.h"
#include "ai-strategy-round41-response.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QObject>
#include <QHash>
#include <QVector>
#include "lua.hpp"
#include <cstring>
#include <QSet>
#include <QPointer>
namespace {
// Raw metadata only: model the audited middleclass lookup, never call __index.
struct StackRestore { lua_State*L;int top;explicit StackRestore(lua_State*s):L(s),top(lua_gettop(s)){}~StackRestore(){lua_settop(L,top);} };
bool origin(lua_State*L,int i,const char*source,int line){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug d{};return lua_getinfo(L,">S",&d)&&(QString::fromUtf8(d.source?d.source:"")==QString::fromLatin1(source)||(QString::fromLatin1(source)=="@lua/lib/middleclass.lua"&&QString::fromUtf8(d.source?d.source:"")=="@./lua/lib/middleclass.lua"))&&d.linedefined==line;
}
void raw(lua_State*L,int i,const char*key){i=lua_absindex(L,i);lua_pushstring(L,key);lua_rawget(L,i);}
bool upvalue(lua_State*L,int i,const char*key){
 i=lua_absindex(L,i);for(int n=1;;++n){const char*k=lua_getupvalue(L,i,n);if(!k)return false;if(!strcmp(k,key))return true;lua_pop(L,1);}
}
// Returns exactly one value on success; recursion is finite and fail-closed.
bool lookup(lua_State*L,int i,const char*key,int depth=0){
 if(depth>12||!lua_istable(L,i))return false;i=lua_absindex(L,i);
 const int top=lua_gettop(L);raw(L,i,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
 if(!lua_getmetatable(L,i)){lua_pushnil(L);return true;}
 raw(L,-1,"__index");
 if(lua_isnil(L,-1)){lua_settop(L,top);lua_pushnil(L);return true;}
 bool ok=false;
 if(lua_istable(L,-1))ok=lookup(L,-1,key,depth+1);
 else if(origin(L,-1,"@lua/lib/middleclass.lua",33)||origin(L,-1,"@lua/lib/middleclass.lua",35)){
  const int fn=lua_absindex(L,-1);const bool inherited=origin(L,fn,"@lua/lib/middleclass.lua",33);
  if(upvalue(L,fn,"dict")&&lookup(L,-1,key,depth+1)){
   if(lua_toboolean(L,-1)||!inherited)ok=true;
   else if(upvalue(L,fn,"superStatic"))ok=lookup(L,-1,key,depth+1);
  }
 }
 if(!ok){lua_settop(L,top);return false;}lua_replace(L,top+1);lua_settop(L,top+1);return true;
}
bool initializer(lua_State*L,int i,QVector<const void*>&proof,int depth=0){
 if(depth>3)return false;i=lua_absindex(L,i);proof<<lua_topointer(L,i);
 if(origin(L,i,"@lua/ai/smart-ai.lua",339))return true;
 const char*up=nullptr;
 if(origin(L,i,"@lua/ai/original-hegemony/heg-common-ai.lua",192))up="initialize";
 else if(origin(L,i,"@lua/ai/basara-ai.lua",58))up="init";
 if(!up||!upvalue(L,i,up))return false;bool ok=initializer(L,-1,proof,depth+1);lua_pop(L,1);return ok;
}
QVector<const void*> constructorProof(lua_State*L){
 StackRestore stack(L);QVector<const void*> proof;
 lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 raw(L,globals,"CloneAI");if(!origin(L,-1,"@lua/ai/smart-ai.lua",23))return{};proof<<lua_topointer(L,-1);
 raw(L,globals,"SmartAI");if(!lua_istable(L,-1))return{};const int cls=lua_absindex(L,-1);proof<<lua_topointer(L,cls);
 if(!lua_getmetatable(L,cls))return{};raw(L,-1,"__call");if(!origin(L,-1,"@lua/lib/middleclass.lua",44))return{};proof<<lua_topointer(L,-1);
 for(const auto&entry:{qMakePair("new",103),qMakePair("allocate",98)}){
  if(!lookup(L,cls,entry.first)||!origin(L,-1,"@lua/lib/middleclass.lua",entry.second))return{};proof<<lua_topointer(L,-1);
 }
 raw(L,cls,"__instanceDict");if(!lua_istable(L,-1))return{};const int dict=lua_absindex(L,-1);proof<<lua_topointer(L,dict);
 raw(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return{};
 if(!lookup(L,dict,"initialize")||!origin(L,-1,"@lua/ai/original-hegemony/heg-common-ai.lua",192)||!initializer(L,-1,proof))return{};proof<<lua_topointer(L,-1);
 if(!lookup(L,dict,"updatePlayers")||!(origin(L,-1,"@lua/ai/mode-ai.lua",2330)||origin(L,-1,"@lua/ai/smart-ai.lua",2118)))return{};proof<<lua_topointer(L,-1);
 // The userdata factory is trusted at startup, and must remain the same C function.
 raw(L,globals,"sgs");if(!lua_istable(L,-1))return{};raw(L,-1,"LuaAI");proof<<lua_topointer(L,-1);
 if(!lua_iscfunction(L,-1)){
  if(!lua_istable(L,-1)||!lua_getmetatable(L,-1))return{};
  raw(L,-1,"__call");if(!lua_iscfunction(L,-1))return{};proof<<lua_topointer(L,-1);
 }
 return proof;
}

struct Row {
 QString state; quint64 stamp=0, writes=0;
 const void*selfIdentity=nullptr;const void*listIdentity=nullptr;
 bool constructorEmpty=false;
 // Native table still is the exact untouched constructor-empty object. This
 // is separate from the isolated shadow state, which may have been cleared.
 bool mirrorEmpty=false,shadowDiverged=false;
};
struct Capture;
struct ChoiceProxyProof {
 const void*proxy=nullptr;const void*meta=nullptr;const void*values=nullptr;const void*writer=nullptr;
 bool operator==(const ChoiceProxyProof&o)const{return proxy==o.proxy&&meta==o.meta&&values==o.values&&writer==o.writer;}
};
struct Watched { enum Kind { Self, List, Metadata, Registry, Namespace, Globals, Choice, ChosenPlayer, ChosenCard, IsolatedGlobals, IsolatedNamespace, ChoiceProxy, ChosenPlayerProxy, ChosenCardProxy, ChoiceMeta, ChosenPlayerMeta, ChosenCardMeta, IsolatedCatalogue } kind; const ServerPlayer*owner=nullptr; bool isolated=false; };
class Ledger final:public QObject {
public:
 explicit Ledger(Room&r):QObject(&r),room(r){}
 ~Ledger()override;
 Room&room;bool initialized=false,interrupted=false,watchRoots=false,constructorEntryPending=false;quint64 generation=0,invalidations=0;
 QVector<const void*> proof;const ServerPlayer*pending=nullptr;
 QHash<const ServerPlayer*,Row> rows;
 QHash<const void*,Watched> watched;
 lua_State*state=nullptr;Capture*capture=nullptr;
 lua_State*choiceState=nullptr;quint64 choiceGeneration=0;bool choiceCaptured=false,choiceValid=false;
 const void*choiceGlobals=nullptr;const void*choiceCatalogue=nullptr;
 QHash<QString,ChoiceProxyProof>choiceProxies;
 void invalidate(const ServerPlayer*p){auto it=rows.find(p);if(it!=rows.end()){it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;++it->stamp;++it->writes;}}
 void invalidateAll(){for(auto it=rows.begin();it!=rows.end();++it){it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;++it->stamp;++it->writes;}}
};
struct Capture {
 Ledger*l=nullptr;const ServerPlayer*p=nullptr;const void*self=nullptr;const void*list=nullptr;const void*baseFunction=nullptr;
 bool assignment=false,registered=false,failed=false;
};
thread_local QHash<lua_State*,Capture*> captures;
void observeTable(lua_State*,const lua_TableObservation*,void*) noexcept;
bool watch(Ledger*l,const void*id,Watched w,lua_State*vm=nullptr){
 if(!id)return false;
 auto it=l->watched.constFind(id);
 if(it!=l->watched.cend())return it->kind==w.kind&&it->owner==w.owner&&it->isolated==w.isolated;
 if(!lua_observetableidentity(vm?vm:l->state,id,observeTable,l))return false;
 try{l->watched.insert(id,w);}catch(...){lua_unobservetableidentity(l->state,id,observeTable,l);return false;}
 return true;
}
void unwatchOwner(Ledger*l,const ServerPlayer*p){
 for(auto it=l->watched.begin();it!=l->watched.end();){
  if(it->owner==p){lua_unobservetableidentity(l->state,it.key(),observeTable,l);it=l->watched.erase(it);}else ++it;
 }
}
Ledger::~Ledger(){
 // FREE notifications removed dead identities synchronously. Only live tables
 // remain, so pointer reuse or a closed Lua state is never dereferenced here.
 for(auto it=watched.cbegin();it!=watched.cend();++it)lua_unobservetableidentity(state,it.key(),observeTable,this);
 watched.clear();
}
bool sameKey(const lua_TableObservation&e,const char*key){return e.string_key&&e.string_key_length==strlen(key)&&!memcmp(e.string_key,key,e.string_key_length);}
bool sourceAssignment(lua_State*L,const Capture*c,const lua_TableObservation&e){
 // Protect leaves top at ci->top: never push a function from this callback.
 // The VM supplies current Lua closure identity without touching its stack.
 lua_Debug d{};
 return e.lua_function_identity==c->baseFunction&&lua_getstack(L,0,&d)
  &&lua_getinfo(L,"Sl",&d)&&d.currentline==418&&d.linedefined==339
  &&d.source&&!strcmp(d.source,"@lua/ai/smart-ai.lua");
}
// Structural checks complement an identity captured at the trusted native core
// load boundary. Source path/line alone is explicitly not the attestation.
bool choiceProxyShape(lua_State*L,int proxy,ChoiceProxyProof&out){
 StackRestore restore(L);proxy=lua_absindex(L,proxy);if(!lua_istable(L,proxy))return false;
 lua_pushnil(L);if(lua_next(L,proxy)){return false;} // raw proxy must stay empty
 if(!lua_getmetatable(L,proxy))return false;const int mt=lua_absindex(L,-1);
 if(lua_getmetatable(L,mt))return false;
 int count=0;lua_pushnil(L);while(lua_next(L,mt)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*k=lua_tostring(L,-2);
  if(strcmp(k,"__index")&&strcmp(k,"__newindex"))return false;++count;lua_pop(L,1);
 }if(count!=2)return false;
 raw(L,mt,"__index");if(!lua_istable(L,-1))return false;const int values=lua_absindex(L,-1);
 if(lua_getmetatable(L,values))return false;
 raw(L,mt,"__newindex");const int writer=lua_absindex(L,-1);
 if(!origin(L,writer,"@lua/ai/isolated/ask-for-choice.lua",29))return false;
 lua_Debug info{};lua_pushvalue(L,writer);if(!lua_getinfo(L,">S",&info)||info.lastlinedefined!=36)return false;
 bool sawValues=false,sawEnvironment=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,writer,i);if(!k)break;
  if(!strcmp(k,"values")){if(sawValues||!lua_rawequal(L,-1,values))return false;sawValues=true;}
  else if(!strcmp(k,"_ENV")){lua_pushglobaltable(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,1);if(sawEnvironment||!same)return false;sawEnvironment=true;}
  else return false;lua_pop(L,1);
 }
 if(!sawValues||!sawEnvironment)return false;
 out={lua_topointer(L,proxy),lua_topointer(L,mt),lua_topointer(L,values),lua_topointer(L,writer)};return true;
}
void observeTable(lua_State*L,const lua_TableObservation*ep,void*ud) noexcept {
 auto*l=static_cast<Ledger*>(ud);const auto&e=*ep;
 try{
  auto it=l->watched.constFind(e.table_identity);if(it==l->watched.cend())return;
  const Watched w=*it;
  const bool choiceProxy=w.kind==Watched::ChoiceProxy||w.kind==Watched::ChosenPlayerProxy||w.kind==Watched::ChosenCardProxy;
  const bool choiceMeta=w.kind==Watched::ChoiceMeta||w.kind==Watched::ChosenPlayerMeta||w.kind==Watched::ChosenCardMeta;
  const bool choiceCatalogue=w.kind==Watched::IsolatedCatalogue;
  if(choiceProxy||choiceMeta||choiceCatalogue){
   const bool changed=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE
    ||(e.event==LUA_TABLE_OBSERVER_WRITE&&(!choiceCatalogue||sameKey(e,"ai_skill_choice")||sameKey(e,"ai_skill_playerchosen")||sameKey(e,"ai_skill_cardchosen")));
   if(changed){l->choiceValid=false;initializeAIChoiceContinuityRoots(l->room,true,false,{});interruptAIChoiceContinuity(l->room);}
   if(e.event==LUA_TABLE_OBSERVER_FREE)l->watched.remove(e.table_identity);
   return;
  }
  if(w.kind==Watched::IsolatedGlobals&&e.event==LUA_TABLE_OBSERVER_WRITE&&sameKey(e,"ai_choice_legacy_registries")){
   l->choiceValid=false;initializeAIChoiceContinuityRoots(l->room,true,false,{});interruptAIChoiceContinuity(l->room);
  }
  const int choiceDomain=w.kind==Watched::Globals||w.kind==Watched::IsolatedGlobals?0:
   w.kind==Watched::Namespace||w.kind==Watched::IsolatedNamespace?1:
   w.kind==Watched::Choice?2:w.kind==Watched::ChosenPlayer?3:w.kind==Watched::ChosenCard?4:-1;
  if(w.isolated&&choiceDomain>=0&&(e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE))l->choiceValid=false;
  if(choiceDomain>=0)observeAIChoiceContinuityTable(l->room,choiceDomain,w.isolated,e);
  if(w.kind==Watched::Choice||w.kind==Watched::ChosenPlayer||w.kind==Watched::ChosenCard
    ||w.kind==Watched::IsolatedGlobals||w.kind==Watched::IsolatedNamespace){
   if(e.event==LUA_TABLE_OBSERVER_FREE)l->watched.remove(e.table_identity);
   return;
  }
  if(e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE){
   // Non-observation proof, not a value query. Ignore unrelated keys and
   // registry/class reads; a self enumeration can reveal key presence/order.
   if(w.kind==Watched::Self&&(e.event==LUA_TABLE_OBSERVER_ENUMERATE||sameKey(e,"toUse"))){
    auto row=l->rows.constFind(w.owner);
    if(row!=l->rows.cend()&&row->shadowDiverged)l->invalidate(w.owner);
   }
   return;
  }
  if(e.event==LUA_TABLE_OBSERVER_FREE){
   // Remove before Lua frees/reuses this identity. The VM frees its watcher.
   l->watched.remove(e.table_identity);
   if(w.owner){l->invalidate(w.owner);auto r=l->rows.find(w.owner);if(r!=l->rows.end()){
    if(r->selfIdentity==e.table_identity)r->selfIdentity=nullptr;
    if(r->listIdentity==e.table_identity)r->listIdentity=nullptr;
   }}else l->invalidateAll();
   if(l->capture&&(l->capture->self==e.table_identity||l->capture->list==e.table_identity))l->capture->failed=true;
   return;
  }
  if(w.kind==Watched::Namespace||w.kind==Watched::Globals){
   if(e.event!=LUA_TABLE_OBSERVER_WRITE||sameKey(e,w.kind==Watched::Namespace?"ais":"sgs")){
    l->invalidateAll();if(l->capture)l->capture->failed=true;
   }
   return;
  }
  if(w.kind==Watched::Metadata){l->invalidateAll();if(l->capture)l->capture->failed=true;return;}
  if(w.kind==Watched::Registry){
   if(e.event!=LUA_TABLE_OBSERVER_WRITE||!e.string_key){l->invalidateAll();if(l->capture)l->capture->failed=true;return;}
   const QString name=QString::fromUtf8(e.string_key,int(e.string_key_length));
   if(l->capture&&name==l->capture->p->objectName()&&e.value_table_identity==l->capture->self){l->capture->registered=true;return;}
   if(l->capture&&name==l->capture->p->objectName())l->capture->failed=true;
   for(auto r=l->rows.begin();r!=l->rows.end();++r)if(r.key()->objectName()==name){l->invalidate(r.key());break;}
   return;
  }
  if(w.kind==Watched::Self&&e.event==LUA_TABLE_OBSERVER_WRITE&&!sameKey(e,"toUse"))return;
  auto*c=l->capture;
  if(w.kind==Watched::Self&&e.event==LUA_TABLE_OBSERVER_WRITE&&sameKey(e,"toUse")
   &&c&&w.owner==c->p&&e.table_identity==c->self&&!c->assignment
   &&e.value_type==LUA_TTABLE&&(e.flags&LUA_TABLE_OBSERVER_VM_SLOW)&&sourceAssignment(L,c,e)){
    c->assignment=true;c->list=e.value_table_identity;
    if(!watch(l,c->list,{Watched::List,c->p}))c->failed=true;
    return;
  }
  l->invalidate(w.owner);
  if(c&&w.owner==c->p)c->failed=true;
 }catch(...){
  try{interruptAIChoiceContinuity(l->room);}catch(...){}
  // Never unwind into Lua. Fail closed on native bookkeeping allocation failure.
  for(auto r=l->rows.begin();r!=l->rows.end();++r){r->state=QStringLiteral("unknown");r->constructorEmpty=false;r->mirrorEmpty=false;r->shadowDiverged=false;++r->writes;}
  if(l->capture)l->capture->failed=true;
 }
}
void constructorHook(lua_State*L,lua_Debug*d) noexcept {
 auto*c=captures.value(L,nullptr);if(!c||c->failed||c->self)return;
 if(d->event!=LUA_HOOKCALL&&d->event!=LUA_HOOKTAILCALL)return;
 const int top=lua_gettop(L);
 try{
  if(!lua_getinfo(L,"S",d)||d->linedefined!=339||!d->source||strcmp(d->source,"@lua/ai/smart-ai.lua"))return;
  if(!lua_getinfo(L,"f",d)){c->failed=true;lua_settop(L,top);return;}
  c->baseFunction=lua_topointer(L,-1);lua_pop(L,1);
  if(!c->l->proof.contains(c->baseFunction)){c->failed=true;lua_settop(L,top);return;}
  // Inspect only the exact fixed initializer's table identity, never a field.
  const char*local=lua_getlocal(L,d,1);
  if(!local||strcmp(local,"self")||!lua_istable(L,-1)){c->failed=true;lua_settop(L,top);return;}
  c->self=lua_topointer(L,-1);
  if(!watch(c->l,c->self,{Watched::Self,c->p}))c->failed=true;
  // Every actual metatable in the audited middleclass chain must remain unchanged.
  int depth=0;
  while(lua_getmetatable(L,-1)){
   if(++depth>16||!watch(c->l,lua_topointer(L,-1),{Watched::Metadata,nullptr})){c->failed=true;break;}
  }
 }catch(...){c->failed=true;}
 lua_settop(L,top);
 // The call hook is needed only to capture the new self identity. All later
 // checks use table observers, so large native projection trees pay no hook cost.
 if(c->self)lua_sethook(L,nullptr,0,0);
}
Ledger*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
QJsonObject denied(const char*reason){return{{"kind","original_turn_use_presence"},{"known",false},{"state","unknown"},{"reason",QString::fromLatin1(reason)}};}
}
// R56 lexical native hook identity. No Lua code or authored hook is invoked.
bool matchesAIRound56OriginalConstructorHook(Room&r,lua_State*L) noexcept {
 const auto*c=captures.value(L,nullptr);
 return c && c->l && &c->l->room == &r&&c->l->state==L&&c->p&&c->p->getRoom()==&r
  &&c->l->capture==c&&c->l->pending==c->p&&lua_gethook(L)==constructorHook
  &&lua_gethookmask(L)==LUA_MASKCALL;
}
bool matchesAIRound56OriginalConstructorHook(lua_State*L) noexcept {
 const auto*c=captures.value(L,nullptr);
 return c&&c->l&&matchesAIRound56OriginalConstructorHook(c->l->room,L);
}
struct AIRound37OriginalConstructor::Data {
 Room&r;ServerPlayer*p;lua_State*L;bool active=false,completed=false;quint64 epoch=0,generation=0;
 Capture capture;QHash<const ServerPlayer*,Row> prior;bool hookInstalled=false;
 lua_Hook oldHook=nullptr;int oldMask=0,oldCount=0;
 Data(Room&room,ServerPlayer*player,lua_State*state):r(room),p(player),L(state){}
};
void captureAIRound41ChoiceProxies(Room&r,lua_State*L){
 auto*l=ledger(r);auto*runtime=LuaRuntime::fromState(L);if(!runtime)return;
 if(l->choiceCaptured&&l->choiceState==L&&l->choiceGeneration==runtime->generation())return; // never reseal
 l->choiceCaptured=true;l->choiceValid=false;l->choiceState=L;l->choiceGeneration=runtime->generation();l->choiceProxies.clear();
 StackRestore restore(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 raw(L,globals,"ai_choice_legacy_registries");if(!lua_istable(L,-1)||lua_getmetatable(L,-1))return;const int catalogue=lua_absindex(L,-1);
 QHash<QString,ChoiceProxyProof> proofs;
 for(const char*name:{"ai_skill_choice","ai_skill_playerchosen","ai_skill_cardchosen"}){
  raw(L,catalogue,name);ChoiceProxyProof proof;if(!choiceProxyShape(L,-1,proof))return;proofs.insert(QString::fromLatin1(name),proof);lua_pop(L,1);
 }
 bool valid=watch(l,lua_topointer(L,globals),{Watched::IsolatedGlobals,nullptr,true},L)
  &&watch(l,lua_topointer(L,catalogue),{Watched::IsolatedCatalogue,nullptr,true},L);
 for(auto it=proofs.cbegin();it!=proofs.cend();++it){
  const bool choice=it.key()=="ai_skill_choice",player=it.key()=="ai_skill_playerchosen";
  valid=watch(l,it->proxy,{choice?Watched::ChoiceProxy:player?Watched::ChosenPlayerProxy:Watched::ChosenCardProxy,nullptr,true},L)&&valid;
  valid=watch(l,it->meta,{choice?Watched::ChoiceMeta:player?Watched::ChosenPlayerMeta:Watched::ChosenCardMeta,nullptr,true},L)&&valid;
  valid=watch(l,it->values,{choice?Watched::Choice:player?Watched::ChosenPlayer:Watched::ChosenCard,nullptr,true},L)&&valid;
 }
 l->choiceGlobals=lua_topointer(L,globals);l->choiceCatalogue=lua_topointer(L,catalogue);l->choiceProxies=proofs;l->choiceValid=valid;
}
bool validateAIRound41ChoiceProxies(Room&r){
 auto*l=ledger(r);if(!l->choiceValid||!l->choiceState)return false;
 auto*runtime=LuaRuntime::fromState(l->choiceState);
 if(!runtime||runtime->generation()!=l->choiceGeneration){l->choiceValid=false;return false;}
 LuaRuntime::Binding binding(*runtime,false);lua_State*L=l->choiceState;StackRestore restore(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 bool valid=lua_topointer(L,globals)==l->choiceGlobals;
 raw(L,globals,"ai_choice_legacy_registries");const int catalogue=lua_absindex(L,-1);
 valid=valid&&lua_istable(L,catalogue)&&lua_topointer(L,catalogue)==l->choiceCatalogue;
 if(valid&&lua_getmetatable(L,catalogue)){lua_pop(L,1);valid=false;}
 raw(L,globals,"sgs");const int ns=lua_absindex(L,-1);valid=valid&&lua_istable(L,ns);
 if(valid)for(auto it=l->choiceProxies.cbegin();it!=l->choiceProxies.cend();++it){
  const auto name=it.key().toUtf8();raw(L,catalogue,name.constData());const int proxy=lua_absindex(L,-1);
  raw(L,ns,name.constData());valid=lua_rawequal(L,proxy,-1);lua_pop(L,1);
  ChoiceProxyProof actual;valid=valid&&choiceProxyShape(L,proxy,actual)&&actual==it.value();lua_pop(L,1);if(!valid)break;
 }
 if(!valid){l->choiceValid=false;initializeAIChoiceContinuityRoots(r,true,false,{});interruptAIChoiceContinuity(r);}
 return valid;
}
void initializeAIRound41ChoiceTables(Room&r,lua_State*L,bool isolated){
 // Capture before observing initial globals. Do not restamp old nil evidence
 // after a transport interruption during watch installation/allocation.
 const quint64 xufaTransportStamp=r.roomRuntime()->xufaTransportEpoch();
 const quint64 xufaGenerationStamp=r.roomRuntime()->lua().generation();
 auto*l=ledger(r);if(!L){initializeAIChoiceContinuityRoots(r,isolated,false,{});return;}
 bool entryReady=true;
 if(!isolated){
  QPointer<Ledger> guard(l);
  entryReady=r.roomRuntime()->lua().installProtectedEntryObserver(l,[guard](lua_State*state,int arguments){
   if(!guard)return;
   // Each R56 certifier is a complete native/source lexical entry proof. Try
   // their union before any legacy partial/current-shape observer can revoke
   // or preserve another source. Unknown entries retire before execution.
   bool round56=false;
   if(arguments>=0)try{
    round56=acceptAIRound54JijiangEntry(guard->room,state,arguments)
     ||acceptAIRound56BootstrapProtectedEntry(guard->room,state,arguments)
     ||acceptAIRound56CloneEntry(guard->room,state,arguments)
     ||acceptAIRound56ModeEvaluationEntry(guard->room,state,arguments)
     ||acceptAIRound56ProjectionGetterEntry(guard->room,state,arguments)
     ||acceptAIRound56PlayerCorrectionGetterEntry(guard->room,state,arguments)
     ||acceptAIRound56ChoulveTargetWriteEntry(guard->room,state,arguments)
     ||acceptAIRound56ConnectionEntry(guard->room,state,arguments)
     ||acceptAIRound56PatternsEntry(guard->room,state,arguments)
     ||acceptAIRound56PlayLoopFilterEntry(guard->room,state,arguments);
   }catch(...){}
   invalidateAIRound53Goodatwep(guard->room);
   if(round56)return;
   observeAIRound54JijiangEntry(guard->room,state,arguments);
   if(hasAIRound56AsyncUniverse(guard->room,state)
      &&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
    std::fprintf(stderr,"R56_QUERY_RETIRE site=protected-entry argc=%d\n",arguments);std::fflush(stderr);
#ifdef __linux__
    // Failed exact-entry union; code addresses only, no Lua arguments/values.
    std::fprintf(stderr,"R56_PROTECTED_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
    void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
    std::fprintf(stderr,"R56_PROTECTED_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
   }
   retireAIRound56Projection(guard->room,state);
   observeAIRound54PriorityEntry(guard->room,state,arguments);
   if(arguments<0){
    interruptAIChoiceContinuityAsync(guard->room);
    interruptAIRound41FaqiPlansAsync(guard->room);
    return;
   }
   // Only a complete pinned effect/binding graph may retain prior observations.
   // Unknown entries and any inspection failure invalidate before execution.
   try {
    if(certifyAIRound45ContinuityEntry(guard->room,state,arguments)){
     completeAIRound47XufaEventProof(guard->room,state,arguments);return;
    }
    if(certifyAIRound47XufaQueryEntry(guard->room,state,arguments)||certifyAIRound47XufaPatternsEntry(guard->room,state,arguments)){
     interruptAIChoiceContinuityExceptXufa(guard->room);clearAIRound41FaqiPlans(guard->room);return;
    }
   }
   catch (...) {} // allocation/proof failure is never a continuity grant
   interruptAIChoiceContinuity(guard->room);
   clearAIRound41FaqiPlans(guard->room);
  });
 }
 StackRestore stack(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 bool valid=entryReady&&watch(l,lua_topointer(L,globals),{isolated?Watched::IsolatedGlobals:Watched::Globals,nullptr,isolated},L);
 raw(L,globals,"sgs");if(!lua_istable(L,-1)){initializeAIChoiceContinuityRoots(r,isolated,false,{});return;}
 const int ns=lua_absindex(L,-1);
 valid=watch(l,lua_topointer(L,ns),{isolated?Watched::IsolatedNamespace:Watched::Namespace,nullptr,isolated},L)&&valid;
 QJsonObject initial;
 for(const auto&name:{"ai_skill_choice","ai_skill_playerchosen","ai_skill_cardchosen"}){
  raw(L,ns,name);if(!lua_istable(L,-1)){valid=false;lua_pop(L,1);continue;}
  const int table=lua_absindex(L,-1);
  if(isolated){valid=validateAIRound41ChoiceProxies(r)&&valid;lua_pop(L,1);continue;}
  if(lua_getmetatable(L,table)){valid=false;lua_pop(L,1);}
  const auto kind=!strcmp(name,"ai_skill_choice")?Watched::Choice:!strcmp(name,"ai_skill_playerchosen")?Watched::ChosenPlayer:Watched::ChosenCard;
  // Startup nil capture precedes watching this registry: no self-invalidation.
  if(!isolated)for(const auto&key:QStringList{"xuhe","olxuhe","xufa","olshebian_from","olshebian_to","olshebian"}){
   const bool belongs=kind==Watched::Choice?!key.startsWith("olshebian"):
    kind==Watched::ChosenPlayer?key.startsWith("olshebian_"):key=="olshebian";
   if(belongs){const auto b=key.toUtf8();raw(L,table,b.constData());initial.insert(key,lua_isnil(L,-1));lua_pop(L,1);}
  }
  valid=watch(l,lua_topointer(L,table),{kind,nullptr,isolated},L)&&valid;lua_pop(L,1);
 }
 initializeAIChoiceContinuityRoots(r,isolated,valid,initial,xufaTransportStamp,xufaGenerationStamp);
 // The actual R53 writer now shares the existing pre-entry/query revocation
 // routes. Every native trigger is revoked BEFORE private selection; this
 // enables only uninterrupted proved source intervals, never move events.
 if(!isolated&&valid){
  if(initializeAIRound53Registry(r,L))enableAIRound53RegistryEntryCoverage(r,true);
  initializeAIRound54Shebian(r,L);initializeAIRound54Jijiang(r,L);
 }
}
void initializeAIRound37OriginalTurnUse(Room&r){
 auto*l=ledger(r);if(l->initialized)return;l->initialized=true;
 l->generation=r.roomRuntime()->lua().generation();
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);
 l->state=r.roomRuntime()->lua().state();
 l->proof=constructorProof(l->state);
 // Only registry identities, captured at trusted startup; no instance fields.
 StackRestore stack(l->state);lua_pushglobaltable(l->state);
 bool roots=watch(l,lua_topointer(l->state,-1),{Watched::Globals,nullptr});
 raw(l->state,-1,"sgs");
 if(lua_istable(l->state,-1)){
  roots=watch(l,lua_topointer(l->state,-1),{Watched::Namespace,nullptr})&&roots;
  raw(l->state,-1,"ais");
  roots=lua_istable(l->state,-1)&&watch(l,lua_topointer(l->state,-1),{Watched::Registry,nullptr})&&roots;
  l->watchRoots=roots;
 }
 initializeAIRound41ChoiceTables(r,l->state,false);
}
void invalidateAIRound37OriginalTurnUse(Room&r){
 auto*l=ledger(r);
 // Every existing invalidation remains in force, including constructor entry.
 const bool expectedConstructorEntry=l->constructorEntryPending;
 l->constructorEntryPending=false;
 if(!expectedConstructorEntry)l->invalidateAll();
 l->interrupted=true;++l->invalidations;
 for(auto it=l->rows.begin();it!=l->rows.end();++it){it->state="unknown";++it->stamp;}
}
AIRound37OriginalConstructor::AIRound37OriginalConstructor(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 auto*l=ledger(r);
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||l->pending||!l->initialized||l->proof.isEmpty()
  ||r.roomRuntime()->lua().state()!=L||l->generation!=r.roomRuntime()->lua().generation()||constructorProof(L)!=l->proof)return;
 l->pending=p;l->constructorEntryPending=true;d->active=true;d->epoch=l->invalidations;d->generation=l->generation;
 d->capture={l,p};
 // Preserve either untouched startup-empty, or an exact isolated nil whose
 // watched legacy mirror has not been read/exposed or changed since divergence.
 // Every ordinary invalidation still fires; only this successful constructor
 // may restore its saved known state. Unknown rows are never candidates.
 for(auto it=l->rows.cbegin();it!=l->rows.cend();++it)
  if(it.key()!=p&&((it->state=="empty"&&it->constructorEmpty)||(it->state=="nil"&&it->shadowDiverged&&it->mirrorEmpty))&&it->selfIdentity&&it->listIdentity)
   d->prior.insert(it.key(),*it);
 unwatchOwner(l,p);
 auto it=l->rows.find(p);if(it==l->rows.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{unwatchOwner(l,p);l->rows.remove(p);});it=l->rows.insert(p,Row{QStringLiteral("unknown"),0});
 }
 it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;it->selfIdentity=nullptr;it->listIdentity=nullptr;++it->stamp;
 d->oldHook=lua_gethook(L);d->oldMask=lua_gethookmask(L);d->oldCount=lua_gethookcount(L);
 // Never displace another debug/budget hook. Such executions remain conservative.
 if(l->watchRoots&&!d->oldHook&&!captures.contains(L)){
  l->capture=&d->capture;captures.insert(L,&d->capture);
  lua_sethook(L,constructorHook,LUA_MASKCALL,0);d->hookInstalled=true;
 }
}
AIRound37OriginalConstructor::~AIRound37OriginalConstructor(){
 if(d->hookInstalled){
  if(lua_gethook(d->L)==constructorHook)lua_sethook(d->L,d->oldHook,d->oldMask,d->oldCount);
  captures.remove(d->L);
 }
 if(d->active){auto*l=ledger(d->r);l->constructorEntryPending=false;if(!d->completed)l->invalidateAll();if(l->capture==&d->capture)l->capture=nullptr;if(l->pending==d->p)l->pending=nullptr;}
 delete d;
}
void AIRound37OriginalConstructor::complete(AI*created){
 if(!d->active||!dynamic_cast<LuaAI*>(created))return;
 auto*l=ledger(d->r);
 if(l->pending!=d->p||l->invalidations!=d->epoch+1
  ||d->r.roomRuntime()->lua().generation()!=d->generation||constructorProof(d->L)!=l->proof)return;
 auto it=l->rows.find(d->p);if(it==l->rows.end())return;
 const bool guarded=d->hookInstalled&&lua_gethook(d->L)==d->oldHook
  &&!d->capture.failed&&d->capture.assignment&&d->capture.registered;
 if(!guarded)return;
 it->state="empty";++it->stamp;
 if(guarded){
  it->selfIdentity=d->capture.self;it->listIdentity=d->capture.list;it->constructorEmpty=true;it->mirrorEmpty=true;it->shadowDiverged=false;
  for(auto saved=d->prior.cbegin();saved!=d->prior.cend();++saved){
   auto now=l->rows.find(saved.key());
   if(now!=l->rows.end()&&now->writes==saved->writes
    &&now->selfIdentity==saved->selfIdentity&&now->listIdentity==saved->listIdentity
    &&((saved->state=="empty"&&now->constructorEmpty)
       ||(saved->state=="nil"&&now->mirrorEmpty&&now->shadowDiverged))
    &&l->watched.contains(now->selfIdentity)&&l->watched.contains(now->listIdentity))
    now->state=saved->state;
  }
 }
 d->completed=true;
 // Only called after successful protectedCall AND SWIG AI* conversion. Never
 // called on the TrustAI error/default path, and never inferred from an old AI.
}
QJsonObject queryAIRound37OriginalTurnUse(Room&r,const AIRequest&q,const ServerPlayer*p,const QString&write){
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||!r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))
  ||q.viewerObjectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||!aiRound35OriginalQuestion(q))return denied("Original owner/request scope mismatch");
 auto*l=ledger(r);
 if(l->generation!=r.roomRuntime()->lua().generation())return denied("Original runtime generation changed");
 if(!write.isEmpty()&&(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||(write!="empty"&&write!="nil"&&write!="unknown")))return denied("Original toUse source writer scope mismatch");
 auto it=l->rows.find(p);
 if(it==l->rows.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{unwatchOwner(l,p);l->rows.remove(p);});
  it=l->rows.insert(p,Row{l->initialized&&!l->interrupted?QStringLiteral("empty"):QStringLiteral("unknown"),0});
 }
 // A read does not change the native mirror. Every exact isolated assignment
 // starts/continues divergence from the still-watched constructor-empty
 // mirror; only a later exact nil state is eligible for restoration. All intervening semantic reads are observed.
 it->constructorEmpty=false;
 if(!write.isEmpty()){
  // Observe all isolated assignment-to-clear intervals. The actual legacy
  // mirror is unchanged; a nonempty/unknown plan is never itself restored.
  // Only a subsequent exact nil writer can yield a restorable shadow state.
  if(it->mirrorEmpty)it->shadowDiverged=true;
  it->state=write;++it->stamp;
 }
 return{{"kind","original_turn_use_presence"},{"known",it->state!="unknown"},{"state",it->state},
  {"viewer",q.viewerObjectName},{"decision_id",q.getDecisionId()},{"state_revision",q.getStateRevision()},
  {"continuity_stamp",QString::number(it->stamp)}};
}
