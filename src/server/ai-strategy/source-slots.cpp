#include "ai-strategy/source-slots.h"

// ---- use-priority-priority-2 ----
#include "ai-strategy/runtime.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include <QPointer>
#include <QHash>
#include <QSet>
#include <cmath>
#include <cstring>
#include <limits>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Grant{QPointer<ServerPlayer>owner;quint64 decision=0,epoch=0,transport=0,serial=0;QWeakPointer<AIConversionLedger>ledger;};
class State;void noteWrite(State*,const lua_TableObservation&)noexcept;void observer(lua_State*,const lua_TableObservation*,void*)noexcept;
class State final:public QObject{public:explicit State(Room&r):QObject(&r),room(r){}
 ~State()override{for(auto p:{globals,space,table})if(p)lua_unobservetableidentity(L,p,observer,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,serial=1;bool valid=false;
 const void*globals=nullptr,*space=nullptr,*table=nullptr;QHash<QString,Grant>grants;QSet<QString>touched;
};
State*state(Room&r,bool make=false){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return make?new State(r):nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
void observer(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{auto*s=static_cast<State*>(u);if(!L||!s->L||G(L)!=G(s->L))return;const auto&e=*ep;
 if(e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE
  ||(e.event==LUA_TABLE_OBSERVER_WRITE&&((e.table_identity==s->globals&&key(e,"sgs"))||(e.table_identity==s->space&&key(e,"ai_use_priority"))))){
  s->valid=false;if(e.event==LUA_TABLE_OBSERVER_FREE){if(e.table_identity==s->globals)s->globals=nullptr;if(e.table_identity==s->space)s->space=nullptr;if(e.table_identity==s->table)s->table=nullptr;}return;
 }
 if(e.event==LUA_TABLE_OBSERVER_WRITE&&e.table_identity==s->table){
  // Serial-only in the noexcept observer: no allocation or hidden value read.
  if(s->serial==std::numeric_limits<quint64>::max())s->valid=false;else ++s->serial;
  noteWrite(s,e);
 }
}
bool current(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&rt->lua().hasProtectedEntryObserver()
 &&p&&p->getRoom()==&r&&q.decisionId&&q.conversionLedger&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&Sanguosha&&Sanguosha->currentRoom()==&r
 &&aiRangeDecisionActive(r,q,p);}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&rt->lua().rawState()==s->L
 &&rt->lua().generation()==s->generation&&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running;}
bool roots(State*s){if(!live(s))return false;Stack z(s->L);AILuaInspect::globals(s->L);if(lua_topointer(s->L,-1)!=s->globals)return false;
 AILuaInspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;AILuaInspect::field(s->L,-1,"ai_use_priority");auto*t=AILuaInspect::table(s->L,-1);return t&&!t->metatable&&lua_topointer(s->L,-1)==s->table;}
State*capture(Room&r,lua_State*L){if(auto*s=state(r))return roots(s)?s:nullptr;auto*rt=r.roomRuntime();if(!rt||rt->lua().rawState()!=L)return nullptr;
 Stack z(L);AILuaInspect::globals(L);const void*g=lua_topointer(L,-1);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return nullptr;const void*ns=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"ai_use_priority");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return nullptr;const void*table=lua_topointer(L,-1);
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();
 if(lua_observetableidentity(L,g,observer,s))s->globals=g;if(lua_observetableidentity(L,ns,observer,s))s->space=ns;if(lua_observetableidentity(L,table,observer,s))s->table=table;
 s->valid=s->globals&&s->space&&s->table;return roots(s)?s:nullptr;
}
bool qualified(State*s,const Grant&g,ServerPlayer*p,const AIRequest&q){auto*rt=s->room.roomRuntime();return g.owner==p&&g.decision==q.decisionId&&g.ledger.toStrongRef()==q.conversionLedger
 &&g.epoch&&g.epoch==rt->prioritySourceEpoch()&&g.transport&&g.transport==rt->xufaTransportEpoch()&&g.serial==s->serial;}
thread_local AIUsePriorityOperation::Data*active=nullptr;
}
struct AIUsePriorityOperation::Data{Room*r;ServerPlayer*p;const AIRequest*q;lua_State*L;AIUsePriorityCFunction fn;void*payload;Mode mode;QString target,source;
 QPointer<State>s;QByteArray targetBytes;Data*previous=nullptr;int depth=0;bool entered=false,invalid=false,armed=false,basis=false,exactWrite=false;quint64 epoch=0,transport=0,serial=0;
};
namespace {
void noteWrite(State*s,const lua_TableObservation&e)noexcept{
 auto*x=active;if(!x||x->s!=s||!x->armed)return;
 if(x->exactWrite||!e.string_key||e.string_key_length!=size_t(x->targetBytes.size())
  ||std::memcmp(e.string_key,x->targetBytes.constData(),e.string_key_length))x->invalid=true;
 else x->exactWrite=true;
}
}
AIUsePriorityOperation::AIUsePriorityOperation(Room&r,ServerPlayer*p,const AIRequest&q,lua_State*L,AIUsePriorityCFunction fn,void*payload,Mode mode,const QString&target,const QString&source):d(new Data{&r,p,&q,L,fn,payload,mode,target,source}){
 d->targetBytes=target.toUtf8();d->previous=active;d->depth=r.roomRuntime()->lua().protectedCallDepth();active=d.get();
 if(!current(r,p,q)||!L||!lua_checkstack(L,512)){d->invalid=true;return;}d->s=capture(r,L);if(!d->s)d->invalid=true;
}
AIUsePriorityOperation::~AIUsePriorityOperation(){active=d->previous;}
void observeAIUsePriorityEntry(Room&r,lua_State*L,int n)noexcept{
 auto*rt=r.roomRuntime();if(!rt)return;auto*d=active;
 // Only the exact native C entry, payload, first entry and protected depth can
 // preserve. A nested/finalizer/debug entry permanently invalidates this scope.
 if(d&&d->r==&r&&!d->invalid&&!d->entered&&n==2&&L==d->L&&rt->lua().protectedCallDepth()==d->depth&&lua_gettop(L)>=3
  &&lua_iscfunction(L,-3)&&lua_tocfunction(L,-3)==d->fn&&lua_type(L,-1)==LUA_TLIGHTUSERDATA&&lua_touserdata(L,-1)==d->payload){d->entered=true;return;}
 rt->revokePrioritySource();for(auto*p=d;p;p=p->previous)if(p->r==&r)p->invalid=true;
}
void AIUsePriorityOperation::beforeWrite(){auto*x=d.get();if(x->invalid||!x->entered||!x->s||!current(*x->r,x->p,*x->q)||!roots(x->s))return;
 auto*rt=x->r->roomRuntime();x->epoch=rt->prioritySourceEpoch();x->transport=rt->xufaTransportEpoch();x->serial=x->s->serial;
 x->basis=x->mode==LiteralTen||(x->mode!=Preserve&&qualified(x->s,x->s->grants.value(x->mode==CopyCell?x->source:x->target),x->p,*x->q));
 x->s->touched.insert(x->target);x->armed=true;
}
void AIUsePriorityOperation::afterWrite(){auto*x=d.get();if(!x->armed||!x->exactWrite||x->invalid||!x->s||!current(*x->r,x->p,*x->q)||!roots(x->s))return;
 auto*rt=x->r->roomRuntime();if(!x->epoch||x->epoch!=rt->prioritySourceEpoch()||!x->transport||x->transport!=rt->xufaTransportEpoch()||x->serial==quint64(-1)||x->s->serial!=x->serial+1)return;
 // Only still-qualified OTHER cells survive this proved single exact-target
 // plain write. Older serials/revoked epochs are never revived or restamped.
 for(auto i=x->s->grants.begin();i!=x->s->grants.end();++i){auto&g=i.value();
  if(i.key()!=x->target&&g.owner==x->p&&g.decision==x->q->decisionId&&g.ledger.toStrongRef()==x->q->conversionLedger
   &&g.epoch==x->epoch&&g.transport==x->transport&&g.serial==x->serial)g.serial=x->s->serial;
 }
 if(x->basis)x->s->grants[x->target]={x->p,x->q->decisionId,x->epoch,x->transport,x->s->serial,x->q->conversionLedger};x->armed=false;
}
void clearAIUsePriority(Room&r){delete state(r);}
QJsonObject readAIUsePriority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=2||v.value("op")!="source_priority_read"||!v.value("key").isString()||!current(r,p,q))return fail();
 auto*s=state(r);const QString k=v.value("key").toString();
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();if(!L||!lua_checkstack(L,64))return fail();Stack z(L);
 const Grant grant=s?s->grants.value(k):Grant{};
 if(!s||!s->touched.contains(k)||!roots(s)||!qualified(s,grant,p,q)){
  auto initial=readAIInitialPriority(r,L,k);
  return current(r,p,q)?initial:fail();
 }
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"ai_use_priority");const auto bytes=k.toUtf8();AILuaInspect::field(L,-1,bytes.constData());
 QJsonObject result{{"known",true},{"present",!lua_isnil(L,-1)}};
 if(lua_isnil(L,-1)){}else if(lua_type(L,-1)==LUA_TBOOLEAN)result.insert("value",bool(lua_toboolean(L,-1)));
 else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(double(lua_tonumber(L,-1))))result.insert("value",double(lua_tonumber(L,-1)));
 else return fail();
 if(!current(r,p,q)||!roots(s)||!qualified(s,grant,p,q))return fail();return result;
}

