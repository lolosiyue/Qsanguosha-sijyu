#include "ai-strategy-round56-clone-proof.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "settings.h"
#include <QSet>
#include <QHash>
#include <cstring>
extern bool matchAIRound56NativeRoom(lua_State*,int,const Room*);
extern const char *aiRound45NativeUserdataClass(lua_State*,int);
namespace {
using namespace AIRound45Inspect;
struct Stack {lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
constexpr const char*smart="lua/ai/smart-ai.lua";
constexpr const char*mode="lua/ai/mode-ai.lua";
constexpr const char*middle="lua/lib/middleclass.lua";
constexpr const char*utility="lua/utilities.lua";
constexpr const char*heg="lua/ai/original-hegemony/heg-common-ai.lua";
bool plain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
bool scalar(lua_State*L,int i){const int t=lua_type(L,i);return t==LUA_TNIL||t==LUA_TNUMBER||t==LUA_TBOOLEAN||t==LUA_TSTRING;}
struct Check {
 Room&r;lua_State*L;QString why;int env=0,ns=0,cls=0,dict=0;const void*standardModes=nullptr;QSet<const void*> protectedRoots;
 bool no(const QString&s){if(why.isEmpty())why=s;return false;}
 bool code(int i,const char*p,int first,int last){return matchAIRound45PinnedFunction(r,L,i,p,first,last)||no(QString("code %1:%2").arg(p).arg(first));}
 bool capture(int i,const char*k){i=lua_absindex(L,i);for(int n=1;;++n){const char*name=lua_getupvalue(L,i,n);if(!name)return no(QString("missing capture ")+k);if(!std::strcmp(name,k))return true;lua_pop(L,1);}}
 bool closure(int i,const char*p,int first,int last,const QSet<QByteArray>&captures={}){
  i=lua_absindex(L,i);if(!code(i,p,first,last))return false;QSet<QByteArray>seen;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;QByteArray key(k);bool ok=false;
   if(key=="_ENV")ok=lua_rawequal(L,-1,env);else ok=captures.contains(key);
   lua_pop(L,1);if(!ok||seen.contains(key))return no(QString("unexpected closure capture ")+key);seen.insert(key);
  }
  for(const auto&k:captures)if(!seen.contains(k))return no(QString("missing audited capture ")+k);return true;
 }
 bool fieldClosure(int t,const char*k,const char*p,int first,int last,const QSet<QByteArray>&captures={}){Stack z(L);field(L,t,k);return closure(-1,p,first,last,captures);}
 bool native(int t,const char*k,const QString&name){Stack z(L);field(L,t,k);return matchAIRound45NativeBinding(r,L,-1,name)||no("native binding "+name);}
 bool capturedClosure(int f,const char*k,const char*p,int first,int last,const QSet<QByteArray>&caps={}){Stack z(L);return capture(f,k)&&closure(-1,p,first,last,caps);}
 bool nil(int t,const char*k){Stack z(L);field(L,t,k);return lua_isnil(L,-1)||no(QString("unexpected metamethod ")+k);}
 // Simulate only the pinned middleclass table lookup. No authored __index runs.
 bool lookup(int t,const char*k,int depth=0){
  if(depth>20||!lua_istable(L,t))return no("class lookup chain");t=lua_absindex(L,t);protectedRoots.insert(lua_topointer(L,t));const int top=lua_gettop(L);
  field(L,t,k);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,t)){push(L,nullptr);return true;}const int mt=lua_absindex(L,-1);protectedRoots.insert(lua_topointer(L,mt));
  if(!nil(mt,"__gc")||!nil(mt,"__close"))return false;field(L,mt,"__index");
  if(lua_isnil(L,-1))return true;
  bool ok=false;
  if(lua_istable(L,-1))ok=lookup(lua_absindex(L,-1),k,depth+1);
  else {
   const int f=lua_absindex(L,-1);const bool inherited=matchAIRound45PinnedFunction(r,L,f,middle,33,33);
   if(!closure(f,middle,inherited?33:35,inherited?33:35,inherited?QSet<QByteArray>{"dict","superStatic"}:QSet<QByteArray>{"dict"}))return false;
   if(!capture(f,"dict")||!lua_istable(L,-1)||!lookup(-1,k,depth+1))return false;
   if(lua_toboolean(L,-1)||!inherited)ok=true;
   else if(capture(f,"superStatic"))ok=lookup(-1,k,depth+1);
  }
  if(!ok)return false;lua_replace(L,top+1);lua_settop(L,top+1);return true;
 }
 bool modeEnabled(int i){
  i=lua_absindex(L,i);if(!closure(i,mode,2288,2305,{"standard_modes","policies","managed_rooms"}))return false;
  // Only standard_modes[public Room mode] is reached. Neither policies nor
  // managed_rooms is read on this public branch, regardless of hidden roles.
  Stack z(L);if(!capture(i,"standard_modes")||!plain(L,-1))return no("standard mode public table");standardModes=lua_topointer(L,-1);protectedRoots.insert(standardModes);field(L,-1,"02p");return (lua_type(L,-1)==LUA_TSTRING||lua_type(L,-1)==LUA_TBOOLEAN)&&lua_toboolean(L,-1)||no("02p public mode registration");
 }
 bool policy(int i){return closure(i,mode,2314,2318);}
 bool currentAI(int i){return closure(i,mode,2280,2285);}
 bool sourceGraph(){
  if(!fieldClosure(env,"CloneAI",smart,23,25))return false;
  if(!lua_getmetatable(L,cls))return no("class metatable");field(L,-1,"__call");if(!closure(-1,middle,44,44))return false;lua_pop(L,2);
  for(auto row:{qMakePair("new",103),qMakePair("allocate",98)}){Stack z(L);if(!lookup(cls,row.first)||!closure(-1,middle,row.second,row.second==103?107:101))return false;}
  field(L,cls,"__instanceDict");dict=lua_absindex(L,-1);protectedRoots.insert(lua_topointer(L,dict));if(!lua_istable(L,dict))return no("instance dict");field(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return no("instance __index");lua_pop(L,1);
  for(const char*k:{"__newindex","__eq","__gc","__close"}){Stack z(L);if(!lookup(dict,k)||!lua_isnil(L,-1))return no(QString("instance metamethod ")+k);}
  // Public no-hegemony bootstrap leaves the ordinary base initializer.
  // basara58 is gated by GetConfig EnableHegemony; original-hegemony common
  // returns unless its publicly gated loader sets original_hegemony_ai_loading.
  {Stack z(L);if(!lookup(dict,"initialize")||!closure(-1,smart,339,429,{"version"}))return false;
   if(!capture(-1,"version")||lua_type(L,-1)!=LUA_TSTRING)return no("version scalar");}
  {Stack z(L);if(!lookup(dict,"updatePlayers")||!closure(-1,mode,2342,2355,{"policy","update"}))return false;const int f=lua_absindex(L,-1);
   if(!capturedClosure(f,"update",smart,2118,2180)||!capture(f,"policy")||!policy(-1))return false;}
  {Stack z(L);field(L,ns,"modeAIEnabled");if(!modeEnabled(-1))return false;}
  {Stack z(L);field(L,env,"isRolePredictable");const int f=lua_absindex(L,-1);
   if(!closure(f,mode,2385,2394,{"current_ai","policy","policies","standard_modes","predictable"})
    ||!capturedClosure(f,"predictable",smart,1425,1437))return false;
   {Stack a(L);if(!capture(f,"policy")||!policy(-1))return false;}
   {Stack a(L);if(!capture(f,"current_ai")||!currentAI(-1))return false;}
   // current_ai sees either the just-created raw room/player fields or an
   // authenticated existing self below. policy always returns nil in 02p.
   for(const char*k:{"policies","standard_modes"}){Stack a(L);if(!capture(f,k)||!plain(L,-1))return no("predictable mode table");protectedRoots.insert(lua_topointer(L,-1));if(!std::strcmp(k,"standard_modes")&&lua_topointer(L,-1)!=standardModes)return no("mode registration capture alias changed");}
  }
  {Stack z(L);field(L,env,"updateAlivePlayerRoles");const int f=lua_absindex(L,-1);
   if(!closure(f,mode,2410,2418,{"current_ai","policy","counts"})||!capturedClosure(f,"counts",smart,2105,2116))return false;
   {Stack a(L);if(!capture(f,"policy")||!policy(-1))return false;}{Stack a(L);if(!capture(f,"current_ai")||!currentAI(-1))return false;}}
  if(!fieldClosure(ns,"qlist_cached",smart,443,454)||!fieldClosure(ns,"QList2Table",utility,44,51)
   ||!fieldClosure(ns,"getValue",smart,560,563)||!fieldClosure(ns,"getDefense",smart,574,709))return false;
  {Stack z(L);field(L,ns,"ai_compare_funcs");protectedRoots.insert(lua_topointer(L,-1));if(!plain(L,-1)||!fieldClosure(lua_absindex(L,-1),"chaofeng",smart,1165,1167))return false;}
  {Stack z(L);field(L,ns,"qlist");const int f=lua_absindex(L,-1);
   if(!closure(f,utility,19,35,{"qlist_userdata_iterator","qlist_table_iterator"})
    ||!capturedClosure(f,"qlist_userdata_iterator",utility,7,11)||!capturedClosure(f,"qlist_table_iterator",utility,13,17))return false;}
  return true;
 }
 bool rootsAndData(){
  globals(L);env=lua_absindex(L,-1);if(!plain(L,env))return no("global environment metatable");protectedRoots.insert(lua_topointer(L,env));
  field(L,env,"sgs");ns=lua_absindex(L,-1);if(!lua_istable(L,ns))return no("sgs namespace");protectedRoots.insert(lua_topointer(L,ns));
  {Stack z(L);field(L,ns,"ai_use_priority");if(!plain(L,-1))return no("actual priority root");protectedRoots.insert(lua_topointer(L,-1));}
  field(L,env,"SmartAI");cls=lua_absindex(L,-1);if(!lua_istable(L,cls))return no("actual class");
  for(int type:{LUA_TNIL,LUA_TNUMBER,LUA_TBOOLEAN,LUA_TFUNCTION}){Stack z(L);if(primitiveMetatable(L,type))return no("primitive metamethod");}
  if(!matchAIRound47NativeStringMetatable(r,L)||!matchAIRound47NativeValueMetamethods(r,L,&why))return false;
  // The mutable caches, cached public mode and current_ai capability pair
  // come only from native namespace birth plus actual bootstrap/constructor
  // writes. Do not traverse current private values, sizes or cache occupancy.
  if(!hasAIRound56CloneInputLineage(r,L))return no("constructor input source lineage unavailable");
  {Stack z(L);field(L,env,"_VERSION");if(lua_type(L,-1)!=LUA_TSTRING)return no("version concatenation operand");}
  // Every source namespace write must use the original raw/native setter path.
  for(const char*k:{"defense","drawData","turncount","throwData","damageData","convertData","recoverData","sort_time","getMode","AIChat","ai_humanized","debugmode","explicit_renegade"})
   if(!matchAIRound45NativeNamespaceWrite(r,L,"sgs",k))return no(QString("namespace writer ")+k);
  return true;
 }
 bool leaves(){
  for(const char*k:{"type","ipairs","pairs","assert","setmetatable","error","tostring"})if(!native(env,k,k))return false;
  for(auto lib:{qMakePair("table",QStringList{"insert","sort"}),qMakePair("string",QStringList{"sub"}),qMakePair("os",QStringList{"time"}),qMakePair("debug",QStringList{"traceback"})}){
   Stack z(L);field(L,env,lib.first);if(!plain(L,-1))return no("library metatable");protectedRoots.insert(lua_topointer(L,-1));for(const auto&k:lib.second)if(!native(lua_absindex(L,-1),k.toUtf8().constData(),QString(lib.first)+'.'+k))return false;}
  if(!native(ns,"GetConfig","sgs.GetConfig"))return false;
  for(const char*k:{"LuaAI","QVariant","IntList"}){Stack z(L);field(L,ns,k);if(!matchAIRound47NativeScratchConstructor(r,L,-1,k))return no(QString("native factory ")+k);}
  if(!matchAIRound56NativeClassSetter(r,L,"LuaAI","callback"))return no("LuaAI callback native setter");
  const QHash<QString,QStringList> methods={
   {"ServerPlayer",{"getRoom","getRole","objectName","getHp","getHandcardNum","getHandPile"}},
   {"Room",{"getTag","setTag","getMode","getPlayers","getAllPlayers","getAlivePlayers","writeToConsole"}},
   {"LuaAI",{"isFriend","isEnemy","relationTo"}},
   {"QVariant",{"toBool"}}, {"IntList",{"length"}}, {"SPlayerList",{"length","at"}}};
  for(auto i=methods.cbegin();i!=methods.cend();++i)for(const auto&k:i.value())if(!matchAIRound45NativeClassMethod(r,L,i.key(),k))return no("native method "+i.key()+'.'+k);
  {Stack z(L);field(L,ns,"AI_Neutrality");if(!matchAIRound45NativeConstant(r,L,-1,"sgs.AI_Neutrality"))return no("relation constant");}
  return true;
 }
};
}
bool registerAIRound56CloneSources(Room&r,lua_State*L,QString*why){
 struct Source{const char*path;const char*hash;};const Source sources[]={
  {smart,"f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"},
  {mode,"5a36ef4194b804790c0bb8c897a7e40ccba26e107b7e0e2965f6fe513eecd9c1"},
  {middle,"32938b2068cb9b8a5b6a8d734bce0a6c73020f6a128e9cf9bfca200490cedf6e"},
  {utility,"b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"},
  {heg,"9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"}};
 for(const auto&s:sources)if(!registerAIRound45PinnedSource(r,L,s.path,s.hash,why))return false;return true;
}
bool proveAIRound56CloneSource(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 if(r.getMode()!="02p"||Config.EnableHegemony||r.getScenario())return fail("public constructor domain");
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||rt->lua().rawState()!=L||!rt->lua().isCurrentThreadOwner()||!capacity(L,8192))return fail("constructor native runtime/inspection stack");
 if(!proveAIRound56ServerLogEffects(r))return fail("constructor logging host graph");
 Stack z(L);Check c{r,L};const bool ok=c.rootsAndData()&&c.sourceGraph()&&c.leaves()&&c.rootsAndData();if(why)*why=c.why;return ok;
}
