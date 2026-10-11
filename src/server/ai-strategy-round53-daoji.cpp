#include "ai-strategy-round53-daoji.h"
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
#include "card.h"
#include "wrapped-card.h"
#include "card-lifetime-manager.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
// Must follow Qt headers: the private Lua implementation defines cast macros.
#include "ai-strategy-round45-lua-inspect.h"
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool scope(const AIRequest&q,ServerPlayer*p,const QString&op){
 if(op=="write")return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill("daoji");
 return(op=="truth"||op=="contains"||op=="read")&&q.kind==AIRequest::CardChosen&&q.choiceOptions.reason=="daoji"&&q.choiceOptions.question=="askForCardChosen";
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

bool publicEquip(const AIRequest&q,ServerPlayer*owner,const Card*c){
 if(!owner||!c)return false;auto views=q.worldView.players;views.prepend(q.worldView.self);
 for(const auto&v:views)if(v.objectName==owner->objectName())for(const auto&e:v.equips)
  if(e.effectiveId==c->getEffectiveId()&&e.nativeIdentity&&globalCardLifetimeManager().isLive(e.nativeIdentity)&&e.nativeIdentity->address==c)return true;
 return false;
}
const Card*sourceWrapped(Room&r,const AIRequest&q,int id){
 auto*holder=r.getCardOwner(id);if(!holder||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 // Only bind the source Sanguosha:getCard identity. No wrapped face/change-chain
 // is read or exported from this observation-only slot operation.
 for(const auto*c:holder->getEquips())if(c->getEffectiveId()==id&&publicEquip(q,holder,c)){
  const auto*w=Sanguosha->getCard(id);return w&&w->getRealCard()==c?w:nullptr;
 }return nullptr;
}
struct Call{Room*r;const AIRequest*q;ServerPlayer*p;QString op;const Card*card=nullptr;ServerPlayer*who=nullptr;QList<const Card*>equips;QJsonObject result;const char*error=nullptr;};
int fail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"Daoji source stack unavailable");
 int fresh=0;if(w->op=="write"){if(w->card){if(!pushAIRound45SlotCard(L,w->card))return fail(L,w,"Daoji exact Card wrapper unavailable");}else lua_pushnil(L);fresh=lua_absindex(L,-1);}
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"Daoji actual Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="write"){
  if(!assignment(L,self,"daoji_throwcard")||!current(*w->r,*w->q,w->p))return fail(L,w,"Daoji source assignment changed");
  if(w->card&&sourceWrapped(*w->r,*w->q,w->card->getEffectiveId())!=w->card)return fail(L,w,"Daoji public source object changed before assignment");
  lua_pushvalue(L,fresh);lua_setfield(L,self,"daoji_throwcard");
 }else{
  if(!field(L,self,"daoji_throwcard"))return fail(L,w,"Daoji actual getter changed");const int value=lua_absindex(L,-1);
  if(w->op=="truth")w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
  else{
   if(w->who->getCards("e")!=w->equips)return fail(L,w,"Daoji actual public equipment list changed");
   for(auto*c:w->equips)if(!publicEquip(*w->q,w->who,c))return fail(L,w,"Daoji current list not disclosed");
   int found=-1;for(int i=0;i<w->equips.size();++i)if(matchAIRound45SlotCard(L,value,w->equips.at(i))){found=i;break;}
   if(found<0){
    // Authenticate fixed source-produced wrapped Cards as well. A WrappedCard
    // and its real EquipCard have different pointer identities, even at one ID.
    bool fixed=lua_isnil(L,value);
    for(int id:Sanguosha->getDeckCardIds(false)){
     const auto*outer=Sanguosha->getCard(id);if(outer&&matchAIRound45SlotCard(L,value,outer))fixed=true;
    }
    if(!fixed)return fail(L,w,"Daoji retained Card outside current physical equipment domain");
   }
   if(w->op=="contains")w->result={{"known",true},{"type","boolean"},{"value",found>=0}};
   else{if(found<0)return fail(L,w,"Daoji source read no longer equals current equipment");w->result={{"known",true},{"index",found+1}};}
  }
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"Daoji receiver changed after operation");
 if(w->op=="write"){AIRound45Inspect::field(L,self,"daoji_throwcard");if(!lua_rawequal(L,fresh,-1))return fail(L,w,"Daoji source assignment did not persist");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject queryAIRound53DaojiCard(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&input){
 if(!current(r,q,p)||!scope(q,p,op))return unknown("Daoji card slot requires exact current own source inquiry");Call w{&r,&q,p,op};
 if(op=="write"){
  if(input.size()!=1)return unknown("Daoji write shape invalid");const auto id=input.value("id");
  if(input.value("clear")!=QJsonValue(true)){
   if(!input.contains("id"))return unknown("Daoji write key invalid");
   const double d=id.toDouble(-1);if(!id.isDouble()||d<0||d>2147483647||d!=std::floor(d))return unknown("Daoji source ID malformed");w.card=sourceWrapped(r,q,int(d));if(!w.card)return unknown("Daoji source ID is not current publicly projected equipment");
  }
 }else if(op=="truth"){if(!input.isEmpty())return unknown("Daoji truth has extra arguments");}
 else{
  if(input.size()!=2||!input.value("who").isString()||!input.value("ids").isArray())return unknown("Daoji equipment observation shape invalid");w.who=roster(r,q,input.value("who").toString());if(!w.who||q.choiceOptions.playerNames!=QStringList{w.who->objectName()})return unknown("Daoji list holder differs from actual question");
  w.equips=w.who->getCards("e");const auto ids=input.value("ids").toArray();if(ids.size()!=w.equips.size())return unknown("Daoji observed equipment order/count changed");
  for(int i=0;i<ids.size();++i)if(!ids.at(i).isDouble()||ids.at(i).toDouble()!=w.equips.at(i)->getEffectiveId()||!publicEquip(q,w.who,w.equips.at(i)))return unknown("Daoji observed equipment object/order changed");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("Daoji game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected Daoji source operation failed");return w.result;
}
