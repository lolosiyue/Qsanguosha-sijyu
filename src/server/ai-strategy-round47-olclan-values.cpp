#include "ai-strategy-round47-olclan-values.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include <QSet>
#include "lua.hpp"
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool active(const AIRequest&q,ServerPlayer*p){
 auto exact=[&](const AiSkillActionContext&a){return a.isValid()&&a.activationRef==a.sourceRef
 &&a.getActivationOwner()==p->objectName()&&a.getActivationSkillName()=="zulianzhuvs";};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;return false;
}
bool scope(const AIRequest&q,ServerPlayer*p,const QString&slot,bool write){
 if(slot=="zujieliTo")return write?(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="zujieli")
  :(q.kind==AIRequest::UseCard&&q.pattern=="@@zujieli"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
 if(slot!="zulianzhuEp")return false;
 if(!write)return q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="zulianzhu";
 if(aiRound23OlClanPlanningQuestion(q,*p->getRoom(),p))return p->hasSkill("zulianzhuvs");
 return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&active(q,p);
}
void field(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
struct Write{const AIRequest*q;ServerPlayer*owner;ServerPlayer*target;QByteArray key;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"OlClan Player assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 if(!pushAIRound45SlotPlayer(L,w->target))return luaL_error(L,"OlClan Player wrapper unavailable");const int value=lua_absindex(L,-1);
 if(!current(*w->q,w->owner)||!pushAIRound45ActualAISelf(*w->owner->getRoom(),w->owner,L)
  ||!lua_rawequal(L,1,-1))return luaL_error(L,"OlClan actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,1)){
  field(L,-1,"__newindex");if(!lua_isnil(L,-1))return luaL_error(L,"OlClan Player assignment has dynamic __newindex");lua_pop(L,2);
 }
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());return 0;
}
}
QJsonObject queryAIRound47OlClanPlayerSlot(const AIRequest&q,ServerPlayer*p,const QString&slot,const QString&op,const QJsonObject&payload){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","olClan.saved_player"}};};
 if(op!="read"&&op!="write")return fail("unknown source Player operation");const bool write=op=="write";
 if(!current(q,p)||!scope(q,p,slot,write))return fail("Player slot requires exact current own source inquiry");
 Room&r=*p->getRoom();LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Player slot game runtime unavailable");Stack stack(L);
 if(!pushAIRound45ActualAISelf(r,p,L))return fail("actual own LuaAI receiver unavailable");const int receiver=lua_absindex(L,-1);
 const auto key=slot.toUtf8();
 if(write){
  if(payload.size()!=1||!payload.value("target").isString())return fail("source Player assignment shape malformed");
  const auto name=payload.value("target").toString();ServerPlayer*target=nullptr;
  for(auto*t:r.getAllPlayers(true))if(t->objectName()==name)target=t;
  bool shown=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)shown=shown||v.objectName==name;
  if(!target||!shown)return fail("source target is not a currently projected roster Player");
  if(slot=="zujieliTo"&&!q.choiceOptions.playerNames.contains(name))return fail("Zujieli source loop target not offered");
  Write w{&q,p,target,key};lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("actual source Player assignment failed");
  if(!current(q,p))return fail("source Player assignment changed request");
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("source Player read cannot supply remembered target");
 if(!lookup(L,receiver,key.constData()))return fail("source Player slot has unproved getter");
 if(!lua_toboolean(L,-1))return{{"status","absent"}};
 ServerPlayer*target=nullptr;for(auto*t:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,-1,t))target=t;
 if(!target)return fail("source Player slot is not an exact live roster Player");
 QStringList methods{"objectName"};if(slot=="zulianzhuEp")methods<<"canSlash";
 if(!matchAIRound45NativeObjectMethods(r,L,-1,"ServerPlayer",methods))return fail("source Player dispatch changed");
 if(!current(q,p))return fail("source Player read changed request");
 return{{"status","player"},{"target",target->objectName()}};
}