// ---- use-priority-priority-bootstrap ----
#include <QObject>
#include <new>
#include <cstdio>
namespace {
struct UsePriorityPriorityBootstrapState;void observed(lua_State*,const lua_TableObservation*,void*)noexcept;
struct UsePriorityPriorityBootstrapState final:QObject{
 explicit UsePriorityPriorityBootstrapState(Room&r):QObject(&r),room(r){}
 ~UsePriorityPriorityBootstrapState()override{for(const void*p:{globals,space,priority})if(p)lua_unobservetableidentity(L,p,observed,this);}
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
UsePriorityPriorityBootstrapState*get(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<UsePriorityPriorityBootstrapState*>(c))return s;return nullptr;}
bool usePriorityPriorityBootstrapKey(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool usePriorityPriorityBootstrapLive(UsePriorityPriorityBootstrapState*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->phase!=UsePriorityPriorityBootstrapState::Retired
 &&s->L&&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation&&rt->lua().isCurrentThreadOwner()
 &&s->epoch&&rt->prioritySourceEpoch()==s->epoch;}
void observed(lua_State*L,const lua_TableObservation*ep,void*data)noexcept{
 auto*s=static_cast<UsePriorityPriorityBootstrapState*>(data);if(!s||!ep||!L||!s->L||G(L)!=G(s->L))return;const auto&e=*ep;
 // FREE is handled even after retirement, so destruction never dereferences a
 // table address after GC and allocation cannot resurrect an old identity.
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  if(e.table_identity==s->globals)s->globals=nullptr;
  if(e.table_identity==s->space)s->space=nullptr;
  if(e.table_identity==s->priority)s->priority=nullptr;
  s->retire("observed-free",ep);return;
 }
 try {
 if(!usePriorityPriorityBootstrapLive(s)){s->retire("continuity-before-observation",ep);return;}
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s->retire("root-metatable-write",ep);return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if(e.table_identity==s->globals&&usePriorityPriorityBootstrapKey(e,"sgs")){s->retire("global-sgs-write",ep);return;}
 if(e.table_identity==s->space&&usePriorityPriorityBootstrapKey(e,"ai_connect_cache"))s->connectAbsence=false;
 if(e.table_identity==s->space&&usePriorityPriorityBootstrapKey(e,"ai_use_priority")){
  if(s->phase!=UsePriorityPriorityBootstrapState::Started||s->priority
    ||!matchAIBootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/smart-ai.lua"),74,"ai_use_priority",true)){
   s->retire("priority-root-source",ep);return;
  }
  const void*created=e.value_table_identity;
  if(!lua_observetableidentity(L,created,observed,s)){s->retire("priority-root-watch",ep);return;}
  s->priority=created;s->jink=UsePriorityPriorityBootstrapState::Absence;s->zunwei=UsePriorityPriorityBootstrapState::Absence;s->exnihilo=UsePriorityPriorityBootstrapState::Absence;return;
 }
 if(e.table_identity!=s->priority)return;
 if(usePriorityPriorityBootstrapKey(e,"Jink")){s->jink=UsePriorityPriorityBootstrapState::Lost;return;} // includes same-value nil write
 if(usePriorityPriorityBootstrapKey(e,"ExNihilo")){
  if(s->phase==UsePriorityPriorityBootstrapState::Started&&s->exnihilo==UsePriorityPriorityBootstrapState::Absence&&e.value_type==LUA_TNUMBER
   &&matchAIBootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/standard_cards-ai.lua"),1873,"ExNihilo",false))
   s->exnihilo=UsePriorityPriorityBootstrapState::Literal;
  else s->exnihilo=UsePriorityPriorityBootstrapState::Lost;
 }
 if(usePriorityPriorityBootstrapKey(e,"ZunweiCard")){
  if(s->phase==UsePriorityPriorityBootstrapState::Started&&s->zunwei==UsePriorityPriorityBootstrapState::Absence&&e.value_type==LUA_TNUMBER
   &&matchAIBootstrapWrite(s->room,L,e,QStringLiteral("lua/ai/tenyear_hc-ai.lua"),5238,"ZunweiCard",false))
   s->zunwei=UsePriorityPriorityBootstrapState::Literal;
  else s->zunwei=UsePriorityPriorityBootstrapState::Lost;
 }
 // Other cells cannot affect these actual source origins inside the caller's
 // complete proved graph. Unknown graphs retire the whole witness BEFORE entry.
 }catch(...){s->retire("observer-native-exception",ep);}
}
struct UsePriorityPriorityBootstrapStack{lua_State*L;int n;explicit UsePriorityPriorityBootstrapStack(lua_State*s):L(s),n(lua_gettop(s)){}~UsePriorityPriorityBootstrapStack(){lua_settop(L,n);}};
bool usePriorityPriorityBootstrapRoots(UsePriorityPriorityBootstrapState*s){
 if(!usePriorityPriorityBootstrapLive(s)||!s->globals||!s->space||!s->priority||!AILuaInspect::capacity(s->L,8))return false;
 UsePriorityPriorityBootstrapStack z(s->L);AILuaInspect::globals(s->L);auto*t=AILuaInspect::table(s->L,-1);
 if(!t||t->metatable||t!=s->globals)return false;
 AILuaInspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;
 AILuaInspect::field(s->L,-1,"ai_use_priority");t=AILuaInspect::table(s->L,-1);
 return t&&!t->metatable&&t==s->priority;
}
}
bool beginAIPriorityBootstrap(Room&r,const LuaRuntime::NativeNamespaceBirth&birth){
 if(get(r))return false; // even a failed/retired initial attempt is one-shot
 auto*s=new UsePriorityPriorityBootstrapState(r);auto*rt=r.roomRuntime();s->L=birth.state();
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
bool completeAIPriorityBootstrap(Room&r,lua_State*L,const AIBootstrapSourceProof&proof)noexcept{
 auto*s=get(r);if(!s)return false;
 const auto fail=[&](const char*why){s->retire(why);return false;};
 if(s->L!=L)return fail("completion-state");
 if(s->phase!=UsePriorityPriorityBootstrapState::Started)return fail("completion-phase");
 if(proof.m_room!=&r||proof.m_roomLifetime.data()!=&r||proof.m_lua!=L||proof.m_consumed||!proof.m_serial
  ||proof.m_generation!=s->generation||!proof.m_current||!proof.m_current(r,L,proof.m_serial))return fail("completion-source-token");
 if(!usePriorityPriorityBootstrapRoots(s))return fail("completion-root-continuity");
 if(s->zunwei!=UsePriorityPriorityBootstrapState::Literal)return fail("completion-zunwei-origin");
 if(s->jink!=UsePriorityPriorityBootstrapState::Absence)return fail("completion-jink-origin");
 if(s->exnihilo!=UsePriorityPriorityBootstrapState::Literal)return fail("completion-exnihilo-origin");
 // Crucially, the original epoch is not changed/restamped here. Any earlier
 // native, callback, async or unknown-source interruption remains permanent.
 s->phase=UsePriorityPriorityBootstrapState::Finished;return true;
}
void retireAIPriorityBootstrap(Room&r)noexcept{if(auto*s=get(r))s->retire("external-source-retirement");}
void clearAIPriorityBootstrap(Room&r)noexcept{delete get(r);}
bool hasAIInitialPriority(Room&r,lua_State*L,const QString&k)noexcept{
 auto*s=get(r);return s&&s->L==L&&s->phase==UsePriorityPriorityBootstrapState::Finished&&usePriorityPriorityBootstrapRoots(s)
  &&((k==QStringLiteral("ZunweiCard")&&s->zunwei==UsePriorityPriorityBootstrapState::Literal)||(k==QStringLiteral("Jink")&&s->jink==UsePriorityPriorityBootstrapState::Absence)
   ||(k==QStringLiteral("ExNihilo")&&s->exnihilo==UsePriorityPriorityBootstrapState::Literal));
}
QJsonObject readAIInitialPriority(Room&r,lua_State*L,const QString&k){
 const auto no=[](){return QJsonObject{{"known",false}};};
 if(!hasAIInitialPriority(r,L,k)||!AILuaInspect::capacity(L,8))return no();UsePriorityPriorityBootstrapStack z(L);
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"ai_use_priority");
 const auto bytes=k.toUtf8();AILuaInspect::field(L,-1,bytes.constData());
 QJsonObject result{{"known",true},{"present",!lua_isnil(L,-1)}};
 if(lua_isnil(L,-1)){}
 else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(double(lua_tonumber(L,-1))))result.insert("value",double(lua_tonumber(L,-1)));
 else return no();
 if(!hasAIInitialPriority(r,L,k))return no();
 // Diagnostic at the successful original-source consumer, after both source
 // checks. No observer, authorization, state write or alternative read route.
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_PRIORITY_INITIAL_READ key=%s present=%d value=%.17g\n",
   k.toUtf8().constData(),int(result.value("present").toBool()),result.value("value").toDouble());
  std::fflush(stderr);
 }
 return result;
}

bool hasAIInitialConnectAbsence(Room&r,lua_State*L)noexcept{
 auto*s=get(r);return s&&s->L==L&&s->phase==UsePriorityPriorityBootstrapState::Finished&&s->connectAbsence&&usePriorityPriorityBootstrapLive(s);
}

QJsonObject inspectAIPriorityBootstrap(Room&r){
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

// ---- use-priority-priority ----
#include "ai-strategy/owner.h"
#include "card-lifetime-manager.h"
#include "ai-strategy/lua-seal.h"
#include "ai-runtime.h"
#include "card.h"
#include <QJsonArray>
namespace {
struct UsePriorityPriorityStack{lua_State*L;int n;explicit UsePriorityPriorityStack(lua_State*s):L(s),n(lua_gettop(s)){}~UsePriorityPriorityStack(){lua_settop(L,n);}};
bool usePriorityPriorityCurrent(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&q.decisionId&&q.kind==AIRequest::Activate
 &&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
QString sourceClass(Room&r,ServerPlayer*p,const AIRequest&q,const QString&target,const QJsonObject&s){
 if(s.size()!=1)return{};
 if(s.value("readonly_recipe_key").isString()){
  const Card*c=r.roomRuntime()->ai().readonlyCard(q,p,s.value("readonly_recipe_key").toString());
  if(!c)return{};
  if(target=="genshunoYURICard"){
   if(r.roomRuntime()->ai().readonlyCardSource(q,p,s.value("readonly_recipe_key").toString())!="#genshunoYURICard"
    ||c->getSkillName(false)!="genshunoYURI"||c->getSuit()!=Card::NoSuit||c->getNumber()!=0
    ||!c->getSubcards().isEmpty()||(!c->isKindOf("BasicCard")&&!c->isNDTrick()))return{};
   return c->getClassName();
  }
  if(!c->getSkillName(false).isEmpty())return{};
  if(target=="YoulongCard"&&(!c->getSubcards().isEmpty()||c->getSuit()!=Card::NoSuit||c->getNumber()!=0))return{};
  if(target=="SecondJinzhiCard"&&(!c->isKindOf("BasicCard")
   ||r.roomRuntime()->ai().readonlyCardSource(q,p,s.value("readonly_recipe_key").toString())!="secondjinzhi"))return{};
  return c->getClassName();
 }
 if(target!="YoulongCard"||!s.value("printed_name").isString())return{};
 const QString name=s.value("printed_name").toString();const auto row=q.worldView.evaluationCards.value(name).toObject();
 // Only this already-issued zero-material public clone descriptor. Catalogue
 // class names alone do not attest a mutable source Card or its constructor.
 if(row.isEmpty()||row.value("evaluation_only")!=true||row.value("virtual_card")!=true||row.value("name")!=name
  ||!row.value("class_name").isString()||!row.value("subcards").isArray()||!row.value("subcards").toArray().isEmpty()
  ||row.value("skill_name").toString()!=""||row.value("suit").toInt(-1)!=int(Card::NoSuit)||row.value("number").toInt(-1)!=0)return{};
 return row.value("class_name").toString();
}
struct Copy{Room*r;ServerPlayer*p;const AIRequest*q;lua_State*L;quint64 generation;QByteArray src,dst;const void*g=nullptr,*sgs=nullptr,*table=nullptr;bool reached=false;AIUsePriorityOperation*priority=nullptr;};
bool usePriorityPriorityLive(const Copy&w){auto*rt=w.r->roomRuntime();return usePriorityPriorityCurrent(*w.r,w.p,*w.q)&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation;}
bool usePriorityPriorityRoots(lua_State*L,const Copy&w){UsePriorityPriorityStack st(L);AILuaInspect::globals(L);if(lua_topointer(L,-1)!=w.g)return false;AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=w.sgs)return false;AILuaInspect::field(L,-1,"ai_use_priority");auto*t=AILuaInspect::table(L,-1);return t&&!t->metatable&&lua_topointer(L,-1)==w.table;}
int assign(lua_State*L){if(!lua_checkstack(L,256))return luaL_error(L,"source priority stack unavailable");auto*w=static_cast<Copy*>(lua_touserdata(L,2));
 if(!w||!usePriorityPriorityLive(*w)||!usePriorityPriorityRoots(L,*w)||lua_topointer(L,1)!=w->table)return luaL_error(L,"source priority context changed");
 lua_pushlstring(L,w->dst.constData(),size_t(w->dst.size()));lua_pushlstring(L,w->src.constData(),size_t(w->src.size()));
 if(!usePriorityPriorityLive(*w)||!usePriorityPriorityRoots(L,*w))return luaL_error(L,"source priority changed during allocation");
 // This is the actual RHS read, not a native metadata peek or exported value.
 // Plain table proof preserves nil without executing unknown __index.
 if(w->priority)w->priority->beforeWrite();
 lua_gettable(L,1);
 if(!usePriorityPriorityLive(*w)||!usePriorityPriorityRoots(L,*w))return luaL_error(L,"source priority changed during read");
 lua_settable(L,1);w->reached=true;if(w->priority)w->priority->afterWrite();return 0;
}
}
QJsonObject copyAISourceUsePriority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=4||v.value("op")!="source_priority"||!v.value("key").isString()||!v.value("source_class").isString()||!v.value("card").isObject()||!usePriorityPriorityCurrent(r,p,q))return fail();
 const QString target=v.value("key").toString();const QString skill=target=="YoulongCard"?"youlong":target=="SecondJinzhiCard"?"secondjinzhi":target=="genshunoYURICard"?"genshunoYURI":QString();
 if(skill.isEmpty()||!p->hasSkill(skill)||!usePriorityPriorityCurrent(r,p,q))return fail();
 const QString cl=sourceClass(r,p,q,target,v.value("card").toObject());if(cl.isEmpty()||cl!=v.value("source_class").toString()||!usePriorityPriorityCurrent(r,p,q))return fail();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail();UsePriorityPriorityStack st(L);
 Copy w{&r,p,&q,L,rt->lua().generation(),cl.toUtf8(),target.toUtf8()};AILuaInspect::globals(L);w.g=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.sgs=lua_topointer(L,-1);AILuaInspect::field(L,-1,"ai_use_priority");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return fail();w.table=lua_topointer(L,-1);
 AIUsePriorityOperation priority(r,p,q,L,assign,&w,AIUsePriorityOperation::CopyCell,target,cl);w.priority=&priority;
 lua_pushcfunction(L,assign);lua_pushvalue(L,-2);lua_pushlightuserdata(L,&w);int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK||!w.reached||!usePriorityPriorityLive(w)||!usePriorityPriorityRoots(L,w))return fail();
 return{{"known",true},{"written",true}};
}

