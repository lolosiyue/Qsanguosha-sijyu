#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round56-factory-source.h"
#include "ai-strategy-round56-host.h"
#include "room-definition-registry.h"
#include "card-lifetime-manager.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "lopcodes.h"
#include <QHash>
#include <cstring>
#include <cstdio>
namespace {
constexpr const char*source="extensions/addFunction.lua";
constexpr const char*sha="32938f895048d1abc62a119f179ed356fda155d1ccc8ba40664ff81c2ecba046";
constexpr const char*utility="lua/utilities.lua";
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
struct State;struct Call;
thread_local Call*active=nullptr;
void changed(lua_State*,const lua_TableObservation*,void*) noexcept;
void born(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
enum Kind{Globals,Ban,Names,Classes,Equips};
struct State final:AIRoomLedger{
 Room&r;lua_State*L;quint64 generation;const void*globals;const void*space;
 bool alive=true,ready=false,prepared=false,completed=false;unsigned genesis=0;
 const void*latest[5]={};QHash<const void*,Kind>tables;
 State(Room&room,const LuaRuntime::NativeNamespaceBirth&b):AIRoomLedger(room),r(room),L(b.state()),generation(b.generation()),globals(b.globalsIdentity()),space(b.namespaceIdentity()){}
 ~State()override{lua_unobservetablebirth(L,born,this);for(auto i=tables.cbegin();i!=tables.cend();++i)lua_unobservetableidentity(L,i.key(),changed,this);}
 void retire(const char*why)noexcept{
  if(alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_PATTERNS_REFUSAL stage=%s\n",why);std::fflush(stderr);}
  alive=false;retireAIRound56Projection(r,L);
 }
 bool watch(const void*p,Kind k){
  if(!p||tables.contains(p)||!lua_observetableidentity(L,p,changed,this))return false;
  try{tables.insert(p,k);return true;}catch(...){lua_unobservetableidentity(L,p,changed,this);throw;}
 }
};
struct Call{State*s;const void*function;int depth;bool entered=false;Call*previous;bool independent=false;};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
bool current(const State&s){auto*rt=s.r.roomRuntime();return s.alive&&rt&&!rt->isClosing()&&rt->lua().rawState()==s.L&&rt->lua().generation()==s.generation&&rt->lua().isCurrentThreadOwner();}
bool nativeContext(const Call &c){
 const auto&s=*c.s;
 if(inAIRound56ProjectionScope(s.r,s.L))return hasAIRound56AsyncUniverse(s.r,s.L);
 auto*rt=s.r.roomRuntime();
 return c.independent&&current(s)&&rt&&LuaRuntime::current()==&rt->lua()
  &&Sanguosha->currentRoom()==&s.r&&hasAIRound56CloneInputLineage(s.r,s.L)
  &&hasAIRound56ProjectionLineage(s.r,s.L)&&hasAIRound56AsyncUniverse(s.r,s.L)
  &&hasAIRound56GlobalFactoryMapOrigin(Sanguosha)
  &&rt->definitions().hasFactorySourceOrigin(s.r,s.L);
}
bool executing(State&s,lua_State*L){return current(s)&&s.ready&&L==s.L&&(!s.completed||(active&&active->s==&s&&active->entered&&nativeContext(*active)));}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool instruction(State&s,lua_State*L,const void*f,int pc,int first,int last,int line){return executing(s,L)&&f&&matchAIRound56PinnedInstruction(s.r,L,f,pc,source,first,last,line);}
bool at(State&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line){return (e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))&&instruction(s,L,e.lua_function_identity,e.lua_instruction_index,first,last,line);}
bool caller(State&s,lua_State*L,int line){
 // Immediate actual caller, never a nearest-Lua-frame search across callbacks.
 if(!executing(s,L)||isLua(L->ci)||!L->ci->previous||!isLua(L->ci->previous))return false;
 auto*ci=L->ci->previous;auto*f=clLvalue(s2v(ci->func.p));int pc=int(ci->u.l.savedpc-f->p->code)-1;
 // Header mismatch is negative-only; acceptance still checks the whole Proto.
 if(f->p->linedefined!=1809||f->p->lastlinedefined!=1838)return false;
 if(pc<0||pc>=f->p->sizecode)return false;auto op=GET_OPCODE(f->p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&instruction(s,L,f,pc,1809,1838,line);
}
bool banProducer(State&s,lua_State*L){return caller(s,L,1813)&&matchAIRound56NativeClassLeafFrame(s.r,L,QStringLiteral("Engine"),QStringLiteral("getBanPackages"));}
bool equipWrite(State&s,lua_State*L,const lua_TableObservation&e){
 if(e.event!=LUA_TABLE_OBSERVER_WRITE||e.table_identity!=s.globals||!key(e,"equip_patterns")||e.value_type!=LUA_TTABLE
   ||!e.value_table_identity||e.value_table_identity!=s.latest[Equips]||s.tables.value(e.value_table_identity,Globals)!=Equips)return false;
 return (!s.completed&&at(s,L,e,0,0,1806))||at(s,L,e,1809,1838,1817);
}
void changed(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto it=s.tables.constFind(e.table_identity);if(it==s.tables.cend())return;Kind k=*it;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  s.tables.remove(e.table_identity);
  if(k==Globals){s.globals=nullptr;s.retire("global-free");}
  else if(s.latest[k]==e.table_identity){s.latest[k]=nullptr;s.retire("current-cache-free");}
  return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire("cache-metatable-write");return;}
  if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
  if(k==Globals){
   if(key(e,"equip_patterns")){if(!equipWrite(s,L,e))s.retire("equip-global-writer");return;}
   if(key(e,"patterns")){
    if(s.completed||e.value_type!=LUA_TFUNCTION||!(at(s,L,e,0,0,1809)||at(s,L,e,0,0,1838)))s.retire("patterns-global-writer");
   }
   return;
  }
  if(e.value_type!=LUA_TSTRING){s.retire("cache-nonstring-write");return;}
  bool ok=false;
  if(k==Ban)ok=!e.flags&&banProducer(s,L);
  else if(k==Classes)ok=e.string_key&&at(s,L,e,1809,1838,1823);
  else if(k==Names||k==Equips)ok=!e.lua_function_identity&&e.lua_instruction_index==-1
   &&(e.flags==0||e.flags==LUA_TABLE_OBSERVER_VM_SLOW)&&caller(s,L,k==Names?1828:1830)&&matchAIRound56NativeLeafFrame(s.r,L,QStringLiteral("table.insert"));
  if(!ok||e.table_identity!=s.latest[k])s.retire("cache-writer-source");
 }catch(...){s.retire("write-observer-exception");}
}
void born(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!e||!s.alive||!s.ready||L!=s.L)return;
 try{
  Kind kind=Globals;bool initial=false;
  if(e->lua_function_identity){
   const auto*f=static_cast<const LClosure*>(e->lua_function_identity);const int pc=e->lua_instruction_index;
   if(!f->p||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE)return;
   if(!s.completed&&f->p->linedefined==0){
    const struct{int line;Kind kind;}rows[]={{1805,Ban},{1806,Equips},{1807,Names},{1808,Classes}};
    for(const auto&row:rows)if(instruction(s,L,f,pc,0,0,row.line)){kind=row.kind;initial=true;break;}
   }else if(f->p->linedefined==1809){
    if(instruction(s,L,f,pc,1809,1838,1816))kind=Names;
    else if(instruction(s,L,f,pc,1809,1838,1817))kind=Equips;
   }
  }else if(banProducer(s,L))kind=Ban;
  if(kind==Globals)return;
  if((initial&&(s.genesis&(1u<<kind)))||!s.watch(e->table_identity,kind)){s.retire("cache-birth-registration");return;}
  if(initial)s.genesis|=1u<<kind;
  s.latest[kind]=e->table_identity;
 }catch(...){s.retire("birth-observer-exception");}
}
bool closureEnv(State&s,lua_State*L,int index,int env,const char*path,int first,int last){
 index=lua_absindex(L,index);if(!matchAIRound45PinnedFunction(s.r,L,index,path,first,last))return false;
 bool seen=false;for(int n=1;;++n){const char*k=lua_getupvalue(L,index,n);if(!k)break;bool ok=!seen&&!std::strcmp(k,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok)return false;seen=true;}return seen;
}
bool graph(State&s,lua_State*L,int fn){
 using namespace AIRound45Inspect;Stack stack(L);fn=lua_absindex(L,fn);
 if(!current(s)||s.genesis!=30||!capacity(L,2048)||!matchAIRound45PinnedFunction(s.r,L,fn,source,1809,1838))return false;
 globals(L);int env=lua_absindex(L,-1);auto*g=table(L,env);if(g!=s.globals||!g||g->metatable)return false;
 field(L,env,"patterns");bool same=lua_rawequal(L,fn,-1);lua_pop(L,1);if(!same)return false;
 field(L,env,"sgs");if(table(L,-1)!=s.space)return false;lua_pop(L,1);
 unsigned seen=0;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,fn,n);if(!k)break;
  if(!std::strcmp(k,"_ENV")){if((seen&1)||!lua_rawequal(L,-1,env))return false;seen|=1;}
  else{
   Kind role=!std::strcmp(k,"banPs")?Ban:!std::strcmp(k,"cardNames")?Names:!std::strcmp(k,"class2Names")?Classes:Globals;
   auto*t=table(L,-1);
   // Original observed origin is checked BEFORE any current-shape inspection.
   if(role==Globals||(seen&(1u<<role))||!t||s.latest[role]!=t||s.tables.value(t,Globals)!=role||t->metatable)return false;
   seen|=1u<<role;
  }lua_pop(L,1);
 }
 if(seen!=15)return false;
 field(L,env,"equip_patterns");auto*eq=table(L,-1);if(!eq||eq!=s.latest[Equips]||s.tables.value(eq,Globals)!=Equips||eq->metatable)return false;lua_pop(L,1);
 for(int type:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})if(G(L)->mt[type])return false;
 if(!matchAIRound47NativeStringMetatable(s.r,L)||!matchAIRound47NativeValueMetamethods(s.r,L))return false;
 for(const char*k:{"type","ipairs"}){Stack z(L);field(L,env,k);if(!matchAIRound45NativeBinding(s.r,L,-1,QString::fromLatin1(k)))return false;}
 {Stack z(L);field(L,env,"table");int lib=lua_absindex(L,-1);auto*t=table(L,lib);if(!t||t->metatable)return false;
  field(L,lib,"insert");if(!matchAIRound45NativeBinding(s.r,L,-1,QStringLiteral("table.insert")))return false;lua_pop(L,1);
  field(L,lib,"contains");if(!closureEnv(s,L,-1,env,utility,95,108))return false;
 }
 {Stack z(L);field(L,env,"string");auto*t=table(L,-1);if(!t||t->metatable)return false;field(L,-1,"sub");if(!matchAIRound45NativeBinding(s.r,L,-1,QStringLiteral("string.sub")))return false;}
 if(!matchAIRound45NativeNamespaceProperty(s.r,L,"sgs","Sanguosha"))return false;
 for(const char*k:{"getBanPackages","getCardCount","getEngineCard"})if(!matchAIRound45NativeClassMethod(s.r,L,"Engine",k))return false;
 return matchAIRound45NativeDerivedMethods(s.r,L,"Card",{"objectName","getClassName","getPackage","getTypeId"});
}
}
struct AIRound56PatternsScope::Data{
 Call call;
 std::unique_ptr<CardLifetimeScope> lifetime;
 std::unique_ptr<AIRound56AsyncScope> async;
 explicit Data(State*s):call{s,nullptr,0,false,active}{}
};
bool beginAIRound56PatternsLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&b)noexcept{
 try{auto*rt=r.roomRuntime();if(!b.isCurrent()||!rt||rt->lua().rawState()!=b.state()||find(r))return false;
  auto*s=new State(r,b);if(!s->watch(s->globals,Globals)||!lua_observetablebirth(s->L,born,s)){s->retire("native-birth-observer");return false;}return true;
 }catch(...){if(auto*s=find(r))s->retire("begin-exception");return false;}
}
bool prepareAIRound56PatternsSources(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||s->prepared||s->L!=L||!current(*s))return false;s->prepared=true;
 try{if(!registerAIRound45PinnedSource(r,L,source,sha)){s->retire("source-pin");return false;}s->ready=true;return true;}
 catch(...){s->retire("prepare-exception");return false;}
}
bool completeAIRound56PatternsLineage(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&p)noexcept{
 auto*s=find(r);if(!s)return false;
 try{
  if(!current(*s)||!s->ready||s->completed||s->L!=L||p.m_consumed||p.m_roomLifetime.data()!=&r||p.m_room!=&r||p.m_lua!=L||p.m_generation!=s->generation||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial)){s->retire("completion-token");return false;}
  if(!lua_checkstack(L,4096)){s->retire("completion-stack");return false;}
  Stack z(L);AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"patterns");
  if(!graph(*s,L,-1)){s->retire("completion-source-graph");return false;}
  s->completed=true;return current(*s)&&p.m_current(r,L,p.m_serial);
 }catch(...){s->retire("completion-exception");return false;}
}
bool hasAIRound56PatternsLineage(const Room&r,lua_State*L)noexcept{auto*s=find(r);return s&&s->L==L&&s->completed&&current(*s);}
void retireAIRound56PatternsLineage(Room&r,lua_State*L)noexcept{if(auto*s=find(r))if(s->L==L)s->alive=false;}
void clearAIRound56PatternsLineage(Room&r)noexcept{delete find(r);}
AIRound56PatternsScope::AIRound56PatternsScope(Room&r,lua_State*L)noexcept{
 // The exact public catalogue call can prove its own complete source graph.
 // It does not grant full Projection, native Card policy or any factory input.
 auto*s=find(r);
 try{
  if(!s||!hasAIRound56PatternsLineage(r,L)||active||!hasAIRound56AsyncUniverse(r,L)||Sanguosha->currentRoom()!=&r
   ||lua_gettop(L)<2||lua_type(L,-1)!=LUA_TBOOLEAN||!lua_toboolean(L,-1)||!lua_checkstack(L,4096)||!graph(*s,L,-2)){
   if(s)s->retire("query-source-graph");else retireAIRound56Projection(r,L);return;
  }
  d.reset(new Data(s));
  if(!inAIRound56ProjectionScope(r,L)) {
   auto*rt=r.roomRuntime();QString why;
   if(!rt||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
     ||!hasAIRound56CloneInputLineage(r,L)||!hasAIRound56ProjectionLineage(r,L)
     ||!proveAIRound56ServerLogEffects(r)||!Sanguosha->aiRound56PublicPatternMetadataDefinitions(r,&why)) {
    if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP"))
     std::fprintf(stderr,"R56_PATTERNS_NATIVE_REFUSAL reason=%s\n",qPrintable(why));
    s->retire("independent-native-source");d.reset();return;
   }
   d->lifetime.reset(new CardLifetimeScope(globalCardLifetimeManager(),"R56PatternsCatalogue"));
   d->async.reset(new AIRound56AsyncScope(r,L,AIRound56AsyncScope::GameplayVariants));
   if(!d->async->accepted()){s->retire("independent-allocation-source");d.reset();return;}
   d->call.independent=true;
  }
  d->call.function=lua_topointer(L,-2);d->call.depth=r.roomRuntime()->lua().protectedCallDepth();active=&d->call;
 }catch(...){if(s)s->retire("query-preflight-exception");else retireAIRound56Projection(r,L);}
}
AIRound56PatternsScope::~AIRound56PatternsScope(){
 if(!d)return;auto&c=d->call;auto&s=*c.s;
 try{
  if(!c.entered||!current(s)||!nativeContext(c))s.retire("query-exit-continuity");
  else{Stack z(s.L);AIRound45Inspect::globals(s.L);AIRound45Inspect::field(s.L,-1,"patterns");if(!graph(s,s.L,-1))s.retire("query-exit-source-graph");}
 }catch(...){s.retire("query-exit-exception");}
 active=c.previous;
}
bool acceptAIRound56PatternsEntry(Room&r,lua_State*L,int n)noexcept{
 auto*c=active;if(!c||&c->s->r!=&r||c->s->L!=L||c->entered||n!=1||lua_gettop(L)<2||!current(*c->s)
  ||!nativeContext(*c)||!hasAIRound56AsyncUniverse(r,L)||r.roomRuntime()->lua().protectedCallDepth()!=c->depth
  ||lua_topointer(L,-2)!=c->function||lua_type(L,-1)!=LUA_TBOOLEAN||!lua_toboolean(L,-1))return false;
 c->entered=true;return true;
}
bool allowAIRound56PatternsGlobalWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=find(r);return s&&s->completed&&current(*s)&&equipWrite(*s,L,e);}catch(...){if(auto*s=find(r))s->retire("shared-write-exception");return false;}
}
