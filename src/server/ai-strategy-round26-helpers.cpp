#include "ai-strategy-round26-helpers.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-readonly-card-store.h"
#include "ai-strategy-public-disclosure.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QMutex>
#include <QMutexLocker>
namespace {
struct Source { QString source, destination; bool foreignFrom=false; };
bool current(const AIRequest &r, const ServerPlayer *p) {
 return p && p->getRoom() && r.viewerObjectName == p->objectName()
  && r.stateRevision == p->getRoom()->roomRuntime()->stateRevision();
}
Source source(const AIRequest &r, const QString &key) {
 if (r.kind == AIRequest::PlayersChosen && r.choiceOptions.reason == key) {
  if (key == "mobilebenxi") return {"mobilebenxiUse", "JincanmouData"};
  if (key == "fanyin") return {"fanyinData", "yb_zhuzhan2_data"};
 }
 if (key == "@@jieyingh" && r.kind == AIRequest::UseCard && r.pattern == key
     && r.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
  return {"jieyinghData", "yb_zhuzhan2_data"};
 if (r.kind == AIRequest::PlayerChosen) {
  const auto reason=r.choiceOptions.reason;
  if(key=="ov_zenhui" && reason==key)return {"ov_zenhuiData","yb_zhuzhan2_data",true};
  if(key=="ov_yizhu" && reason==key)return {"ov_yizhuData","yb_zhuzhan2_data",true};
  if(reason=="ov_qirang" && key=="ov_qirang:yb_fujia2_data")return {"ov_qirangData","yb_fujia2_data",true};
  if(reason=="ov_qirang" && key=="ov_qirang:yb_zhuzhan2_data")return {"ov_qirangData","yb_zhuzhan2_data",true};
 }
 if(key=="ov_ronggui" && r.kind==AIRequest::RespondCard && r.choiceOptions.question=="askForCard"
    && r.pattern=="BasicCard" && r.prompt.startsWith("ov_ronggui0:"))
  return {"@incoming","JincanmouData",true};
 return {};
}
bool slot(const QString &s) { return s == "JincanmouData" || s == "yb_zhuzhan2_data" || s == "yb_fujia2_data"; }
bool reader(const AIRequest &r,const QString &s) {
 if (s == "JincanmouData") return r.kind == AIRequest::PlayerChosen && (r.choiceOptions.reason == "jincanmou" || r.choiceOptions.reason == "ov_ronggui");
 if(s=="yb_fujia2_data")return r.kind==AIRequest::PlayerChosen && r.choiceOptions.reason=="yb_fujia2";
 return s == "yb_zhuzhan2_data" && (((r.kind == AIRequest::PlayerChosen || r.kind == AIRequest::Discard) && r.choiceOptions.reason == "yb_zhuzhan2")
  || (r.kind == AIRequest::PlayerChosen && r.choiceOptions.reason == "ov_lihuo"));
}
bool disclosed(const AIRequest &r, ServerPlayer *p, int id) {
 for (const auto &c:r.worldView.handCards) if (c.effectiveId==id) return true;
 for (const auto &c:r.worldView.discardPile) if (c.effectiveId==id) return true;
 for (const auto &c:r.choiceOptions.cards) if (c.effectiveId==id) return true;
 auto players=r.worldView.players; players.prepend(r.worldView.self);
 for (const auto &v:players) {
  for (const auto &c:v.equips) if(c.effectiveId==id)return true;
  for (const auto &c:v.knownCards) if(c.effectiveId==id)return true;
  for (const auto &c:v.judgingArea) if(c.effectiveId==id)return true;
  for (const auto &pile:v.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 }
 return readAIStrategyPublicDisclosure(*p->getRoom(),p,id).value("known")==true;
}
bool roster(Room &room,const Player *p) {
 for (const Player *v:room.getAllPlayers(true)) if (v==p) return true;
 return false;
}
bool observed(const AIRequest&r,ServerPlayer*p,const CardUseStruct&u,bool foreignFrom=false) {
 if (!u.card || !u.from || !roster(*p->getRoom(),u.from) || (!foreignFrom && u.from!=p)) return false;
 for (const auto *t:u.to) if (!t || !roster(*p->getRoom(),t)) return false;
 // Only material IDs are inspected before disclosure, never derived face.
 if (!u.card->isVirtualCard() && !disclosed(r,p,u.card->getEffectiveId())) return false;
 for(int id:u.card->getSubcards())if(!disclosed(r,p,id))return false;
 return true;
}
QJsonObject useJson(const Card *card,const QString &from,const QStringList &to,const AIRound26UseProfile &profile) {
 return {{"card",profile(card)},{"from",from},{"to",QJsonArray::fromStringList(to)}};
}
class DelegateMetadata final : public AIRoomLedger {
public: DelegateMetadata(Room &room,bool value):AIRoomLedger(room),knownNil(value){}
 const bool knownNil;
};
const DelegateMetadata *metadata(Room &room) {
 for(auto*c:room.children())if(auto*m=dynamic_cast<DelegateMetadata*>(c))return m;
 return nullptr;
}
struct Observation {
 QPointer<ServerPlayer> viewer;
 AIReadonlyCardLease lease;
 QList<int> materials;
 int physical=-1;
 bool constructedLihuo=false;
 QString from;
 QStringList to;
};
QHash<Room *,QHash<const Player *,QHash<QString,Observation>>> observations;
QSet<Room *> connected;
struct Input { Observation observation; quint64 decision=0, revision=0; };
QHash<Room *,QHash<const Player *,QHash<QString,Input>>> inputs;
QRecursiveMutex observationMutex;
void connectRoom(Room *room) {
 if(connected.contains(room))return;
 connected.insert(room);
 QObject::connect(room,&QObject::destroyed,[room]{
  QMutexLocker lock(&observationMutex);
  observations.remove(room);inputs.remove(room);connected.remove(room);
 });
}
Observation capture(ServerPlayer*p,const CardUseStruct&u) {
 Observation o;o.viewer=p;o.lease=AIOwnerReadonlyCardStore::observe(u.card);
 o.from=u.from->objectName();o.materials=u.card->getSubcards();
 o.physical=u.card->isVirtualCard()?-1:u.card->getEffectiveId();
 for(const auto*t:u.to)o.to<<t->objectName();return o;
}
bool validObservation(const AIRequest&r,ServerPlayer*p,const Observation&o) {
 if(o.viewer!=p || !o.lease)return false;
 if(o.physical>=0&&!disclosed(r,p,o.physical))return false;
 for(int id:o.materials)if(!disclosed(r,p,id))return false;
 const Card*c=o.lease();if(!c||c->getSubcards()!=o.materials)return false;
 QStringList names=o.to;names<<o.from;
 for(const auto&name:names){bool found=false;for(const auto*v:p->getRoom()->getAllPlayers(true))if(v->objectName()==name)found=true;if(!found)return false;}
 return true;
}
}
void projectAIRound26Delegates(AIRequest&r,Room&room,ServerPlayer*p,const AIRound26UseProfile&profile,const QVariant&incoming) {
 if(!current(r,p)||p->getRoom()!=&room)return;
 // Capture only authenticated current native source uses, not a Lua-supplied face.
 QJsonObject overseas;
 for(const QString &k:{QString("ov_zenhui"),QString("ov_qirang:yb_fujia2_data"),QString("ov_qirang:yb_zhuzhan2_data"),QString("ov_yizhu"),QString("ov_ronggui")}) {
  const auto spec=source(r,k);if(spec.source.isEmpty())continue;
  QJsonObject row{{"source_key",k},{"destination",spec.destination},{"known",false}};
  const auto v=spec.source=="@incoming"?incoming:p->getTag(spec.source);
  if(v.canConvert<CardUseStruct>()) {
   const auto u=v.value<CardUseStruct>();
   if(observed(r,p,u,spec.foreignFrom)) {
    auto o=capture(p,u);
    if(o.lease&&o.lease()) {
     row.insert("known",true);row.insert("use",useJson(u.card,o.from,o.to,profile));
     QMutexLocker lock(&observationMutex);connectRoom(&room);
     inputs[&room][p].insert(k,Input{o,r.decisionId,r.stateRevision});
    }
   }
  }
  overseas.insert(k,row);
 }
 if(!overseas.isEmpty())r.choiceOptions.context.insert("overseas_delegate_uses",overseas);
 const QString key=r.kind==AIRequest::UseCard?r.pattern:r.choiceOptions.reason;
 const auto s=source(r,key);if(s.source.isEmpty())return;
 QJsonObject row{{"source_key",key},{"destination",s.destination},{"known",false}};
 const QVariant v=p->getTag(s.source);
 if(v.canConvert<CardUseStruct>()) {
  const auto u=v.value<CardUseStruct>();
  if(observed(r,p,u)) {
   QStringList to;for(const auto *t:u.to)to<<t->objectName();
   row.insert("known",true);row.insert("use",useJson(u.card,p->objectName(),to,profile));
  }
 } else if(!v.isValid() && key=="@@jieyingh") {row.insert("known",true);row.insert("use",false);}
 if(key=="mobilebenxi")row.insert("global_p_known_nil",metadata(room)&&metadata(room)->knownNil);
 r.choiceOptions.context.insert("nested_delegate",row);
}
bool recordAIRound26DelegateUse(const AIRequest&r,ServerPlayer*p,const QString&key,QString*error) {
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 if(!current(r,p))return fail("delegate observation requires current viewer request");
 const auto s=source(r,key);if(s.source.isEmpty())return fail("delegate source does not match current native question");
 const bool overseas=key.startsWith("ov_");
 const auto row=overseas?r.choiceOptions.context.value("overseas_delegate_uses").toObject().value(key).toObject():r.choiceOptions.context.value("nested_delegate").toObject();
 if(row.value("source_key")!=key||row.value("destination")!=s.destination||row.value("known")!=true||!row.value("use").isObject())return fail("delegate source was not lawfully projected");
 Observation o;
 if(overseas) {
  QMutexLocker lock(&observationMutex);
  const auto input=inputs.value(p->getRoom()).value(p).value(key);
  if(input.decision!=r.decisionId||input.revision!=r.stateRevision)return fail("delegate native source capture is stale");
  o=input.observation;
 } else {
  const auto v=p->getTag(s.source);if(!v.canConvert<CardUseStruct>())return fail("delegate source use unavailable");
  const auto u=v.value<CardUseStruct>();if(!observed(r,p,u))return fail("delegate source use is not currently disclosed");
  o=capture(p,u);
 }
 if(!validObservation(r,p,o))return fail("delegate observed card graph is no longer disclosed or live");
 // Tag-backed source setTag reads the current native tag at the reached write.
 // Decision/revision alone cannot prove a native same-revision replacement.
 // Ronggui instead consumes the immutable incoming native question capture.
 if(overseas && s.source!="@incoming") {
  const auto v=p->getTag(s.source);
  if(!v.canConvert<CardUseStruct>())return fail("delegate source tag was cleared or replaced");
  const auto u=v.value<CardUseStruct>();
  if(!observed(r,p,u,s.foreignFrom) || u.card!=o.lease() || u.from->objectName()!=o.from)
   return fail("delegate source tag card or actor changed after projection");
  QStringList targets;for(const auto*t:u.to)targets<<t->objectName();
  if(targets!=o.to)return fail("delegate source tag target order changed after projection");
 }
 {QMutexLocker lock(&observationMutex);Room*room=p->getRoom();connectRoom(room);observations[room][p].insert(s.destination,std::move(o));}
 if(error)error->clear();return true;
}
bool recordAIRound28LihuoDelegateUse(const AIRequest &r,ServerPlayer *p,const CardUseStruct &use,
                                    const AIReadonlyCardLease &lease,QString *error) {
 auto fail=[&](const char *why){if(error)*error=QString::fromLatin1(why);return false;};
 if(!current(r,p)||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="ov_lihuo"
    ||r.choiceOptions.question!="askForSkillInvoke"||!p->hasSkill("ov_lihuo")
    ||!lease||lease()!=use.card||use.from!=p||!use.card||use.card->objectName()!="fire_slash"
    ||use.card->getSkillName(false)!="mobilelihuo"||!observed(r,p,use))
  return fail("Lihuo constructed observation does not match the current owner inquiry");
 Observation value;value.viewer=p;value.lease=lease;value.materials=use.card->getSubcards();value.physical=-1;value.constructedLihuo=true;value.from=p->objectName();
 for(auto *target:use.to)value.to<<target->objectName();
 {QMutexLocker lock(&observationMutex);Room *room=p->getRoom();connectRoom(room);observations[room][p].insert("yb_zhuzhan2_data",value);}
 if(error)error->clear();return true;
}

QJsonObject restoreAIRound26DelegateUse(const AIRequest&r,ServerPlayer*p,const QString&destination,const AIRound26UseProfile&profile,const AIRound26UseProfile&constructedLihuoProfile) {
 QJsonObject out{{"present",false}};
 if(!current(r,p)||!slot(destination)||!reader(r,destination))return out;
 Observation o;
 {
  QMutexLocker lock(&observationMutex);
  const auto ri=observations.constFind(p->getRoom());if(ri==observations.cend())return out;
  const auto pi=ri->constFind(p);if(pi==ri->cend())return out;
  const auto i=pi->constFind(destination);if(i==pi->cend())return out;
  o=*i;
 }
 out.insert("present",true);out.insert("known",false);
 // Keep unknown in place on failure; falling back resurrects an older native tag.
 if(!validObservation(r,p,o))return out;
 if(o.constructedLihuo && (!constructedLihuoProfile || o.from!=p->objectName()
    || o.lease()->objectName()!="fire_slash" || o.lease()->getSkillName(false)!="mobilelihuo"))return out;
 out.insert("known",true);out.insert("use",useJson(o.lease(),o.from,o.to,o.constructedLihuo?constructedLihuoProfile:profile));return out;
}
void noteAIRound26DelegateTagWrite(const Player*p,const QString&key) {
 if(!p||!slot(key))return;
 QMutexLocker lock(&observationMutex);
 for(auto ri=observations.begin();ri!=observations.end();++ri){auto pi=ri->find(p);if(pi!=ri->end())pi->remove(key);}
}
void noteAIRound26DelegateTagsClear(const Player*p) {
 if(!p)return;QMutexLocker lock(&observationMutex);for(auto ri=observations.begin();ri!=observations.end();++ri)ri->remove(p);
}
void clearAIRound26DelegateObservations(Room&room) { QMutexLocker lock(&observationMutex);observations.remove(&room);inputs.remove(&room); }

void captureAIRound26DelegateMetadata(Room &room) {
 if(metadata(room))return;
 auto &vm=room.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);
 lua_State *L=vm.state();if(!L){new DelegateMetadata(room,false);return;}
 const int top=lua_gettop(L);const auto reset=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 lua_pushliteral(L,"p");lua_rawget(L,globals);bool known=lua_isnil(L,-1);lua_pop(L,1);
 if(lua_getmetatable(L,globals)){lua_pushliteral(L,"__index");lua_rawget(L,-2);known=known&&lua_isnil(L,-1);lua_pop(L,2);}
 // Load-time only, with fixed effective function provenance. Full fixed corpus
 // bytecode audit found zero _ENV.p stores; the two SETFIELD p instructions
 // are local AmazingGrace count tables (smart-ai.lua9361), not globals.
 bool fixed=false;lua_pushliteral(L,"sgs");lua_rawget(L,globals);
 if(lua_istable(L,-1)){lua_pushliteral(L,"ai_skill_playerschosen");lua_rawget(L,-2);
  if(lua_istable(L,-1)){lua_pushliteral(L,"mobilebenxi");lua_rawget(L,-2);
   if(lua_isfunction(L,-1)){lua_Debug info{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&info))
    fixed=QString::fromUtf8(info.source?info.source:"").endsWith("/mobile_st_standard-ai.lua")&&info.linedefined==1873&&QString::fromLatin1(info.what?info.what:"")=="Lua";
   }
  }
 }
 new DelegateMetadata(room,known&&fixed);
}
int aiRound26DelegatePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype) {
 return current(r,p)&&p->hasSkill("jieyingh")&&skill=="jieyingh"&&prototype=="JieyinghCard"
  &&r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@jieyingh"?0:-1;
}
