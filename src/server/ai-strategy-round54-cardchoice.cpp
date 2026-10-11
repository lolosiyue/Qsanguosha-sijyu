#include "ai-strategy-round54-cardchoice.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "card.h"
#include <QJsonArray>
#include "ai-strategy-round45-inovation-providers.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include <QSet>
#include <cmath>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
// Local declaration keeps the identity-only native helper out of shared headers.
extern "C" {
LUALIB_API int luaL_isstandardstringadd(lua_CFunction function);
}
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool current(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(p)
 &&q.decisionId&&q.conversionLedger&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&Sanguosha&&Sanguosha->currentRoom()==&r&&aiRound44RangeDecisionActive(r,q,p);}
// Exact table-valued __index inheritance only. Never invoke a replaced getter.
bool lookup(lua_State*L,int self,const char*key){QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AIRound45Inspect::field(L,table,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;
 }return false;}
struct Call{Room*r;ServerPlayer*p;const AIRequest*q;QByteArray key;lua_State*L=nullptr;quint64 generation=0;
 const void*globals=nullptr,*space=nullptr,*table=nullptr,*self=nullptr;bool arithmetic=false,written=false;const char*error=nullptr;};
bool roots(lua_State*L,Call&w,bool capture=false){Stack z(L);auto*rt=w.r->roomRuntime();if(!current(*w.r,w.p,*w.q)||rt->lua().rawState()!=w.L||rt->lua().generation()!=w.generation)return false;
 AIRound45Inspect::globals(L);const int env=lua_absindex(L,-1);const void*g=lua_topointer(L,env);
 AIRound45Inspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;const void*ns=lua_topointer(L,-1);
 AIRound45Inspect::field(L,-1,"throwData");auto*t=AIRound45Inspect::table(L,-1);if(!t||t->metatable)return false;const void*target=lua_topointer(L,-1);
 if(!pushAIRound45ActualAISelf(*w.r,w.p,L))return false;const int self=lua_absindex(L,-1);const void*owner=lua_topointer(L,self);
 if(!lookup(L,self,"askForCardChosen")||!matchAIRound45PinnedFunction(*w.r,L,-1,"lua/ai/smart-ai.lua",3435,3626))return false;const int fn=lua_absindex(L,-1);bool saw=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,fn,i);if(!k)break;bool ok=!std::strcmp(k,"_ENV")&&!saw&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;saw=true;}
 if(!saw)return false;
 if(capture){w.globals=g;w.space=ns;w.table=target;w.self=owner;return true;}
 return w.globals==g&&w.space==ns&&w.table==target&&w.self==owner;
}
int fail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool sourceArithmeticBinding(lua_State*L,int index){const bool string=lua_type(L,index)==LUA_TSTRING;
 if(!lua_getmetatable(L,index))return true;AIRound45Inspect::field(L,-1,"__add");const auto*method=AIRound45Inspect::value(L,-1);
 // Admit only Lua's exact noncapturing string-add function, never a closure
 // observed in the current VM. Other operand types still require no __add.
 const bool admitted=lua_isnil(L,-1)||(string&&ttislcf(method)&&luaL_isstandardstringadd(fvalue(method)));
 lua_pop(L,2);return admitted;}
