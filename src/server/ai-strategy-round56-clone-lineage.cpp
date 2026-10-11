#include "ai-strategy-round56-playloop-filter.h"
#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-connection-cache.h"
#include <QSet>
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-clone-scope.h"
#include "ai-strategy-round56-bootstrap-write.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round53-ainoto.h"
#include "room.h"
#include "room-runtime.h"
#include <QHash>
#include <cstring>
extern "C" {
#include "lstate.h"
}
namespace {
constexpr const char*smart="lua/ai/smart-ai.lua";
enum Kind {Globals,Namespace,Ais,AiSelf,ScalarMap,RoleMap,ScalarChild,PlayerCache,PlayerArray};
struct Root {const char*key;int line;Kind kind;const char*source;bool required;};
const Root roots[]={
 {"ais",64,Ais,smart,true},{"ai_role",65,ScalarMap,smart,true},
 {"roleValue",66,RoleMap,smart,true},{"ai_NeedPeach",118,ScalarMap,smart,true},
 {"playerRoles",239,ScalarMap,smart,true},{"qlist_cache",243,PlayerCache,smart,true},
 {"defense",378,ScalarMap,smart,false}
};
constexpr int rootCount=sizeof roots/sizeof*roots;
struct State;void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
struct State final:QObject {
 Room&r;lua_State*L;quint64 generation;bool alive=true,completed=false;
 const void*globals;const void*space;const void*root[rootCount]={};
 QHash<const void*,Kind>watches;QSet<QString>connectionKeys;
 State(Room&room,const LuaRuntime::NativeNamespaceBirth&birth):QObject(&room),r(room),L(birth.state()),generation(birth.generation()),globals(birth.globalsIdentity()),space(birth.namespaceIdentity()){}
 ~State()override{if(L)for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),observe,this);}
 void retire()noexcept{alive=false;retireAIRound56AsyncUniverse(L);retireAIRound53AiNoTo(r);if(auto*rt=r.roomRuntime())rt->revokePrioritySource();}
 bool watch(const void*p,Kind k){
  if(!p)return false;
  const auto old=watches.constFind(p);
  if(old!=watches.cend())return *old==k;
  if(!lua_observetableidentity(L,p,observe,this))return false;
  try{watches.insert(p,k);return true;}catch(...){lua_unobservetableidentity(L,p,observe,this);throw;}
 }
};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool source(State&s,lua_State*L,const lua_TableObservation&e,const char*path,int first,int last,int line){
 return e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_SLOW|LUA_TABLE_OBSERVER_VM_FAST))
  &&matchAIRound56PinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,path,first,last,line);
}
bool namespaceSource(State&s,lua_State*L,const lua_TableObservation&e,const char*k,int line){
 if(source(s,L,e,smart,339,429,line))return true;
 // The SWIG namespace __newindex is the sole native bridge. No stack push;
 // validate its exact actual frame and the parent Lua instruction provenance.
 if(!matchAIRound56NativeNamespaceWriteFrame(s.r,L,e.table_identity,QStringLiteral("sgs"),k))return false;
 auto*ci=L->ci->previous;if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 auto*cl=clLvalue(s2v(ci->func.p));auto*p=cl->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 return matchAIRound56PinnedInstruction(s.r,L,cl,int(ci->u.l.savedpc-p->code)-1,smart,339,429,line);
}
bool numberOrString(const lua_TableObservation&e){return e.value_type==LUA_TNUMBER||e.value_type==LUA_TSTRING;}
bool sourceScalar(State&s,lua_State*L,const lua_TableObservation&e){
 if(!numberOrString(e))return false;
 // Full constructor graph proves every such operand scalar before this event.
 for(int line:{399,401,406,407,408,411,412,414,415})
  if(source(s,L,e,smart,339,429,line))return true;
 return source(s,L,e,smart,2105,2116,2111)||source(s,L,e,smart,2105,2116,2114);
}
bool rootWrite(State&s,lua_State*L,const lua_TableObservation&e,int n){
 const Root&r=roots[n];
 if(!s.completed){
  // Native namespace birth established absence. Each root can be produced only
  // once here, from the actual fresh-allocation source branch.
  if(s.root[n]||!r.required||!matchAIRound56BootstrapWrite(s.r,L,e,r.source,r.line,r.key,true))return false;
 }else{
  // The only root replacement in the complete 02p constructor graph.
  if(std::strcmp(r.key,"defense")||!inAIRound56CloneScope(s.r,L)
   ||!matchAIRound56BootstrapWrite(s.r,L,e,smart,378,"defense",true,339,429))return false;
 }
 if(!s.watch(e.value_table_identity,r.kind))return false;
 s.root[n]=e.value_table_identity;return true;
}
void observe(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;const Kind kind=*i;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  // Identities are never retained past FREE or reused as lineage for a new table.
  bool rooted=e.table_identity==s.globals||e.table_identity==s.space;
  for(auto&p:s.root)if(p==e.table_identity){p=nullptr;rooted=true;}
  s.watches.remove(e.table_identity);if(rooted)s.retire();return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire();return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 try{
  // Exact compiled event source writes, independently checked before either
  // observer updates its ledger. No epoch/root value is adopted or restored.
  if(s.completed&&(kind==Globals||kind==Namespace)&&allowAIRound56PlayLoopWrite(s.r,L,e)){
   if(kind==Namespace&&key(e,"qlist_cache")){
    int n=0;for(;n<rootCount;++n)if(!std::strcmp(roots[n].key,"qlist_cache"))break;
    if(n==rootCount||!s.watch(e.value_table_identity,PlayerCache)){s.retire();return;}
    s.root[n]=e.value_table_identity;s.connectionKeys.clear();
   }
   return;
  }
  if(kind==Globals){
   if(s.completed&&allowAIRound56PatternsGlobalWrite(s.r,L,e))return;
   if(key(e,"sgs")){s.retire();return;}
   if(key(e,"global_room")||key(e,"current_self")){
    if(!s.completed||!inAIRound56CloneScope(s.r,L)){s.retire();return;}
    const int line=key(e,"global_room")?385:421;
    if(!source(s,L,e,smart,339,429,line)){s.retire();return;}
    if(line==421&&(e.value_type!=LUA_TTABLE||!s.watch(e.value_table_identity,AiSelf)))s.retire();
   }
   if(s.completed&&!key(e,"global_room")&&!key(e,"current_self")
    &&!(key(e,"global_delay")&&inAIRound56CloneScope(s.r,L)&&source(s,L,e,smart,339,429,389)))s.retire();
   return;
  }
  if(s.completed&&(kind==Namespace||kind==PlayerCache)&&allowAIRound56ConnectionCacheWrite(s.r,L,e)){
   if(kind==PlayerCache)s.connectionKeys.insert(QString::fromUtf8(e.string_key,int(e.string_key_length)));
   return;
  }
  if(kind==Namespace){
   for(int n=0;n<rootCount;++n)if(key(e,roots[n].key)){if(!rootWrite(s,L,e,n))s.retire();return;}
   if(s.completed){
    struct Write{const char*k;int line;};const Write writes[]={
     {"drawData",379},{"turncount",380},{"throwData",381},{"damageData",382},
     {"convertData",383},{"recoverData",384},{"sort_time",386},{"getMode",387},
     {"AIChat",388},{"ai_humanized",391},{"explicit_renegade",410},{"debugmode",425}};
    bool ok=false;for(const auto&w:writes)if(key(e,w.k)){
     ok=inAIRound56CloneScope(s.r,L)&&namespaceSource(s,L,e,w.k,w.line);break;
    }
    // Any other namespace write could introduce a protected-root alias. This
    // conservative retirement is source-time, not a scan of its private value.
    if(!ok)s.retire();
   }
   return;
  }
  if(kind==AiSelf){
   // Only raw Room/player fields are used by current_ai. Other self fields are
   // independent foreign-cache receipts and cannot alter this capability pair.
   if(key(e,"room")||key(e,"player"))s.retire();return;
  }
  if(!s.completed||!inAIRound56CloneScope(s.r,L)){s.retire();return;}
  bool ok=false;
  if(kind==ScalarMap||kind==ScalarChild)ok=sourceScalar(s,L,e);
  else if(kind==RoleMap){
   if(e.value_type==LUA_TTABLE){
    ok=source(s,L,e,smart,339,429,400);
    // Each permitted RHS is a fresh scalar-literal table or this same already
    // watched child (`or {}`). No heap walk or value-dependent admission.
    if(ok)ok=s.watch(e.value_table_identity,ScalarChild);
   }
  }else if(kind==PlayerCache){
   ok=e.value_type==LUA_TTABLE&&source(s,L,e,smart,443,454,451);
   // Complete QList2Table copies borrowed native ServerPlayer handles into a
   // fresh plain array. All native methods on this cache were proved upfront.
   if(ok)ok=s.watch(e.value_table_identity,PlayerArray);
  }else if(kind==Ais){
   ok=e.value_type==LUA_TTABLE&&source(s,L,e,smart,339,429,427);
   if(ok)ok=s.watch(e.value_table_identity,AiSelf);
  }
  // PlayerArray has no writer in CloneAI; any raw/native/Lua write retires.
  if(!ok)s.retire();
 }catch(...){s.retire();}
}
}
bool beginAIRound56CloneInputLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth)noexcept{
 try{
  auto*rt=r.roomRuntime();if(!birth.isCurrent()||!rt||rt->lua().rawState()!=birth.state()||find(r))return false;
  auto*s=new State(r,birth);
  if(!s->watch(s->globals,Globals)||!s->watch(s->space,Namespace)){s->retire();return false;}
  return true;
 }catch(...){if(auto*s=find(r))s->retire();return false;}
}
bool completeAIRound56CloneInputLineage(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&proof)noexcept{
 auto*s=find(r);auto*rt=r.roomRuntime();
 if(!s||!s->alive||s->completed||!rt||s->L!=L||s->generation!=rt->lua().generation()
  ||proof.m_consumed||proof.m_roomLifetime.data()!=static_cast<QObject*>(&r)||proof.m_room!=&r||proof.m_lua!=L||proof.m_generation!=s->generation
  ||!proof.m_serial||!proof.m_current||!proof.m_current(r,L,proof.m_serial))return false;
 for(int n=0;n<rootCount;++n)if(roots[n].required&&!s->root[n])return false;
 s->completed=true;return true;
}
bool hasAIRound56CloneInputLineage(const Room&r,lua_State*L)noexcept{
 auto*s=find(r);auto*rt=const_cast<Room&>(r).roomRuntime();
 return s&&s->alive&&s->completed&&rt&&!rt->isClosing()&&s->L==L
  &&rt->lua().rawState()==L&&rt->lua().generation()==s->generation;
}
void retireAIRound56CloneInputLineage(Room&r,lua_State*L)noexcept{auto*s=find(r);if(s&&s->L==L)s->alive=false;}

void clearAIRound56CloneInputLineage(Room&r)noexcept{delete find(r);}

bool hasAIRound56InitialQlistKeyAbsence(const Room&r,lua_State*L,const QString&key)noexcept{
 return (key==QStringLiteral("global_alive_players")||key.startsWith(QStringLiteral("skills_")))
  &&hasAIRound56CloneInputLineage(r,L)&&!find(r)->connectionKeys.contains(key);
}

bool captureAIRound56InitialConnectionRoots(const Room&r,lua_State*L,const void*&space,const void*&qlist)noexcept{
 if(!hasAIRound56CloneInputLineage(r,L))return false;auto*s=find(r);
 for(int n=0;n<rootCount;++n)if(!std::strcmp(roots[n].key,"qlist_cache")){
  if(!s->space||!s->root[n])return false;space=s->space;qlist=s->root[n];return true;
 }return false;
}
