#include "ai-strategy-round45-natural-event.h"
#include "room.h"
#include "serverplayer.h"
#include "ai.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "structs.h"
#include "lua.hpp"
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QPointer>
#include <QVariant>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
thread_local QHash<Room *,QList<const AIRound45NaturalFilterDispatch *>> dispatches;
thread_local QHash<Room *,QList<const AIRound45NaturalFilterEntry *>> entries;
bool method(lua_State *L,int index) {
 if(lua_type(L,index)!=LUA_TSTRING)return false;
 size_t n=0;const char *s=lua_tolstring(L,index,&n);
 return n==11&&std::memcmp(s,"filterEvent",11)==0;
}
bool faqiAG(const QVariant &v) {
 if(v.metaType().id()!=QMetaType::QString)return false;
 const QString s=v.toString(),prefix=QStringLiteral("AGChosen:faqi:");
 if(!s.startsWith(prefix))return false;
 const QString number=s.mid(prefix.size());bool ok=false;int id=number.toInt(&ok);
 return ok&&id>=-1&&number==QString::number(id);
}
template<class T>void removeFrame(QHash<Room *,QList<const T *>> &map,Room *room,const T *frame) {
 auto it=map.find(room);if(it==map.end())return;
 it->removeAll(frame);if(it->isEmpty())map.erase(it);
}
}
struct AIRound45NaturalFilterDispatch::Data {
 Room *room=nullptr;AI *ai=nullptr;QPointer<ServerPlayer> actor;
 int event=0;const QVariant *original=nullptr;
};
struct AIRound45NaturalFilterEntry::Data {
 Room *room=nullptr;AI *ai=nullptr;QPointer<ServerPlayer> actor;
 int event=0,top=0;lua_State *state=nullptr;const QVariant *copy=nullptr;
 const AIRound45NaturalFilterDispatch *dispatch=nullptr;
 const void *function=nullptr,*playerArgument=nullptr,*dataArgument=nullptr,*callFrame=nullptr;
 bool valid=false;
};
struct AIRound45NaturalEventAccess {
 static AIRound45NaturalFilterDispatch::Data *dispatch(Room *r) {
  auto it=dispatches.constFind(r);
  return it==dispatches.cend()||it->isEmpty()?nullptr:it->last()->d.get();
 }
 static AIRound45NaturalFilterEntry::Data *current(Room &r,lua_State *L,int count) {
  auto it=entries.constFind(&r);if(count!=4||!L||it==entries.cend()||it->isEmpty())return nullptr;
  auto *d=it->last()->d.get();auto di=dispatches.constFind(&r);
  if(!d->valid||d->state!=L||!d->actor||d->actor->getRoom()!=&r
     ||di==dispatches.cend()||di->isEmpty()||di->last()!=d->dispatch
     ||L->ci!=d->callFrame||lua_gettop(L)<d->top)return nullptr;
  const int f=d->top-4;
  if(!lua_isfunction(L,f)||lua_topointer(L,f)!=d->function||!method(L,f+1)
     ||!lua_isinteger(L,f+2)||lua_tointeger(L,f+2)!=d->event
     ||lua_type(L,f+3)!=LUA_TUSERDATA||lua_topointer(L,f+3)!=d->playerArgument
     ||lua_type(L,f+4)!=LUA_TUSERDATA||lua_topointer(L,f+4)!=d->dataArgument)return nullptr;
  return d;
 }
};
AIRound45NaturalFilterDispatch::AIRound45NaturalFilterDispatch(Room&r,AI*ai,ServerPlayer*p,int e,const QVariant&v):d(new Data) {
 d->room=&r;d->ai=ai;d->actor=p;d->event=e;d->original=&v;dispatches[&r].append(this);
}
AIRound45NaturalFilterDispatch::~AIRound45NaturalFilterDispatch(){removeFrame(dispatches,d->room,this);}
AIRound45NaturalFilterEntry::AIRound45NaturalFilterEntry(Room&r,AI*ai,ServerPlayer*p,int e,const QVariant&original,const QVariant&copy,lua_State*L):d(new Data) {
 d->room=&r;d->ai=ai;d->actor=p;d->event=e;d->copy=&copy;d->state=L;entries[&r].append(this);
 auto *outer=AIRound45NaturalEventAccess::dispatch(&r);
 if(!L||!outer||outer->ai!=ai||!ai||outer->actor!=p||!p||p->getRoom()!=&r
    ||outer->event!=e||outer->original!=&original||r.getLuaState()!=L)return;
 d->dispatch=dispatches.value(&r).last();d->callFrame=L->ci;d->top=lua_gettop(L);const int f=d->top-4;
 if(f<1||!lua_isfunction(L,f)||!method(L,f+1)||!lua_isinteger(L,f+2)
    ||lua_tointeger(L,f+2)!=e||lua_type(L,f+3)!=LUA_TUSERDATA||lua_type(L,f+4)!=LUA_TUSERDATA)return;
 d->function=lua_topointer(L,f);d->playerArgument=lua_topointer(L,f+3);d->dataArgument=lua_topointer(L,f+4);d->valid=true;
}
AIRound45NaturalFilterEntry::~AIRound45NaturalFilterEntry(){removeFrame(entries,d->room,this);}
bool readAIRound45NaturalFilterEntry(Room&r,lua_State*L,int count,AIRound45NaturalFilterProof*out) {
 if(out)*out={};auto*d=AIRound45NaturalEventAccess::current(r,L,count);if(!d||!out)return false;
 out->event=d->event;out->faqiAGFormat=d->event==int(ChoiceMade)&&faqiAG(*d->copy);return true;
}
bool matchesAIRound45NaturalFilterPayload(Room&r,lua_State*L,int count,const QVariant*v,ServerPlayer*p) {
 auto*d=AIRound45NaturalEventAccess::current(r,L,count);return d&&d->copy==v&&d->actor==p;
}

bool inspectAIRound45NaturalFilterEntry(Room&r,lua_State*L,int count,
 const std::function<bool(int,ServerPlayer *,const QVariant &)> &inspector) {
 auto*d=AIRound45NaturalEventAccess::current(r,L,count);
 return d&&inspector&&inspector(d->event,d->actor,*d->copy);
}

bool pushAIRound45NaturalFilterSelf(Room&r,lua_State*L,int count) {
 auto*d=AIRound45NaturalEventAccess::current(r,L,count);if(!d)return false;
 const ServerPlayer*owner=nullptr;
 for(const auto*p:r.getPlayers())if(p&&p->getSmartAI()==d->ai){if(owner)return false;owner=p;}
 return owner&&pushAIRound45ActualAISelf(r,owner,L);
}

bool matchesAIRound47NativeFilterDispatch(Room&r,lua_State*L,int count,int e,
 ServerPlayer*p,const QVariant*original){
 auto*d=AIRound45NaturalEventAccess::current(r,L,count);auto*dispatch=AIRound45NaturalEventAccess::dispatch(&r);
 return d&&dispatch&&d->event==e&&d->actor==p&&dispatch->event==e&&dispatch->actor==p
   &&dispatch->original==original;
}

// The scope caller is compiled LuaAI::filterEvent before pushCallback. The
// outer dispatch records identity, not payload values or private skill state.
bool matchesAIRound56NaturalFilterPreEntry(Room&r,AI*ai,ServerPlayer*p,int event,
 const QVariant*original,lua_State*L) noexcept {
 auto*d=AIRound45NaturalEventAccess::dispatch(&r);
 return d&&ai&&p&&p->getRoom()==&r&&d->room==&r&&d->ai==ai
  &&d->actor==p&&d->event==event&&d->original==original&&r.getLuaState()==L;
}