// R53: two fixed arithmetic writer suffixes. No RHS/decision value is exported.
namespace {
QString arithmeticClass(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&s){
 if(s.size()==1&&s.value("readonly_recipe_key").isString()){
  const Card*c=r.roomRuntime()->ai().readonlyCard(q,p,s.value("readonly_recipe_key").toString());return c?c->getClassName():QString();
 }
 if(s.size()==1&&s.value("printed_name").isString())return sourceClass(r,p,q,"YoulongCard",s);
 if(s.size()!=2||!s.value("physical_id").isDouble()||!s.value("legacy_identity_token").isString())return{};
 const int id=s.value("physical_id").toInt(-1);if(id<0||s.value("physical_id").toDouble()!=id||!p->handCards().contains(id))return{};
 const Card*c=r.getCard(id);if(!c||c->isVirtualCard())return{};
 for(const auto&v:q.worldView.handCards)if(v.effectiveId==id&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==s.value("legacy_identity_token").toString()&&v.legacyIdentityToken==aiLegacyCardIdentity(r,p,c)
  &&v.nativeIdentity&&globalCardLifetimeManager().isLive(v.nativeIdentity)&&v.nativeIdentity->address==c
  &&v.cardId==c->getId()&&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)
  &&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard())return c->getClassName();
 return{};
}
struct Add{Copy base;int amount=0;bool conditional=false,ordinary=false,unsupported=false;};
int addPriority(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"source arithmetic priority stack unavailable");
 auto*w=static_cast<Add*>(lua_touserdata(L,2));if(!w||!usePriorityPriorityLive(w->base)||!usePriorityPriorityRoots(L,w->base)||lua_topointer(L,1)!=w->base.table)
  return luaL_error(L,"source arithmetic priority context changed");
 lua_pushlstring(L,w->base.dst.constData(),size_t(w->base.dst.size()));lua_pushvalue(L,-1);
 if(w->base.priority)w->base.priority->beforeWrite();lua_gettable(L,1);
 if(!usePriorityPriorityLive(w->base)||!usePriorityPriorityRoots(L,w->base))return luaL_error(L,"source arithmetic priority changed during lookup");
 if(w->conditional&&!lua_toboolean(L,-1)){w->base.reached=true;return 0;}
 const int type=lua_type(L,-1);
 // Do not invoke a private replacement object's arithmetic metamethod. Fixed
 // scalar number/string coercion and nil/boolean ordinary errors are retained.
 if(type!=LUA_TNUMBER&&type!=LUA_TSTRING&&type!=LUA_TNIL&&type!=LUA_TBOOLEAN){w->unsupported=true;return 0;}
 if(AILuaInspect::primitiveMetatable(L,type)){
  // Numeric strings use Lua arithmetic coercion before any metamethod. Other
  // primitive metatable arithmetic would require a separate exact effect proof.
  lua_pop(L,1);int numeric=0;lua_tonumberx(L,-1,&numeric);
  if(!numeric&&(type!=LUA_TSTRING||!matchAINativeStringMetatable(*w->base.r,L))){w->unsupported=true;return 0;}
 }
 lua_pushinteger(L,w->amount);w->ordinary=true;lua_arith(L,LUA_OPADD);w->ordinary=false;
 if(!usePriorityPriorityLive(w->base)||!usePriorityPriorityRoots(L,w->base))return luaL_error(L,"source arithmetic priority changed during addition");
 lua_settable(L,1);w->base.reached=true;if(w->base.priority)w->base.priority->afterWrite();return 0;
}
}
QJsonObject incrementAISourceUsePriority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=4||v.value("op")!="source_priority_increment"||!v.value("writer").isString()
  ||!v.value("source_class").isString()||!v.value("card").isObject()||!usePriorityPriorityCurrent(r,p,q))return fail();
 const QString writer=v.value("writer").toString();if(writer!="sijyu_qizhan"&&writer!="TenyearJianying")return fail();
 // The source TenyearJianying callback is NOT the native skill name
 // tenyearjianying. Never invent an alias or require that unrelated spelling.
 if(writer=="sijyu_qizhan"){
  bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName==writer){owned=true;break;}
  if(!owned)return fail();
 }
 if(!usePriorityPriorityCurrent(r,p,q))return fail();
 const auto cl=arithmeticClass(r,p,q,v.value("card").toObject());if(cl.isEmpty()||cl!=v.value("source_class").toString()||!usePriorityPriorityCurrent(r,p,q))return fail();
 if(writer=="TenyearJianying"){
  const auto selector=v.value("card").toObject();const Card*c=nullptr;
  if(selector.contains("readonly_recipe_key"))c=r.roomRuntime()->ai().readonlyCard(q,p,selector.value("readonly_recipe_key").toString());
  else if(selector.contains("physical_id"))c=r.getCard(selector.value("physical_id").toInt(-1));
  // arithmeticClass already validated a printed selector as exact NoSuit/0.
  const QString suit=c?c->getSuitString():Card::Suit2String(Card::NoSuit);
  const QString number=c?c->getNumberString():QStringLiteral("0");
  bool reached=false;for(const auto&m:p->getMarkNames()){
   const auto fields=m.split('+');if(fields.contains("&TenyearJianying")
    &&(fields.contains(suit+"_char")||fields.contains(number))){reached=true;break;}
  }
  if(!reached||!usePriorityPriorityCurrent(r,p,q))return fail();
 }
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail();UsePriorityPriorityStack st(L);
 Add w;w.base={&r,p,&q,L,rt->lua().generation(),cl.toUtf8(),cl.toUtf8()};w.conditional=writer=="sijyu_qizhan";w.amount=w.conditional?1:5;
 AILuaInspect::globals(L);w.base.g=lua_topointer(L,-1);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.base.sgs=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"ai_use_priority");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return fail();w.base.table=lua_topointer(L,-1);
 AIUsePriorityOperation priority(r,p,q,L,addPriority,&w,AIUsePriorityOperation::Arithmetic,cl);w.base.priority=&priority;
 lua_pushcfunction(L,addPriority);lua_pushvalue(L,-2);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK){if(w.ordinary&&lua_type(L,-1)==LUA_TSTRING)return{{"known",true},{"source_error",QString::fromUtf8(lua_tostring(L,-1))}};return fail();}
 if(w.unsupported||!w.base.reached||!usePriorityPriorityLive(w.base)||!usePriorityPriorityRoots(L,w.base))return fail();
 return{{"known",true},{"completed",true}}; // No conditional outcome/RHS export.
}

