#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round29-choice.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round40-choice.h"
#include "ai-strategy-round36-planning.h"
#include "ai-strategy-round38-manbing.h"
#include "ai-strategy-round37-response-init.h"
#include "ai-strategy-round37-planning.h"
#include "ai-strategy-round36-choice.h"
#include "ai-strategy-round37-choice.h"
#include "ai-strategy-round27-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "card.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QHash>
#include <QSet>
#include <QScopeGuard>
#include <cmath>
namespace {
class Priorities final:public QObject {
public:explicit Priorities(Room &r):QObject(&r){}
 bool captured=false,baselineLive=true,entered=false;
 QString viewer;
 QSet<QString> keys;
 QHash<QString,QJsonObject> baseline;
 QHash<QString,QHash<QString,QJsonObject>> values;
};
Priorities *state(Room &room) {
 for(auto *o:room.children())if(auto *s=dynamic_cast<Priorities *>(o))return s;
 return new Priorities(room);
}
bool current(Room &room,const AIRequest &r,const ServerPlayer *p) {
 return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()
  &&r.worldView.self.objectName==p->objectName()
  &&aiRound44RangeRequestCurrent(room,r,p);
}
QJsonObject unknown() {return{{"known",false},{"reason","priority continuity has no lawful last-writer witness"}};}
QJsonObject rejected(const QString &reason) {return{{"error",reason},{"key","OverseasVersion.priority"}};}
bool plain(lua_State *L,int i) {
 if(!lua_istable(L,i))return false;
 if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;
}
}
void captureAIRound29ChoicePriorities(Room &room) {
 captureAIRound40OLJijiang(room);
 captureAIRound37SD(room);
 auto *s=state(room);if(s->captured)return;
 s->captured=true;
 for(const auto &k:QStringList{"ov_lingbaoCard","ov_jichouCard","ov_bomingCard","Tiaojiyanmei","ov_muyueCard","ov_yanshiYLCard"})s->keys.insert(k);
 // Public registered card definitions, never draw order or private hand faces.
 for(int i=0;i<Sanguosha->getCardCount();++i)if(const Card *c=Sanguosha->getEngineCard(i))s->keys.insert(c->getClassName());
 // Capturing after any policy entry cannot establish an initial default.
 if(s->entered){s->baselineLive=false;return;}
 LuaRuntime &runtime=room.roomRuntime()->lua();LuaRuntime::Binding binding(runtime,false);
 lua_State *L=runtime.state();if(!L){s->baselineLive=false;return;}
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);lua_pushliteral(L,"sgs");lua_rawget(L,-2);
 // SWIG namespaces have a metatable; rawget below never invokes it.
 if(!lua_istable(L,-1)){s->baselineLive=false;return;}
 lua_pushliteral(L,"ai_use_priority");lua_rawget(L,-2);
 if(!plain(L,-1)){s->baselineLive=false;return;}
 const int table=lua_absindex(L,-1);
 for(const auto &key:s->keys) {
  const auto bytes=key.toUtf8();lua_pushlstring(L,bytes.constData(),bytes.size());lua_rawget(L,table);
  QJsonObject row=unknown();
  if(lua_isnil(L,-1))row={{"known",true},{"removed",true}};
  else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(lua_tonumber(L,-1)))
   row={{"known",true},{"removed",false},{"value",lua_tonumber(L,-1)}};
  row.insert("basis","configured_registry_initial_value");s->baseline.insert(key,row);lua_pop(L,1);
 }
}
void beginAIRound29ChoicePolicy(Room &room,const QString &viewer) {
 beginAIChoiceContinuityPolicy(room,viewer);
 beginAIRound40OLJijiang(room,viewer);
 beginAIRound37SD(room,viewer);
 auto *s=state(room);s->entered=true;
 if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer)) {
  s->baselineLive=false;s->values.clear();
 }
 s->viewer=viewer;
}
void invalidateAIRound29ChoicePriorities(Room &room,bool invalidateChoices) {
 if(invalidateChoices)invalidateAIChoiceContinuity(room);
 invalidateAIRound40OLJijiang(room);
 invalidateAIRound37SD(room);
 invalidateAIRound36KurouShadow(room);
 invalidateAIRound38Manbing(room);
 invalidateAIRound37OriginalTurnUse(room);
 invalidateAIRound37CrossbowShadow(room);
 invalidateAIRound36GuanxingShadow(room);
 auto *s=state(room);s->entered=true;s->baselineLive=false;s->values.clear();
}
QJsonObject queryAIRound29ChoicePriority(Room &room,const AIRequest &r,ServerPlayer *p,const QJsonObject &spec) {
 if(!current(room,r,p))return rejected("stale priority source request");
 auto *s=state(room);const QString key=spec.value("key").toString();
 if(!s->captured)return rejected("configured priority registry was not captured before policy execution");
 if(!s->keys.contains(key))return{{"kind","overseas_priority"},{"tracked",false}};
 if(s->viewer!=p->objectName())return rejected("priority policy-entry witness missing");
 const QString op=spec.value("op").toString();QJsonObject row;
 if(op=="read"&&spec.size()==3) {
  const auto own=s->values.value(p->objectName());
  row=own.contains(key)?own.value(key):(s->baselineLive?s->baseline.value(key,unknown()):unknown());
 }else if(op=="write"&&spec.size()==5&&spec.value("removed").isBool()) {
  const bool removed=spec.value("removed").toBool();const auto v=spec.value("value");
  if((removed&&!v.isNull())||(!removed&&(!v.isDouble()||!std::isfinite(v.toDouble()))))return rejected("invalid raw priority assignment");
  row={{"known",true},{"removed",removed},{"basis","reached_viewer_source_write"}};
  if(!removed)row.insert("value",v);
  s->values[p->objectName()].insert(key,row);
 }else return rejected("invalid priority operation shape");
 row.insert("kind","overseas_priority");row.insert("tracked",true);row.insert("key",key);
 row.insert("viewer",p->objectName());row.insert("decision_id",r.getDecisionId());row.insert("state_revision",r.getStateRevision());
 return row;
}
QJsonObject writeAIRound29XiafengFlag(Room &room,const AIRequest &r,ServerPlayer *p,int id,bool enabled) {
 auto fail=[](const QString &s){return QJsonObject{{"error",s},{"key","ov_xiafeng.card_flag"}};};
 if(!current(room,r,p)||!aiRound27ChoicePlanningQuestion(r,room,p)
  ||r.choiceOptions.question!="askForSkillInvoke"||room.getCurrent()!=p)
  return fail("Xiafeng source flag write is outside the actual owner inquiry");
 const Card *c=room.getCard(id);
 if(id<0||!c||c->isVirtualCard()||c->getEffectiveId()!=id||!p->handCards().contains(id)
  ||room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand)
  return fail("Xiafeng source flag requires an actual owner hand card");
 bool disclosed=false;
 for(const auto &v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber())disclosed=true;
 if(!disclosed)return fail("Xiafeng source hand identity changed");
 if(enabled&&!(c->isDamageCard()&&(c->getTypeId()==Card::TypeBasic||c->isKindOf("AOE")||c->isKindOf("SingleTargetTrick"))))
  return fail("Xiafeng positive source flag predicate rejected");
 // Fixed donor AI-only side effect. Card::setFlags mutates only its flag list;
 // no room revision, card lifetime, payment, gameplay rule, or target changes.
 c->setFlags(enabled?"AIGlobal_KillOff":"-AIGlobal_KillOff");
 return{{"kind","overseas_xiafeng_flag"},{"card_id",id},{"enabled",enabled},
  {"viewer",p->objectName()},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()}};
}
