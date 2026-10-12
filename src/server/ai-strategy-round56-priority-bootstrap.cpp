#include "ai-strategy-round56-priority-bootstrap.h"
#include "ai-strategy-round56-bootstrap-write.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include <QObject>
#include <cmath>
#include <cstring>
#include <new>
#include <cstdio>
namespace {
struct State;void observed(lua_State*,const lua_TableObservation*,void*)noexcept;
struct State final:QObject{
 explicit State(Room&r):QObject(&r),room(r){}
 ~State()override{for(const void*p:{globals,space,priority})if(p)lua_unobservetableidentity(L,p,observed,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=0;
 const void*globals=nullptr,*space=nullptr,*priority=nullptr;
 bool connectAbsence=true;
 const char*failureTag="";int failureEvent=0,failurePC=-1,failureFirst=-1,failureLast=-1;
 void retire(const char*why,const lua_TableObservation*e=nullptr)noexcept{
  if(!*failureTag){failureTag=why;if(e){failureEvent=e->event;failurePC=e->lua_instruction_index;
   if(e->lua_function_identity){const auto*cl=static_cast<const LClosure*>(e->lua_function_identity);failureFirst=cl->p->linedefined;failureLast=cl->p->lastlinedefined;}}}
  phase=Retired;
 }
 enum Phase{Started,Finished,Retired};Phase phase=Started;
 enum Cell{Awaiting,Absence,Literal,Lost};Cell zunwei=Awaiting,jink=Awaiting,exnihilo=Awaiting;
};
State*get(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->phase!=State::Retired
 &&s->L&&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation&&rt->lua().isCurrentThreadOwner()
 &&s->epoch&&rt->prioritySourceEpoch()==s->epoch;}
void observed(lua_State*L,const lua_TableObservation*ep,void*data)noexcept{
 auto*s=static_cast<State*>(data);if(!s||!ep||!L||!s->L||G(L)!=G(s->L))return;const auto&e=*ep;
 // FREE is handled even after retirement, so destruction never dereferences a
 // table address after GC and allocation cannot resurrect an old identity.
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  if(e.table_identity==s->globals)s->globals=nullptr;
  if(e.table_identity==s->space)s->space=nullptr;
  if(e.table_identity==s->priority)s->priority=nullptr;
  s->retire("observed-free",ep);return;
 }
 try {
 if(!live(s)){s->retire("continuity-before-observation",ep);return;}
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s->retire("root-metatable-write",ep);return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if(e.table_identity==s->globals&&key(e,"sgs")){s->retire("global-sgs-write",ep);return;}
 if(e.table_identity==s->space&&key(e,"ai_connect_cache"))s->connectAbsence=false;
 if(e.table_identity==s->space&&key(e,"ai_use_priority")){
  if(s->phase!=State::Started||s->priority
    ||!matchAIRound56BootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/smart-ai.lua"),74,"ai_use_priority",true)){
   s->retire("priority-root-source",ep);return;
  }
  const void*created=e.value_table_identity;
  if(!lua_observetableidentity(L,created,observed,s)){s->retire("priority-root-watch",ep);return;}
  s->priority=created;s->jink=State::Absence;s->zunwei=State::Absence;s->exnihilo=State::Absence;return;
 }
 if(e.table_identity!=s->priority)return;
 if(key(e,"Jink")){s->jink=State::Lost;return;} // includes same-value nil write
 if(key(e,"ExNihilo")){
  if(s->phase==State::Started&&s->exnihilo==State::Absence&&e.value_type==LUA_TNUMBER
   &&matchAIRound56BootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/standard_cards-ai.lua"),1873,"ExNihilo",false))
   s->exnihilo=State::Literal;
  else s->exnihilo=State::Lost;
 }
 if(key(e,"ZunweiCard")){
  if(s->phase==State::Started&&s->zunwei==State::Absence&&e.value_type==LUA_TNUMBER
   &&matchAIRound56BootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/tenyear_hc-ai.lua"),5238,"ZunweiCard",false))
   s->zunwei=State::Literal;
  else s->zunwei=State::Lost;
 }
 // Other cells cannot affect these actual source origins inside the caller's
 // complete proved graph. Unknown graphs retire the whole witness BEFORE entry.
 }catch(...){s->retire("observer-native-exception",ep);}
}
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool roots(State*s){
 if(!live(s)||!s->globals||!s->space||!s->priority||!AIRound45Inspect::capacity(s->L,8))return false;
 Stack z(s->L);AIRound45Inspect::globals(s->L);auto*t=AIRound45Inspect::table(s->L,-1);
 if(!t||t->metatable||t!=s->globals)return false;
 AIRound45Inspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;
 AIRound45Inspect::field(s->L,-1,"ai_use_priority");t=AIRound45Inspect::table(s->L,-1);
 return t&&!t->metatable&&t==s->priority;
}
}
bool beginAIRound56PriorityBootstrap(Room&r,const LuaRuntime::NativeNamespaceBirth&birth){
 if(get(r))return false; // even a failed/retired initial attempt is one-shot
 auto*s=new State(r);auto*rt=r.roomRuntime();s->L=birth.state();
 if(!birth.isCurrent()||!s->L||!rt||rt->lua().rawState()!=s->L||!rt->lua().isCurrentThreadOwner()){
  s->retire("native-birth-contract");return false;
 }
 s->generation=birth.generation();s->epoch=rt->prioritySourceEpoch();
 // Identities and ai_connect_cache absence originate only from the compiled
 // native birth descriptor. No Lua stack use/current-value bootstrap reseed.
 if(lua_observetableidentity(s->L,birth.globalsIdentity(),observed,s))s->globals=birth.globalsIdentity();
 if(lua_observetableidentity(s->L,birth.namespaceIdentity(),observed,s))s->space=birth.namespaceIdentity();
 if(!s->globals||!s->space||!s->epoch){s->retire("native-birth-watch");return false;}
 return true;
}
bool completeAIRound56PriorityBootstrap(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&proof)noexcept{
 auto*s=get(r);if(!s)return false;
 const auto fail=[&](const char*why){s->retire(why);return false;};
 if(s->L!=L)return fail("completion-state");
 if(s->phase!=State::Started)return fail("completion-phase");
 if(proof.m_room!=&r||proof.m_roomLifetime.data()!=&r||proof.m_lua!=L||proof.m_consumed||!proof.m_serial
  ||proof.m_generation!=s->generation||!proof.m_current||!proof.m_current(r,L,proof.m_serial))return fail("completion-source-token");
 if(!roots(s))return fail("completion-root-continuity");
 if(s->zunwei!=State::Literal)return fail("completion-zunwei-origin");
 if(s->jink!=State::Absence)return fail("completion-jink-origin");
 if(s->exnihilo!=State::Literal)return fail("completion-exnihilo-origin");
 // Crucially, the original epoch is not changed/restamped here. Any earlier
 // native, callback, async or unknown-source interruption remains permanent.
 s->phase=State::Finished;return true;
}
void retireAIRound56PriorityBootstrap(Room&r)noexcept{if(auto*s=get(r))s->retire("external-source-retirement");}
void clearAIRound56PriorityBootstrap(Room&r)noexcept{delete get(r);}
bool hasAIRound56InitialPriority(Room&r,lua_State*L,const QString&k)noexcept{
 auto*s=get(r);return s&&s->L==L&&s->phase==State::Finished&&roots(s)
  &&((k==QStringLiteral("ZunweiCard")&&s->zunwei==State::Literal)||(k==QStringLiteral("Jink")&&s->jink==State::Absence)
   ||(k==QStringLiteral("ExNihilo")&&s->exnihilo==State::Literal));
}
QJsonObject readAIRound56InitialPriority(Room&r,lua_State*L,const QString&k){
 const auto no=[](){return QJsonObject{{"known",false}};};
 if(!hasAIRound56InitialPriority(r,L,k)||!AIRound45Inspect::capacity(L,8))return no();Stack z(L);
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"ai_use_priority");
 const auto bytes=k.toUtf8();AIRound45Inspect::field(L,-1,bytes.constData());
 QJsonObject result{{"known",true},{"present",!lua_isnil(L,-1)}};
 if(lua_isnil(L,-1)){}
 else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(double(lua_tonumber(L,-1))))result.insert("value",double(lua_tonumber(L,-1)));
 else return no();
 if(!hasAIRound56InitialPriority(r,L,k))return no();
 // Diagnostic at the successful original-source consumer, after both source
 // checks. No observer, authorization, state write or alternative read route.
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_PRIORITY_INITIAL_READ key=%s present=%d value=%.17g\n",
   k.toUtf8().constData(),int(result.value("present").toBool()),result.value("value").toDouble());
  std::fflush(stderr);
 }
 return result;
}

bool hasAIRound56InitialConnectAbsence(Room&r,lua_State*L)noexcept{
 auto*s=get(r);return s&&s->L==L&&s->phase==State::Finished&&s->connectAbsence&&live(s);
}

QJsonObject inspectAIRound56PriorityBootstrap(Room&r){
 auto*s=get(r);if(!s)return{{"phase","absent"}};
 static const char*phases[]={"started","finished","retired"};
 static const char*cells[]={"awaiting","absence","literal","lost"};
 auto*rt=r.roomRuntime();
 return{{"phase",phases[int(s->phase)]},{"failureTag",s->failureTag},
  {"event",s->failureEvent},{"instruction",s->failurePC},{"first",s->failureFirst},{"last",s->failureLast},
  {"epoch_matches",rt&&rt->prioritySourceEpoch()==s->epoch},
  {"root_seen",s->priority!=nullptr},{"zunwei_origin",cells[int(s->zunwei)]},{"jink_origin",cells[int(s->jink)]},
  {"exnihilo_origin",cells[int(s->exnihilo)]}};
}
