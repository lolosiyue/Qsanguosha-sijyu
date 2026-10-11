#include "ai-strategy-round53-globals.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include <QPointer>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool current(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&q.decisionId&&q.kind==AIRequest::Activate
 &&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
struct Write{Room*r;ServerPlayer*p;const AIRequest*q;QString key;QByteArray bytes;QPointer<ServerPlayer>target;
 bool zero=false,playerValue=false,reached=false;lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr;};
bool live(const Write&w){auto*rt=w.r->roomRuntime();return current(*w.r,w.p,*w.q)&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation;}
bool roots(lua_State*L,const Write&w){Stack st(L);AIRound45Inspect::globals(L);if(lua_topointer(L,-1)!=w.globals)return false;
 AIRound45Inspect::field(L,-1,"sgs");return lua_istable(L,-1)&&lua_topointer(L,-1)==w.space
  &&matchAIRound45NativeNamespaceWrite(*w.r,L,"sgs",w.key);}
int assign(lua_State*L){if(!lua_checkstack(L,2048))return luaL_error(L,"source global assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));if(!w||!live(*w)||!roots(L,*w)||lua_topointer(L,1)!=w->space)return luaL_error(L,"source global assignment context changed");
 lua_pushlstring(L,w->bytes.constData(),size_t(w->bytes.size()));
 if(w->zero)lua_pushinteger(L,0);else if(!w->playerValue)lua_pushnil(L);else if(!w->target||!pushAIRound45SlotPlayer(L,w->target))return luaL_error(L,"source global Player binding unavailable");
 if(!live(*w)||!roots(L,*w)||(w->playerValue&&(!w->target||w->target->getRoom()!=w->r)))return luaL_error(L,"source global assignment changed during allocation");
 // Ordinary source syntax is preserved: the exact captured SWIG namespace
 // __newindex and missing .set entry are proved; no rawset shortcut is used.
 lua_settable(L,1);w->reached=true;return 0;
}
}
QJsonObject writeAIRound53HCGlobal(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=3||v.value("op")!="source_global"||!v.value("key").isString()||!v.value("value").isObject()||!current(r,p,q))return fail();
 const QString key=v.value("key").toString();const QString skill=key=="xiaowu_n"?"xiaowu":key=="yujue_target"?"yujue":key=="secondyujue_target"?"secondyujue":QString();
 if(skill.isEmpty()||!p->hasSkill(skill)||!current(r,p,q))return fail();
 Write w;w.r=&r;w.p=p;w.q=&q;w.key=key;w.bytes=key.toUtf8();const auto value=v.value("value").toObject();
 if(key=="xiaowu_n"){
  if(value.size()!=2||value.value("kind")!="scalar"||!value.value("value").isDouble()||value.value("value").toDouble()!=0)return fail();w.zero=true;
 }else if(value.size()==1&&value.value("kind")=="nil"){
  // Reached source getYujueTarget returning nil clears the actual global.
 }else if(value.size()==2&&value.value("kind")=="player"&&value.value("name").isString()){
  const QString name=value.value("name").toString();bool disclosed=q.worldView.self.objectName==name;for(const auto&row:q.worldView.players)if(row.objectName==name)disclosed=true;
  if(!disclosed)return fail();w.playerValue=true;w.target=r.findPlayerByObjectName(name,true);if(!w.target||w.target->getRoom()!=&r)return fail();
 }else return fail();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,2048))return fail();Stack st(L);
 w.L=L;w.generation=rt->lua().generation();AIRound45Inspect::globals(L);w.globals=lua_topointer(L,-1);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.space=lua_topointer(L,-1);const int space=lua_absindex(L,-1);
 if(!roots(L,w))return fail();lua_pushcfunction(L,assign);lua_pushvalue(L,space);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK||!w.reached||!live(w)||!roots(L,w))return fail();return{{"known",true},{"written",true}};
}
