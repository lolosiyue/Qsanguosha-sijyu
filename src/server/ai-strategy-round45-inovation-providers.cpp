#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua.hpp"
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QJsonObject>
#include <QJsonArray>
#include <cstring>
#include <memory>
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-lua-inspect.h"
namespace {
constexpr const char *ledgerName="AI.R45.InovationProviders";
constexpr const char *source="lua/ai/inovation-ai.lua";
struct Anchor {const char*name;int first,last;};
const Anchor methods[]={
 {"willSkipPlayPhase",1450,1486},{"willSkipDrawPhase",1488,1513},
 {"resetCards",1515,1523},{"shouldUseRende",1525,1583},
 {"getJijiangSlashNum",1585,1595},{"isValuableCard",1598,1612},
 {"getWoundedFriend",1614,1672},{"hasLiyuEffect",1674,1728},
 {"getLijianCard",1730,1773},{"findLijianTarget",1775,2033},
 {"canUseJieyuanDecrease",2035,2047}};
struct Stack {lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
void raw(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool environment(lua_State*L,int index){
 index=lua_absindex(L,index);bool saw=false;
 for(int n=1;;++n){const char*key=lua_getupvalue(L,index,n);if(!key)break;
  if(strcmp(key,"_ENV")||saw){lua_pop(L,1);return false;}
  AIRound45Inspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);
  if(!same)return false;saw=true;
 }
 return true; // resetCards has no global reads and consequently no _ENV.
}
struct GameBinding {
 RoomRuntime*runtime;std::unique_ptr<LuaRuntime::Binding> binding;
 explicit GameBinding(Room&r):runtime(r.roomRuntime()){
  if(runtime&&runtime->lua().rawState())binding.reset(new LuaRuntime::Binding(runtime->lua(),false));
 }
};
bool pushCallback(lua_State*L,LuaAI*ai){
 if(!ai||!ai->callback.isValid()||ai->callback.state()!=L)return false;
 AIRound45Inspect::rawGeti(L,LUA_REGISTRYINDEX,ai->callback.registryRef());
 return lua_isfunction(L,-1)&&!lua_iscfunction(L,-1);
}
struct Row {QPointer<LuaAI> native;const void*callback=nullptr;const void*self=nullptr;const void*cls=nullptr;const void*dict=nullptr;const void*dictMeta=nullptr;QHash<QString,const void*> providers;};
class Ledger final:public QObject{
public: explicit Ledger(Room&r):QObject(&r){setObjectName(ledgerName);}
 lua_State*state=nullptr;quint64 generation=0;bool initialized=false,valid=false;
 const void*cls=nullptr;const void*dict=nullptr;const void*dictMeta=nullptr;
 QHash<QString,const void*> providers;QHash<const ServerPlayer*,Row> rows;
};
Ledger*get(Room&r,bool create=false){for(auto*c:r.children())if(c->objectName()==ledgerName)return static_cast<Ledger*>(c);return create?new Ledger(r):nullptr;}
bool current(Room&r,Ledger*l,lua_State*&L){
 auto*runtime=r.roomRuntime();L=runtime?runtime->lua().state():nullptr;
 return L&&l&&l->valid&&l->state==L&&l->generation==runtime->lua().generation();
}
bool classShape(lua_State*L,Ledger*l,int&dict){
 AIRound45Inspect::globals(L);raw(L,-1,"SmartAI");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=l->cls)return false;
 raw(L,-1,"__instanceDict");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=l->dict)return false;dict=lua_absindex(L,-1);
 const void*mt=nullptr;if(lua_getmetatable(L,dict)){mt=lua_topointer(L,-1);lua_pop(L,1);}if(mt!=l->dictMeta)return false;
 raw(L,dict,"__index");const bool same=lua_rawequal(L,-1,dict);lua_pop(L,1);return same;
}
bool callbackSelf(Room&r,lua_State*L,LuaAI*ai,const ServerPlayer*p,int&self){
 if(!ai||!pushCallback(L,ai)||!matchAIRound45PinnedFunction(r,L,-1,"lua/ai/smart-ai.lua",344,375))return false;
 const int fn=lua_absindex(L,-1);bool sawSelf=false,sawEnv=false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,fn,n);if(!k)break;
  if(!strcmp(k,"self")){
   if(sawSelf||!lua_istable(L,-1))return false;sawSelf=true;self=lua_absindex(L,-1);
  }else if(!strcmp(k,"_ENV")){
   if(sawEnv)return false;sawEnv=true;AIRound45Inspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
  }else return false;
 }
 if(!sawSelf||!sawEnv)return false;
 raw(L,self,"player");const bool own=aiRound45InovationPlayerMatches(L,-1,p);lua_pop(L,1);
 raw(L,self,"room");const bool room=aiRound45InovationRoomMatches(L,-1,&r);lua_pop(L,1);return own&&room;
}
}
void initializeAIRound45InovationProviders(Room&r){
 GameBinding binding(r);
 auto*runtime=r.roomRuntime();auto*L=runtime?runtime->lua().state():nullptr;if(!L)return;
 if(!lua_checkstack(L,128))return;
 auto*l=get(r,true);if(l->initialized&&l->state==L&&l->generation==runtime->lua().generation())return;
 l->initialized=true;l->valid=false;l->rows.clear();l->providers.clear();l->state=L;l->generation=runtime->lua().generation();
 if(!registerAIRound45PinnedSource(r,L,source,QByteArray("7fb80aef6edb15d836f9c0afeb7808919fa737d05c396a0c472844638c2494c4")))return;
 // Fixed current game SmartAI (including accepted callback diagnostics); the
 // independently compiled file is NEVER executed by this metadata helper.
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5")))return;
 Stack stack(L);AIRound45Inspect::globals(L);raw(L,-1,"SmartAI");if(!lua_istable(L,-1))return;l->cls=lua_topointer(L,-1);
 raw(L,-1,"__instanceDict");if(!lua_istable(L,-1))return;const int dict=lua_absindex(L,-1);l->dict=lua_topointer(L,dict);
 l->dictMeta=nullptr;if(lua_getmetatable(L,dict)){l->dictMeta=lua_topointer(L,-1);lua_pop(L,1);}
 raw(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return;lua_pop(L,1);
 for(const auto&m:methods){raw(L,dict,m.name);
  if(matchAIRound45PinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))l->providers.insert(m.name,lua_topointer(L,-1));
  lua_pop(L,1);
 }
 l->valid=true;
 // Existing native objects, if initialization follows constructor creation.
 for(auto*p:r.getAllPlayers(true))captureAIRound45InovationProviders(r,p,p->getSmartAI());
}
void captureAIRound45InovationProviders(Room&r,ServerPlayer*p,AI*created){
 GameBinding binding(r);
 auto*l=get(r);lua_State*L=nullptr;if(!p||p->getRoom()!=&r||!current(r,l,L)||!lua_checkstack(L,128))return;l->rows.remove(p);
 auto*ai=dynamic_cast<LuaAI*>(created);Stack stack(L);int dict=0,self=0;
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self))return;
 if(!lua_getmetatable(L,self)||!lua_rawequal(L,-1,dict))return;lua_pop(L,1);
 raw(L,self,"class");const bool same=lua_topointer(L,-1)==l->cls;lua_pop(L,1);if(!same)return;
 Row row;row.native=ai;row.self=lua_topointer(L,self);row.cls=l->cls;row.dict=l->dict;row.dictMeta=l->dictMeta;
 if(!pushCallback(L,ai))return;row.callback=lua_topointer(L,-1);lua_pop(L,1);
 for(const auto&m:methods){raw(L,self,m.name);if(lua_isnil(L,-1)){lua_pop(L,1);raw(L,dict,m.name);}
  if(l->providers.contains(m.name)&&lua_topointer(L,-1)==l->providers.value(m.name)
     &&matchAIRound45PinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))row.providers.insert(m.name,lua_topointer(L,-1));
  lua_pop(L,1);
 }
 l->rows.insert(p,row);QObject::connect(p,&QObject::destroyed,l,[l,p]{l->rows.remove(p);});
}
void projectAIRound45InovationProviders(AIWorldView&w,Room&r,const ServerPlayer*p){
 GameBinding binding(r);
 if(!p||p->getRoom()!=&r||w.self.objectName!=p->objectName())return;
 QJsonObject rows;for(const auto&m:methods)rows.insert(m.name,QJsonObject{{"known",false}});
 auto publish=[&]{auto facts=w.strategyFacts.value("inovation").toObject();facts.insert("providers",rows);w.strategyFacts.insert("inovation",facts);};
 auto*l=get(r);lua_State*L=nullptr;if(!current(r,l,L)||!lua_checkstack(L,128)){publish();return;}
 const auto revision=r.roomRuntime()->stateRevision();const auto generation=l->generation;
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||!it->native||it->native!=p->getSmartAI()){publish();return;}
 Stack stack(L);int dict=0,self=0;auto*ai=it->native.data();
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self)||lua_topointer(L,self)!=it->self){publish();return;}
 if(!pushCallback(L,ai)||lua_topointer(L,-1)!=it->callback){publish();return;}lua_pop(L,1);
 if(!lua_getmetatable(L,self)||lua_topointer(L,-1)!=it->dict){publish();return;}lua_pop(L,1);
 raw(L,self,"class");const bool same=lua_topointer(L,-1)==it->cls;lua_pop(L,1);if(!same){publish();return;}
 for(const auto&m:methods){raw(L,self,m.name);if(lua_isnil(L,-1)){lua_pop(L,1);raw(L,dict,m.name);}
  if(it->providers.contains(m.name)&&lua_topointer(L,-1)==it->providers.value(m.name)
   &&matchAIRound45PinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))
   {QJsonObject result{{"known",true},{"source","inovation"},{"first",m.first},{"last",m.last}};
    if(!strcmp(m.name,"hasLiyuEffect")){AIRound45Inspect::globals(L);raw(L,-1,"getPriorFriendsOfLiyu");result.insert("prior_friends_function_absent",lua_isnil(L,-1));lua_pop(L,2);}
    rows.insert(m.name,result);}
  lua_pop(L,1);
 }
 // Everything above is allocation-free with respect to Lua, including raw
 // metadata reads. Also reject a native lifecycle/state change before publish.
 if(r.roomRuntime()->stateRevision()!=revision||r.roomRuntime()->lua().generation()!=generation||p->getSmartAI()!=ai){
  for(const auto&m:methods)rows.insert(m.name,QJsonObject{{"known",false}});
 }
 publish();
}
bool aiRound45InovationProviderKnown(Room&r,const ServerPlayer*p,const QString&method){
 AIWorldView w;if(!p)return false;w.self.objectName=p->objectName();projectAIRound45InovationProviders(w,r,p);
 return w.strategyFacts.value("inovation").toObject().value("providers").toObject().value(method).toObject().value("known").toBool();
}
bool aiRound45InovationPhaseConstruction(const AIRequest&q,const ServerPlayer*p,const QJsonObject&s){
 if(!p||!p->getRoom()||q.viewerObjectName!=p->objectName()||!aiRound44RangeRequestCurrent(*p->getRoom(),q,p)
  ||s.size()!=6||s.value("source_key")!=QJsonValue("inovation.skip_phase")||s.value("skill")!=QJsonValue("")
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 if(!aiRound45InovationProviderKnown(*p->getRoom(),p,"willSkipPlayPhase")
  &&!aiRound45InovationProviderKnown(*p->getRoom(),p,"willSkipDrawPhase"))return false;
 for(const Card*c:p->getCards("he"))if(c&&s.value("name")==QJsonValue(c->objectName())
  &&s.value("suit")==QJsonValue(int(c->getSuit()))&&s.value("number")==QJsonValue(c->getNumber()))return true;
 return false;
}
// Shared native-only binding proof for the independent continuity verifier.
// Success leaves exactly the current native owner's captured self table on L;
// failure leaves the stack unchanged. This grants no source effect/call rights.
bool pushAIRound45ActualAISelf(Room&r,const ServerPlayer*p,lua_State*L){
 if(!p||p->getRoom()!=&r||!L||!r.roomRuntime()||r.roomRuntime()->lua().state()!=L||!AIRound45Inspect::capacity(L,128))return false;
 auto*l=get(r);lua_State*bound=nullptr;if(!current(r,l,bound)||bound!=L)return false;
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||!it->native||it->native!=p->getSmartAI())return false;
 Stack stack(L);int dict=0,self=0;auto*ai=it->native.data();
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self)||lua_topointer(L,self)!=it->self)return false;
 if(!pushCallback(L,ai)||lua_topointer(L,-1)!=it->callback)return false;lua_pop(L,1);
 if(!lua_getmetatable(L,self)||lua_topointer(L,-1)!=it->dict)return false;lua_pop(L,1);
 raw(L,self,"class");if(lua_topointer(L,-1)!=it->cls)return false;lua_pop(L,1);
 lua_pushvalue(L,self);lua_replace(L,stack.n+1);lua_settop(L,stack.n+1);++stack.n;return true;
}
