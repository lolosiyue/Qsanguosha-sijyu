#include "ai-strategy-round56-playloop-filter.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-bootstrap-write.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-clone-scope.h"
#include "ai-strategy-round56-connection-cache.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round45-natural-event.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round44-focus.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QHash>
#include <QSet>
#include <QScopeGuard>
#include <cstring>
#include <cstdio>
#include <limits>
#include <typeinfo>
extern "C" {
#include "lopcodes.h"
}
extern bool matchAIRound56NativeRoom(lua_State*,int,const Room*);
extern bool matchAIRound56NativePlayer(lua_State*,int,const ServerPlayer*);
namespace {
using namespace AIRound45Inspect;
constexpr const char*smart="lua/ai/smart-ai.lua";
constexpr const char*middle="lua/lib/middleclass.lua";
constexpr const char*middleHash="32938b2068cb9b8a5b6a8d734bce0a6c73020f6a128e9cf9bfca200490cedf6e";
constexpr const char*smartHash="f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5";
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
enum Kind{Globals,Namespace,Events,Row,Filter,Queue,Qlist,Connect,Crossbow,Slash,ClassSource,SelfSource,Count};
void trace(const char*tag,int a=0,int b=0)noexcept{
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_PLAYLOOP_TRACE tag=%s a=%d b=%d\n",tag,a,b);std::fflush(stderr);}
}
struct State;
void changed(lua_State*,const lua_TableObservation*,void*) noexcept;
void born(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
thread_local AIRound56PlayLoopFilterScope*active=nullptr;
struct State final:AIRoomLedger{
 Room&r;lua_State*L;quint64 generation,epoch;bool alive=true,ready=false,completed=false,prepareAttempted=false;
 bool generationScalar=false,turnScalar=false,pristine=false,resetComplete=false;
 quint64 resetSerial=0;
 const void*root[Count]={};const void*latest[Count]={};
 QHash<const void*,Kind>watches;
 // These identities exist only after actual middleclass NEWTABLE receipts.
 // No preflight scan populates or repairs these source ledgers.
 QHash<const void*,const void*>selfClasses,selfDictionaries;
 State(Room&room,const LuaRuntime::NativeNamespaceBirth&b):AIRoomLedger(room),r(room),L(b.state()),generation(b.generation()),epoch(room.roomRuntime()->prioritySourceEpoch()){
  root[Globals]=b.globalsIdentity();root[Namespace]=b.namespaceIdentity();
 }
 ~State()override{lua_unobservetablebirth(L,born,this);for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),changed,this);}
 void retire(int site=0)noexcept{if(!alive)return;trace("source-retire",site,int(completed));alive=false;retireAIRound56Projection(r,L);}
 bool watch(const void*p,Kind k){
  if(!p||watches.contains(p)||!lua_observetableidentity(L,p,changed,this))return false;
  try{watches.insert(p,k);}catch(...){lua_unobservetableidentity(L,p,changed,this);throw;}return true;
 }
 void forget(const void*p){if(!p)return;lua_unobservetableidentity(L,p,changed,this);watches.remove(p);}
};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
bool current(const State&s){auto*rt=s.r.roomRuntime();return s.alive&&rt&&!rt->isClosing()&&s.L&&rt->lua().rawState()==s.L
 &&rt->lua().generation()==s.generation&&rt->lua().isCurrentThreadOwner()&&s.epoch&&rt->prioritySourceEpoch()==s.epoch;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool at(State&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line,const char*namespaceKey=nullptr){
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return false;
 if(e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW)))
  return matchAIRound56PinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,smart,first,last,line);
 if(!namespaceKey||e.table_identity!=s.root[Namespace]
  ||!matchAIRound56NativeNamespaceWriteFrame(s.r,L,e.table_identity,QStringLiteral("sgs"),namespaceKey))return false;
 auto*ci=L->ci->previous;
 if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 auto*f=clLvalue(s2v(ci->func.p));auto*p=f->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 return matchAIRound56PinnedInstruction(s.r,L,f,int(ci->u.l.savedpc-p->code)-1,smart,first,last,line);
}
bool integerKey(lua_State*L,const lua_TableObservation&e,lua_Integer&out){
 if(!e.lua_function_identity||!isLua(L->ci)||!ttisLclosure(s2v(L->ci->func.p)))return false;
 auto*f=clLvalue(s2v(L->ci->func.p));auto*p=f->p;const int pc=e.lua_instruction_index;
 if(f!=e.lua_function_identity||pc<0||pc>=p->sizecode)return false;const Instruction i=p->code[pc];
 if(GET_OPCODE(i)==OP_SETI){out=GETARG_B(i);return true;}
 if(GET_OPCODE(i)!=OP_SETTABLE||GETARG_B(i)>=p->maxstacksize)return false;
 const TValue*v=s2v(L->ci->func.p+1+GETARG_B(i));if(!ttisinteger(v))return false;out=ivalue(v);return true;
}
bool fresh(const State&s,const lua_TableObservation&e,Kind k){return e.value_type==LUA_TTABLE&&e.value_table_identity
 &&e.value_table_identity==s.latest[k]&&s.watches.value(e.value_table_identity,Globals)==k;}
