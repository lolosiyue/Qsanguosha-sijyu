#include "ai-strategy-round54-priority.h"
#include "ai-strategy-round53-overseas-prefix.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QPointer>
#include <QRegularExpression>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
bool current(Room&r,ServerPlayer*p,const AIRequest&q){return p&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
 &&r.roomRuntime()->lua().isCurrentThreadOwner()&&r.roomRuntime()->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.decisionId
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRound44RangeRequestCurrent(r,q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool sourceScope(ServerPlayer*p,const AIRequest&q,const QString&reg,const QString&key){
 const bool play=q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 const auto reason=q.choiceOptions.reason;
 if((reg=="ai_use_priority"&&key=="ZunweiCard")||(reg=="ai_skill_choice"&&key=="zunwei")){
  // Ownership auth must not introduce a correction/effect call between the
  // source's adjacent priority and choice assignments.
  bool owns=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="zunwei"){owns=true;break;}return play&&owns;
 }
 if(reg=="ai_skill_playerchosen"&&key=="ov_yingjia")return q.kind==AIRequest::RespondCard&&q.prompt.startsWith("ov_yingjia0:");
 if(reg=="ai_skill_invoke"&&key=="ov_beini")return play&&p->hasSkill("ov_beini");
 if(reg!="ai_skill_choice")return false;
 if(key=="ov_xiafeng")return q.kind==AIRequest::SkillInvoke&&reason==key;
 if(key=="wangxing")return q.kind==AIRequest::SkillInvoke&&reason=="ov_xiawei";
 if(key=="ov_zenhui"||key=="ov_yizhu")return q.kind==AIRequest::PlayerChosen&&reason==key;
 if(key=="shifa")return (q.kind==AIRequest::RespondCard&&q.prompt.startsWith("ov_fengji0:"))
  ||(play&&(p->hasSkill("ov_sfzhouhu")||p->hasSkill("ov_sffengqi")||p->hasSkill("ov_xingzhui")));
 return false;
}
struct Write{Room*r;ServerPlayer*p;const AIRequest*q;QByteArray registry,key,text;QPointer<ServerPlayer>target;
 enum Kind{Nil,Boolean,Text,Player,Number}kind=Nil;bool boolean=false,reached=false;
 AIRound54PriorityOperation*priority=nullptr;
 lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr,*table=nullptr;};
bool roots(lua_State*L,const Write&w){Stack st(L);AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=w.globals)return false;
 AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=w.space)return false;
 AIRound45Inspect::field(L,-1,w.registry.constData());auto*t=AIRound45Inspect::table(L,-1);return t&&!t->metatable&&lua_topointer(L,-1)==w.table;}
