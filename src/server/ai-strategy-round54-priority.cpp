#include "ai-strategy-round54-priority.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round56-priority-bootstrap.h"
#include "ai-strategy-round44-certificate.h"
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
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Grant{QPointer<ServerPlayer>owner;quint64 decision=0,epoch=0,transport=0,serial=0;QWeakPointer<AIConversionLedger>ledger;};
class State;void noteWrite(State*,const lua_TableObservation&)noexcept;void observer(lua_State*,const lua_TableObservation*,void*)noexcept;
class State final:public AIRoomLedger{public:explicit State(Room&r):AIRoomLedger(r),room(r){}
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
 &&aiRound44RangeDecisionActive(r,q,p);}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&rt->lua().rawState()==s->L
 &&rt->lua().generation()==s->generation&&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running;}
bool roots(State*s){if(!live(s))return false;Stack z(s->L);AIRound45Inspect::globals(s->L);if(lua_topointer(s->L,-1)!=s->globals)return false;
 AIRound45Inspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;AIRound45Inspect::field(s->L,-1,"ai_use_priority");auto*t=AIRound45Inspect::table(s->L,-1);return t&&!t->metatable&&lua_topointer(s->L,-1)==s->table;}
State*capture(Room&r,lua_State*L){if(auto*s=state(r))return roots(s)?s:nullptr;auto*rt=r.roomRuntime();if(!rt||rt->lua().rawState()!=L)return nullptr;
 Stack z(L);AIRound45Inspect::globals(L);const void*g=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return nullptr;const void*ns=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"ai_use_priority");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return nullptr;const void*table=lua_topointer(L,-1);
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();
 if(lua_observetableidentity(L,g,observer,s))s->globals=g;if(lua_observetableidentity(L,ns,observer,s))s->space=ns;if(lua_observetableidentity(L,table,observer,s))s->table=table;
 s->valid=s->globals&&s->space&&s->table;return roots(s)?s:nullptr;
}
bool qualified(State*s,const Grant&g,ServerPlayer*p,const AIRequest&q){auto*rt=s->room.roomRuntime();return g.owner==p&&g.decision==q.decisionId&&g.ledger.toStrongRef()==q.conversionLedger
 &&g.epoch&&g.epoch==rt->prioritySourceEpoch()&&g.transport&&g.transport==rt->xufaTransportEpoch()&&g.serial==s->serial;}
thread_local AIRound54PriorityOperation::Data*active=nullptr;
}
struct AIRound54PriorityOperation::Data{Room*r;ServerPlayer*p;const AIRequest*q;lua_State*L;AIR54PriorityCFunction fn;void*payload;Mode mode;QString target,source;
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
AIRound54PriorityOperation::AIRound54PriorityOperation(Room&r,ServerPlayer*p,const AIRequest&q,lua_State*L,AIR54PriorityCFunction fn,void*payload,Mode mode,const QString&target,const QString&source):d(new Data{&r,p,&q,L,fn,payload,mode,target,source}){
 d->targetBytes=target.toUtf8();d->previous=active;d->depth=r.roomRuntime()->lua().protectedCallDepth();active=d.get();
 if(!current(r,p,q)||!L||!lua_checkstack(L,512)){d->invalid=true;return;}d->s=capture(r,L);if(!d->s)d->invalid=true;
}
AIRound54PriorityOperation::~AIRound54PriorityOperation(){active=d->previous;}
void observeAIRound54PriorityEntry(Room&r,lua_State*L,int n)noexcept{
 auto*rt=r.roomRuntime();if(!rt)return;auto*d=active;
 // Only the exact native C entry, payload, first entry and protected depth can
 // preserve. A nested/finalizer/debug entry permanently invalidates this scope.
 if(d&&d->r==&r&&!d->invalid&&!d->entered&&n==2&&L==d->L&&rt->lua().protectedCallDepth()==d->depth&&lua_gettop(L)>=3
  &&lua_iscfunction(L,-3)&&lua_tocfunction(L,-3)==d->fn&&lua_type(L,-1)==LUA_TLIGHTUSERDATA&&lua_touserdata(L,-1)==d->payload){d->entered=true;return;}
 rt->revokePrioritySource();for(auto*p=d;p;p=p->previous)if(p->r==&r)p->invalid=true;
}
void AIRound54PriorityOperation::beforeWrite(){auto*x=d.get();if(x->invalid||!x->entered||!x->s||!current(*x->r,x->p,*x->q)||!roots(x->s))return;
 auto*rt=x->r->roomRuntime();x->epoch=rt->prioritySourceEpoch();x->transport=rt->xufaTransportEpoch();x->serial=x->s->serial;
 x->basis=x->mode==LiteralTen||(x->mode!=Preserve&&qualified(x->s,x->s->grants.value(x->mode==CopyCell?x->source:x->target),x->p,*x->q));
 x->s->touched.insert(x->target);x->armed=true;
}
void AIRound54PriorityOperation::afterWrite(){auto*x=d.get();if(!x->armed||!x->exactWrite||x->invalid||!x->s||!current(*x->r,x->p,*x->q)||!roots(x->s))return;
 auto*rt=x->r->roomRuntime();if(!x->epoch||x->epoch!=rt->prioritySourceEpoch()||!x->transport||x->transport!=rt->xufaTransportEpoch()||x->serial==quint64(-1)||x->s->serial!=x->serial+1)return;
 // Only still-qualified OTHER cells survive this proved single exact-target
 // plain write. Older serials/revoked epochs are never revived or restamped.
 for(auto i=x->s->grants.begin();i!=x->s->grants.end();++i){auto&g=i.value();
  if(i.key()!=x->target&&g.owner==x->p&&g.decision==x->q->decisionId&&g.ledger.toStrongRef()==x->q->conversionLedger
   &&g.epoch==x->epoch&&g.transport==x->transport&&g.serial==x->serial)g.serial=x->s->serial;
 }
 if(x->basis)x->s->grants[x->target]={x->p,x->q->decisionId,x->epoch,x->transport,x->s->serial,x->q->conversionLedger};x->armed=false;
}
void clearAIRound54Priority(Room&r){delete state(r);}
QJsonObject readAIRound54Priority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=2||v.value("op")!="source_priority_read"||!v.value("key").isString()||!current(r,p,q))return fail();
 auto*s=state(r);const QString k=v.value("key").toString();
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();if(!L||!lua_checkstack(L,64))return fail();Stack z(L);
 const Grant grant=s?s->grants.value(k):Grant{};
 if(!s||!s->touched.contains(k)||!roots(s)||!qualified(s,grant,p,q)){
  auto initial=readAIRound56InitialPriority(r,L,k);
  return current(r,p,q)?initial:fail();
 }
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"ai_use_priority");const auto bytes=k.toUtf8();AIRound45Inspect::field(L,-1,bytes.constData());
 QJsonObject result{{"known",true},{"present",!lua_isnil(L,-1)}};
 if(lua_isnil(L,-1)){}else if(lua_type(L,-1)==LUA_TBOOLEAN)result.insert("value",bool(lua_toboolean(L,-1)));
 else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(double(lua_tonumber(L,-1))))result.insert("value",double(lua_tonumber(L,-1)));
 else return fail();
 if(!current(r,p,q)||!roots(s)||!qualified(s,grant,p,q))return fail();return result;
}
