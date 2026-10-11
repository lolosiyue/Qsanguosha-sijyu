#include "ai-strategy-round54-priority.h"
#include "ai-strategy-round54-shebian.h"
#include "ai-strategy-round53-pingjian.h"
#include "ai-strategy-round47-xufa-event-scope.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round53-globals.h"
#include "ai-strategy-round53-priority.h"
#include "ai-strategy-round53-overseas-prefix.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai-strategy-round37-response-init.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QPointer>
#include <QHash>
#include <cmath>
#include <cstring>
#include <limits>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Row {QPointer<ServerPlayer>writer;QJsonObject value;bool known=false,diverged=false;quint64 epoch=0;quint64 xufaTransportEpoch=0,xufaWriteSerial=0,xufaGeneration=0;};
struct Continuity:QObject {
 explicit Continuity(Room&r):QObject(&r){}
 quint64 epoch=1;bool exhausted=false;
 quint64 serial=0;bool legacyReady=false,isolatedReady=false,entered=false;
 QString lastViewer;QHash<QString,Row>rows;
 bool*xufaWriteGuard=nullptr;quint64 nextXufaWriteSerial=1,xufaRootGeneration=0;
};
Continuity*ledger(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c))return s;return new Continuity(r);}
const QStringList keys{"xuhe","olxuhe","xufa","olshebian_from","olshebian_to","olshebian"};
QString registry(const QString&k){return k=="olshebian"?"ai_skill_cardchosen":k.startsWith("olshebian_")?"ai_skill_playerchosen":"ai_skill_choice";}
int domain(const QString&k){return k=="olshebian"?4:k.startsWith("olshebian_")?3:2;}
void forget(Row&r){r.known=false;r.value={};r.writer=nullptr;r.diverged=false;}
void hard(Continuity*s){if(s->xufaWriteGuard)*s->xufaWriteGuard=true;for(auto i=s->rows.begin();i!=s->rows.end();++i)forget(*i);}
thread_local AIChoiceContinuityInquiry*active=nullptr;
QJsonObject unknown(){return{{"known",false}};}
QString questionKind(const AIRequest&r){switch(r.kind){case AIRequest::SkillInvoke:return"invoke";case AIRequest::Choice:return"choice";case AIRequest::Activate:return"activate";case AIRequest::PlayerChosen:return"player_chosen";case AIRequest::CardChosen:return"card_chosen";default:return{};}}
bool eq(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==strlen(k)&&!memcmp(e.string_key,k,e.string_key_length);}
bool admittedWrite(const AIRequest&r,const ServerPlayer*p,const QString&key){
 if(key=="xuhe"||key=="olxuhe")return r.kind==AIRequest::SkillInvoke
  &&(r.choiceOptions.reason==key||(key=="xuhe"&&r.choiceOptions.reason=="olxuhe"))
  &&p->hasSkill(r.choiceOptions.reason);
 if(key=="xufa")return r.kind==AIRequest::Activate&&p->hasSkill("xufa");
 return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="olshebian"&&p->hasSkill("olshebian");
}
bool admittedRead(const AIRequest&r,const QString&key){
 if(key=="xuhe"&&(r.kind==AIRequest::SkillInvoke)&&(r.choiceOptions.reason=="xuhe"||r.choiceOptions.reason=="olxuhe"))return true;
 if(key=="olshebian")return r.kind==AIRequest::CardChosen&&r.choiceOptions.reason==key;
 if(key.startsWith("olshebian_"))return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason==key;
 return r.kind==AIRequest::Choice&&r.choiceOptions.reason==key;
}
// Bookkeeping may run with the isolated VM bound. Do not call state() here:
// it deliberately rejects a noncurrent game VM. No Lua stack is accessed.
bool liveXufaGame(Room&r){
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing())return false;
 const auto&vm=rt->lua();return vm.isCurrentThreadOwner()&&vm.lifecycle()==LuaRuntime::Lifecycle::Running&&vm.rawState();
}
QJsonObject read(Continuity*s,ServerPlayer*p,const QString&key){
 auto i=s->rows.find(key);if(i==s->rows.end())return unknown();auto&r=*i;
 if(s->exhausted||r.epoch!=s->epoch||!r.known||(r.writer&&r.writer!=p))return unknown();
 if(key=="xufa"){
  if(!p||!p->getRoom()||!s->nextXufaWriteSerial
     ||(r.value.value("present")==true&&!r.xufaWriteSerial))return unknown();
  auto*rt=p->getRoom()->roomRuntime();
  if(!liveXufaGame(*p->getRoom())||!r.xufaGeneration
     ||r.xufaGeneration!=s->xufaRootGeneration||r.xufaGeneration!=rt->lua().generation()
     ||!r.xufaTransportEpoch||r.xufaTransportEpoch!=rt->xufaTransportEpoch())return unknown();
 }
 return r.value;
}
bool validValue(Room&room,const AIRequest&r,ServerPlayer*p,const QString&key,const QJsonObject&v){
 if(v.value("known")!=true||!v.value("present").isBool())return false;
 if(!v.value("present").toBool())return false; // fixed reached writers never assign nil; copy/consume do so internally
 if(v.size()!=3)return false;
 auto value=v.value("value");
 if(key=="xuhe"||key=="olxuhe")return value.isString()&&(value=="draw"||value=="discard");
 if(key=="xufa"){
  if(!value.isString())return false;QList<int>ids=p->handCards();for(auto*c:p->getEquips())ids<<c->getEffectiveId();ids<<p->getPile("xufa");
  for(int id:ids){auto*c=room.getCard(id);if(c&&c->isNDTrick()&&c->objectName()==value.toString())return true;}return false;
 }
 if(key!="olshebian")return value.isString()&&room.findPlayerByObjectName(value.toString(),true);
 if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()!=value.toInt(-1)||value.toInt(-1)<0)return false;
 const int id=value.toInt();
 // MoveField(nil,"e") supplies an actual public equip, never a hidden face.
 for(auto*owner:room.getAllPlayers(true))for(auto*c:owner->getEquips())if(c->getEffectiveId()==id)return true;
 return false;
}
// R46: the two fixed Xufa producer statements assign the actual game registry.
// This is independent of whether later native entries have a nonwriter proof.
// In particular, a successful write is NOT a lease through an unknown entry.
struct XufaGlobalWrite {
 Continuity*s;Room*room;ServerPlayer*owner;const AIRequest*request;
 const void*globals=nullptr;const void*sgs=nullptr;const void*registry=nullptr;
 bool interrupted=false,armed=false;quint64 transportEpoch=0,generation=0;lua_State*state=nullptr;
};
bool xufaCurrent(const XufaGlobalWrite&w){
 return w.owner&&w.owner->getRoom()==w.room&&w.request->kind==AIRequest::Activate
  &&w.request->viewerObjectName==w.owner->objectName()
  &&w.request->worldView.self.objectName==w.owner->objectName()
  &&w.request->stateRevision==w.room->roomRuntime()->stateRevision()
  &&!w.room->roomRuntime()->isClosing()&&w.s->legacyReady&&w.s->isolatedReady
  &&w.transportEpoch&&w.transportEpoch==w.room->roomRuntime()->xufaTransportEpoch()
  &&w.state&&w.state==w.room->roomRuntime()->lua().state()
  &&w.generation&&w.generation==w.s->xufaRootGeneration
  &&w.generation==w.room->roomRuntime()->lua().generation();
}
bool xufaRoots(lua_State*L,const XufaGlobalWrite&w){
 const int top=lua_gettop(L);
 AIRound45Inspect::globals(L);const bool globals=lua_topointer(L,-1)==w.globals;
 AIRound45Inspect::field(L,-1,"sgs");const bool ns=lua_istable(L,-1)&&lua_topointer(L,-1)==w.sgs;
 AIRound45Inspect::field(L,-1,"ai_skill_choice");auto*t=AIRound45Inspect::table(L,-1);
 const bool registry=t&&!t->metatable&&lua_topointer(L,-1)==w.registry;
 lua_settop(L,top);return globals&&ns&&registry;
}
int assignXufaGlobal(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"Xufa scalar assignment stack unavailable");
 auto*w=static_cast<XufaGlobalWrite*>(lua_touserdata(L,3));
 if(!w||lua_type(L,2)!=LUA_TSTRING||!xufaCurrent(*w)||!xufaRoots(L,*w)
  ||lua_topointer(L,1)!=w->registry)return luaL_error(L,"Xufa scalar assignment context changed");
 // The outer protected-entry observer has already invalidated old observations.
 // Arm only now. Any reentry/async/structural failure during the write prevents
 // publishing a new known row, even if its final scalar happens to equal ours.
 w->armed=true;w->s->xufaWriteGuard=&w->interrupted;
 lua_pushvalue(L,2);lua_setfield(L,1,"xufa"); // source setfield, not a shadow/raw override
 return 0;
}
bool writeActualXufaGlobal(Room&room,ServerPlayer*p,const AIRequest&r,Continuity*s,const QString&value,quint64&transportStamp,quint64&generationStamp){
 transportStamp=0;generationStamp=0;
 auto*rt=room.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,512))return false;
 const int top=lua_gettop(L);struct Restore {lua_State*L;int top;Continuity*s;bool*previous;
  ~Restore(){s->xufaWriteGuard=previous;lua_settop(L,top);}} restore{L,top,s,s->xufaWriteGuard};
 XufaGlobalWrite w;w.s=s;w.room=&room;w.owner=p;w.request=&r;w.transportEpoch=rt->xufaTransportEpoch();w.generation=rt->lua().generation();w.state=L;
 AIRound45Inspect::globals(L);w.globals=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;w.sgs=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"ai_skill_choice");auto*t=AIRound45Inspect::table(L,-1);
 if(!t||t->metatable)return false;w.registry=lua_topointer(L,-1);const int registryIndex=lua_absindex(L,-1);
 lua_pushcfunction(L,assignXufaGlobal);lua_pushvalue(L,registryIndex);
 const auto bytes=value.toUtf8();lua_pushlstring(L,bytes.constData(),size_t(bytes.size()));lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,3,0,0)!=LUA_OK)return false;
 if(!w.armed||w.interrupted||!xufaCurrent(w)||!xufaRoots(L,w))return false;
 AIRound45Inspect::field(L,registryIndex,"xufa");size_t n=0;const char*actual=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&n):nullptr;
 if(!actual||n!=size_t(bytes.size())||std::memcmp(actual,bytes.constData(),n)||!xufaCurrent(w))return false;
 transportStamp=w.transportEpoch;generationStamp=w.generation;return true;
}
bool actualXufaGlobalMatches(Room&room,const QJsonObject&row){
 if(row.value("known")!=true||row.value("present")!=true||!row.value("value").isString())return false;
 auto*rt=room.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,64))return false;const int top=lua_gettop(L);
 struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 AIRound45Inspect::field(L,-1,"ai_skill_choice");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;
 AIRound45Inspect::field(L,-1,"xufa");size_t n=0;const char*actual=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&n):nullptr;
 const auto expected=row.value("value").toString().toUtf8();return actual&&n==size_t(expected.size())&&!std::memcmp(actual,expected.constData(),n);
}
}
AIChoiceContinuityInquiry::AIChoiceContinuityInquiry(Room&r,const QString&k,ServerPlayer*p,const QString&why)
 :room(&r),kind(k),reason(why),viewer(p),previous(active){enterAIRound54ShebianInquiry(r,p,k,why);serial=++ledger(r)->serial;active=this;}