bool live(const Write&w){auto*rt=w.r->roomRuntime();return rt&&!rt->isClosing()&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation&&current(*w.r,w.p,*w.q);}
int assign(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"named registry prefix stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));if(!w||!live(*w)||!roots(L,*w)||lua_topointer(L,1)!=w->table)return luaL_error(L,"named registry prefix context changed");
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));
 if(w->kind==Write::Nil)lua_pushnil(L);
 else if(w->kind==Write::Boolean)lua_pushboolean(L,w->boolean);
 else if(w->kind==Write::Number)lua_pushnumber(L,10);
 else if(w->kind==Write::Text)lua_pushlstring(L,w->text.constData(),size_t(w->text.size()));
 else if(!w->target||!pushAIRound45SlotPlayer(L,w->target))return luaL_error(L,"named registry prefix Player binding unavailable");
 if(!live(*w)||!roots(L,*w)||(w->kind==Write::Player&&(!w->target||w->target->getRoom()!=w->r)))return luaL_error(L,"named registry prefix changed during allocation");
 if(w->priority&&w->kind==Write::Number)w->priority->beforeWrite();
 lua_settable(L,1);w->reached=true;
 if(w->priority&&w->kind==Write::Number)w->priority->afterWrite();return 0;
}
}
QJsonObject writeAIRound53OverseasPrefix(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto fail=[&](const char*why){return QJsonObject{{"known",false},{"error",QString::fromLatin1(why)}};};
 if(input.size()!=4||input.value("op")!="source_prefix"||!input.value("registry").isString()||!input.value("key").isString()||!input.value("value").isObject())return fail("named registry source envelope malformed");
 const QString reg=input.value("registry").toString(),key=input.value("key").toString();
 if(!current(r,p,q)||!sourceScope(p,q,reg,key)||!current(r,p,q))return fail("named registry source question unavailable");
 Write w;w.r=&r;w.p=p;w.q=&q;w.registry=reg.toUtf8();w.key=key.toUtf8();const auto v=input.value("value").toObject();const auto kind=v.value("kind").toString();
 if(reg=="ai_use_priority"){
  if(key!="ZunweiCard"||kind!="scalar"||v.size()!=2||!v.value("value").isDouble()||v.value("value").toDouble()!=10)return fail("Zunwei source priority must be exact ten");w.kind=Write::Number;
 }else if(reg=="ai_skill_playerchosen"){
  if(kind=="nil"&&v.size()==1)w.kind=Write::Nil;
  else if(kind=="player"&&v.size()==2&&v.value("name").isString()){
   const QString name=v.value("name").toString();bool publicName=q.worldView.self.objectName==name;
   for(const auto&player:q.worldView.players)if(player.objectName==name)publicName=true;
   if(!publicName)return fail("named registry target is outside public roster");
   w.target=r.findPlayerByObjectName(name,true);if(!w.target||w.target->getRoom()!=&r)return fail("named registry target is no longer in room");w.kind=Write::Player;
  }else return fail("Yingjia source value must be exact Player or nil");
 }else if(reg=="ai_skill_invoke"){
  if(kind!="scalar"||v.size()!=2||!v.value("value").isBool())return fail("Beini source value must be boolean");w.kind=Write::Boolean;w.boolean=v.value("value").toBool();
 }else{
  if(kind!="scalar"||v.size()!=2||!v.value("value").isString())return fail("named source choice value must be string");const QString value=v.value("value").toString();
  bool valid=false;
  if(key=="zunwei")valid=value=="recover"||value=="draw"||value=="equip";
  else if(key=="ov_zenhui")valid=value=="ov_zenhui1"||value=="ov_zenhui2";
  else if(key=="ov_yizhu")valid=value=="ov_yizhu2"||value=="ov_yizhu3";
  else if(key=="ov_xiafeng")valid=QRegularExpression("^[0-3](?:\\.0)?$").match(value).hasMatch();
  else if(key=="wangxing")valid=QRegularExpression("^[1-4](?:\\.0)?$").match(value).hasMatch();
  else if(key=="shifa")valid=value.size()<=32&&QRegularExpression("^shifa-?[0-9]+(?:\\.0)?$").match(value).hasMatch();
  if(!valid)return fail("named source choice string is outside fixed writer range");w.kind=Write::Text;w.text=value.toUtf8();
 }
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail("named registry game VM unavailable");Stack st(L);
 w.L=L;w.generation=rt->lua().generation();AIRound45Inspect::globals(L);w.globals=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("named source namespace unavailable");w.space=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,w.registry.constData());auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return fail("named source registry is not plain");w.table=lua_topointer(L,-1);const int table=lua_absindex(L,-1);
 AIRound54PriorityOperation priority(r,p,q,L,assign,&w,w.kind==Write::Number?AIRound54PriorityOperation::LiteralTen:AIRound54PriorityOperation::Preserve,key);
 w.priority=&priority;
 lua_pushcfunction(L,assign);lua_pushvalue(L,table);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 // Actual assignment is never rolled back. A subsequent reentry/root change
 // denies completion; neither equality nor an old cached value restores it.
 if(status!=LUA_OK||!w.reached||!live(w)||!roots(L,w))return fail("named registry source prefix failed or was superseded");
 return{{"known",true},{"written",true}};
}