// ---- source-immediate-effects ----
#include <QWeakPointer>
namespace {
struct Sequence final:QObject {
 explicit Sequence(Room&r):QObject(&r){}
 const AIRequest*request=nullptr;quint64 decision=0;QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;
 bool written=false;QMap<QString,bool>flagReads;
};
Sequence*sequence(Room&r,const AIRequest&q,ServerPlayer*p){
 Sequence*s=nullptr;for(auto*c:r.children())if((s=dynamic_cast<Sequence*>(c)))break;
 if(!s)s=new Sequence(r);
 if(s->request!=&q||s->decision!=q.decisionId||s->owner!=p||s->ledger.toStrongRef()!=q.conversionLedger){
  s->request=&q;s->decision=q.decisionId;s->owner=p;s->ledger=q.conversionLedger;s->written=false;s->flagReads.clear();
 }return s;
}
QJsonObject sourceImmediateEffectsFail(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool sourceImmediateEffectsCurrent(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&p->objectName()==q.viewerObjectName&&q.worldView.self.objectName==q.viewerObjectName
  &&q.conversionLedger&&q.decisionId&&aiRangeRequestCurrent(r,q,p);
}
bool own(ServerPlayer*p,const QString&skill){for(const auto&i:p->getSkillInstances())if(i.skillName==skill)return true;return false;}
bool activate(const AIRequest&q){return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;}
const Card*material(Room&r,const AIRequest&q,ServerPlayer*p,int id,bool handOnly){
 if(id<0||r.getCardOwner(id)!=p)return nullptr;
 const auto place=r.getCardPlace(id);if(place!=Player::PlaceHand&&(handOnly||place!=Player::PlaceEquip))return nullptr;
 const auto check=[&](const QList<AICardView>&rows)->const Card*{for(const auto&v:rows)if(v.effectiveId==id){
  const auto*c=r.getCard(id);if(c&&c->getEffectiveId()==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
    &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
    &&v.legacyIdentityToken==aiLegacyCardIdentity(r,p,c))return c;
 }return nullptr;};
 return place==Player::PlaceHand?check(q.worldView.handCards):check(q.worldView.self.equips);
}
}
QJsonObject queryAISourceEffects(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&input){
 if(!sourceImmediateEffectsCurrent(r,q,p))return sourceImmediateEffectsFail("source effect requires exact current owner question");
 const QSet<QString>fields{"source","op","key","target","subcards"};
 if(input.size()!=fields.size())return sourceImmediateEffectsFail("source effect schema differs");
 for(auto it=input.begin();it!=input.end();++it)if(!fields.contains(it.key()))return sourceImmediateEffectsFail("unexpected source effect field");
 for(const auto&k:{"source","op","key","target"})if(!input.value(k).isString())return sourceImmediateEffectsFail("source effect strings missing");
 if(!input.value("subcards").isArray())return sourceImmediateEffectsFail("source effect materials missing");
 const auto source=input.value("source").toString(),op=input.value("op").toString(),key=input.value("key").toString(),name=input.value("target").toString();
 const auto ids=input.value("subcards").toArray();
 const QMap<QString,Card::Suit>suits{{"AI_has_clubs",Card::Club},{"AI_has_spades",Card::Spade},{"AI_has_diamonds",Card::Diamond},{"AI_has_hearts",Card::Heart}};
 bool mark=false,value=false;ServerPlayer*target=p;
 if(source=="heapridge"){
  if(q.kind!=AIRequest::UseCard||q.pattern!="@@heapridge"||q.prompt!="@heapridge"
   ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||!suits.contains(key)||name!=p->objectName())return sourceImmediateEffectsFail("heapridge source question differs");
  mark=true;
  if(op=="read"){
   if(!ids.isEmpty())return sourceImmediateEffectsFail("heapridge mark read has materials");
   const int n=p->getMark(key);if(!sourceImmediateEffectsCurrent(r,q,p))return sourceImmediateEffectsFail("heapridge current changed during read");return{{"known",true},{"value",n}};
  }
  if(op!="write"||ids.size()!=4)return sourceImmediateEffectsFail("heapridge reached source add requires four costs");
  QSet<int>seen;for(const auto&v:ids){const int id=v.toInt(-1);if(!v.isDouble()||v.toDouble()!=id||seen.contains(id))return sourceImmediateEffectsFail("heapridge costs malformed");seen.insert(id);
   const Card*c=material(r,q,p,id,true);if(!c||c->getSuit()!=suits.value(key))return sourceImmediateEffectsFail("heapridge source costs not current owner suit");}
 }else if(source=="anzhi"){
  if(key!="ai_anzhi-PlayClear"||name!=p->objectName()||!ids.isEmpty())return sourceImmediateEffectsFail("anzhi mark schema differs");
  mark=true;
  if(op=="read"&&activate(q)&&own(p,"anzhi")){
   const int n=p->getMark(key);if(!sourceImmediateEffectsCurrent(r,q,p))return sourceImmediateEffectsFail("anzhi read changed current");return{{"known",true},{"value",n}};
  }
  if(op!="write"||q.kind!=AIRequest::PlayerChosen||q.choiceOptions.reason!="anzhi")return sourceImmediateEffectsFail("anzhi reached source add requires actual player choice");
 }else if(source=="maji"){
  if(q.kind!=AIRequest::RespondCard||q.choiceOptions.question!="askForCard"||q.pattern!=".."||q.prompt.section(':',0,0)!="@goddess-put"
   ||q.handlingMethod!=Card::MethodNone||q.worldView.decisionContext.value("judge").toObject().value("reason")!=QJsonValue("miyadokyo")
   ||name!=p->objectName()||(key!="majiBad"&&key!="majiGood"&&key!="majiHeal"))return sourceImmediateEffectsFail("maji clear outside exact owner judge inquiry");
  if(op=="read"){
   if(!ids.isEmpty())return sourceImmediateEffectsFail("maji read has material");const bool yes=p->getFlagList().contains(key);
   if(!sourceImmediateEffectsCurrent(r,q,p))return sourceImmediateEffectsFail("maji read changed current");sequence(r,q,p)->flagReads.insert(key,yes);return{{"known",true},{"value",yes}};
  }
  if(op!="write"||ids.size()!=1||!sequence(r,q,p)->flagReads.value(key,false))return sourceImmediateEffectsFail("maji clear has no reached positive flag read");
  const int id=ids.first().toInt(-1);if(!ids.first().isDouble()||ids.first().toDouble()!=id)return sourceImmediateEffectsFail("maji material malformed");
  const Card*c=material(r,q,p,id,false);if(!c||(key=="majiBad"?!c->isRed():!c->isBlack())
   ||(key=="majiHeal"&&p->getLostHp()<=1))return sourceImmediateEffectsFail("maji source color or heal condition differs");
 }else if(source=="turned"){
  if(!activate(q)||!own(p,"turned")||op!="write"||key!="turned_target"||!ids.isEmpty())return sourceImmediateEffectsFail("turned source write outside current Play");
  target=r.findPlayerByObjectName(name);value=true;
  if(!target||!target->isAlive()||!q.worldView.playerOrder.contains(name))return sourceImmediateEffectsFail("turned target is not live public roster");
 }else return sourceImmediateEffectsFail("source effect is outside finite catalog");
 if(!sourceImmediateEffectsCurrent(r,q,p))return sourceImmediateEffectsFail("source effect current changed before write");
 auto*s=sequence(r,q,p);if(s->written)return sourceImmediateEffectsFail("source effect already reached; no duplicate or reselect");
 s->written=true; // Mark the reached prefix before native signals can reenter.
 AISourceEffectPermit permit(r,q,p,target,key,mark,value);QString error;
 if(!writeAISourceEffect(r,q,p,target,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}

// ---- current-source-slots ----
#include "ai-strategy/inovation-ai.h"
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool currentSourceSlotsCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool question(const AIRequest&q,AIRequest::DecisionKind k,const char*reason,const char*method){return q.kind==k&&q.choiceOptions.reason==reason&&q.choiceOptions.question==method;}
bool play(const AIRequest&q){
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty())return true;
 const auto c=q.worldView.strategyFacts.value("readonly_play_context").toObject();const auto source=c.value("source").toString();
 if(c.value("known")!=true||c.value("readonly")!=true||c.value("viewer")!=q.viewerObjectName||c.value("decision_id")!=q.getDecisionId()||c.value("state_revision")!=q.getStateRevision()||c.value("kind")!="activate"||c.value("reason")!=int(CardUseStruct::CARD_USE_REASON_PLAY)||c.value("pattern")!=""||c.value("handling_method")!=int(Card::MethodUse))return false;
 if(q.kind==AIRequest::SkillInvoke)return (source=="olClan.zuheta"&&(q.choiceOptions.reason=="zuheta"||q.choiceOptions.reason=="zuheta$-1"))||(source=="OverseasVersion.ov_xiafeng"&&q.choiceOptions.reason=="ov_xiafeng");
 if(q.kind==AIRequest::Choice)return source=="OverseasVersion.ov_shelie"&&q.choiceOptions.reason=="ov_shelie"&&q.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
 return source=="OverseasVersion.ov_niwo"&&q.kind==AIRequest::UseCard&&q.pattern=="@@ov_niwo"&&q.prompt=="ov_niwo0"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool use(const AIRequest&q,const char*pattern){return q.kind==AIRequest::UseCard&&q.pattern==pattern;}
bool scope(const AIRequest&q,const QString&k,bool write){
 if(k=="wannawin")return write&&question(q,AIRequest::SkillInvoke,"wannawin","askForSkillInvoke");
 if(k=="jiewq_lose")return play(q)||(!write&&question(q,AIRequest::Choice,"jiewq","askForChoice"));
 if(k=="s3_fans_handou_card")return write?use(q,"@@s3_fans_handou"):question(q,AIRequest::RespondCard,"s3_fans_handou","askForPindian");
 if(k=="s3_jxwj_ganglichoice")return write?question(q,AIRequest::SkillInvoke,"s3_jxwj_gangli","askForSkillInvoke"):question(q,AIRequest::Choice,"s3_jxwj_gangli","askForChoice");
 if(k=="guanxu_friends")return play(q)||question(q,AIRequest::PlayerChosen,"yashi","askForPlayerChosen")||(!write&&use(q,"@@guanxu1"));
 if(k=="isfriend"||k=="thwuyanDp"||k=="thwuyanHs")return play(q);
 if(k=="qianlong_use")return use(q,"@@qianlong")||(!write&&question(q,AIRequest::PlayerChosen,"juetao","askForPlayerChosen"));
 return false;
}
bool valid(const QString&k,const QJsonObject&v){
 if(k=="wannawin")return v.size()==1&&v.value("type")=="nil";
 const auto t=v.value("type").toString();if(t=="nil")return k=="isfriend"&&v.size()==1;
 if(v.size()!=2||!v.contains("value"))return false;
 if(k=="jiewq_lose"||k=="thwuyanDp"||k=="thwuyanHs"||k=="s3_fans_handou_card")return t=="number"&&v.value("value").isDouble()&&std::isfinite(v.value("value").toDouble());
 if(k=="s3_jxwj_ganglichoice")return t=="string"&&(v.value("value")=="s3_jxwj_gangli_throwtohp"||v.value("value")=="s3_jxwj_gangli_throwhp");
 return t=="boolean"&&v.value("value").isBool()&&(k!="isfriend"||v.value("value").toBool());
}
bool field(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool assignment(lua_State*L,int self,const char*key){AILuaInspect::field(L,self,key);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct Write{Room*r;const AIRequest*q;ServerPlayer*p;QByteArray key;QJsonObject value;const char*error=nullptr;};
int currentSourceSlotsFail(lua_State*L,Write*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int currentSourceSlotsAssign(lua_State*L){
 auto*w=static_cast<Write*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return currentSourceSlotsFail(L,w,"source slot assignment capacity unavailable");
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));int key=lua_absindex(L,-1);const auto t=w->value.value("type").toString();
 if(t=="nil")lua_pushnil(L);else if(t=="boolean")lua_pushboolean(L,w->value.value("value").toBool());else if(t=="number")lua_pushnumber(L,w->value.value("value").toDouble());else{auto s=w->value.value("value").toString().toUtf8();lua_pushlstring(L,s.constData(),size_t(s.size()));}int value=lua_absindex(L,-1);
 if(!currentSourceSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return currentSourceSlotsFail(L,w,"source slot current Self unavailable before write");int self=lua_absindex(L,-1);
 if(!assignment(L,self,w->key.constData())||!currentSourceSlotsCurrent(*w->r,*w->q,w->p))return currentSourceSlotsFail(L,w,"source slot assignment handler not certified");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 if(!currentSourceSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return currentSourceSlotsFail(L,w,"source slot current Self changed after write");
 AILuaInspect::field(L,self,w->key.constData());if(!lua_rawequal(L,value,-1))return currentSourceSlotsFail(L,w,"source slot write changed");return 0;
}
bool listScope(const AIRequest&q,const QString&key,const QString&op){
 if(key=="shilie_n")return (op=="reset"||op=="append"||op=="length")?play(q):(op=="contains"&&use(q,"@@shilie!"));
 if(key=="yuanrongIds")return op=="write"?question(q,AIRequest::AmazingGrace,"yuanrong","askForAG"):(op=="read"&&use(q,"@@yuanrong!"));
 return false;
}
struct ListCall{Room*r;const AIRequest*q;ServerPlayer*p;QString key,op;QJsonObject payload,result;const char*error=nullptr;};
int listFail(lua_State*L,ListCall*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool integer(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
int listCall(lua_State*L){
 auto*w=static_cast<ListCall*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return listFail(L,w,"source list frame capacity unavailable");
 const auto k=w->key.toUtf8();lua_pushlstring(L,k.constData(),size_t(k.size()));int key=lua_absindex(L,-1);
 const bool replace=w->op=="reset"||w->op=="write";int value=0;
 if(replace){
  if(w->key=="shilie_n"){
   lua_pushglobaltable(L);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return listFail(L,w,"IntList namespace unavailable");AILuaInspect::field(L,-1,"IntList");
   if(!matchAINativeScratchConstructor(*w->r,L,-1,"IntList"))return listFail(L,w,"IntList source constructor changed");lua_call(L,0,1);
   if(!matchAINativeObjectMethods(*w->r,L,-1,"IntList",{"append","length","contains"}))return listFail(L,w,"IntList source methods changed");
  }else{
   lua_newtable(L);int table=lua_absindex(L,-1);int i=0;for(const auto&v:w->payload.value("ids").toArray()){lua_pushinteger(L,v.toInt());lua_rawseti(L,table,++i);}
  }
  value=lua_absindex(L,-1);
 }
 if(!currentSourceSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return listFail(L,w,"source list current Self unavailable");int self=lua_absindex(L,-1);
 if(replace){
  if(!assignment(L,self,k.constData())||!currentSourceSlotsCurrent(*w->r,*w->q,w->p))return listFail(L,w,"source list assignment handler changed");
  lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 }else{
  if(!field(L,self,k.constData()))return listFail(L,w,"source list lookup is not certified");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)){w->result={{"known",true},{"type","nil"}};}
  else if(w->key=="yuanrongIds"){
   if(!lua_istable(L,value)||lua_getmetatable(L,value))return listFail(L,w,"Yuanrong current slot is not a plain source list");
   QJsonArray ids;const size_t n=lua_rawlen(L,value);if(n>4096)return listFail(L,w,"Yuanrong source list exceeds finite card domain");
   for(size_t i=1;i<=n;++i){lua_rawgeti(L,value,lua_Integer(i));if(lua_type(L,-1)!=LUA_TNUMBER)return listFail(L,w,"Yuanrong source list has a non-ID entry");double d=lua_tonumber(L,-1);if(!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return listFail(L,w,"Yuanrong source list has a non-ID entry");ids.append(int(d));lua_pop(L,1);}
   size_t entries=0;lua_pushnil(L);while(AILuaInspect::next(L,value)){if(lua_type(L,-2)!=LUA_TNUMBER)return listFail(L,w,"Yuanrong source list has non-array fields");double i=lua_tonumber(L,-2);if(i<1||i>double(n)||i!=std::floor(i))return listFail(L,w,"Yuanrong source list is sparse");++entries;lua_pop(L,1);}if(entries!=n)return listFail(L,w,"Yuanrong source list is sparse");
   w->result={{"known",true},{"type","ids"},{"value",ids}};
  }else{
   auto method=w->op.toUtf8();if(!matchAINativeObjectMethods(*w->r,L,value,"IntList",{w->op}))return listFail(L,w,"Shilie current IntList method changed");
   // This invokes only the sealed native QList method reached by the source.
   // Its userdata and mutable backing stay entirely in the gameplay VM.
   lua_getfield(L,value,method.constData());lua_pushvalue(L,value);int nargs=1;
   if(w->op!="length"){lua_pushinteger(L,w->payload.value("id").toInt());++nargs;}
   if(!currentSourceSlotsCurrent(*w->r,*w->q,w->p))return listFail(L,w,"Shilie request changed before source method");
   lua_call(L,nargs,1);
   if(w->op=="length"){if(lua_type(L,-1)!=LUA_TNUMBER)return listFail(L,w,"IntList length ABI changed");w->result={{"known",true},{"type","number"},{"value",double(lua_tonumber(L,-1))}};}
   else if(w->op=="contains"){if(lua_type(L,-1)!=LUA_TBOOLEAN)return listFail(L,w,"IntList contains ABI changed");w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,-1))}};}
  }
 }
 if(!currentSourceSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return listFail(L,w,"source list current Self changed after operation");
 if(replace){AILuaInspect::field(L,self,k.constData());if(!lua_rawequal(L,value,-1))return listFail(L,w,"source list assignment changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
QJsonObject listQuery(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&payload){
 if(!listScope(q,key,op)||!currentSourceSlotsCurrent(r,q,p))return unknown("source list exact question unsupported");
 if(key=="yuanrongIds"&&op=="write"){
  if(payload.size()!=1||!payload.value("ids").isArray())return unknown("Yuanrong assignment requires exact offered ID list");const auto ids=payload.value("ids").toArray();if(ids.size()!=q.choiceOptions.cardIds.size())return unknown("Yuanrong assignment changed offered list");
  for(int i=0;i<ids.size();++i){int id;if(!integer(ids[i],id)||id!=q.choiceOptions.cardIds[i])return unknown("Yuanrong assignment changed offered order");}
 }else if(op=="append"||op=="contains"){
  int id;if(payload.size()!=1||!integer(payload.value("id"),id))return unknown("Shilie source method requires exact ID");
  // append's producer enumerates this current owner's disclosed Shilie pile.
  if(op=="append"){bool known=false;for(const auto&pile:q.worldView.self.piles)if(pile.name=="shilie"&&pile.cardIds.contains(id))known=true;if(!known||!p->getPile("shilie").contains(id))return unknown("Shilie append material not current own disclosed pile");}
 }else if(!payload.isEmpty())return unknown("source list operation unexpected arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("source list gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 ListCall w{&r,&q,p,key,op,payload};lua_pushcfunction(L,listCall);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected source list operation failed");return w.result;
}
}
QJsonObject queryAICurrentSourceSlot(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&v){
 if(key=="shilie_n"||key=="yuanrongIds")return listQuery(r,q,p,key,op,v);
 const bool write=op=="write";if((!write&&op!="read")||!currentSourceSlotsCurrent(r,q,p)||!scope(q,key,write)||(write?!valid(key,v):!v.isEmpty()))return unknown("source slot shape or exact current question unsupported");
 if(write&&key=="s3_fans_handou_card"){int id;if(!integer(v.value("value"),id)||!p->handCards().contains(id))return unknown("Handou saved ID must be current own hand");bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;if(!disclosed)return unknown("Handou saved ID is not current disclosed hand");}
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("source slot gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(write){Write w{&r,&q,p,key.toUtf8(),v};lua_pushcfunction(L,currentSourceSlotsAssign);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected source slot write failed");return{{"known",true}};}
 if(!currentSourceSlotsCurrent(r,q,p)||!pushAIActualAISelf(r,p,L))return unknown("source slot actual Self unavailable");int self=lua_absindex(L,-1);auto name=key.toUtf8();if(!field(L,self,name.constData()))return unknown("source slot dynamic lookup is unsupported");
 QJsonObject result{{"known",true}};switch(lua_type(L,-1)){case LUA_TNIL:result.insert("type","nil");break;case LUA_TBOOLEAN:result.insert("type","boolean");result.insert("value",bool(lua_toboolean(L,-1)));break;case LUA_TNUMBER:{double d=lua_tonumber(L,-1);if(!std::isfinite(d))return unknown("source slot nonfinite scalar unsupported");result.insert("type","number");result.insert("value",d);break;}case LUA_TSTRING:{size_t n;const char*s=lua_tolstring(L,-1,&n);if(n>256)return unknown("source slot scalar string exceeds finite domain");result.insert("type","string");result.insert("value",QString::fromUtf8(s,int(n)));break;}default:return unknown("source slot is not scalar");}
 if(!currentSourceSlotsCurrent(r,q,p))return unknown("source slot request changed after read");return result;
}

// ---- self-target-slots ----
// Must follow Qt headers: the private Lua implementation defines cast macros.
namespace {
QJsonObject selfTargetSlotsUnknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool selfTargetSlotsCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool selfTargetSlotsQuestion(const AIRequest&q,AIRequest::DecisionKind k,const char*reason,const char*method){return q.kind==k&&q.choiceOptions.reason==reason&&q.choiceOptions.question==method;}
bool playerSlot(const QString&key){return key=="luochong_to"||key=="diting_to"||key=="xiaoxinf_to"||key=="tj_to"||key=="shouxingTo"||key=="geassTarget"||key=="tychendeTo";}
bool selfTargetSlotsScope(const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op){
 if(key=="tychendeTo")return(op=="write"||op=="truth"||op=="read")&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill("tychende");
 if(key=="geassTarget"){
  if(op=="write")return selfTargetSlotsQuestion(q,AIRequest::AmazingGrace,"geass","askForAG")||selfTargetSlotsQuestion(q,AIRequest::AmazingGrace,"xinshengll","askForAG");
  return (op=="read"||op=="truth")&&q.kind==AIRequest::UseCard&&q.pattern=="@@geass"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
 }
 if(key=="tj_to"||key=="shouxingTo"){
  const QString skill=key=="tj_to"?QStringLiteral("tianjiang"):QStringLiteral("shouxing");
  return (op=="write"||op=="read"||(op=="truth"&&key=="shouxingTo"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to"){
  const QString skill=key=="fuhui_to"?QStringLiteral("fuhui"):(key=="jg_to"?QStringLiteral("juguan"):(key=="mx_to"?QStringLiteral("miaoxian"):QStringLiteral("shouli")));
  return (op=="write"||op=="read"||(op=="truth"&&key=="shouli_to"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="xiaoxinf_to")return op=="write"?selfTargetSlotsQuestion(q,AIRequest::Choice,"xiaoxinf","askForChoice"):(op=="read"&&selfTargetSlotsQuestion(q,AIRequest::PlayerChosen,"xiaoxinf_slash","askForPlayerChosen"));
 if(key=="diting_to")return op=="write"?selfTargetSlotsQuestion(q,AIRequest::SkillInvoke,"diting","askForSkillInvoke"):(op=="read"&&selfTargetSlotsQuestion(q,AIRequest::AmazingGrace,"diting","askForAG"));
 if(key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"){
  const QString skill=key=="fengying_to"?QStringLiteral("fengying"):(key=="fuping_to"?QStringLiteral("fuping"):QStringLiteral("jiusi"));
  return (op=="write"||op=="read"||(op=="truth"&&key!="fengying_to"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="youlong_to"||key=="secondjinzhi_to"){
  const QString skill=key=="youlong_to"?QStringLiteral("youlong"):QStringLiteral("secondjinzhi");
  return (op=="write"||op=="read")&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="luochong_to")return op=="write"?selfTargetSlotsQuestion(q,AIRequest::Choice,"luochong","askForChoice"):(op=="read"&&selfTargetSlotsQuestion(q,AIRequest::PlayerChosen,"luochong","askForPlayerChosen"));
 if(key!="cl_to")return false;
 if(op=="write")return selfTargetSlotsQuestion(q,AIRequest::PlayerChosen,"choulve","askForPlayerChosen");
 return (op=="truth"||op=="read"||op=="clear")&&q.kind==AIRequest::UseCard&&q.pattern=="@@choulve!"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool selfTargetSlotsField(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool selfTargetSlotsAssignment(lua_State*L,int self,const char*key){AILuaInspect::field(L,self,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
ServerPlayer*roster(Room&r,const AIRequest&q,const QString&name){
 bool projected=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)projected=projected||v.objectName==name;
 if(!projected)return nullptr;for(auto*p:r.getAllPlayers(true))if(p->objectName()==name)return p;return nullptr;
}
ServerPlayer*actualPlayer(Room&r,lua_State*L,int at){for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,at,p))return p;return nullptr;}
struct Call{Room*r;const AIRequest*q;ServerPlayer*p;QString key,op;QList<ServerPlayer*>targets;bool nil=false;QJsonObject result;const char*error=nullptr;};
thread_local Call*activeChoulveTargetWrite=nullptr;
struct TargetWriteFrame {Call*previous;explicit TargetWriteFrame(Call*w):previous(activeChoulveTargetWrite){activeChoulveTargetWrite=w;}~TargetWriteFrame(){activeChoulveTargetWrite=previous;}};
int selfTargetSlotsFail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return selfTargetSlotsFail(L,w,"source target slot stack unavailable");const auto k=w->key.toUtf8();
 const bool write=w->op=="write"||w->op=="clear";int value=0;
 if(write){
  if(w->nil||w->op=="clear")lua_pushnil(L);
  else if(playerSlot(w->key)){if(!pushAISlotPlayer(L,w->targets.first()))return selfTargetSlotsFail(L,w,"source Player wrapper unavailable");}
  else if(!pushAISlotTargets(L,w->targets))return selfTargetSlotsFail(L,w,"source SPlayerList wrapper unavailable");
  value=lua_absindex(L,-1);
 }
 if(!selfTargetSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return selfTargetSlotsFail(L,w,"current actual gameplay Self unavailable");const int self=lua_absindex(L,-1);
 if(write){
  if(!selfTargetSlotsAssignment(L,self,k.constData())||!selfTargetSlotsCurrent(*w->r,*w->q,w->p))return selfTargetSlotsFail(L,w,"source assignment dispatch changed");
  lua_pushvalue(L,value);lua_setfield(L,self,k.constData());
 }else{
  if(!selfTargetSlotsField(L,self,k.constData()))return selfTargetSlotsFail(L,w,"actual Self slot getter changed");value=lua_absindex(L,-1);
  if(w->op=="truth")w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
  else if(lua_isnil(L,value))w->result={{"known",true},{"type","nil"}};
  else if(playerSlot(w->key)){
   // Scalars retain the source's ordinary :objectName error in the Lua caller.
   const int t=lua_type(L,value);
   if(t==LUA_TBOOLEAN)w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
   else if(t==LUA_TNUMBER){double d=lua_tonumber(L,value);if(!std::isfinite(d))return selfTargetSlotsFail(L,w,"nonfinite source scalar unsupported");w->result={{"known",true},{"type","number"},{"value",d}};}
   else if(t==LUA_TSTRING){size_t n;const char*s=lua_tolstring(L,value,&n);if(n>4096)return selfTargetSlotsFail(L,w,"source scalar exceeds finite domain");w->result={{"known",true},{"type","string"},{"value",QString::fromUtf8(s,int(n))}};}
   else{
    auto*p=actualPlayer(*w->r,L,value);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAINativeObjectMethods(*w->r,L,value,"ServerPlayer",{"objectName"}))return selfTargetSlotsFail(L,w,"actual source target or objectName binding unavailable");
    w->result={{"known",true},{"type","player"},{"value",p->objectName()}};
   }
  }else{
   // Read the *current* native list, including lists written by legacy AI and
   // later genuine source mutations. No hidden snapshot or remembered copy.
   auto certified=[&](){
    QList<ServerPlayer*>owned;
    if(readAISlotTargets(L,value,owned)){
     // Tracked wrappers have a private GC metatable; this native bridge checks
     // its exact shape and all compiled methods, including mutable backing.
     return matchAINativeClassMethod(*w->r,L,"SPlayerList","length")&&matchAINativeClassMethod(*w->r,L,"SPlayerList","at");
    }
    return matchAINativeObjectMethods(*w->r,L,value,"SPlayerList",{"length","at"});
   };
   QJsonArray names;
   // Match sgs.qlist's recomputed native length on every iteration, including
   // the terminal check. The list stays rooted across native wrapper creation.
   for(int i=0;;++i){
    if(!selfTargetSlotsCurrent(*w->r,*w->q,w->p)||!certified())return selfTargetSlotsFail(L,w,"source list dispatch changed during iteration");
    lua_getfield(L,value,"length");lua_pushvalue(L,value);lua_call(L,1,1);
    if(lua_type(L,-1)!=LUA_TNUMBER)return selfTargetSlotsFail(L,w,"source list length ABI changed");const double d=lua_tonumber(L,-1);lua_pop(L,1);
    if(!std::isfinite(d)||d<0||d>4096||d!=std::floor(d))return selfTargetSlotsFail(L,w,"source list exceeds finite target domain");
    if(i>=int(d))break;if(i>=4096)return selfTargetSlotsFail(L,w,"source list iteration exceeds finite target domain");
    lua_getfield(L,value,"at");lua_pushvalue(L,value);lua_pushinteger(L,i);lua_call(L,2,1);
    auto*p=actualPlayer(*w->r,L,-1);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAINativeObjectMethods(*w->r,L,-1,"ServerPlayer",{"objectName"}))return selfTargetSlotsFail(L,w,"source list element is not a live projected Player");names.append(p->objectName());lua_pop(L,1);
   }
   w->result={{"known",true},{"type","players"},{"value",names}};
  }
 }
 if(!selfTargetSlotsCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return selfTargetSlotsFail(L,w,"actual source receiver changed after operation");
 if(write){AILuaInspect::field(L,self,k.constData());if(!lua_rawequal(L,value,-1))return selfTargetSlotsFail(L,w,"source slot assignment changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
bool acceptAIChoulveTargetWriteEntry(Room&r,lua_State*L,int n) noexcept {
 auto*w=activeChoulveTargetWrite;
 return w&&w->r==&r&&w->key=="cl_to"&&w->op=="write"&&!w->nil
  &&inAIProjectionSourceScope(r,L)&&n==1&&lua_gettop(L)>=2
  &&lua_iscfunction(L,-2)&&lua_tocfunction(L,-2)==run
  &&lua_type(L,-1)==LUA_TLIGHTUSERDATA&&lua_touserdata(L,-1)==w;
}
QJsonObject queryAISelfTargetSlot(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&payload){
 if(!selfTargetSlotsCurrent(r,q,p)||!selfTargetSlotsScope(q,p,key,op))return selfTargetSlotsUnknown("target slot requires exact current own source question");
 Call w{&r,&q,p,key,op};
 if(op=="write"){
  const auto t=payload.value("type").toString();if(t=="nil"&&payload.size()==1&&(!playerSlot(key)||key=="shouxingTo"||key=="geassTarget"||key=="tychendeTo"))w.nil=true;
  else if(playerSlot(key)&&t=="player"&&payload.size()==2&&payload.value("value").isString()){
   auto*target=roster(r,q,payload.value("value").toString());if(!target)return selfTargetSlotsUnknown("source target is not projected live roster");w.targets<<target;
  }else if((key=="cl_to"||key=="youlong_to"||key=="secondjinzhi_to"||key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"||key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to")&&t=="players"&&payload.size()==2&&payload.value("value").isArray()){
   const auto names=payload.value("value").toArray();if(names.size()>4096)return selfTargetSlotsUnknown("source target list too large");
   for(const auto&v:names){if(!v.isString())return selfTargetSlotsUnknown("source target list element malformed");auto*target=roster(r,q,v.toString());if(!target)return selfTargetSlotsUnknown("source target list element not projected live roster");w.targets<<target;}
  }else return selfTargetSlotsUnknown("source target assignment shape unsupported");
 }else if(!payload.isEmpty())return selfTargetSlotsUnknown("source target read/clear has extra arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return selfTargetSlotsUnknown("source target game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 // The complete projection union was admitted before private strategy gates.
 // This frame identifies the exact existing native source store; it grants no
 // permission to read a prior slot or to a different target-list operation.
 TargetWriteFrame choulveWriteFrame(inAIProjectionSourceScope(r,L)&&key=="cl_to"&&op=="write"&&!w.nil?&w:nullptr);
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return selfTargetSlotsUnknown(w.error?w.error:"protected source target operation failed");
 if(key=="cl_to"&&op=="write"&&!w.nil&&w.result.value("known").toBool())recordAIChoulveTargetWrite(r);
 return w.result;
}

// ---- ainoto-planning ----
void initializeAITargetRevises(Room&r){initializeAIAiNoTo(r);}
void beginAITargetRevises(Room&r,const QString&viewer){beginAIAiNoTo(r,viewer);}
void invalidateAITargetRevises(Room&r){retireAIAiNoTo(r);}
QJsonObject queryAITargetRevises(Room&r,const AIRequest&q,const ServerPlayer*p,const QJsonObject&s){return queryAIAiNoTo(r,q,p,s);}

// ---- ainoto ----
#include <QDynamicPropertyChangeEvent>
namespace {
// A witness is provenance only. No bool value is cached or substituted.
struct Witness {bool initialAbsent=false;QString viewer;quint64 transport=0;};
class AinotoState final:public QObject {
public:
 explicit AinotoState(Room&r):QObject(&r){}
 bool initialized=false;QString viewer;quint64 generation=0,initialReads=0,choulveWrites=0,choulveAccepted=0;
 QHash<const ServerPlayer*,Witness>witnesses;QSet<const ServerPlayer*>watched;
 QPointer<ServerPlayer>writing;
 // Callback/transport continuity is required only for policy-written values.
 // Constructor absence survives until its actual QObject property changes.
 void retire(){++generation;for(auto it=witnesses.begin();it!=witnesses.end();){
  if(!it->initialAbsent)it=witnesses.erase(it);else ++it;
 }}
 bool eventFilter(QObject*object,QEvent*e)override{
  if(e->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName()=="aiNoTo"){
   // Every real property change ends constructor absence, including a
   // certified write. Never reconstruct it by sampling a later missing value.
   witnesses.remove(static_cast<ServerPlayer*>(object));
   // The same exact certified setter is checked again after all synchronous
   // observers. Every other setter loses permission; never imports its value.
   if(object!=writing.data())retire();
  }
  return false;
 }
};
AinotoState*ainotoState(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c))return s;return new AinotoState(r);}
QJsonObject error(const char*s){return{{"error",QString::fromLatin1(s)},{"key","targetRevises.aiNoTo"}};}
}
void createdAIAiNoTo(Room&r,ServerPlayer*p){
 if(!p||p->getRoom()!=&r)return;auto*s=ainotoState(r);if(s->watched.contains(p))return;
 s->watched.insert(p);p->installEventFilter(s);
 QObject::connect(p,&QObject::destroyed,s,[s,p](){s->witnesses.remove(p);s->watched.remove(p);});
 // Called only from ServerPlayer construction. A later runtime reset cannot
 // sample an already-running native property and turn it into a baseline.
 if(!p->property("aiNoTo").isValid())s->witnesses.insert(p,{true,{},0});
}
bool hasAIInitialAiNoToWitness(const Room&r,const ServerPlayer*p) noexcept {
 auto*rt=const_cast<Room&>(r).roomRuntime();
 if(!p||p->getRoom()!=&r||!rt)return false;
 for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c)){
  const auto i=s->witnesses.constFind(p);
  return i!=s->witnesses.cend()&&i->initialAbsent;
 }
 return false;
}
quint64 observedAIInitialAiNoToReads(const Room&r) noexcept {
 for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c))return s->initialReads;
 return 0;
}
void recordAIChoulveTargetWrite(Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c)){++s->choulveWrites;return;}}
void recordAIChoulveNativeAcceptance(Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c)){++s->choulveAccepted;return;}}
quint64 observedAIChoulveTargetWrites(const Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c))return s->choulveWrites;return 0;}
quint64 observedAIChoulveNativeAcceptances(const Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c))return s->choulveAccepted;return 0;}
void initializeAIAiNoTo(Room&r){ainotoState(r)->initialized=true;}
void beginAIAiNoTo(Room&r,const QString&viewer){
 auto*s=ainotoState(r);
 // This happens before the next viewer's policy private gates, irrespective of
 // whether that policy actually reaches a write. It exports no write occurrence.
 if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer))s->retire();
 s->viewer=viewer;
}
void retireAIAiNoTo(Room&r) noexcept {
 // A preselection/async barrier only revokes existing provenance. It must not
 // allocate a Room child or establish a new baseline during observation.
 for(auto*c:r.children())if(auto*s=dynamic_cast<AinotoState*>(c)){s->retire();return;}
}
QJsonObject queryAIAiNoTo(Room&r,const AIRequest&q,const ServerPlayer*viewer,const QJsonObject&spec){
 if(!viewer||viewer->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||q.viewerObjectName!=viewer->objectName()||q.worldView.self.objectName!=viewer->objectName()
  ||!aiRangeRequestCurrent(r,q,viewer))return error("target-revise property request/viewer is stale");
 auto*s=ainotoState(r);if(!s->initialized||s->viewer!=viewer->objectName())return error("target-revise source entry unavailable");
 if(spec.value("kind")!="target_revise_property"||!spec.value("target").isString()||!spec.value("op").isString())return error("target-revise operation malformed");
 auto*target=r.findPlayerByObjectName(spec.value("target").toString(),true);bool projected=q.worldView.self.objectName==spec.value("target").toString();
 for(const auto&p:q.worldView.players)projected=projected||p.objectName==spec.value("target").toString();
 if(!target||!projected||!s->watched.contains(target))return error("target-revise target lacks constructor/current roster identity");
 const auto op=spec.value("op").toString();
 if(op=="write"){
  if(spec.size()!=4||!spec.value("value").isBool()||s->writing)return error("target-revise writer shape/reentry invalid");
  const bool value=spec.value("value").toBool();const auto generation=s->generation;
  const auto transport=r.roomRuntime()->xufaTransportEpoch();QString why;
  AIAiNoToPermit permit(&r,&q,viewer,target,value);s->writing=target;
  bool ok=false;try{ok=writeAIAiNoToProperty(r,q,const_cast<ServerPlayer*>(viewer),target,value,permit,&why);}catch(...){s->writing.clear();s->retire();throw;}
  s->writing.clear();
  if(!ok||s->generation!=generation||!transport||transport!=r.roomRuntime()->xufaTransportEpoch()){s->retire();return error("target-revise prefix committed but native continuation is uncertified");}
  s->witnesses.insert(target,{false,viewer->objectName(),transport});
 }else if(op!="read"||spec.size()!=3)return error("target-revise reader shape invalid");
 const auto it=s->witnesses.constFind(target);const QVariant actual=target->property("aiNoTo");
 const bool known=it!=s->witnesses.cend()&&(it->initialAbsent?!actual.isValid():
  (it->transport&&it->transport==r.roomRuntime()->xufaTransportEpoch()
   &&it->viewer==viewer->objectName()&&actual.userType()==QMetaType::Bool));
 if(op=="read"&&known&&it->initialAbsent)++s->initialReads;
 QJsonObject row{{"kind","target_revise_property"},{"target",target->objectName()},{"known",known},{"viewer",viewer->objectName()},{"decision_id",q.getDecisionId()},{"state_revision",q.getStateRevision()}};
 if(known)row.insert("value",actual.toBool());else row.insert("reason","target-revise actual property has no lawful source witness for this viewer");
 return row;
}

// ---- pingjian ----
#include "general.h"
#include "skill.h"
namespace {
struct PingjianStack{lua_State*L;int n;explicit PingjianStack(lua_State*s):L(s),n(lua_gettop(s)){}~PingjianStack(){lua_settop(L,n);}};
class PingjianState;void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
class PingjianState final:public QObject{public:explicit PingjianState(Room&r):QObject(&r),room(r){}
 ~PingjianState()override{for(auto id:{globals,space,fill})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr,*fill=nullptr,*can=nullptr,*sampler=nullptr;
 bool valid=false;QSet<QString>truthy;
};
PingjianState*pingjianState(Room&r,bool make=false){for(auto*c:r.children())if(auto*s=dynamic_cast<PingjianState*>(c))return s;return make?new PingjianState(r):nullptr;}
bool pingjianKey(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
void observe(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{auto*s=static_cast<PingjianState*>(u);if(!L||!s->L||G(L)!=G(s->L))return;const auto&e=*ep;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&(e.table_identity==s->fill
  ||(e.table_identity==s->globals&&(pingjianKey(e,"sgs")||pingjianKey(e,"canAiSkills")||pingjianKey(e,"PingjianSkill")))
  ||(e.table_identity==s->space&&pingjianKey(e,"ai_fill_skill")))))s->valid=false;
 if(e.event==LUA_TABLE_OBSERVER_FREE){if(e.table_identity==s->globals)s->globals=nullptr;if(e.table_identity==s->space)s->space=nullptr;if(e.table_identity==s->fill)s->fill=nullptr;}
}
bool environment(lua_State*L,int i){i=lua_absindex(L,i);bool seen=false;for(int n=1;;++n){const char*k=lua_getupvalue(L,i,n);if(!k)break;
 if(std::strcmp(k,"_ENV")||seen){lua_pop(L,1);return false;}seen=true;AILuaInspect::globals(L);bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;}return seen;}
bool utility(Room&r,lua_State*L,int i,int first,int last){return environment(L,i)&&matchAIPinnedFunction(r,L,i,"lua/utilities.lua",first,last);}
bool bindings(PingjianState*s){auto*L=s->L;Room&r=s->room;PingjianStack st(L);AILuaInspect::globals(L);const int g=lua_absindex(L,-1);
 AILuaInspect::field(L,g,"sgs");const int sg=lua_absindex(L,-1);AILuaInspect::field(L,sg,"list");if(!utility(r,L,-1,38,41))return false;lua_pop(L,1);
 AILuaInspect::field(L,sg,"qlist");const int qlist=lua_absindex(L,-1);if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){AILuaInspect::field(L,g,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 AILuaInspect::field(L,g,"table");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;const int table=lua_absindex(L,-1);
 AILuaInspect::field(L,table,"contains");if(!utility(r,L,-1,95,108))return false;lua_pop(L,1);AILuaInspect::field(L,table,"insert");if(!matchAINativeBinding(r,L,-1,"table.insert"))return false;
 AILuaInspect::field(L,g,"string");t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;AILuaInspect::field(L,-1,"find");if(!matchAINativeBinding(r,L,-1,"string.find"))return false;
 if(!matchAINativeNamespaceProperty(r,L,"sgs","Sanguosha"))return false;
 for(const char*k:{"getAllGenerals","getBanPackages","getSkill"})if(!matchAINativeClassMethod(r,L,"Engine",k))return false;
 for(const char*c:{"GeneralList","SkillList"})for(const char*k:{"length","at"})if(!matchAINativeClassMethod(r,L,c,k))return false;
 return matchAINativeDerivedMethods(r,L,"General",{"getPackage","isHidden","isTotallyHidden","getSkillList"})
  &&matchAINativeDerivedMethods(r,L,"Skill",{"objectName","getDescription"})
  &&matchAINativeDerivedMethods(r,L,"Player",{"property"})&&matchAINativeClassMethod(r,L,"QVariant","toStringList");
}
bool functions(PingjianState*s){auto*L=s->L;PingjianStack st(L);AILuaInspect::globals(L);const int g=lua_absindex(L,-1);
 AILuaInspect::field(L,g,"canAiSkills");if(lua_topointer(L,-1)!=s->can||!environment(L,-1)||!matchAIPinnedFunction(s->room,L,-1,"lua/ai/smart-ai.lua",8826,8844))return false;lua_pop(L,1);
 AILuaInspect::field(L,g,"PingjianSkill");return lua_topointer(L,-1)==s->sampler&&environment(L,-1)&&matchAIPinnedFunction(s->room,L,-1,"lua/ai/tenyear_hc-ai.lua",4868,4888);
}
bool pingjianLive(PingjianState*s){auto*rt=s?s->room.roomRuntime():nullptr;if(!rt||rt->isClosing()||!s->valid||!rt->lua().isCurrentThreadOwner()
 ||rt->lua().rawState()!=s->L||rt->lua().generation()!=s->generation||rt->lua().lifecycle()!=LuaRuntime::Lifecycle::Running)return false;
 PingjianStack st(s->L);AILuaInspect::globals(s->L);if(lua_topointer(s->L,-1)!=s->globals)return false;
 AILuaInspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;
 AILuaInspect::field(s->L,-1,"ai_fill_skill");auto*t=AILuaInspect::table(s->L,-1);return t&&!t->metatable&&lua_topointer(s->L,-1)==s->fill&&functions(s)&&bindings(s);
}
}
bool initializeAIPingjian(Room&r,lua_State*L){auto*rt=r.roomRuntime();if(!rt||!L||L!=rt->lua().rawState()||!rt->lua().isCurrentThreadOwner()||rt->isClosing()||!lua_checkstack(L,4096))return false;
 if(auto*old=pingjianState(r))return pingjianLive(old); // Never recapture a replaced or rewritten registry.
 auto*s=pingjianState(r,true);s->L=L;s->generation=rt->lua().generation();PingjianStack st(L);
 if(!registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"))
 ||!registerAIPinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"))
 ||!registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5")))return false;
 AILuaInspect::globals(L);const void*g=lua_topointer(L,-1);const int gi=lua_absindex(L,-1);
 AILuaInspect::field(L,gi,"canAiSkills");s->can=lua_topointer(L,-1);lua_pop(L,1);AILuaInspect::field(L,gi,"PingjianSkill");s->sampler=lua_topointer(L,-1);lua_pop(L,1);if(!functions(s))return false;
 AILuaInspect::field(L,gi,"sgs");if(!lua_istable(L,-1))return false;const void*ns=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"ai_fill_skill");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;const int table=lua_absindex(L,-1);const void*fill=lua_topointer(L,table);
 int count=0;lua_pushnil(L);while(AILuaInspect::next(L,table)){
  if(++count>16384||lua_type(L,-2)!=LUA_TSTRING)return false;size_t len=0;const char*k=lua_tolstring(L,-2,&len);if(len>4096)return false;
  if(lua_toboolean(L,-1))s->truthy.insert(QString::fromUtf8(k,int(len)));lua_pop(L,1);
 }
 if(lua_observetableidentity(L,g,observe,s))s->globals=g;if(lua_observetableidentity(L,ns,observe,s))s->space=ns;if(lua_observetableidentity(L,fill,observe,s))s->fill=fill;
 s->valid=s->globals&&s->space&&s->fill;return pingjianLive(s);
}
void clearAIPingjian(Room&r){delete pingjianState(r);}
void projectAIPingjian(AIWorldView&w,Room&,ServerPlayer*p){if(!p||w.self.objectName!=p->objectName())return;
 // Exact source QVariant conversion. Do not split the QString written by the
 // native Pingjian implementation; QString and QStringList are distinct types.
 auto props=w.strategyFacts.value("owner_skill_properties").toObject();props.insert("pingjian_has_used_skills",QJsonArray::fromStringList(p->property("pingjian_has_used_skills").toStringList()));w.strategyFacts.insert("owner_skill_properties",props);
}
QJsonObject queryAIPingjianCatalog(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){auto fail=[](){return QJsonObject{{"known",false}};};
 auto*rt=r.roomRuntime();if(v.size()!=1||v.value("op")!="pingjian_catalog"||!p||p->getRoom()!=&r||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
 ||!Sanguosha||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!rt||rt->isClosing()||Sanguosha->currentRoom()!=&r||!aiRangeRequestCurrent(r,q,p))return fail();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,4096))return fail();auto*s=pingjianState(r);if(!pingjianLive(s))return fail();
 QJsonArray rows;QJsonObject descriptions;const QStringList banned=Sanguosha->getBanPackages();
 for(const General*g:Sanguosha->getAllGenerals()){
  if(banned.contains(g->getPackage())||g->isHidden()||g->isTotallyHidden())continue;
  for(const Skill*sk:g->getSkillList())if(s->truthy.contains(sk->objectName())){
   const QString name=sk->objectName();rows.append(QJsonObject{{"name",name}});const Skill*actual=Sanguosha->getSkill(name);
   descriptions.insert(name,actual?QJsonValue(actual->getDescription()):QJsonValue(false));
  }
 }
 if(!pingjianLive(s)||!aiRangeRequestCurrent(r,q,p))return fail();return{{"known",true},{"skills",rows},{"descriptions",descriptions}};
}

// ---- card-choice-prefix ----
// Local declaration keeps the identity-only native helper out of shared headers.
extern "C" {
LUALIB_API int luaL_isstandardstringadd(lua_CFunction function);
}
namespace {
struct CardChoicePrefixStack{lua_State*L;int n;explicit CardChoicePrefixStack(lua_State*s):L(s),n(lua_gettop(s)){}~CardChoicePrefixStack(){lua_settop(L,n);}};
bool cardChoicePrefixCurrent(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(p)
 &&q.decisionId&&q.conversionLedger&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&Sanguosha&&Sanguosha->currentRoom()==&r&&aiRangeDecisionActive(r,q,p);}
// Exact table-valued __index inheritance only. Never invoke a replaced getter.
bool lookup(lua_State*L,int self,const char*key){QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AILuaInspect::field(L,table,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;
 }return false;}
struct CardChoicePrefixCall{Room*r;ServerPlayer*p;const AIRequest*q;QByteArray key;lua_State*L=nullptr;quint64 generation=0;
 const void*globals=nullptr,*space=nullptr,*table=nullptr,*self=nullptr;bool arithmetic=false,written=false;const char*error=nullptr;};
bool cardChoicePrefixRoots(lua_State*L,CardChoicePrefixCall&w,bool capture=false){CardChoicePrefixStack z(L);auto*rt=w.r->roomRuntime();if(!cardChoicePrefixCurrent(*w.r,w.p,*w.q)||rt->lua().rawState()!=w.L||rt->lua().generation()!=w.generation)return false;
 AILuaInspect::globals(L);const int env=lua_absindex(L,-1);const void*g=lua_topointer(L,env);
 AILuaInspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;const void*ns=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"throwData");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;const void*target=lua_topointer(L,-1);
 if(!pushAIActualAISelf(*w.r,w.p,L))return false;const int self=lua_absindex(L,-1);const void*owner=lua_topointer(L,self);
 if(!lookup(L,self,"askForCardChosen")||!matchAIPinnedFunction(*w.r,L,-1,"lua/ai/smart-ai.lua",3435,3626))return false;const int fn=lua_absindex(L,-1);bool saw=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,fn,i);if(!k)break;bool ok=!std::strcmp(k,"_ENV")&&!saw&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;saw=true;}
 if(!saw)return false;
 if(capture){w.globals=g;w.space=ns;w.table=target;w.self=owner;return true;}
 return w.globals==g&&w.space==ns&&w.table==target&&w.self==owner;
}
int fail(lua_State*L,CardChoicePrefixCall*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool sourceArithmeticBinding(lua_State*L,int index){const bool string=lua_type(L,index)==LUA_TSTRING;
 if(!lua_getmetatable(L,index))return true;AILuaInspect::field(L,-1,"__add");const auto*method=AILuaInspect::value(L,-1);
 // Admit only Lua's exact noncapturing string-add function, never a closure
 // observed in the current VM. Other operand types still require no __add.
 const bool admitted=lua_isnil(L,-1)||(string&&ttislcf(method)&&luaL_isstandardstringadd(fvalue(method)));
 lua_pop(L,2);return admitted;}
int cardChoicePrefixRun(lua_State*L){auto*w=static_cast<CardChoicePrefixCall*>(lua_touserdata(L,1));if(!w||!lua_checkstack(L,4096))return luaL_error(L,"ordinary card-choice prefix frame unavailable");
 if(!cardChoicePrefixRoots(L,*w,true))return fail(L,w,"ordinary actual source provider or registry binding changed");
 // Allocate the exact string before reading the source cell; reauthenticate
 // after allocation. All later reads/writes use the rooted actual table/key.
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));int key=lua_absindex(L,-1);
 if(!cardChoicePrefixRoots(L,*w))return fail(L,w,"ordinary source changed during key allocation");
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"throwData");int table=lua_absindex(L,-1);
 lua_pushvalue(L,key);lua_gettable(L,table);
  if(!lua_toboolean(L,-1)){lua_pop(L,1);lua_pushinteger(L,0);}
  const int type=lua_type(L,-1);if((type!=LUA_TNUMBER&&type!=LUA_TSTRING&&type!=LUA_TBOOLEAN)||!sourceArithmeticBinding(L,-1))return fail(L,w,"ordinary throwData arithmetic requires an unproved value/metamethod");
  lua_pushinteger(L,1);
  // Standard string arithmetic delegates failed conversion to the RHS __add;
  // prove its absence before entering arithmetic or classifying source errors.
  if(!sourceArithmeticBinding(L,-1))return fail(L,w,"ordinary throwData right operand arithmetic binding changed");
  // Lua performs its own integer/float/string arithmetic and ordinary errors.
  // A bad primitive stops before assignment; never coerce it to zero.
  w->arithmetic=true;lua_arith(L,LUA_OPADD);w->arithmetic=false;
  lua_pushvalue(L,key);lua_insert(L,-2);
 if(!cardChoicePrefixRoots(L,*w))return fail(L,w,"ordinary source request changed before prefix assignment");
 lua_settable(L,table);w->written=true;
 if(!cardChoicePrefixRoots(L,*w))return fail(L,w,"ordinary source request changed after prefix assignment");return 0;
}
// R55: source sortByKeepValue's #cards<2 prefix has no keep-cache reads or
// writes. Prove the actual foreign receiver and reached source dependencies;
// do not expose another AI's keepValue/keepdata/toUse or call its policy.
QJsonObject trivialForeignKeep(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto no=[](){return QJsonObject{{"error","foreign trivial keep-sort source unavailable"},{"key","askForCardChosen.ownerSort"}};};
 if(input.size()!=3||input.value("op")!="foreign_keep_trivial"||!input.value("owner").isString()
  ||!input.value("ids").isArray()||input.value("ids").toArray().size()>1||!cardChoicePrefixCurrent(r,p,q))return no();
 const QString name=input.value("owner").toString();if(name.isEmpty()||name==p->objectName())return no();
 const AIPlayerView*shown=nullptr;for(const auto&v:q.worldView.players)if(v.objectName==name){shown=&v;break;}
 if(!shown||shown->equips.size()>1||shown->equips.size()!=input.value("ids").toArray().size())return no();
 const auto ids=input.value("ids").toArray();
 for(int i=0;i<ids.size();++i)if(!ids[i].isDouble()||ids[i].toDouble()!=shown->equips[i].effectiveId)return no();
 ServerPlayer*owner=nullptr;for(auto*t:r.getAllPlayers(true))if(t&&t->objectName()==name){owner=t;break;}
 if(!owner)return no();const auto actual=owner->getEquips();if(actual.size()!=ids.size())return no();
 for(int i=0;i<actual.size();++i)if(!actual[i]||actual[i]->getEffectiveId()!=ids[i].toInt())return no();
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,4096))return no();CardChoicePrefixStack stack(L);const auto generation=vm.generation();
 AILuaInspect::globals(L);const int env=lua_absindex(L,-1);
 AILuaInspect::field(L,env,"sgs");if(!lua_istable(L,-1))return no();const int ns=lua_absindex(L,-1);
 auto plain=[&](int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;};
 auto code=[&](int n,const char*path,int first,int last){n=lua_absindex(L,n);
  if(!matchAIPinnedFunction(r,L,n,path,first,last))return false;bool saw=false;
  for(int i=1;;++i){const char*k=lua_getupvalue(L,n,i);if(!k)break;
   const bool ok=!std::strcmp(k,"_ENV")&&!saw&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;saw=true;}return true;};
 if(!pushAIActualAISelf(r,p,L)||!lookup(L,-1,"askForCardChosen")||!code(-1,"lua/ai/smart-ai.lua",3435,3626))return no();
 if(!pushAIActualAISelf(r,owner,L))return no();const int receiver=lua_absindex(L,-1);
 AILuaInspect::field(L,ns,"ais");if(!plain(-1))return no();
 const auto key=name.toUtf8();AILuaInspect::field(L,-1,key.constData());if(!lua_rawequal(L,receiver,-1))return no();
 if(!lookup(L,receiver,"sortByKeepValue")||!code(-1,"lua/ai/smart-ai.lua",1251,1293))return no();
 AILuaInspect::field(L,ns,"QList2Table");if(!code(-1,"lua/utilities.lua",44,51))return no();
 AILuaInspect::field(L,ns,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return no();
 bool sawEnv=false,sawUser=false,sawTable=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,qlist,i);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){ok=!sawEnv&&lua_rawequal(L,env,-1);sawEnv=true;}
  else if(!std::strcmp(k,"qlist_userdata_iterator")){ok=!sawUser&&code(-1,"lua/utilities.lua",7,11);sawUser=true;}
  else if(!std::strcmp(k,"qlist_table_iterator")){ok=!sawTable&&matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",13,17);sawTable=true;}
  lua_pop(L,1);if(!ok)return no();}
 if(!sawEnv||!sawUser||!sawTable)return no();
 AILuaInspect::field(L,env,"type");if(!matchAINativeBinding(r,L,-1,"type"))return no();
 AILuaInspect::field(L,env,"table");if(!plain(-1))return no();AILuaInspect::field(L,-1,"insert");
 if(!matchAINativeBinding(r,L,-1,"table.insert")
  ||!matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName","getEquips"})
  ||!matchAINativeClassMethod(r,L,"CardList","length")||!matchAINativeClassMethod(r,L,"CardList","at"))return no();
 if(!cardChoicePrefixCurrent(r,p,q)||vm.generation()!=generation)return no();return{{"certified",true}};
}

}
bool registerAICardChoiceSource(Room&r,lua_State*L){return registerAIPinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"))
 &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"));}
QJsonObject queryAICardChoicePrefix(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 if(input.value("op")=="foreign_keep_trivial")return trivialForeignKeep(r,p,q,input);
 auto no=[](const QString&s){return QJsonObject{{"error",s},{"key","askForCardChosen.prefix"}};};
 if(input.size()!=2||input.value("op")!="increment_throw"||!input.value("key").isString()||!cardChoicePrefixCurrent(r,p,q))return no("ordinary throwData prefix schema or current request unavailable");
 const QString key=input.value("key").toString();if(key.size()>256||key.contains(QChar(0)))return no("ordinary reason outside finite string domain");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("ordinary gameplay VM unavailable");CardChoicePrefixStack z(L);
 CardChoicePrefixCall w;w.r=&r;w.p=p;w.q=&q;w.key=key.toUtf8();w.L=L;w.generation=vm.generation();
 lua_pushcfunction(L,cardChoicePrefixRun);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,1,0,0);
 if(status!=LUA_OK){if(w.arithmetic&&status==LUA_ERRRUN&&!w.error)return{{"source_error",QString::fromUtf8(lua_tostring(L,-1)?lua_tostring(L,-1):"ordinary throwData arithmetic error")}};
  return no(w.error?QString::fromLatin1(w.error):QStringLiteral("ordinary protected prefix operation failed"));}
 if(!w.written||!cardChoicePrefixCurrent(r,p,q))return no("ordinary source prefix did not complete on current request");return{{"written",true}};
}