AIChoiceContinuityInquiry::~AIChoiceContinuityInquiry(){active=previous;}
void invalidateAIChoiceContinuity(Room&r){auto*s=ledger(r);s->entered=true;hard(s);}

ServerPlayer*currentAIRound47XufaContinuityWriter(Room&r) noexcept {
 auto*rt=r.roomRuntime();if(!liveXufaGame(r))return nullptr;
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c)){
  if(s->exhausted||!s->legacyReady||!s->isolatedReady||s->xufaWriteGuard)return nullptr;
  auto i=s->rows.constFind(QStringLiteral("xufa"));
  return i!=s->rows.cend()&&i->known&&i->epoch==s->epoch&&!i->diverged&&i->writer
   &&s->nextXufaWriteSerial&&i->xufaWriteSerial
   &&i->xufaGeneration&&i->xufaGeneration==s->xufaRootGeneration&&i->xufaGeneration==rt->lua().generation()
   &&i->xufaTransportEpoch&&i->xufaTransportEpoch==r.roomRuntime()->xufaTransportEpoch()
   &&i->writer->getRoom()==&r?i->writer.data():nullptr;
 }return nullptr;
}
quint64 currentAIRound47XufaWriteSerial(Room&r) noexcept {
 if(!currentAIRound47XufaContinuityWriter(r))return 0;
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c))return s->rows.constFind(QStringLiteral("xufa"))->xufaWriteSerial;
 return 0;
}
void interruptAIRound47XufaContinuity(Room&r) {
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c)){
  if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
  auto i=s->rows.find(QStringLiteral("xufa"));if(i!=s->rows.end())forget(*i);return;
 }
}
void interruptAIChoiceContinuityExceptXufa(Room&r){invalidateAIRound53Goodatwep(r);
 auto*s=ledger(r);s->entered=true;if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
 for(auto i=s->rows.begin();i!=s->rows.end();++i)if(i.key()!=QStringLiteral("xufa"))forget(*i);
}