bool sourceKind(const State&s,const void*p,Kind k){
 auto i=s.watches.constFind(p);return p&&i!=s.watches.cend()&&*i==k;
}
bool selfClassWrite(State&s,lua_State*L,const lua_TableObservation&e){
 return inAIRound56CloneScope(s.r,L)&&!s.selfClasses.contains(e.table_identity)
  &&e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))
  &&e.value_type==LUA_TTABLE&&sourceKind(s,e.value_table_identity,ClassSource)
  &&matchAIRound56PinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,middle,98,101,100);
}
bool selfMetatableWrite(State&s,lua_State*L,const lua_TableObservation&e){
 // Exact first native setter immediately called by allocate's pinned source.
 // Inspect its actual arguments, not a later current metatable snapshot.
 if(!inAIRound56CloneScope(s.r,L)||!s.selfClasses.contains(e.table_identity)
  ||s.selfDictionaries.contains(e.table_identity)
  ||!matchAIRound56NativeLeafFrame(s.r,L,QStringLiteral("setmetatable"))
  ||table(L,1)!=e.table_identity||!sourceKind(s,table(L,2),ClassSource))return false;
 auto*ci=L->ci->previous;if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 const auto*f=clLvalue(s2v(ci->func.p));const auto*p=f->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 const int pc=int(ci->u.l.savedpc-p->code)-1;const auto op=GET_OPCODE(p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&matchAIRound56PinnedInstruction(s.r,L,f,pc,middle,98,101,100);
}
struct Reset{const char*key;Kind kind;int line;unsigned bit;};
constexpr Reset resets[]={{"qlist_cache",Qlist,434,2},{"ai_connect_cache",Connect,436,4},
 {"ai_crossbow_cache",Crossbow,438,16},{"ai_slash_query_cache",Slash,439,32},{"_deferred_delete_cards",Queue,5485,128}};
constexpr unsigned allWrites=511; // current_self, six invalidation writes, queue, filterData
bool nestedCloneWrite(State&s,lua_State*L,const lua_TableObservation&e){
 return inAIRound56CloneScope(s.r,L)&&e.value_type==LUA_TTABLE&&e.string_key
  &&at(s,L,e,443,454,451); // complete Clone helper graph + native source producer
}
}
struct AIRound56PlayLoopFilterScope::Data{
 Room&r;LuaAI*ai;ServerPlayer*p;lua_State*L;const QVariant*original;AIRound56PlayLoopFilterScope*previous=nullptr;
 State*s=nullptr;std::unique_ptr<AIRound56AsyncScope>async;
 const void*callback=nullptr,*self=nullptr,*frame=nullptr;quint64 generation=0,epoch=0,transport=0;
 int depth=0;unsigned writes=0;bool ready=false,entered=false,invalid=false;
 Data(Room&room,LuaAI*a,ServerPlayer*player,lua_State*l,const QVariant&v):r(room),ai(a),p(player),L(l),original(&v){}
};
struct AIRound56PlayLoopAccess{
 static AIRound56PlayLoopFilterScope::Data*call(Room&r,lua_State*L,bool entered=true)noexcept{
  if(!active||!active->d||&active->d->r!=&r||active->d->L!=L||!active->accepted())return nullptr;
  return !entered||active->d->entered?active->d.get():nullptr;
 }
};
namespace {
bool permitted(State&s,lua_State*L,const lua_TableObservation&e){
 auto*c=AIRound56PlayLoopAccess::call(s.r,L);if(!c||c->s!=&s||e.event!=LUA_TABLE_OBSERVER_WRITE)return false;
 if(e.table_identity==s.root[Globals])return key(e,"current_self")&&e.value_type==LUA_TTABLE
  &&e.value_table_identity==c->self&&at(s,L,e,344,375,360);
 if(e.table_identity==s.root[Filter]){lua_Integer event=0;return e.value_type==LUA_TUSERDATA
  &&at(s,L,e,2491,2866,2493)&&integerKey(L,e,event)&&event==int(EventPlayPhaseLoop);}
 if(e.table_identity!=s.root[Namespace])return false;
 for(const auto&w:resets)if(key(e,w.key))return fresh(s,e,w.kind)
  &&at(s,L,e,w.kind==Queue?5481:433,w.kind==Queue?5486:441,w.line,w.key);
 if(key(e,"ai_active_recommends_cache"))return e.value_type==LUA_TNIL&&at(s,L,e,433,441,437,"ai_active_recommends_cache");
 if(key(e,"qlist_cache_gen"))return e.value_type==LUA_TNUMBER&&s.generationScalar&&at(s,L,e,433,441,440,"qlist_cache_gen");
 return false;
}
void noteWrite(State&s,lua_State*L,const lua_TableObservation&e){
 auto*c=AIRound56PlayLoopAccess::call(s.r,L);if(!c){s.retire(__LINE__);return;}unsigned bit=0;
 if(e.table_identity==s.root[Globals])bit=1;
 else if(e.table_identity==s.root[Filter])bit=256;
 else if(key(e,"ai_active_recommends_cache"))bit=8;
 else if(key(e,"qlist_cache_gen"))bit=64;
 else for(const auto&w:resets)if(key(e,w.key)){
  bit=w.bit;s.root[w.kind]=e.value_table_identity;
  if(w.kind==Qlist){
   if(s.resetSerial==std::numeric_limits<quint64>::max()){s.retire(__LINE__);return;}
   ++s.resetSerial;s.resetComplete=false;s.pristine=true;
  }else if(w.kind==Connect)s.resetComplete=(c->writes&2)!=0;
  break;
 }
 if(!bit||(c->writes&bit)){s.retire(__LINE__);return;}c->writes|=bit;trace("writer-receipt",int(bit),int(c->writes));
}
void changed(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;const Kind k=*i;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  s.watches.remove(e.table_identity);s.selfClasses.remove(e.table_identity);s.selfDictionaries.remove(e.table_identity);
  bool rooted=s.completed&&(k==ClassSource||k==SelfSource);
  for(int n=0;n<Count;++n){if(s.root[n]==e.table_identity){s.root[n]=nullptr;rooted=true;}if(s.latest[n]==e.table_identity)s.latest[n]=nullptr;}
  if(rooted)s.retire(__LINE__);return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  if(k==ClassSource){
   // Complete bootstrap source token closes all class construction/registration
   // writes. Every later alias write or metatable replacement retires forever.
   if(s.completed){trace("class-source-mutation",e.event);s.retire(__LINE__);}return;
  }
  if(k==SelfSource){
   if(e.event==LUA_TABLE_OBSERVER_METATABLE){
    if(!selfMetatableWrite(s,L,e)){trace("self-source-metatable");s.retire(__LINE__);return;}
    s.selfDictionaries.insert(e.table_identity,table(L,2));trace("self-source-metatable-receipt");return;
   }
   if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
   if(key(e,"filterEvent")){trace("self-source-filter-write");s.retire(__LINE__);return;}
   if(key(e,"class")){
    if(!selfClassWrite(s,L,e)){trace("self-source-class-write");s.retire(__LINE__);return;}
    s.selfClasses.insert(e.table_identity,e.value_table_identity);
   }
   return; // Existing CloneInputLineage owns room/player and other self effects.
  }
  if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire(__LINE__);return;}
  if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
  if(k==Globals){if(key(e,"sgs")){s.retire(__LINE__);return;}if(s.completed&&permitted(s,L,e))noteWrite(s,L,e);return;} // clone lineage owns all postbootstrap global writers
  if(k==Namespace){
   const struct {const char*key;Kind kind;int line;}initial[]={{"ai_event_callback",Events,117},{"filterData",Filter,2361},{"_deferred_delete_cards",Queue,5479}};
   for(const auto&w:initial)if(key(e,w.key)){
    if(!s.completed){
     if(s.root[w.kind]||!s.ready||!fresh(s,e,w.kind)||!at(s,L,e,0,0,w.line,w.key)){s.retire(__LINE__);return;}
     s.root[w.kind]=e.value_table_identity;return;
    }
    if(w.kind==Queue&&permitted(s,L,e)){noteWrite(s,L,e);return;}
    s.retire(__LINE__);return;
   }
   if(key(e,"qlist_cache_gen")){
    if(!s.completed){if(!s.ready||s.generationScalar||e.value_type!=LUA_TNUMBER||!at(s,L,e,0,0,244,"qlist_cache_gen")){s.retire(__LINE__);return;}s.generationScalar=true;return;}
    if(permitted(s,L,e)){noteWrite(s,L,e);return;}s.retire(__LINE__);return;
   }
   if(key(e,"turncount")){
    if(s.completed&&inAIRound56CloneScope(s.r,L)&&e.value_type==LUA_TNUMBER&&at(s,L,e,339,429,380,"turncount")){s.turnScalar=true;return;}
    s.retire(__LINE__);return;
   }
   for(const auto&w:resets)if(key(e,w.key)){
    if(!s.completed)return; // complete bootstrap graph owns unrelated initial cache roots
    if(permitted(s,L,e)){noteWrite(s,L,e);return;}
    // The pinned aiConnect producer assigns its root even after the event
    // reset. Preserve that actual source write only when it carries the exact
    // previously issued reset identity. Never adopt a replacement or restore
    // a retired lineage; equal contents alone grant nothing.
    if(w.kind==Connect&&allowAIRound56ConnectionCacheWrite(s.r,L,e)
      &&(!s.root[Connect]||s.root[Connect]==e.value_table_identity))return;
    s.retire(__LINE__);return;
   }
   if(key(e,"ai_active_recommends_cache")&&s.completed){if(permitted(s,L,e))noteWrite(s,L,e);else s.retire(__LINE__);}
   return;
  }
  if(k==Events){
   lua_Integer event=0;
   if(s.completed||!s.ready||!fresh(s,e,Row)||!at(s,L,e,0,0,235)||!integerKey(L,e,event)
    ||event<int(NonTrigger)||event>int(EventForDiy)){s.retire(__LINE__);return;}
   if(event==int(EventPlayPhaseLoop)){if(s.root[Row]){s.retire(__LINE__);return;}s.root[Row]=e.value_table_identity;}
   else s.forget(e.value_table_identity);
   return;
  }
  if(k==Filter){if(s.completed&&permitted(s,L,e)){noteWrite(s,L,e);return;}s.retire(__LINE__);return;}
  if(k==Qlist||k==Connect){
   // Preserve valid source evolution, but never late-capture a populated root.
   if(e.table_identity!=s.root[k]){s.retire(__LINE__);return;}
   if(allowAIRound56ConnectionCacheWrite(s.r,L,e)||(k==Qlist&&nestedCloneWrite(s,L,e))){s.pristine=false;return;}
  }
  // Selected event row, deferred queue and other caches have no nested writer
  // in this bounded graph. Same-value nil writes are still real writes.
  s.retire(__LINE__);
 }catch(...){s.retire(__LINE__);}
}
void born(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!e||!s.alive||!s.ready||!L||G(L)!=G(s.L)||!e->lua_function_identity)return;
 try{
  Kind kind=Count;
  // All four tables in _createClass's literal are fresh public class metadata.
  // They are watched before initialization writes, then frozen at completion.
  if(!s.completed&&matchAIRound56PinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,middle,48,54,49))kind=ClassSource;
  else if(s.completed&&inAIRound56CloneScope(s.r,L)
   &&matchAIRound56PinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,middle,98,101,100))kind=SelfSource;
  else if(!s.completed){
   for(const auto&w:{qMakePair(117,Events),qMakePair(235,Row),qMakePair(2361,Filter),qMakePair(5479,Queue)})
    if(matchAIRound56PinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,smart,0,0,w.first)){kind=w.second;break;}
  }else if(AIRound56PlayLoopAccess::call(s.r,L)){
   for(const auto&w:resets)if(matchAIRound56PinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,
     smart,w.kind==Queue?5481:433,w.kind==Queue?5486:441,w.line)){kind=w.kind;break;}
  }
  if(kind==Count)return;
  const auto*f=static_cast<const LClosure*>(e->lua_function_identity);const int pc=e->lua_instruction_index;
  if(!f->p||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE||!s.watch(e->table_identity,kind)){s.retire(__LINE__);return;}
  if(kind!=ClassSource&&kind!=SelfSource)s.latest[kind]=e->table_identity;
  trace("source-birth",int(kind),s.completed?1:0);
 }catch(...){s.retire(__LINE__);}
}
bool plain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
struct Graph{
 State&s;Room&r;lua_State*L;LuaAI*ai;ServerPlayer*actor;int env=0,ns=0,self=0;const void*callback=nullptr,*selfIdentity=nullptr;
 Graph(State&state,LuaAI*a,ServerPlayer*p):s(state),r(state.r),L(state.L),ai(a),actor(p){}
 bool closure(int i,int first,int last){
  i=lua_absindex(L,i);if(!matchAIRound45PinnedFunction(r,L,i,smart,first,last)){trace("closure-source",first,last);return false;}
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok){trace("closure-environment",first,last);return false;}}return true;
 }
 bool filterClosure(int i){
  i=lua_absindex(L,i);
  // Full recursive Proto seal includes the inner pcall closure, its upvalue
  // routing, every event comparison and every helper read/call instruction.
  if(!matchAIRound45PinnedFunction(r,L,i,smart,2477,2867)){trace("filter-source");return false;}
  static_assert(EventPlayPhaseLoop!=TargetSpecified&&EventPlayPhaseLoop!=DamageInflicted&&EventPlayPhaseLoop!=HpRecover,
    "PlayLoop cannot execute the captured intention helper");
  bool sawEnv=false,sawIntention=false;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;bool ok=false;
   if(!std::strcmp(k,"_ENV")){
    ok=!sawEnv&&lua_rawequal(L,-1,env);sawEnv=true;
    if(!ok)trace("filter-capture-environment",u);
   }else if(!std::strcmp(k,"getCardIntention")){
    // Only captured into the child for three other event arms (2537/2628/2657).
    // Their native enum bindings are sealed in leaves(), and exact PlayLoop
    // arguments are required at entry. No helper call/effect is granted here.
    ok=!sawIntention&&closure(-1,1948,1956);sawIntention=true;
    if(!ok)trace("filter-capture-getCardIntention",1948,1956);
   }else trace("filter-capture-unexpected",u);
   lua_pop(L,1);if(!ok)return false;
  }
  if(!sawEnv||!sawIntention){trace("filter-capture-missing",int(sawEnv),int(sawIntention));return false;}
  return true;
 }
 bool lookup(int object,const char*k){
  object=lua_absindex(L,object);QSet<const void*>seen;
  for(int n=0;n<16;++n){if(!table(L,object)||seen.contains(lua_topointer(L,object)))return false;seen.insert(lua_topointer(L,object));
   field(L,object,k);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
   if(!lua_getmetatable(L,object)){trace("lookup-no-metatable",n);return false;}
   // Lua reads the metatable's __index slot raw; that table may itself inherit.
   // Permit only source-born, continuously frozen class dictionaries. This is
   // source membership, never adoption of an arbitrary current shape/value.
   if(!sourceKind(s,table(L,-1),ClassSource)){trace("lookup-metatable-source",n);return false;}
   field(L,-1,"__index");if(!sourceKind(s,table(L,-1),ClassSource)){trace("lookup-index-source",n);return false;}object=lua_absindex(L,-1);
  }return false;
 }
 bool native(int t,const char*k,const QString&path){Stack z(L);field(L,t,k);const bool ok=matchAIRound45NativeBinding(r,L,-1,path);if(!ok)trace(k);return ok;}
 bool spaceClosure(const char*k,int first,int last){Stack z(L);field(L,ns,k);return closure(-1,first,last);}
 bool roots(){
  globals(L);env=lua_absindex(L,-1);if(!plain(L,env)||lua_topointer(L,env)!=s.root[Globals])return false;
  field(L,env,"sgs");ns=lua_absindex(L,-1);if(!table(L,ns)||lua_topointer(L,ns)!=s.root[Namespace])return false;
  for(const auto&x:{qMakePair("ai_event_callback",Events),qMakePair("filterData",Filter),qMakePair("_deferred_delete_cards",Queue)}){
   Stack z(L);field(L,ns,x.first);if(!plain(L,-1)||lua_topointer(L,-1)!=s.root[x.second])return false;
   if(x.second==Events){rawGeti(L,-1,int(EventPlayPhaseLoop));if(!plain(L,-1)||lua_topointer(L,-1)!=s.root[Row])return false;}
  }
  // Empty row/queue authority is their actual NEWTABLE plus every subsequent
  // mutation observer, not size/value inspection at this entry.
  return s.generationScalar&&s.turnScalar;
 }
 bool callbackGraph(){
  // Diagnostic tags expose only the failed structural gate, never self fields,
  // userdata payloads, callback names, addresses or private game values.
  if(!ai){trace("callback-ai-null");return false;}
  if(typeid(*ai)!=typeid(LuaAI)){trace("callback-ai-type");return false;}
  if(!ai->callback.isValid()){trace("callback-invalid");return false;}
  if(ai->callback.state()!=L){trace("callback-state");return false;}
  rawGeti(L,LUA_REGISTRYINDEX,ai->callback.registryRef());const int fn=lua_absindex(L,-1);
  if(!matchAIRound45PinnedFunction(r,L,fn,smart,344,375)){trace("callback-source",344,375);return false;}callback=lua_topointer(L,fn);
  bool sawSelf=false,sawEnv=false;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,fn,u);if(!k)break;
   if(!std::strcmp(k,"self")){
    if(sawSelf||!table(L,-1)){trace("callback-self-upvalue",u,int(sawSelf));return false;}sawSelf=true;self=lua_absindex(L,-1);
   }else if(!std::strcmp(k,"_ENV")){
    const bool ok=!sawEnv&&lua_rawequal(L,-1,env);lua_pop(L,1);
    if(!ok){trace("callback-env-upvalue",u,int(sawEnv));return false;}sawEnv=true;
   }else{trace("callback-unexpected-upvalue",u);return false;}
  }
  if(!sawSelf||!sawEnv){trace("callback-missing-upvalue",int(sawSelf),int(sawEnv));return false;}selfIdentity=lua_topointer(L,self);
  // Source-created self comes from the existing real CloneAI receipt; locating
  // the native observer owner uses only AI pointer identity, not skill state.
  const ServerPlayer*owner=nullptr;for(auto*p:r.getPlayers())if(p&&p->getSmartAI()==ai){if(owner){trace("callback-owner-duplicate");return false;}owner=p;}
  if(!owner){trace("callback-owner-missing");return false;}
  {Stack z(L);
   if(!pushAIRound45ActualAISelf(r,owner,L)){trace("callback-self-receipt");return false;}
   if(!lua_rawequal(L,-1,self)){trace("callback-self-binding");return false;}
  }
  // Actual allocation and one-shot class/metatable writes precede all queries.
  // Self's filterEvent slot has remained absent since that literal's birth.
  if(!sourceKind(s,selfIdentity,SelfSource)||!s.selfClasses.contains(selfIdentity)
   ||!s.selfDictionaries.contains(selfIdentity)){trace("callback-self-source");return false;}
  {Stack z(L);
   if(!lookup(self,"filterEvent")){trace("callback-filter-lookup");return false;}
   if(!filterClosure(-1)){trace("callback-filter-closure");return false;}
  }
  {Stack z(L);
   if(!lookup(self,"room")){trace("callback-room-lookup");return false;}
   if(!matchAIRound56NativeRoom(L,-1,&r)){trace("callback-room-binding");return false;}
   if(!matchAIRound45NativeObjectMethods(r,L,-1,"Room",{"writeToConsole","outputEventStack"})){trace("callback-room-methods");return false;}
  }
  if(!spaceClosure("invalidate_qlist_cache",433,441)){trace("callback-invalidate-closure");return false;}
  if(!spaceClosure("flushDeferredDeleteCards",5481,5486)){trace("callback-flush-closure");return false;}
  return true;
 }
 bool leaves(){
  for(const char*k:{"type","pcall","pairs","ipairs","tostring"})if(!native(env,k,QString::fromLatin1(k)))return false;
  for(const auto&lib:{qMakePair("string",QStringList{"find","sub"}),qMakePair("io",QStringList{"open"})}){
   Stack z(L);field(L,env,lib.first);if(!plain(L,-1))return false;const int t=lua_absindex(L,-1);
   for(const auto&k:lib.second)if(!native(t,k.toUtf8().constData(),QString::fromLatin1(lib.first)+'.'+k))return false;
  }
  if(!matchAIRound45NativeFileMethods(r,L,{"write","close"})){trace("leaf-file");return false;}
  if(!matchAIRound47NativeStringMetatable(r,L)){trace("leaf-string-meta");return false;}
  if(!matchAIRound47NativeValueMetamethods(r,L)){trace("leaf-value-meta");return false;}
  if(!aiRound56FocusBindings(r,L)){trace("leaf-focus");return false;}
  for(int type:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){Stack z(L);if(primitiveMetatable(L,type))return false;}
  // Both diagnostic flag outcomes are proved. A nil namespace read needs the
  // exact SWIG missing-field path; no branch is granted because the flag is off.
  {Stack z(L);field(L,ns,"aiHandCardVisible");if(lua_isnil(L,-1)&&!matchAIRound45NativeNamespaceRead(r,L,"sgs","aiHandCardVisible"))return false;}
  for(const char*k:{"qlist_cache","ai_connect_cache","ai_active_recommends_cache","ai_crossbow_cache","ai_slash_query_cache","qlist_cache_gen","_deferred_delete_cards"})
   if(!matchAIRound45NativeNamespaceWrite(r,L,"sgs",QString::fromLatin1(k)))return false;
  for(const char*k:{"Death","BeforeGameOverJudge","Revived","TargetSpecified","ChoiceMade","CardEffect","DamageInflicted","PreCardUsed","HpRecover","ShowCards","CardsMoveOneTime","StartJudge","AskForRetrial","RoundStart","GameReady"}){
   Stack z(L);field(L,ns,k);if(!matchAIRound45NativeConstant(r,L,-1,QStringLiteral("sgs.")+k))return false;
  }
  return matchAIRound45NativeClassMethod(r,L,"ServerPlayer","getLogName")
   &&matchAIRound45NativeClassMethod(r,L,"QVariant","toString")
   &&proveAIRound56ServerLogEffects(r);
 }
 bool prove(){if(!roots()){trace("preflight-source-roots");return false;}if(!callbackGraph()){trace("preflight-callback-graph");return false;}if(!leaves()){trace("preflight-native-leaves");return false;}return true;}
};
}
bool beginAIRound56PlayLoopSource(Room&r,const LuaRuntime::NativeNamespaceBirth&b)noexcept{
 try{if(!b.isCurrent()||find(r))return false;auto*s=new State(r,b);
  if(!s->epoch||!s->watch(s->root[Globals],Globals)||!s->watch(s->root[Namespace],Namespace)
   ||!lua_observetablebirth(s->L,born,s)){s->retire(__LINE__);return false;}return true;
 }catch(...){if(auto*s=find(r))s->retire(__LINE__);return false;}
}
bool prepareAIRound56PlayLoopSource(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||!current(*s)||s->L!=L||s->prepareAttempted)return false;s->prepareAttempted=true;
 try{if(!registerAIRound45PinnedSource(r,L,smart,smartHash)||!registerAIRound45PinnedSource(r,L,middle,middleHash)){s->retire(__LINE__);return false;}s->ready=true;return true;}
 catch(...){s->retire(__LINE__);return false;}
}
bool completeAIRound56PlayLoopSource(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&p)noexcept{
 auto*s=find(r);if(!s||!current(*s)||s->L!=L||!s->ready||s->completed||!s->generationScalar
  ||p.m_consumed||p.m_room!=&r||p.m_roomLifetime.data()!=&r||p.m_lua!=L||p.m_generation!=s->generation
  ||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial))return false;
 for(Kind k:{Events,Row,Filter,Queue})if(!s->root[k]||s->watches.value(s->root[k],Globals)!=k)return false;
 s->completed=true;trace("bootstrap-complete");return true;
}
bool hasAIRound56PlayLoopSource(const Room&r,lua_State*L)noexcept{
 auto*s=find(r);return s&&s->L==L&&s->completed&&current(*s)
  &&hasAIRound56CloneInputLineage(r,L)&&hasAIRound56ProjectionLineage(r,L)
  &&hasAIRound56ModeEvaluationLineage(r,L)&&hasAIRound56PatternsLineage(r,L)&&hasAIRound56AsyncUniverse(r,L);
}
void retireAIRound56PlayLoopSource(Room&r,lua_State*L)noexcept{if(auto*s=find(r))if(s->L==L)s->alive=false;}
void clearAIRound56PlayLoopSource(Room&r)noexcept{delete find(r);}
bool allowAIRound56PlayLoopWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=find(r);return s&&s->L==L&&permitted(*s,L,e);}catch(...){if(auto*s=find(r))s->retire(__LINE__);return false;}
}
bool captureAIRound56PlayLoopCacheOrigin(const Room&r,lua_State*L,const void*&space,const void*&qlist,const void*&connect,quint64&serial)noexcept{
 space=qlist=connect=nullptr;serial=0;auto*s=find(r);
 if(!hasAIRound56PlayLoopSource(r,L)||!s->pristine||!s->resetComplete||!s->resetSerial||!s->root[Qlist]||!s->root[Connect]){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   auto*rt=const_cast<Room&>(r).roomRuntime();
   std::fprintf(stderr,"R56_CACHE_RESET_ORIGIN_REFUSAL state=%d alive=%d completed=%d current=%d pristine=%d reset_complete=%d serial=%llu qlist=%d connect=%d saved_epoch=%llu actual_epoch=%llu clone=%d projection=%d mode=%d patterns=%d async=%d\n",
    int(s!=nullptr),int(s&&s->alive),int(s&&s->completed),int(s&&current(*s)),int(s&&s->pristine),int(s&&s->resetComplete),s?static_cast<unsigned long long>(s->resetSerial):0ULL,int(s&&s->root[Qlist]),int(s&&s->root[Connect]),s?static_cast<unsigned long long>(s->epoch):0ULL,rt?static_cast<unsigned long long>(rt->prioritySourceEpoch()):0ULL,
    int(hasAIRound56CloneInputLineage(r,L)),int(hasAIRound56ProjectionLineage(r,L)),int(hasAIRound56ModeEvaluationLineage(r,L)),int(hasAIRound56PatternsLineage(r,L)),int(hasAIRound56AsyncUniverse(r,L)));std::fflush(stderr);
  }
  return false;
 }
 space=s->root[Namespace];qlist=s->root[Qlist];connect=s->root[Connect];serial=s->resetSerial;return true;
}
AIRound56PlayLoopFilterScope::AIRound56PlayLoopFilterScope(Room&r,LuaAI*ai,ServerPlayer*p,int event,const QVariant&data,lua_State*L)noexcept{
 // Other genuine events retain all original behavior and default retirement.
 if(event!=int(EventPlayPhaseLoop))return;
 try{
  d.reset(new Data(r,ai,p,L,data));d->previous=active;active=this;
  const auto fail=qScopeGuard([&]{if(!accepted())retireAIRound56Projection(r,L);});
  auto*rt=r.roomRuntime();
  if(!rt||!L||!p||p->getRoom()!=&r||typeid(*p)!=typeid(ServerPlayer)){trace("preflight-native-domain");return;}
  if(!matchesAIRound56NaturalFilterPreEntry(r,ai,p,event,&data,L)){trace("preflight-dispatch-token");return;}
  if(!hasAIRound56PlayLoopSource(r,L)){trace("preflight-source-lineage");return;}
  // Graph retains at most 40 slots; the largest nested native seal needs
  // 1024 additional slots (64-level native resolution is below that bound).
  // Use existing capacity only; no stack allocation before source proof.
  if(!capacity(L,2048)){trace("preflight-stack");return;}
  d->s=find(r);Stack z(L);Graph g(*d->s,ai,p);if(!g.prove())return;
  d->callback=g.callback;d->self=g.selfIdentity;d->frame=L->ci;d->generation=rt->lua().generation();
  d->epoch=rt->prioritySourceEpoch();d->transport=rt->xufaTransportEpoch();d->depth=rt->lua().protectedCallDepth();
  // Complete finite source/effect/allocation union is proved before the first
  // callback/player/owned-QVariant wrapper can allocate or run finalizers.
  d->async.reset(new AIRound56AsyncScope(r,L,AIRound56AsyncScope::GameplayVariants));
  d->ready=d->callback&&d->self&&d->epoch&&d->transport&&d->async->accepted();trace("preflight-complete",int(d->ready));
 }catch(...){if(d)d->invalid=true;retireAIRound56Projection(r,L);}
}
AIRound56PlayLoopFilterScope::~AIRound56PlayLoopFilterScope(){
 if(!d)return;
 trace("scope-exit",int(accepted()),int(d->writes));
 if(!d->entered||d->writes!=allWrites||!accepted())retireAIRound56Projection(d->r,d->L);
 active=d->previous;
}
bool AIRound56PlayLoopFilterScope::accepted()const noexcept{
 if(!d||!d->ready||d->invalid)return false;auto*rt=d->r.roomRuntime();
 return rt&&!rt->isClosing()&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&rt->prioritySourceEpoch()==d->epoch&&rt->xufaTransportEpoch()==d->transport
  &&hasAIRound56PlayLoopSource(d->r,d->L);
}
bool acceptAIRound56PlayLoopFilterEntry(Room&r,lua_State*L,int n)noexcept{
 auto*c=AIRound56PlayLoopAccess::call(r,L,false);if(!c)return false;
 try{
  AIRound45NaturalFilterProof proof;auto*rt=r.roomRuntime();const int f=lua_gettop(L)-n;
  if(!c->entered&&n==4&&f>=1&&L->ci==c->frame&&rt->lua().protectedCallDepth()==c->depth
   &&lua_isfunction(L,f)&&lua_topointer(L,f)==c->callback
   &&readAIRound45NaturalFilterEntry(r,L,n,&proof)&&proof.event==int(EventPlayPhaseLoop)
   &&matchesAIRound47NativeFilterDispatch(r,L,n,int(EventPlayPhaseLoop),c->p,c->original)
   &&matchAIRound56NativePlayer(L,f+3,c->p)
   &&matchAIRound45NativeObjectMethods(r,L,f+4,"QVariant",{"toString"})
   &&aiRound44FocusContinuityNoop(r,L,n)){
   c->entered=true;trace("entry-accepted",int(EventPlayPhaseLoop));return true;
  }
 }catch(...){}
 trace("entry-refused");c->invalid=true;retireAIRound56Projection(r,L);return false;
}