int run(lua_State*L){auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!w||!lua_checkstack(L,4096))return luaL_error(L,"ordinary card-choice prefix frame unavailable");
 if(!roots(L,*w,true))return fail(L,w,"ordinary actual source provider or registry binding changed");
 // Allocate the exact string before reading the source cell; reauthenticate
 // after allocation. All later reads/writes use the rooted actual table/key.
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));int key=lua_absindex(L,-1);
 if(!roots(L,*w))return fail(L,w,"ordinary source changed during key allocation");
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"sgs");AIRound45Inspect::field(L,-1,"throwData");int table=lua_absindex(L,-1);
 lua_pushvalue(L,key);lua_gettable(L,table);
  if(!lua_toboolean(L,-1)){lua_pop(L,1);lua_pushinteger(L,0);}
  const int type=lua_type(L,-1);if((type!=LUA_TNUMBER&&type!=LUA_TSTRING&&type!=LUA_TBOOLEAN)||!sourceArithmeticBinding(L,-1))return fail(L,w,"ordinary throwData arithmetic requires an unproved value/metamethod");
  lua_pushinteger(L,1);
  // Standard string arithmetic delegates failed conversion to the RHS __add;
  // prove its absence before entering arithmetic or classifying source errors.
  if(!sourceArithmeticBinding(L,-1))return fail(L,w,"ordinary throwData right operand arithmetic binding changed");
  // Lua performs its own integer/float/string arithmetic and ordinary errors.
  // A bad primitive stops before assignment; never coerce it to zero.
  w->arithmetic=true;lua_arith(L,LUA_OPADD);w->arithmetic=false;
  lua_pushvalue(L,key);lua_insert(L,-2);
 if(!roots(L,*w))return fail(L,w,"ordinary source request changed before prefix assignment");
 lua_settable(L,table);w->written=true;
 if(!roots(L,*w))return fail(L,w,"ordinary source request changed after prefix assignment");return 0;
}
// R55: source sortByKeepValue's #cards<2 prefix has no keep-cache reads or
// writes. Prove the actual foreign receiver and reached source dependencies;
// do not expose another AI's keepValue/keepdata/toUse or call its policy.
QJsonObject trivialForeignKeep(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto no=[](){return QJsonObject{{"error","foreign trivial keep-sort source unavailable"},{"key","askForCardChosen.ownerSort"}};};
 if(input.size()!=3||input.value("op")!="foreign_keep_trivial"||!input.value("owner").isString()
  ||!input.value("ids").isArray()||input.value("ids").toArray().size()>1||!current(r,p,q))return no();
 const QString name=input.value("owner").toString();if(name.isEmpty()||name==p->objectName())return no();
 const AIPlayerView*shown=nullptr;for(const auto&v:q.worldView.players)if(v.objectName==name){shown=&v;break;}
 if(!shown||shown->equips.size()>1||shown->equips.size()!=input.value("ids").toArray().size())return no();
 const auto ids=input.value("ids").toArray();
 for(int i=0;i<ids.size();++i)if(!ids[i].isDouble()||ids[i].toDouble()!=shown->equips[i].effectiveId)return no();
 ServerPlayer*owner=nullptr;for(auto*t:r.getAllPlayers(true))if(t&&t->objectName()==name){owner=t;break;}
 if(!owner)return no();const auto actual=owner->getEquips();if(actual.size()!=ids.size())return no();
 for(int i=0;i<actual.size();++i)if(!actual[i]||actual[i]->getEffectiveId()!=ids[i].toInt())return no();
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,4096))return no();Stack stack(L);const auto generation=vm.generation();
 AIRound45Inspect::globals(L);const int env=lua_absindex(L,-1);
 AIRound45Inspect::field(L,env,"sgs");if(!lua_istable(L,-1))return no();const int ns=lua_absindex(L,-1);
 auto plain=[&](int n){auto*t=AIRound45Inspect::table(L,n);return t&&!t->metatable;};
 auto code=[&](int n,const char*path,int first,int last){n=lua_absindex(L,n);
  if(!matchAIRound45PinnedFunction(r,L,n,path,first,last))return false;bool saw=false;
  for(int i=1;;++i){const char*k=lua_getupvalue(L,n,i);if(!k)break;
   const bool ok=!std::strcmp(k,"_ENV")&&!saw&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;saw=true;}return true;};
 if(!pushAIRound45ActualAISelf(r,p,L)||!lookup(L,-1,"askForCardChosen")||!code(-1,"lua/ai/smart-ai.lua",3435,3626))return no();
 if(!pushAIRound45ActualAISelf(r,owner,L))return no();const int receiver=lua_absindex(L,-1);
 AIRound45Inspect::field(L,ns,"ais");if(!plain(-1))return no();
 const auto key=name.toUtf8();AIRound45Inspect::field(L,-1,key.constData());if(!lua_rawequal(L,receiver,-1))return no();
 if(!lookup(L,receiver,"sortByKeepValue")||!code(-1,"lua/ai/smart-ai.lua",1251,1293))return no();
 AIRound45Inspect::field(L,ns,"QList2Table");if(!code(-1,"lua/utilities.lua",44,51))return no();
 AIRound45Inspect::field(L,ns,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIRound45PinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return no();
 bool sawEnv=false,sawUser=false,sawTable=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,qlist,i);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){ok=!sawEnv&&lua_rawequal(L,env,-1);sawEnv=true;}
  else if(!std::strcmp(k,"qlist_userdata_iterator")){ok=!sawUser&&code(-1,"lua/utilities.lua",7,11);sawUser=true;}
  else if(!std::strcmp(k,"qlist_table_iterator")){ok=!sawTable&&matchAIRound45PinnedFunction(r,L,-1,"lua/utilities.lua",13,17);sawTable=true;}
  lua_pop(L,1);if(!ok)return no();}
 if(!sawEnv||!sawUser||!sawTable)return no();
 AIRound45Inspect::field(L,env,"type");if(!matchAIRound45NativeBinding(r,L,-1,"type"))return no();
 AIRound45Inspect::field(L,env,"table");if(!plain(-1))return no();AIRound45Inspect::field(L,-1,"insert");
 if(!matchAIRound45NativeBinding(r,L,-1,"table.insert")
  ||!matchAIRound45NativeDerivedMethods(r,L,"ServerPlayer",{"objectName","getEquips"})
  ||!matchAIRound45NativeClassMethod(r,L,"CardList","length")||!matchAIRound45NativeClassMethod(r,L,"CardList","at"))return no();
 if(!current(r,p,q)||vm.generation()!=generation)return no();return{{"certified",true}};
}

}
bool registerAIRound54CardChoiceSource(Room&r,lua_State*L){return registerAIRound45PinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"))
 &&registerAIRound45PinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"));}
QJsonObject queryAIRound54CardChoicePrefix(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 if(input.value("op")=="foreign_keep_trivial")return trivialForeignKeep(r,p,q,input);
 auto no=[](const QString&s){return QJsonObject{{"error",s},{"key","askForCardChosen.prefix"}};};
 if(input.size()!=2||input.value("op")!="increment_throw"||!input.value("key").isString()||!current(r,p,q))return no("ordinary throwData prefix schema or current request unavailable");
 const QString key=input.value("key").toString();if(key.size()>256||key.contains(QChar(0)))return no("ordinary reason outside finite string domain");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("ordinary gameplay VM unavailable");Stack z(L);
 Call w;w.r=&r;w.p=p;w.q=&q;w.key=key.toUtf8();w.L=L;w.generation=vm.generation();
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,1,0,0);
 if(status!=LUA_OK){if(w.arithmetic&&status==LUA_ERRRUN&&!w.error)return{{"source_error",QString::fromUtf8(lua_tostring(L,-1)?lua_tostring(L,-1):"ordinary throwData arithmetic error")}};
  return no(w.error?QString::fromLatin1(w.error):QStringLiteral("ordinary protected prefix operation failed"));}
 if(!w.written||!current(r,p,q))return no("ordinary source prefix did not complete on current request");return{{"written",true}};
}
