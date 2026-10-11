#include "ai-strategy-round53-peiqi.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include "card-lifetime-manager.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
// Must follow Qt headers: the private Lua implementation defines cast macros.
#include "ai-strategy-round45-lua-inspect.h"
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p,const AIEventView*e){
 return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r
  &&(e?(q.decisionId==0&&e->revision==q.stateRevision):q.decisionId>0);
}
bool scope(const AIRequest&q,ServerPlayer*p,const AIEventView*e,const QString&op,const QString&key){
 // canDamageHp(to) invokes peiqi even when the observer does not own that skill.
 // This finite scratch table carries no Card face or payment permission.
 if(op=="reset")return key.isEmpty();
 if(key!="from"&&key!="to"&&key!="cid")return false;
 if(op=="write")return true;
 if(op!="read")return false;
 if(e){const auto a=e->details.value("prompt_list").toArray();return e->triggerEvent==int(ChoiceMade)&&a.size()>=2&&a.at(0)==QJsonValue("cardChosen")
  &&(a.at(1)==QJsonValue("peiqi")||a.at(1)==QJsonValue("s4_longhun")||a.at(1)==QJsonValue("s4_yuezhe"))&&(key=="cid"||key=="to");}
 if(key=="from"&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getRoom()->getCurrent()==p&&p->getPhase()==Player::Play&&p->hasSkill("dressing"))return true; // dressing use_func reads the record just written by invoke
 if(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"&&q.choiceOptions.reason=="dressing")return key=="to";
 if(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen")return(q.choiceOptions.reason=="peiqi_from"&&key=="from")||(q.choiceOptions.reason=="peiqi_to"&&key=="to");
 return q.kind==AIRequest::CardChosen&&q.choiceOptions.question=="askForCardChosen"&&(q.choiceOptions.reason=="peiqi"||q.choiceOptions.reason=="dressing")&&key=="cid";
}
bool field(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool assignment(lua_State*L,int self,const char*key){AIRound45Inspect::field(L,self,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
ServerPlayer*roster(Room&r,const AIRequest&q,const QString&name){
 bool projected=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)projected=projected||v.objectName==name;
 if(!projected)return nullptr;for(auto*p:r.getAllPlayers(true))if(p->objectName()==name)return p;return nullptr;
}
ServerPlayer*actualPlayer(Room&r,lua_State*L,int at){for(auto*p:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,at,p))return p;return nullptr;}

// Actual-Self storage alone is not disclosure of a legacy-mutated physical ID.
// The fixed writer takes cid from a public e/j card. Require that same lawful
// domain at each numeric export; no hidden-zone face or historical tag lookup.
bool publicCid(Room&r,const AIRequest&q,double n){
 if(!std::isfinite(n)||n<0||n>2147483647||n!=std::floor(n))return false;
 const int id=int(n);auto views=q.worldView.players;views.prepend(q.worldView.self);
 for(const auto&v:views){
  auto*holder=roster(r,q,v.objectName);if(!holder)continue;
  for(int zone=0;zone<2;++zone){
   const auto&rows=zone?v.judgingArea:v.equips;
   for(const auto&c:rows){
    if(c.effectiveId!=id||!c.nativeIdentity||!globalCardLifetimeManager().isLive(c.nativeIdentity))continue;
    const auto place=zone?Player::PlaceDelayedTrick:Player::PlaceEquip;
    if(r.getCardOwner(id)!=holder||r.getCardPlace(id)!=place)continue;
    const auto actual=zone?holder->getJudgingArea():holder->getEquips();
    for(const auto*card:actual)if(card&&card->getEffectiveId()==id&&c.nativeIdentity->address==card)return true;
   }
  }
 }
 return false;
}
struct Call{Room*r;const AIRequest*q;ServerPlayer*p;const AIEventView*e;QString op,key;ServerPlayer*target=nullptr;int id=0;QJsonObject result;const char*error=nullptr;};
int fail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool now(Call*w){return current(*w->r,*w->q,w->p,w->e);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"Peiqi source stack unavailable");
 int fresh=0;if(w->op=="reset"){lua_newtable(L);fresh=lua_absindex(L,-1);}
 else if(w->op=="write"){if(w->key=="cid")lua_pushinteger(L,w->id);else if(!pushAIRound45SlotPlayer(L,w->target))return fail(L,w,"Peiqi Player wrapper unavailable");fresh=lua_absindex(L,-1);}
 if(!now(w)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"Peiqi current gameplay Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="reset"){
  if(!assignment(L,self,"peiqiData")||!now(w))return fail(L,w,"Peiqi reset dispatch changed");
  lua_pushvalue(L,fresh);lua_setfield(L,self,"peiqiData");
 }else{
  if(!field(L,self,"peiqiData"))return fail(L,w,"Peiqi Self getter changed");const int record=lua_absindex(L,-1);
  if(!lua_istable(L,record)){
   const int t=lua_type(L,record);
   if(t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER){w->result={{"known",true},{"ordinary_error",QString("attempt to index a %1 value (field 'peiqiData')").arg(QString::fromLatin1(lua_typename(L,t)))}};return 0;}
   return fail(L,w,"Peiqi record is not a source plain table");
  }
  if(lua_getmetatable(L,record))return fail(L,w,"Peiqi record metatable is outside the fixed source domain");const auto key=w->key.toUtf8();
  if(w->op=="write"){
   if(!now(w))return fail(L,w,"Peiqi record writer became stale");
   if(w->key=="cid"&&!publicCid(*w->r,*w->q,w->id))return fail(L,w,"Peiqi cid is not currently projected public e/j");lua_pushvalue(L,fresh);lua_setfield(L,record,key.constData());
   AIRound45Inspect::field(L,record,key.constData());if(!lua_rawequal(L,fresh,-1))return fail(L,w,"Peiqi record write changed");lua_pop(L,1);
  }else{
   AIRound45Inspect::field(L,record,key.constData());const int v=lua_absindex(L,-1),t=lua_type(L,v);
   if(t==LUA_TNIL)w->result={{"known",true},{"type","nil"}};
   else if(t==LUA_TBOOLEAN)w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,v))}};
   else if(t==LUA_TNUMBER){const double n=lua_tonumber(L,v);if(!std::isfinite(n))return fail(L,w,"Peiqi nonfinite field");if(w->key=="cid"&&!publicCid(*w->r,*w->q,n))return fail(L,w,"Peiqi cid is not currently projected public e/j");w->result={{"known",true},{"type","number"},{"value",n}};}
   else if(t==LUA_TSTRING){size_t n;const char*c=lua_tolstring(L,v,&n);if(n>4096)return fail(L,w,"Peiqi string field exceeds finite domain");w->result={{"known",true},{"type","string"},{"value",QString::fromUtf8(c,int(n))}};}
   else{auto*p=actualPlayer(*w->r,L,v);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAIRound45NativeObjectMethods(*w->r,L,v,"ServerPlayer",{"objectName"}))return fail(L,w,"Peiqi field is not a projected native Player");w->result={{"known",true},{"type","player"},{"value",p->objectName()}};}
  }
  // Do not substitute another table or reconstruct an aggregate after each field write.
  if(!field(L,self,"peiqiData")||!lua_rawequal(L,record,-1))return fail(L,w,"Peiqi actual record changed during operation");
 }
 if(!now(w)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"Peiqi receiver changed after operation");
 if(w->op=="reset"){AIRound45Inspect::field(L,self,"peiqiData");if(!lua_rawequal(L,fresh,-1))return fail(L,w,"Peiqi reset table changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject queryAIRound53PeiqiRecord(Room&r,const AIRequest&q,ServerPlayer*p,const AIEventView*e,const QString&op,const QString&key,const QJsonObject&input){
 if(!current(r,q,p,e)||!scope(q,p,e,op,key))return unknown("Peiqi operation requires its actual current owner policy/source reader");
 Call w{&r,&q,p,e,op,key};
 if(op=="write"){
  if(input.size()!=1)return unknown("Peiqi field assignment shape invalid");
  if(key=="cid"){const auto v=input.value("value");const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n>2147483647||n!=std::floor(n))return unknown("Peiqi cid is not a source integer");w.id=int(n);if(!publicCid(r,q,n))return unknown("Peiqi cid is not currently projected public e/j");}
  else{if(!input.value("value").isString())return unknown("Peiqi target name malformed");w.target=roster(r,q,input.value("value").toString());if(!w.target)return unknown("Peiqi target is not projected roster");}
 }else if(!input.isEmpty())return unknown("Peiqi read/reset has extra arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("Peiqi game VM unavailable");
 const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected Peiqi source operation failed");return w.result;
}
