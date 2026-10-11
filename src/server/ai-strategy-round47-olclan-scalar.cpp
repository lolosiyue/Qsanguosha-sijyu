#include "ai-strategy-round47-olclan-scalar.h"
#include "ai-strategy-round26-planning.h"
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
#include "card.h"
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
void field(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
struct Write{const AIRequest*q;ServerPlayer*owner;QByteArray value;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Zujiewu assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_pushlstring(L,w->value.constData(),w->value.size());const int value=lua_absindex(L,-1);
 if(!current(*w->q,w->owner)||!pushAIRound45ActualAISelf(*w->owner->getRoom(),w->owner,L)
  ||!lua_rawequal(L,1,-1))return luaL_error(L,"Zujiewu actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,"zujiewusuit");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,1)){
  field(L,-1,"__newindex");if(!lua_isnil(L,-1))return luaL_error(L,"Zujiewu assignment has dynamic __newindex");lua_pop(L,2);
 }
 lua_pushvalue(L,value);lua_setfield(L,1,"zujiewusuit");return 0;
}
}
void projectAIRound47OlClanScalar(const AIRequest&q,Room&r,const ServerPlayer*p,const QVariant&data,QJsonObject&context){
 if(!p||q.viewerObjectName!=p->objectName()||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="zujiewu"
  ||!data.canConvert<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();
 // Fixed TargetSpecified caller uses its owner's actual played Card. A raw tag,
 // public ID alone, foreign actor or pre-use-only frame is not this witness.
 if(!use.card||use.from!=p||!aiRound26PlanningPublicCardWitness(q,r,p,use.card))return;
 context.insert("olclan_zujiewu_suit",use.card->getSuitString());
}
QJsonObject queryAIRound47OlClanScalarSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","olClan.zujiewusuit"}};};
 if(op!="read"&&op!="write")return fail("unknown source scalar operation");const bool writing=op=="write";
 const bool invoke=q.kind==AIRequest::SkillInvoke&&q.choiceOptions.reason=="zujiewu";
 const bool chosen=q.kind==AIRequest::CardChosen&&q.choiceOptions.reason=="zujiewu";
 if(!current(q,p)||!(invoke||(!writing&&chosen)))return fail("Zujiewu requires exact current own source inquiry");
 Room&r=*p->getRoom();LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Zujiewu game runtime unavailable");Stack stack(L);
 if(!pushAIRound45ActualAISelf(r,p,L))return fail("actual own LuaAI receiver unavailable");const int receiver=lua_absindex(L,-1);
 if(writing){
  const auto actual=q.choiceOptions.context.value("olclan_zujiewu_suit");
  if(payload.size()!=1||!payload.value("value").isString()||!actual.isString()||payload.value("value")!=actual)
   return fail("Zujiewu value is not the current public-use source suit");
  Write w{&q,p,actual.toString().toUtf8()};lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("actual Zujiewu scalar assignment failed");
  if(!current(q,p))return fail("Zujiewu assignment changed request");
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("Zujiewu read cannot supply remembered suit");
 if(!lookup(L,receiver,"zujiewusuit"))return fail("Zujiewu slot has unproved getter");
 if(lua_isnil(L,-1))return{{"status","absent"}};
 if(lua_type(L,-1)==LUA_TSTRING){size_t size=0;const char*value=lua_tolstring(L,-1,&size);if(size>4096)return fail("source scalar budget exceeded");return{{"status","value"},{"value",QString::fromUtf8(value,int(size))}};}
 // Fixed source compares against a suit string. Primitive types are preserved,
 // never converted to strings. No hidden userdata/table is serialized.
 if(lua_type(L,-1)==LUA_TBOOLEAN)return{{"status","value"},{"value",bool(lua_toboolean(L,-1))}};
 if(lua_type(L,-1)==LUA_TNUMBER)return{{"status","value"},{"value",lua_tonumber(L,-1)}};
 return fail("source scalar is not an observable primitive");
}
