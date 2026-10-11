#include "ai-strategy-round53-pingjian.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "general.h"
#include "skill.h"
#include "lua-runtime.h"
#include <QSet>
#include <QJsonArray>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
class State;void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
class State final:public QObject{public:explicit State(Room&r):QObject(&r),room(r){}
 ~State()override{for(auto id:{globals,space,fill})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr,*fill=nullptr,*can=nullptr,*sampler=nullptr;
 bool valid=false;QSet<QString>truthy;
};
State*state(Room&r,bool make=false){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return make?new State(r):nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
void observe(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{auto*s=static_cast<State*>(u);if(!L||!s->L||G(L)!=G(s->L))return;const auto&e=*ep;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&(e.table_identity==s->fill
  ||(e.table_identity==s->globals&&(key(e,"sgs")||key(e,"canAiSkills")||key(e,"PingjianSkill")))
  ||(e.table_identity==s->space&&key(e,"ai_fill_skill")))))s->valid=false;
 if(e.event==LUA_TABLE_OBSERVER_FREE){if(e.table_identity==s->globals)s->globals=nullptr;if(e.table_identity==s->space)s->space=nullptr;if(e.table_identity==s->fill)s->fill=nullptr;}
}
bool environment(lua_State*L,int i){i=lua_absindex(L,i);bool seen=false;for(int n=1;;++n){const char*k=lua_getupvalue(L,i,n);if(!k)break;
 if(std::strcmp(k,"_ENV")||seen){lua_pop(L,1);return false;}seen=true;AIRound45Inspect::globals(L);bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;}return seen;}
bool utility(Room&r,lua_State*L,int i,int first,int last){return environment(L,i)&&matchAIRound45PinnedFunction(r,L,i,"lua/utilities.lua",first,last);}
bool bindings(State*s){auto*L=s->L;Room&r=s->room;Stack st(L);AIRound45Inspect::globals(L);const int g=lua_absindex(L,-1);
 AIRound45Inspect::field(L,g,"sgs");const int sg=lua_absindex(L,-1);AIRound45Inspect::field(L,sg,"list");if(!utility(r,L,-1,38,41))return false;lua_pop(L,1);
 AIRound45Inspect::field(L,sg,"qlist");const int qlist=lua_absindex(L,-1);if(!matchAIRound45PinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){AIRound45Inspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=matchAIRound45PinnedFunction(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=matchAIRound45PinnedFunction(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){AIRound45Inspect::field(L,g,k);if(!matchAIRound45NativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 AIRound45Inspect::field(L,g,"table");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;const int table=lua_absindex(L,-1);
 AIRound45Inspect::field(L,table,"contains");if(!utility(r,L,-1,95,108))return false;lua_pop(L,1);AIRound45Inspect::field(L,table,"insert");if(!matchAIRound45NativeBinding(r,L,-1,"table.insert"))return false;
 AIRound45Inspect::field(L,g,"string");t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;AIRound45Inspect::field(L,-1,"find");if(!matchAIRound45NativeBinding(r,L,-1,"string.find"))return false;
 if(!matchAIRound45NativeNamespaceProperty(r,L,"sgs","Sanguosha"))return false;
 for(const char*k:{"getAllGenerals","getBanPackages","getSkill"})if(!matchAIRound45NativeClassMethod(r,L,"Engine",k))return false;
 for(const char*c:{"GeneralList","SkillList"})for(const char*k:{"length","at"})if(!matchAIRound45NativeClassMethod(r,L,c,k))return false;
 return matchAIRound45NativeDerivedMethods(r,L,"General",{"getPackage","isHidden","isTotallyHidden","getSkillList"})
  &&matchAIRound45NativeDerivedMethods(r,L,"Skill",{"objectName","getDescription"})
  &&matchAIRound45NativeDerivedMethods(r,L,"Player",{"property"})&&matchAIRound45NativeClassMethod(r,L,"QVariant","toStringList");
}
bool functions(State*s){auto*L=s->L;Stack st(L);AIRound45Inspect::globals(L);const int g=lua_absindex(L,-1);
 AIRound45Inspect::field(L,g,"canAiSkills");if(lua_topointer(L,-1)!=s->can||!environment(L,-1)||!matchAIRound45PinnedFunction(s->room,L,-1,"lua/ai/smart-ai.lua",8826,8844))return false;lua_pop(L,1);
 AIRound45Inspect::field(L,g,"PingjianSkill");return lua_topointer(L,-1)==s->sampler&&environment(L,-1)&&matchAIRound45PinnedFunction(s->room,L,-1,"lua/ai/tenyear_hc-ai.lua",4868,4888);
}
bool live(State*s){auto*rt=s?s->room.roomRuntime():nullptr;if(!rt||rt->isClosing()||!s->valid||!rt->lua().isCurrentThreadOwner()
 ||rt->lua().rawState()!=s->L||rt->lua().generation()!=s->generation||rt->lua().lifecycle()!=LuaRuntime::Lifecycle::Running)return false;
 Stack st(s->L);AIRound45Inspect::globals(s->L);if(lua_topointer(s->L,-1)!=s->globals)return false;
 AIRound45Inspect::field(s->L,-1,"sgs");if(lua_topointer(s->L,-1)!=s->space)return false;
 AIRound45Inspect::field(s->L,-1,"ai_fill_skill");auto*t=AIRound45Inspect::table(s->L,-1);return t&&!t->metatable&&lua_topointer(s->L,-1)==s->fill&&functions(s)&&bindings(s);
}
}
bool initializeAIRound53Pingjian(Room&r,lua_State*L){auto*rt=r.roomRuntime();if(!rt||!L||L!=rt->lua().rawState()||!rt->lua().isCurrentThreadOwner()||rt->isClosing()||!lua_checkstack(L,4096))return false;
 if(auto*old=state(r))return live(old); // Never recapture a replaced or rewritten registry.
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();Stack st(L);
 if(!registerAIRound45PinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"))
 ||!registerAIRound45PinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"))
 ||!registerAIRound45PinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5")))return false;
 AIRound45Inspect::globals(L);const void*g=lua_topointer(L,-1);const int gi=lua_absindex(L,-1);
 AIRound45Inspect::field(L,gi,"canAiSkills");s->can=lua_topointer(L,-1);lua_pop(L,1);AIRound45Inspect::field(L,gi,"PingjianSkill");s->sampler=lua_topointer(L,-1);lua_pop(L,1);if(!functions(s))return false;
 AIRound45Inspect::field(L,gi,"sgs");if(!lua_istable(L,-1))return false;const void*ns=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"ai_fill_skill");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;const int table=lua_absindex(L,-1);const void*fill=lua_topointer(L,table);
 int count=0;lua_pushnil(L);while(AIRound45Inspect::next(L,table)){
  if(++count>16384||lua_type(L,-2)!=LUA_TSTRING)return false;size_t len=0;const char*k=lua_tolstring(L,-2,&len);if(len>4096)return false;
  if(lua_toboolean(L,-1))s->truthy.insert(QString::fromUtf8(k,int(len)));lua_pop(L,1);
 }
 if(lua_observetableidentity(L,g,observe,s))s->globals=g;if(lua_observetableidentity(L,ns,observe,s))s->space=ns;if(lua_observetableidentity(L,fill,observe,s))s->fill=fill;
 s->valid=s->globals&&s->space&&s->fill;return live(s);
}
void clearAIRound53Pingjian(Room&r){delete state(r);}
void projectAIRound53Pingjian(AIWorldView&w,Room&,ServerPlayer*p){if(!p||w.self.objectName!=p->objectName())return;
 // Exact source QVariant conversion. Do not split the QString written by the
 // native Pingjian implementation; QString and QStringList are distinct types.
 auto props=w.strategyFacts.value("owner_skill_properties").toObject();props.insert("pingjian_has_used_skills",QJsonArray::fromStringList(p->property("pingjian_has_used_skills").toStringList()));w.strategyFacts.insert("owner_skill_properties",props);
}
QJsonObject queryAIRound53PingjianCatalog(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){auto fail=[](){return QJsonObject{{"known",false}};};
 auto*rt=r.roomRuntime();if(v.size()!=1||v.value("op")!="pingjian_catalog"||!p||p->getRoom()!=&r||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
 ||!Sanguosha||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!rt||rt->isClosing()||Sanguosha->currentRoom()!=&r||!aiRound44RangeRequestCurrent(r,q,p))return fail();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,4096))return fail();auto*s=state(r);if(!live(s))return fail();
 QJsonArray rows;QJsonObject descriptions;const QStringList banned=Sanguosha->getBanPackages();
 for(const General*g:Sanguosha->getAllGenerals()){
  if(banned.contains(g->getPackage())||g->isHidden()||g->isTotallyHidden())continue;
  for(const Skill*sk:g->getSkillList())if(s->truthy.contains(sk->objectName())){
   const QString name=sk->objectName();rows.append(QJsonObject{{"name",name}});const Skill*actual=Sanguosha->getSkill(name);
   descriptions.insert(name,actual?QJsonValue(actual->getDescription()):QJsonValue(false));
  }
 }
 if(!live(s)||!aiRound44RangeRequestCurrent(r,q,p))return fail();return{{"known",true},{"skills",rows},{"descriptions",descriptions}};
}
