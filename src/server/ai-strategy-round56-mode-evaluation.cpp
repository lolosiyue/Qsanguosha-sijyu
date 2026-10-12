#include "ai-strategy-round56-bootstrap-capture-metadata.h"
#include <cstdio>
#ifdef Q_OS_LINUX
#include <execinfo.h>
#endif
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round45-continuity.h"
#include <QScopeGuard>
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "lopcodes.h"
#include <QHash>
#include <QSet>
#include <cstring>
namespace {
struct Stack{lua_State*L;int top;Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
void raw(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool plain(lua_State*L,int i){if(!lua_istable(L,i))return false;if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;}
const Proto*proto(lua_State*L,int i){return lua_isfunction(L,i)&&!lua_iscfunction(L,i)?static_cast<const LClosure*>(lua_topointer(L,i))->p:nullptr;}
struct Source{const char*path;const char*sha;};
constexpr Source modeSource{"lua/ai/mode-ai.lua","cd0a6fa3b6de7e90db276846f04fe54b01199c28a1d68df3c452f6b14b8ac3e5"};
bool inModeScope(const Room&,lua_State*)noexcept;
struct State;void observeRoot(lua_State*,const lua_TableObservation*,void*)noexcept;
void observeBirth(lua_State*,const lua_TableBirthObservation*,void*)noexcept;
void barrier(Room&r,lua_State*L)noexcept{
 retireAIRound56AsyncUniverse(L);retireAIRound56CloneInputLineage(r,L);retireAIRound53AiNoTo(r);
 if(auto*rt=r.roomRuntime()){rt->revokePrioritySource();rt->revokeXufaTransportContinuity();}
 observeAIRound45UnprovedNativeQuery(&r);
}
struct State final:QObject {
 Room&room;lua_State*L;quint64 generation;
 bool alive=true,completed=false,seeding=true,sourceReady=false,prepareAttempted=false;
 QHash<QString,const void*>privateRoots;QSet<const void*>tables;
 State(Room&r,lua_State*l,quint64 g):QObject(&r),room(r),L(l),generation(g){}
 ~State()override{if(L){lua_unobservetablebirth(L,observeBirth,this);for(const void*p:tables)lua_unobservetableidentity(L,p,observeRoot,this);}}
 void retire()noexcept{
  if(alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {std::fprintf(stderr,"R56_MODE_SOURCE_RETIRE seeding=%d ready=%d tracked=%lld\n",int(seeding),int(sourceReady),static_cast<long long>(tables.size()));std::fflush(stderr);}
  alive=false;barrier(room,L);
 }
 bool watch(const void*p){if(tables.contains(p))return true;
  if(!lua_observetableidentity(L,p,observeRoot,this))return false;
  try{tables.insert(p);}catch(...){lua_unobservetableidentity(L,p,observeRoot,this);throw;}return true;
 }
};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
const QList<int>modeAnchors={10,24,40,49,59,209,221,240,250,310,317,321,327,338,395,656,662,671,889,903,1067,1198,1274,1280,1292,1302,1326,1347,1386,1404,1586,1597,1624,1642,1650};
thread_local AIRound56ModeEvaluationScope*active=nullptr;
thread_local Room*activeRoom=nullptr;thread_local lua_State*activeLua=nullptr;
struct NativeWorld{Room*r;lua_State*L;CallInfo*frame;int depth;NativeWorld*previous;const void*root=nullptr;};
// Metadata from actual ordinary prepare NEWTABLE instructions; never token data.
struct ObjectiveReceipt{Room*r;lua_State*L;const void*result=nullptr;const void*token=nullptr;bool returned=false,committed=false;};
thread_local ObjectiveReceipt*objectiveReceipt=nullptr;
thread_local NativeWorld*nativeWorld=nullptr;
bool nativeProducer(State&s,lua_State*L)noexcept{
 auto*rt=s.room.roomRuntime();return nativeWorld&&nativeWorld->r==&s.room&&nativeWorld->L==L
  &&L->ci==nativeWorld->frame&&rt&&rt->lua().protectedCallDepth()==nativeWorld->depth&&inModeScope(s.room,L);
}
bool sourceInstruction(State&s,lua_State*L,const void*fn,int pc,int opcode=-1){
 if(!s.sourceReady||(!s.seeding&&!inModeScope(s.room,L)))return false;
 return matchAIRound56PinnedLiveInstruction(s.room,L,fn,pc,modeSource.path,
   s.seeding?QList<int>{}:modeAnchors,opcode);
}
bool nativeSourceWrite(State&s,lua_State*L){
 if(nativeProducer(s,L))return true;
 // Every native mutator is both the exact pre-script binding and immediately
 // called at an authenticated source PC. No nearest-frame heuristic crosses
 // pcall, an unknown native callback, or a coroutine boundary.
 if(isLua(L->ci)||!L->ci->previous||!isLua(L->ci->previous))return false;
 bool leaf=false;for(const char*k:{"setmetatable","table.insert","table.remove","table.sort"})
  if(matchAIRound56NativeLeafFrame(s.room,L,QString::fromLatin1(k))){leaf=true;break;}
 if(!leaf)return false;
 auto*ci=L->ci->previous;const auto*fn=clLvalue(s2v(ci->func.p));const int pc=int(ci->u.l.savedpc-fn->p->code)-1;
 if(pc<0||pc>=fn->p->sizecode)return false;
 const auto op=GET_OPCODE(fn->p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&sourceInstruction(s,L,fn,pc);
}
void observeRoot(lua_State*L,const lua_TableObservation*e,void*u)noexcept{
 auto&s=*static_cast<State*>(u);
 if(e->event==LUA_TABLE_OBSERVER_FREE){
  s.tables.remove(e->table_identity);bool root=false;
  if(objectiveReceipt&&objectiveReceipt->r==&s.room&&objectiveReceipt->L==L){
   if(objectiveReceipt->result==e->table_identity){objectiveReceipt->result=nullptr;s.retire();}
   if(objectiveReceipt->token==e->table_identity){objectiveReceipt->token=nullptr;s.retire();}
  }
  for(auto i=s.privateRoots.begin();i!=s.privateRoots.end();)if(i.value()==e->table_identity){root=true;i=s.privateRoots.erase(i);}else ++i;
  if(root&&s.alive)s.retire();return;
 }
 if(!s.alive||e->event==LUA_TABLE_OBSERVER_READ||e->event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  bool ok=false;
  if(e->event==LUA_TABLE_OBSERVER_WRITE&&e->lua_function_identity)
   ok=sourceInstruction(s,L,e->lua_function_identity,e->lua_instruction_index);
  else if(e->event==LUA_TABLE_OBSERVER_WRITE||e->event==LUA_TABLE_OBSERVER_METATABLE)
   ok=nativeSourceWrite(s,L);
  if(!ok){
   if(s.alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {std::fprintf(stderr,"R56_MODE_WRITE_REFUSAL event=%d flags=%u lua_frame=%d pc=%d\n",e->event,e->flags,int(e->lua_function_identity!=nullptr),e->lua_instruction_index);std::fflush(stderr);}
   s.retire();
  }
 }catch(...){s.retire();}
}
void observeBirth(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!s.alive||!s.sourceReady)return;
 try{
  const bool source=e->lua_function_identity&&sourceInstruction(s,L,e->lua_function_identity,e->lua_instruction_index,OP_NEWTABLE);
  const bool native=!e->lua_function_identity&&nativeProducer(s,L);
  if(native&&!nativeWorld->root)nativeWorld->root=e->table_identity;
  if(source&&objectiveReceipt&&objectiveReceipt->r==&s.room&&objectiveReceipt->L==L){
   // Original standard_query_result creates these two tables once per call.
   // Full Proto + independent instruction map, never a live source hint.
   const void**role=nullptr;
   if(matchAIRound56PinnedInstruction(s.room,L,e->lua_function_identity,e->lua_instruction_index,modeSource.path,1597,1620,1614))role=&objectiveReceipt->result;
   else if(matchAIRound56PinnedInstruction(s.room,L,e->lua_function_identity,e->lua_instruction_index,modeSource.path,1597,1620,1615))role=&objectiveReceipt->token;
   if(role){if(*role){s.retire();return;}*role=e->table_identity;}
  }
  if((source||native)&&!s.watch(e->table_identity))s.retire();
  // Unrelated births confer no authority. During a proved evaluation all
  // new tables must be one of the two source products above. Bootstrap's
  // unrelated roots are covered separately by its complete source receipt.
  else if(!source&&!native&&inModeScope(s.room,L))s.retire();
 }catch(...){s.retire();}
}
// Independent refresh uses the Game VM binding and continuous source histories,
// never a completed projection scope as a transferable capability.
bool refreshContext(const Room&r,lua_State*L){
 auto*rt=const_cast<Room&>(r).roomRuntime();
 return rt&&!rt->isClosing()&&rt->lua().rawState()==L&&rt->lua().isCurrentThreadOwner()
  &&LuaRuntime::current()==&rt->lua()&&Sanguosha&&Sanguosha->currentRoom()==&r
  &&hasAIRound56CloneInputLineage(r,L)&&hasAIRound56ProjectionLineage(r,L)
  &&hasAIRound56AsyncUniverse(r,L);
}
bool publicMode(const QString&m){return m==QStringLiteral("02p")||m==QStringLiteral("04p");}
struct Check {
 State &c; lua_State *L; QString failure; int budget=100000;
 QSet<const void *> functions,tables;
 explicit Check(State &v):c(v),L(v.L){}
 bool no(const QString &where,const QString &why){if(failure.isEmpty())failure=where+": "+why;return false;}
 bool nativeBinding(int actual,const char *path,const QString &where){
  return matchAIRound45NativeBinding(c.room,L,actual,QString::fromLatin1(path))
    ?true:no(where,"native binding replaced or unaudited");
 }
 bool global(const QString &key,const QString &where,const Proto *caller){
  static const QSet<QString> values={"assert","error","getmetatable","ipairs","next","pairs","pcall",
   "rawequal","rawget","select","setmetatable","tonumber","tostring","type","unpack"};
  Stack stack(L);AIRound45Inspect::globals(L);raw(L,-1,key.toUtf8().constData());
  if(values.contains(key))return nativeBinding(-1,key.toUtf8().constData(),where);
  static const QHash<QString,QStringList> fields={
   {"math",{"abs","floor","ceil","max","min","huge"}},
   {"string",{"sub","find","match","gsub","format","len"}},
   {"table",{"insert","remove","sort","concat","unpack"}}};
  if(!fields.contains(key)||!plain(L,-1))return no(where,"unapproved global capability "+key);
  const int table=lua_absindex(L,-1);
  for(const auto &field:fields.value(key)){
   raw(L,table,field.toUtf8().constData());
   const QByteArray path=(key+"."+field).toUtf8();
   bool ok;
   if(key=="math"&&field=="huge")ok=matchAIRound45NativeConstant(c.room,L,-1,path);
   else ok=nativeBinding(-1,path.constData(),where+"."+field);
   lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool environment(const Proto *p,int up,const QString &where){
  // Lua's emitted _ENV accesses must be literal GETTABUP. Passing _ENV as a
  // value, dynamic indexing or writing globals is outside this pure-mode proof.
  for(int n=0;n<p->sizecode;++n){
   const Instruction instruction=p->code[n];const OpCode op=GET_OPCODE(instruction);
   if(op==OP_GETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment escapes as a value");
   if(op==OP_SETTABUP&&GETARG_A(instruction)==up)return no(where,"global write outside mode proof");
   if(op==OP_SETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment replacement");
   if(op==OP_GETTABUP&&GETARG_B(instruction)==up){
    const int constant=GETARG_C(instruction);
    if(constant<0||constant>=p->sizek||!ttisstring(&p->k[constant]))return no(where,"nonliteral environment lookup");
    const auto *s=tsvalue(&p->k[constant]);
    if(!global(QString::fromUtf8(getstr(s),qsizetype(tsslen(s))),where,p))return false;
   }
  }
  return true;
 }
 bool publicValue(int index,const QString &where,int depth=0){
  if(--budget<0||depth>64)return no(where,"public closure graph limit");
  index=lua_absindex(L,index);const int t=lua_type(L,index);
  if(t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER||t==LUA_TSTRING)return true;
  if(t==LUA_TFUNCTION)return function(index,where,depth+1);
  if(t!=LUA_TTABLE||!plain(L,index))return no(where,"public capture is not a plain value table");
  const void*p=lua_topointer(L,index);if(tables.contains(p))return true;tables.insert(p);
  lua_pushnil(L);while(AIRound45Inspect::next(L,index)){
   if(!publicValue(-2,where+".key",depth+1)||!publicValue(-1,where+".value",depth+1)){lua_pop(L,2);return false;}lua_pop(L,1);
  }return true;
 }
 bool ownedRoot(int index,const QString&name){
  // The complete bootstrap source receipt proves representation and absence
  // of escaped aliases. Do not enumerate a mind/index or inspect its values.
  if(!lua_istable(L,index))return no(name,"source-owned capture identity missing");
  const void*p=lua_topointer(L,index);auto i=c.privateRoots.constFind(name);
  if(c.seeding){if(!c.tables.contains(p))return no(name,"capture lacks exact source table birth");if(i!=c.privateRoots.cend())return *i==p;c.privateRoots.insert(name,p);return true;}
  return i!=c.privateRoots.cend()&&*i==p||no(name,"source-owned capture rebound");
 }
 bool policy(int index){
  if(!plain(L,index))return no("policies","public policy registry metatable");
  Stack z(L);raw(L,index,c.room.getMode().toUtf8().constData());if(!plain(L,-1))return no("02p","standard policy absent");
  int p=lua_absindex(L,-1);
  for(const char*k:{"roles","teams"}){raw(L,p,k);if(!plain(L,-1))return false;lua_pushnil(L);
   if(AIRound45Inspect::next(L,-2))return no("02p","author role/team override");lua_pop(L,1);}
  struct Hook{const char*name;int first,last;};
  for(const Hook &h: {Hook{"relation",1067,1077},Hook{"objective",889,898},Hook{"gameProcess",338,393},Hook{"rolePredictable",662,667}}){
   raw(L,p,h.name);if(!matchAIRound45PinnedFunction(c.room,L,-1,modeSource.path,h.first,h.last)||!function(-1,QString::fromLatin1(h.name),0))return no(h.name,"public standard hook provider changed");lua_pop(L,1);
  }
  return true;
 }
 bool capture(int index,const char*name,const QString&where,int depth){
  if(!std::strcmp(name,"minds")||!std::strcmp(name,"active_indexes")||!std::strcmp(name,"readonly_worlds")||!std::strcmp(name,"prepared_objectives"))return ownedRoot(index,QString::fromLatin1(name));
  // generation's value is private history, not an admission predicate. Its
  // source-owned upvalue is changed only by the proved writers; never read it.
  if(!std::strcmp(name,"generation"))return true;
  // Source nil at mainchunk1534; only unadmitted event entrypoints1696/1711
  // assign it. Ordinary prepare passes no event_prefix, and its source token
  // consequently has no session. Never inspect a selected event-session value.
  if(!std::strcmp(name,"active_event_session"))return true;
  if(!std::strcmp(name,"policies"))return policy(index);
  if(!std::strcmp(name,"standard_modes")){
   Stack z(L);if(!plain(L,index))return false;raw(L,index,c.room.getMode().toUtf8().constData());
   return lua_type(L,-1)==LUA_TSTRING&&!std::strcmp(lua_tostring(L,-1),"identity")||no(where,"public standard mode registration changed");
  }
  if(!std::strcmp(name,"scenario_modes"))return plain(L,index); // base_mode first arm always true for public 02p
  return publicValue(index,where,depth);
 }
 bool childEnvironments(const Proto *p,const QString &where){
  for(int i=0;i<p->sizep;++i){const Proto *child=p->p[i];
   for(int u=0;u<child->sizeupvalues;++u)
    if(child->upvalues[u].name&&!std::strcmp(getstr(child->upvalues[u].name),"_ENV")
       &&!environment(child,u,where+".<child>"))return false;
   if(!childEnvironments(child,where+".<child>"))return false;
  }return true;
 }
 bool primitiveMetatables(){
  Stack stack(L);
  for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){
   if(kind==LUA_TNIL)lua_pushnil(L);
   else if(kind==LUA_TBOOLEAN)lua_pushboolean(L,0);
   else if(kind==LUA_TNUMBER)lua_pushnumber(L,0);
   else {AIRound45Inspect::globals(L);raw(L,-1,"type");lua_remove(L,-2);}
   if(lua_getmetatable(L,-1))return no("primitive.metatable","unexpected primitive metamethod capability");
   lua_pop(L,1);
  }
  if(!matchAIRound47NativeStringMetatable(c.room,L))return no("string.metatable","native string metatable changed");
  AIRound45Inspect::globals(L);raw(L,-1,"string");const int strings=lua_absindex(L,-1);
  if(!plain(L,strings))return no("string.methods","method library redirected");
  for(const char *key:{"sub","find","match"}){raw(L,strings,key);
   bool ok=nativeBinding(-1,(QByteArray("string.")+key).constData(),"string.methods");lua_pop(L,1);if(!ok)return false;}
  return true;
 }
 bool function(int index,const QString &where,int depth){
  index=lua_absindex(L,index);const Proto *p=proto(L,index);
  if(!p||!matchAIRound45PinnedFunctionFamily(c.room,L,index,QString::fromLatin1(modeSource.path),modeAnchors))return no(where,"Lua callback is outside pinned mode closure union");
  const void *identity=lua_topointer(L,index);if(functions.contains(identity))return true;functions.insert(identity);
  if(!childEnvironments(p,where))return false;
  for(int n=1;;++n){
   const char *name=lua_getupvalue(L,index,n);if(!name)break;
   bool ok;
   if(!std::strcmp(name,"_ENV")){
    AIRound45Inspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);
    ok=ok&&environment(p,n-1,where+"._ENV");
   }else ok=capture(-1,name,where+"."+QString::fromUtf8(name),depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};

bool graph(State&s,QString*why){
 Stack z(s.L);Check check(s);AIRound45Inspect::globals(s.L);
 if(!plain(s.L,-1))return false;raw(s.L,-1,"sgs");if(!lua_istable(s.L,-1))return false;
 const int ns=lua_absindex(s.L,-1);
 if(!check.primitiveMetatables()){if(why)*why=check.failure;return false;}
 struct Root{const char*name;int first,last;};
 for(const auto&root:{Root{"evaluateModeAI",1404,1578},Root{"prepareStandardObjective",1642,1648},Root{"prepareStandardRelations",1650,1655},Root{"commitStandardObjective",1624,1640}}){
  Stack item(s.L);raw(s.L,ns,root.name);
  if(!matchAIRound45PinnedFunction(s.room,s.L,-1,modeSource.path,root.first,root.last)||!check.function(-1,QString::fromLatin1(root.name),0)){
   if(why)*why=check.failure.isEmpty()?QStringLiteral("mode root source differs: ")+QString::fromLatin1(root.name):check.failure;return false;
  }
 }
 if(why)*why=check.failure;return true;
}
}
bool beginAIRound56ModeEvaluationLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth)noexcept{
 try{
  auto*rt=r.roomRuntime();if(find(r)||!rt||!birth.isCurrent()||rt->lua().rawState()!=birth.state())return false;
  auto*s=new State(r,birth.state(),birth.generation());
  if(!lua_observetablebirth(s->L,observeBirth,s)){s->retire();return false;}return true;
 }catch(...){if(auto*s=find(r))s->retire();return false;}
}
bool prepareAIRound56ModeEvaluationSources(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||!s->alive||s->L!=L||s->prepareAttempted)return false;s->prepareAttempted=true;
 try{
  // Sole lexical caller: after Binding/native seals, BEFORE config/rules.
  // Compilation here has only the audited pre-authored native allocations.
  s->sourceReady=registerAIRound45PinnedSource(r,L,modeSource.path,modeSource.sha);
  if(!s->sourceReady)s->retire();return s->sourceReady;
 }catch(...){s->retire();return false;}
}
bool completeAIRound56ModeEvaluationLineage(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&p)noexcept{
 auto*s=find(r);try{
  auto*rt=r.roomRuntime();if(!s||!s->alive||s->completed||!s->sourceReady||!rt||s->L!=L||!publicMode(r.getMode())
   ||p.m_consumed||p.m_roomLifetime.data()!=static_cast<QObject*>(&r)||p.m_room!=&r||p.m_lua!=L
   ||p.m_generation!=rt->lua().generation()||p.m_generation!=s->generation||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial))return false;
  if(!lua_checkstack(L,4096)||!graph(*s,nullptr)){s->retire();s->seeding=false;return false;}
  s->completed=s->privateRoots.contains("minds")&&s->privateRoots.contains("active_indexes")&&s->privateRoots.contains("readonly_worlds")&&s->privateRoots.contains("prepared_objectives");
  s->seeding=false;s->completed=s->completed&&p.m_current(r,L,p.m_serial);if(!s->completed)s->retire();return s->completed;
 }catch(...){if(s)s->retire();return false;}
}
bool hasAIRound56ModeEvaluationLineage(const Room&r,lua_State*L)noexcept{
 auto*s=find(r);auto*rt=const_cast<Room&>(r).roomRuntime();return s&&s->alive&&s->completed&&rt&&!rt->isClosing()
  &&s->L==L&&rt->lua().rawState()==L&&s->generation==rt->lua().generation()&&hasAIRound56AsyncUniverse(r,L);
}
bool proveAIRound56ModeEvaluationUnion(Room&r,lua_State*L,QString*why)noexcept{
 try{
  if(!hasAIRound56ModeEvaluationLineage(r,L)||!publicMode(r.getMode())||!lua_checkstack(L,4096)){
   if(why)*why=QStringLiteral("source-owned standard mode lineage unavailable");return false;
  }
  QString detail;
  if(graph(*find(r),why?why:&detail))return true;
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-graph reason=%s\n",(why?*why:detail).toUtf8().constData());std::fflush(stderr);
  }
 }catch(...){}
 retireAIRound56ModeEvaluationLineage(r,L);return false;
}
void retireAIRound56ModeEvaluationLineage(Room&r,lua_State*L)noexcept{auto*s=find(r);if(s&&s->L==L){
 if(s->alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_MODE_EXTERNAL_RETIRE seeding=%d tracked=%lld\n",int(s->seeding),static_cast<long long>(s->tables.size()));std::fflush(stderr);
#ifdef Q_OS_LINUX
  // Code addresses only at an already-decided retirement; no gameplay values,
  // source admission, callback dispatch or retirement conditions are changed.
  std::fprintf(stderr,"R56_MODE_RETIRE_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
  void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
  std::fprintf(stderr,"R56_MODE_RETIRE_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
 }
 s->alive=false;
}}
void clearAIRound56ModeEvaluationLineage(Room&r)noexcept{delete find(r);}
struct AIRound56ModeEvaluationScope::Data{
 Room&r;lua_State*L;AIRound56ModeEvaluationScope*previous=nullptr;Room*previousRoom=nullptr;lua_State*previousLua=nullptr;
 std::unique_ptr<AIRound56AsyncScope>async;const void*function=nullptr,*commit=nullptr,*world=nullptr;
 Entry entry=Evaluation;ObjectiveReceipt objective;ObjectiveReceipt*previousObjective=nullptr;
 int depth=0;bool ready=false,entered=false,invalid=false;
 Data(Room&a,lua_State*b):r(a),L(b),objective{&a,b}{}
};
AIRound56ModeEvaluationScope::AIRound56ModeEvaluationScope(Room&r,lua_State*L,const QString&publicMode,Entry entry):d(new Data(r,L)){
 d->entry=entry;d->previousObjective=objectiveReceipt;
 auto failure=qScopeGuard([&]{if(!d->ready){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-scope-preflight\n");std::fflush(stderr);}
  retireAIRound56ModeEvaluationLineage(r,L);barrier(r,L);
 }});
 d->previous=active;d->previousRoom=activeRoom;d->previousLua=activeLua;active=this;activeRoom=&r;activeLua=L;auto*rt=r.roomRuntime();
 try{
  if(!rt||publicMode!=r.getMode()||!Sanguosha||Sanguosha->currentRoom()!=&r
   ||(entry==StandardObjective&&!inAIRound56ProjectionScope(r,L))
   ||(entry==StandardRelations&&!refreshContext(r,L))
   ||(entry!=Evaluation&&(d->previous||objectiveReceipt))
   ||!proveAIRound56ModeEvaluationUnion(r,L))return;
  Stack z(L);AIRound45Inspect::globals(L);raw(L,-1,"sgs");int ns=lua_absindex(L,-1);
  raw(L,ns,entry==Evaluation?"evaluateModeAI":entry==StandardRelations?"prepareStandardRelations":"prepareStandardObjective");d->function=lua_topointer(L,-1);lua_pop(L,1);
  if(entry!=Evaluation){raw(L,ns,"commitStandardObjective");d->commit=lua_topointer(L,-1);objectiveReceipt=&d->objective;}
  d->depth=rt->lua().protectedCallDepth();d->async.reset(new AIRound56AsyncScope(r,L));d->ready=d->async->accepted();
 }catch(...){d->invalid=true;}
}
AIRound56ModeEvaluationScope::~AIRound56ModeEvaluationScope(){objectiveReceipt=d->previousObjective;active=d->previous;activeRoom=d->previousRoom;activeLua=d->previousLua;}
bool AIRound56ModeEvaluationScope::accepted()const noexcept{return d->ready&&!d->invalid&&hasAIRound56ModeEvaluationLineage(d->r,d->L)
 &&(d->entry==Evaluation||(d->entry==StandardRelations?refreshContext(d->r,d->L):inAIRound56ProjectionScope(d->r,d->L)));}
void AIRound56ModeEvaluationScope::preparedObjective(int resultIndex,int tokenIndex)noexcept{
 if(d->entry==Evaluation)return;
 auto*s=find(d->r);
 if(!accepted()||!d->entered||d->objective.returned||!s||!d->objective.result||!d->objective.token
  ||lua_topointer(d->L,resultIndex)!=d->objective.result||lua_topointer(d->L,tokenIndex)!=d->objective.token
  ||!s->tables.contains(d->objective.result)||!s->tables.contains(d->objective.token)){
  d->invalid=true;retireAIRound56ModeEvaluationLineage(d->r,d->L);barrier(d->r,d->L);return;
 }
 d->objective.returned=true;
}
namespace {bool inModeScope(const Room&r,lua_State*L)noexcept{return active&&activeRoom==&r&&activeLua==L&&active->accepted();}}
bool acceptAIRound56ModeEvaluationEntry(Room&r,lua_State*L,int n)noexcept{
 if(!active||&active->d->r!=&r||active->d->L!=L)return false;auto&d=*active->d;auto*rt=r.roomRuntime();
 if(!active->accepted()||!rt||n<0||lua_gettop(L)<n+1)return false;
 const int f=lua_gettop(L)-n;
 if(!d.entered&&n==2&&rt->lua().protectedCallDepth()==d.depth&&lua_topointer(L,f)==d.function){
  if(d.entry!=AIRound56ModeEvaluationScope::Evaluation&&(!d.world||lua_topointer(L,f+1)!=d.world))return false;
  if(d.entry==AIRound56ModeEvaluationScope::StandardObjective&&lua_type(L,f+2)!=LUA_TSTRING)return false;
  if(d.entry==AIRound56ModeEvaluationScope::StandardRelations&&!lua_isnil(L,f+2)
    &&(!lua_isinteger(L,f+2)||lua_tointeger(L,f+2)<0))return false;
  d.entered=true;return true;
 }
 if(d.entry!=AIRound56ModeEvaluationScope::Evaluation&&d.entered&&d.objective.returned&&!d.objective.committed&&n==1
  &&rt->lua().protectedCallDepth()==d.depth&&lua_topointer(L,f)==d.commit&&d.objective.token
  &&lua_topointer(L,f+1)==d.objective.token){d.objective.committed=true;return true;}
 // Reentrant pcall targets can only be the proved fixed query hooks/helpers or
 // fresh lexical descendants constructed by this same complete source graph.
 if(d.entered&&rt->lua().protectedCallDepth()>d.depth
  &&matchAIRound45PinnedFunctionFamily(r,L,f,modeSource.path,modeAnchors))return true;
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-entry argc=%d entered=%d relative_depth=%d\n",n,int(d.entered),rt->lua().protectedCallDepth()-d.depth);std::fflush(stderr);
 }
 d.invalid=true;retireAIRound56ModeEvaluationLineage(r,L);retireAIRound56AsyncUniverse(L);return false;
}

struct AIRound56ModeWorldSource::Data{NativeWorld source;bool installed=false;};
AIRound56ModeWorldSource::AIRound56ModeWorldSource(Room&r,lua_State*L):d(new Data){
 if(!inModeScope(r,L))return;auto*rt=r.roomRuntime();if(!rt)return;
 d->source={&r,L,L->ci,rt->lua().protectedCallDepth(),nativeWorld};nativeWorld=&d->source;d->installed=true;
}
AIRound56ModeWorldSource::~AIRound56ModeWorldSource(){if(d->installed){
 if(active&&activeRoom==d->source.r&&activeLua==d->source.L&&active->d->entry!=AIRound56ModeEvaluationScope::Evaluation){
  // Actual first native table born inside this exact pushAIWorldView span.
  if(active->d->world||!d->source.root){active->d->invalid=true;barrier(*d->source.r,d->source.L);}
  else active->d->world=d->source.root;
 }
 nativeWorld=d->source.previous;
}}

bool hasAIRound56BootstrapModeTableOrigin(const Room &r,lua_State *L,const void *identity) noexcept {
 auto *s=find(r);auto *rt=const_cast<Room &>(r).roomRuntime();
 const bool okay=s&&rt&&identity&&s->alive&&s->seeding&&!s->completed&&s->sourceReady
  &&s->L==L&&rt->lua().rawState()==L&&rt->lua().generation()==s->generation
  &&rt->lua().isCurrentThreadOwner()&&s->tables.contains(identity);
 if(!okay&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
  std::fprintf(stderr,"R56_MODE_ORIGIN_REFUSAL state=%d alive=%d seeding=%d completed=%d ready=%d member=%d tracked=%lld\n",int(s!=nullptr),int(s&&s->alive),int(s&&s->seeding),int(s&&s->completed),int(s&&s->sourceReady),int(s&&s->tables.contains(identity)),s?static_cast<long long>(s->tables.size()):0LL);std::fflush(stderr);
 }
 return okay;
}
