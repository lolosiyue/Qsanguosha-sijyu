#include "ai-strategy-round54-priority.h"
#include "ai-strategy-card-identity.h"
#include "card-lifetime-manager.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round53-priority.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-runtime.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include <QJsonArray>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool current(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&q.decisionId&&q.kind==AIRequest::Activate
 &&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
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
struct Copy{Room*r;ServerPlayer*p;const AIRequest*q;lua_State*L;quint64 generation;QByteArray src,dst;const void*g=nullptr,*sgs=nullptr,*table=nullptr;bool reached=false;AIRound54PriorityOperation*priority=nullptr;};
bool live(const Copy&w){auto*rt=w.r->roomRuntime();return current(*w.r,w.p,*w.q)&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation;}
bool roots(lua_State*L,const Copy&w){Stack st(L);AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=w.g)return false;AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=w.sgs)return false;AIRound45Inspect::field(L,-1,"ai_use_priority");auto*t=AIRound45Inspect::table(L,-1);return t&&!t->metatable&&lua_topointer(L,-1)==w.table;}
int assign(lua_State*L){if(!lua_checkstack(L,256))return luaL_error(L,"source priority stack unavailable");auto*w=static_cast<Copy*>(lua_touserdata(L,2));
 if(!w||!live(*w)||!roots(L,*w)||lua_topointer(L,1)!=w->table)return luaL_error(L,"source priority context changed");
 lua_pushlstring(L,w->dst.constData(),size_t(w->dst.size()));lua_pushlstring(L,w->src.constData(),size_t(w->src.size()));
 if(!live(*w)||!roots(L,*w))return luaL_error(L,"source priority changed during allocation");
 // This is the actual RHS read, not a native metadata peek or exported value.
 // Plain table proof preserves nil without executing unknown __index.
 if(w->priority)w->priority->beforeWrite();
 lua_gettable(L,1);
 if(!live(*w)||!roots(L,*w))return luaL_error(L,"source priority changed during read");
 lua_settable(L,1);w->reached=true;if(w->priority)w->priority->afterWrite();return 0;
}
}
QJsonObject copyAIRound53SourcePriority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=4||v.value("op")!="source_priority"||!v.value("key").isString()||!v.value("source_class").isString()||!v.value("card").isObject()||!current(r,p,q))return fail();
 const QString target=v.value("key").toString();const QString skill=target=="YoulongCard"?"youlong":target=="SecondJinzhiCard"?"secondjinzhi":target=="genshunoYURICard"?"genshunoYURI":QString();
 if(skill.isEmpty()||!p->hasSkill(skill)||!current(r,p,q))return fail();
 const QString cl=sourceClass(r,p,q,target,v.value("card").toObject());if(cl.isEmpty()||cl!=v.value("source_class").toString()||!current(r,p,q))return fail();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail();Stack st(L);
 Copy w{&r,p,&q,L,rt->lua().generation(),cl.toUtf8(),target.toUtf8()};AIRound45Inspect::globals(L);w.g=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.sgs=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"ai_use_priority");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return fail();w.table=lua_topointer(L,-1);
 AIRound54PriorityOperation priority(r,p,q,L,assign,&w,AIRound54PriorityOperation::CopyCell,target,cl);w.priority=&priority;
 lua_pushcfunction(L,assign);lua_pushvalue(L,-2);lua_pushlightuserdata(L,&w);int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK||!w.reached||!live(w)||!roots(L,w))return fail();
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
 auto*w=static_cast<Add*>(lua_touserdata(L,2));if(!w||!live(w->base)||!roots(L,w->base)||lua_topointer(L,1)!=w->base.table)
  return luaL_error(L,"source arithmetic priority context changed");
 lua_pushlstring(L,w->base.dst.constData(),size_t(w->base.dst.size()));lua_pushvalue(L,-1);
 if(w->base.priority)w->base.priority->beforeWrite();lua_gettable(L,1);
 if(!live(w->base)||!roots(L,w->base))return luaL_error(L,"source arithmetic priority changed during lookup");
 if(w->conditional&&!lua_toboolean(L,-1)){w->base.reached=true;return 0;}
 const int type=lua_type(L,-1);
 // Do not invoke a private replacement object's arithmetic metamethod. Fixed
 // scalar number/string coercion and nil/boolean ordinary errors are retained.
 if(type!=LUA_TNUMBER&&type!=LUA_TSTRING&&type!=LUA_TNIL&&type!=LUA_TBOOLEAN){w->unsupported=true;return 0;}
 if(AIRound45Inspect::primitiveMetatable(L,type)){
  // Numeric strings use Lua arithmetic coercion before any metamethod. Other
  // primitive metatable arithmetic would require a separate exact effect proof.
  lua_pop(L,1);int numeric=0;lua_tonumberx(L,-1,&numeric);
  if(!numeric&&(type!=LUA_TSTRING||!matchAIRound47NativeStringMetatable(*w->base.r,L))){w->unsupported=true;return 0;}
 }
 lua_pushinteger(L,w->amount);w->ordinary=true;lua_arith(L,LUA_OPADD);w->ordinary=false;
 if(!live(w->base)||!roots(L,w->base))return luaL_error(L,"source arithmetic priority changed during addition");
 lua_settable(L,1);w->base.reached=true;if(w->base.priority)w->base.priority->afterWrite();return 0;
}
}
QJsonObject incrementAIRound53SourcePriority(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=4||v.value("op")!="source_priority_increment"||!v.value("writer").isString()
  ||!v.value("source_class").isString()||!v.value("card").isObject()||!current(r,p,q))return fail();
 const QString writer=v.value("writer").toString();if(writer!="sijyu_qizhan"&&writer!="TenyearJianying")return fail();
 // The source TenyearJianying callback is NOT the native skill name
 // tenyearjianying. Never invent an alias or require that unrelated spelling.
 if(writer=="sijyu_qizhan"){
  bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName==writer){owned=true;break;}
  if(!owned)return fail();
 }
 if(!current(r,p,q))return fail();
 const auto cl=arithmeticClass(r,p,q,v.value("card").toObject());if(cl.isEmpty()||cl!=v.value("source_class").toString()||!current(r,p,q))return fail();
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
  if(!reached||!current(r,p,q))return fail();
 }
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail();Stack st(L);
 Add w;w.base={&r,p,&q,L,rt->lua().generation(),cl.toUtf8(),cl.toUtf8()};w.conditional=writer=="sijyu_qizhan";w.amount=w.conditional?1:5;
 AIRound45Inspect::globals(L);w.base.g=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.base.sgs=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"ai_use_priority");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return fail();w.base.table=lua_topointer(L,-1);
 AIRound54PriorityOperation priority(r,p,q,L,addPriority,&w,AIRound54PriorityOperation::Arithmetic,cl);w.base.priority=&priority;
 lua_pushcfunction(L,addPriority);lua_pushvalue(L,-2);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK){if(w.ordinary&&lua_type(L,-1)==LUA_TSTRING)return{{"known",true},{"source_error",QString::fromUtf8(lua_tostring(L,-1))}};return fail();}
 if(w.unsupported||!w.base.reached||!live(w.base)||!roots(L,w.base))return fail();
 return{{"known",true},{"completed",true}}; // No conditional outcome/RHS export.
}