void interruptAIChoiceContinuity(Room&r){invalidateAIRound53Goodatwep(r);auto*s=ledger(r);s->entered=true;hard(s);}
void interruptAIChoiceContinuityAsync(Room&r)noexcept{
 invalidateAIRound53Goodatwep(r);
 // Lookup only. No QObject creation, container detach, value/lease destruction,
 // Lua access or notification. A fresh observed write receives the new epoch;
 // other owners' old rows cannot be resurrected by that write.
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c)){
  s->entered=true;
  if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
  if(s->epoch==std::numeric_limits<quint64>::max())s->exhausted=true;
  else ++s->epoch;
  return;
 }
}
void beginAIChoiceContinuityPolicy(Room&r,const QString&viewer){
 enterAIRound53Goodatwep(r,viewer);
 auto*s=ledger(r);s->entered=true;
 if(pendingAIRound47XufaEvent(r)){interruptAIChoiceContinuityExceptXufa(r);return;}
 // Both isolated decisions and events enter here before executing policy.
 // A foreign event must not reveal its conditional registry access pattern.
 if(viewer.isEmpty()||(!s->lastViewer.isEmpty()&&s->lastViewer!=viewer))hard(s);
 s->lastViewer=viewer;
}
void observeAIChoiceContinuityEvent(Room&r,const QString&viewer,const QString&actor){
 invalidateAIRound53Goodatwep(r); // fixed source events have no tag-write authority
 if(pendingAIRound47XufaEvent(r)){interruptAIChoiceContinuityExceptXufa(r);return;}
 beginAIChoiceContinuityPolicy(r,viewer);
 if(viewer.isEmpty()||actor!=viewer)interruptAIChoiceContinuity(r);
}
void initializeAIChoiceContinuityRoots(Room&r,bool isolated,bool valid,const QJsonObject&initialNil,quint64 xufaTransportStamp,quint64 xufaGenerationStamp){
 auto*s=ledger(r);if(isolated){s->isolatedReady=valid;return;}
 if(s->legacyReady)return;s->legacyReady=valid;
 s->xufaRootGeneration=valid&&xufaGenerationStamp&&liveXufaGame(r)
   &&xufaGenerationStamp==r.roomRuntime()->lua().generation()?xufaGenerationStamp:0;
 if(!valid||s->entered)return;
 for(const auto&k:keys)if(initialNil.value(k)==true){
  s->rows[k]={nullptr,{{"known",true},{"present",false}},true,false,s->epoch};
  if(k=="xufa"){
   s->rows[k].xufaTransportEpoch=xufaTransportStamp;
   s->rows[k].xufaGeneration=xufaGenerationStamp;
   if(!s->xufaRootGeneration||!xufaTransportStamp||xufaTransportStamp!=r.roomRuntime()->xufaTransportEpoch())forget(s->rows[k]);
  }
 }
}
void observeAIChoiceContinuityTable(Room&r,int d,bool isolated,const lua_TableObservation&e)noexcept{
 try{
  auto*s=ledger(r);
  if(d<2){
   const bool affected=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE
    ||(e.event==LUA_TABLE_OBSERVER_WRITE&&(d==0?eq(e,"sgs"):(eq(e,"ai_skill_choice")||eq(e,"ai_skill_playerchosen")||eq(e,"ai_skill_cardchosen"))));
   if(affected){if(isolated)s->isolatedReady=false;else s->legacyReady=false;hard(s);}return;
  }
  const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
  if(structural){if(isolated)s->isolatedReady=false;else s->legacyReady=false;}
  for(const auto&k:keys){if(domain(k)!=d)continue;const auto bytes=k.toUtf8();
   const bool matched=eq(e,bytes.constData());auto i=s->rows.find(k);if(i==s->rows.end())continue;
   if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&matched)
    ||(!isolated&&i->diverged&&((e.event==LUA_TABLE_OBSERVER_READ&&matched)||e.event==LUA_TABLE_OBSERVER_ENUMERATE)))forget(*i);
  }
 }catch(...){/* No exception crosses Lua's observer boundary. */try{auto*s=ledger(r);s->legacyReady=false;s->isolatedReady=false;hard(s);}catch(...){}}
}
void observeAIChoiceContinuityDecision(Room&r,const AIRequest&q){auto*s=ledger(r);s->entered=true;
 if(pendingAIRound47XufaEvent(r))hard(s); // Reentrant decision never inherits event custody.
 if(!s->lastViewer.isEmpty()&&s->lastViewer!=q.viewerObjectName)hard(s);s->lastViewer=q.viewerObjectName;
 if(!active||active->room!=&r||!active->viewer||q.viewerObjectName!=active->viewer->objectName()
  ||questionKind(q)!=active->kind||(active->kind!="activate"&&q.choiceOptions.reason!=active->reason))hard(s);
}
QJsonObject queryAIChoiceContinuity(Room&room,ServerPlayer*p,const AIRequest&r,const QJsonObject&q){
 if(q.value("key")=="olshebian"||q.value("key")=="olshebian_from"||q.value("key")=="olshebian_to")return queryAIRound54Shebian(room,p,r,q);
 if(q.value("op")=="pingjian_catalog")return queryAIRound53PingjianCatalog(room,p,r,q);
 if(q.value("op")=="source_global")return writeAIRound53HCGlobal(room,p,r,q);
 if(q.value("op")=="source_priority")return copyAIRound53SourcePriority(room,p,r,q);
 if(q.value("op")=="source_priority_read")return readAIRound54Priority(room,p,r,q);
 if(q.value("op")=="source_priority_increment")return incrementAIRound53SourcePriority(room,p,r,q);
 // Immediate named source effects have their own exact current-question gate.
 // They neither consume nor grant a retained continuity observation.
 if(q.value("op")=="source_prefix")return writeAIRound53OverseasPrefix(room,p,r,q);
 auto*s=ledger(room);
 if(pendingAIRound47XufaEvent(room))return unknown(); // No event-time read/write grant.
 if(!validateAIRound41ChoiceProxies(room))return unknown();
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||r.viewerObjectName!=p->objectName()
  ||r.worldView.self.objectName!=p->objectName()||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||!active||active->room!=&room||active->viewer!=p||active->serial!=s->serial
  ||questionKind(r)!=active->kind||(active->kind!="activate"&&r.choiceOptions.reason!=active->reason)
  ||s->exhausted||!s->legacyReady||!s->isolatedReady)return unknown();
 const QString op=q.value("op").toString(),key=q.value("key").toString();if(!keys.contains(key))return unknown();
 if(op=="read"||op=="consume"){
  if(q.size()!=2||!admittedRead(r,key)||(op=="consume"&&key!="olshebian"))return unknown();
  auto out=read(s,p,key);
  if(key=="xufa"){
   if(out.value("present")==true&&!actualXufaGlobalMatches(room,out))return unknown();
   // The retained stamp was captured BEFORE the source write, never refreshed
   // by a read. A concurrent disconnect cannot be blessed by a later load.
   if(read(s,p,key).value("known")!=true)return unknown();
  }
  if(op=="consume"&&out.value("known")==true&&out.value("present")==true)
   s->rows[key]={p,{{"known",true},{"present",false}},true,true,s->epoch};
  return out;
 }
 if(op=="copy"){
  if(q.size()!=3||key!="olxuhe"||q.value("from")!="xuhe"||!admittedWrite(r,p,key))return unknown();
  auto value=read(s,p,"xuhe");if(value.value("known")!=true)return unknown();
  s->rows[key]={p,value,true,true,s->epoch};return{{"known",true},{"recorded",true}};
 }
 if(op!="record"||q.size()!=3||!q.value("row").isObject()||!admittedWrite(r,p,key))return unknown();
 const auto value=q.value("row").toObject();if(!validValue(room,r,p,key,value))return unknown();
 if(key=="xufa"){
  quint64 transportStamp=0,generationStamp=0;
  if(!writeActualXufaGlobal(room,p,r,s,value.value("value").toString(),transportStamp,generationStamp))return unknown();
  // Zero is an exhausted tombstone. Even the same owner/scalar after a clear
  // is a NEW reached write and must not revive an older native scope.
  if(!s->nextXufaWriteSerial)return unknown();
  const quint64 writeSerial=s->nextXufaWriteSerial++;
  if(!s->nextXufaWriteSerial){forget(s->rows[key]);return unknown();}
  s->rows[key]={p,value,true,false,s->epoch,transportStamp,writeSerial,generationStamp};
  if(read(s,p,key).value("known")!=true)return unknown();
 }else s->rows[key]={p,value,true,true,s->epoch};
 return{{"known",true},{"recorded",true}};
}
