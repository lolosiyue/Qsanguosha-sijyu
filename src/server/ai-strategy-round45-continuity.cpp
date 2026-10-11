#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round47-heqia.h"
#include "ai-strategy-round47b-leiluan.h"
#include "ai-strategy-round46-jianman.h"
#include "ai-strategy-round45-xufa-slot.h"
#include "ai-strategy-round45-continuity.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round47-planning-slots.h"
#include "ai-strategy-round46-planning-slots.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round41-response.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-natural-filter.h"
#include "ai-strategy-round45-correction-proof.h"
#include "ai-strategy-round45-xufa-filter.h"
#include "room.h"
#include "engine.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "runtime-paths.h"
#include "lua.hpp"
#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QObject>
#include <QScopeGuard>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"

namespace {
constexpr const char *ledgerName = "AI.R45.ContinuityCertificate";
struct Stack {
 lua_State *L; int top;
 explicit Stack(lua_State *s):L(s),top(lua_gettop(s)){}
 ~Stack(){lua_settop(L,top);}
};
void raw(lua_State *L,int i,const char *key){AIRound45Inspect::field(L,i,key);}
bool plain(lua_State *L,int i){
 if(!lua_istable(L,i))return false;
 if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;
}
const Proto *proto(lua_State *L,int i){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return nullptr;
 return static_cast<const LClosure *>(lua_topointer(L,i))->p;
}

struct Source {
 const char *path; const char *sha;
};
const Source modeSource{"lua/ai/mode-ai.lua","f1f4b9ef3a5e9878bf0d66b521447e62c30f9e8a2a1cec94b155e9af65e7c8e5"};
const Source smartSource{"lua/ai/smart-ai.lua","f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"};
const Source utilitiesSource{"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"};
const Source sanguoshaSource{"lua/sanguosha.lua","fb7ac93090859f607f17158ea9a9ad92c86479457f5193de8c3486d606c5e601"};
const Source sgsExSource{"lua/sgs_ex.lua","26445717639068578986fefa44c396f40426b155410b47c3538e450794e4aece"};
class Certificate final:public QObject {
public:
 explicit Certificate(Room &r):QObject(&r),room(r){setObjectName(ledgerName);}
 Room &room; lua_State *L=nullptr; quint64 generation=0;
 bool initialized=false, modeReady=false, patternsReady=false, bossReady=false, naturalReady=false, correctionReady=false;
 void release(){L=nullptr;modeReady=false;patternsReady=false;bossReady=false;naturalReady=false;correctionReady=false;}

};
Certificate *ledger(Room &r,bool create=false){
 for(QObject *child:r.children())if(child->objectName()==ledgerName)
  return static_cast<Certificate *>(child);
 return create?new Certificate(r):nullptr;
}
// Audit domain: these bodies operate solely on native primitive mode snapshots
// and private primitive mode minds. The registration routines, real SmartAI
// wrappers and intention/event entrypoints are deliberately not roots. Nested
// closures created inside these exact bodies are included by complete Proto
// identity, not admitted by a path/line range.
const QList<int> modeAnchors={10,24,40,49,59,74,209,221,240,250,310,317,321,327,333,
 360,621,627,636,846,860,942,1017,1027,1039,1054,1158,1184,1193,1234,1240,1252,1262,1286,1307,1346,1364};

struct Check {
 Certificate &c; lua_State *L; QString failure; int budget=100000;
 QSet<const void *> functions,tables;
 explicit Check(Certificate &v):c(v),L(v.L){}
 bool no(const QString &where,const QString &why){if(failure.isEmpty())failure=where+": "+why;return false;}
 bool nativeBinding(int actual,const char *path,const QString &where){
  return matchAIRound45NativeBinding(c.room,L,actual,QString::fromLatin1(path))
    ?true:no(where,"native binding replaced or unaudited");
 }
 bool global(const QString &key,const QString &where,const Proto *caller){
  static const QSet<QString> values={"assert","error","getmetatable","ipairs","next","pairs","pcall",
   "rawequal","rawget","select","setmetatable","tonumber","tostring","type","unpack"};
  Stack stack(L);AIRound45Inspect::globals(L);raw(L,-1,key.toUtf8().constData());
  if(values.contains(key))return nativeBinding(-1,key.toUtf8().constData(),where);
  if(key=="sgs"&&caller->linedefined==1054){
   // Audited standard_intention reads only the native immutable enum.
   if(!lua_istable(L,-1))return no(where,"sgs namespace absent");
   raw(L,-1,"Player_Play");return matchAIRound45NativeConstant(c.room,L,-1,QStringLiteral("sgs.Player_Play"))
    ?true:no(where,"Player_Play enum replaced");
  }
  static const QHash<QString,QStringList> fields={
   {"math",{"abs","floor","ceil","max","min","huge","random"}},
   {"string",{"sub","find","match","gsub","format","len"}},
   {"table",{"insert","remove","sort","concat","unpack"}}};
  if(!fields.contains(key)||!plain(L,-1))return no(where,"unapproved global capability "+key);
  const int table=lua_absindex(L,-1);
  for(const auto &field:fields.value(key)){
   raw(L,table,field.toUtf8().constData());
   const QByteArray path=(key+"."+field).toUtf8();
   bool ok;
   if(key=="math"&&field=="huge")ok=matchAIRound45NativeConstant(c.room,L,-1,path);
   else ok=nativeBinding(-1,path.constData(),where+"."+field);
   lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool environment(const Proto *p,int up,const QString &where){
  // Lua's emitted _ENV accesses must be literal GETTABUP. Passing _ENV as a
  // value, dynamic indexing or writing globals is outside this pure-mode proof.
  for(int n=0;n<p->sizecode;++n){
   const Instruction instruction=p->code[n];const OpCode op=GET_OPCODE(instruction);
   if(op==OP_GETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment escapes as a value");
   if(op==OP_SETTABUP&&GETARG_A(instruction)==up)return no(where,"global write outside mode proof");
   if(op==OP_SETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment replacement");
   if(op==OP_GETTABUP&&GETARG_B(instruction)==up){
    const int constant=GETARG_C(instruction);
    if(constant<0||constant>=p->sizek||!ttisstring(&p->k[constant]))return no(where,"nonliteral environment lookup");
    const auto *s=tsvalue(&p->k[constant]);
    if(!global(QString::fromUtf8(getstr(s),qsizetype(tsslen(s))),where,p))return false;
   }
  }
  return true;
 }
 bool value(int index,const QString &where,int depth=0,bool callbacks=true){
  if(--budget<0||depth>64)return no(where,"finite graph budget exceeded");
  index=lua_absindex(L,index);const int type=lua_type(L,index);
  if(type==LUA_TNIL||type==LUA_TBOOLEAN||type==LUA_TNUMBER||type==LUA_TSTRING)return true;
  if(type==LUA_TFUNCTION)return callbacks?function(index,where,depth+1):no(where,"function in primitive native argument");
  if(type!=LUA_TTABLE)return no(where,"userdata/thread/native capability in primitive graph");
  const void *identity=lua_topointer(L,index);if(tables.contains(identity))return true;tables.insert(identity);
  if(lua_getmetatable(L,index)){
   // Only weak-index bookkeeping created by the fixed module is permitted at
   // entry. Readonly proxies are local to an invocation and cannot be inputs.
   const int meta=lua_absindex(L,-1);bool weak=plain(L,meta);int fields=0;
   lua_pushnil(L);while(AIRound45Inspect::next(L,meta)){
    ++fields;weak=weak&&lua_type(L,-2)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-2))=="__mode"
      &&lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))=="k";lua_pop(L,1);
   }lua_pop(L,1);
   if(!callbacks||!weak||fields!=1)return no(where,"unapproved metatable");
  }
  lua_pushnil(L);while(AIRound45Inspect::next(L,index)){
   if(!value(-2,where+".<key>",depth+1,callbacks)||!value(-1,where+".<value>",depth+1,callbacks)){
    lua_pop(L,2);return false;
   }lua_pop(L,1);
  }return true;
 }
 bool childEnvironments(const Proto *p,const QString &where){
  for(int i=0;i<p->sizep;++i){const Proto *child=p->p[i];
   for(int u=0;u<child->sizeupvalues;++u)
    if(child->upvalues[u].name&&!std::strcmp(getstr(child->upvalues[u].name),"_ENV")
       &&!environment(child,u,where+".<child>"))return false;
   if(!childEnvironments(child,where+".<child>"))return false;
  }return true;
 }
 bool primitiveMetatables(){
  Stack stack(L);
  for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){
   if(kind==LUA_TNIL)lua_pushnil(L);
   else if(kind==LUA_TBOOLEAN)lua_pushboolean(L,0);
   else if(kind==LUA_TNUMBER)lua_pushnumber(L,0);
   else {AIRound45Inspect::globals(L);raw(L,-1,"type");lua_remove(L,-2);}
   if(lua_getmetatable(L,-1))return no("primitive.metatable","unexpected primitive metamethod capability");
   lua_pop(L,1);
  }
  if(!AIRound45Inspect::primitiveMetatable(L,LUA_TSTRING)||!plain(L,-1))return no("string.metatable","missing native string metatable");
  const int meta=lua_absindex(L,-1);int count=0;
  lua_pushnil(L);while(AIRound45Inspect::next(L,meta)){
   ++count;if(lua_type(L,-2)!=LUA_TSTRING||std::strcmp(lua_tostring(L,-2),"__index"))return no("string.metatable","additional string metamethod");
   AIRound45Inspect::globals(L);raw(L,-1,"string");bool same=lua_rawequal(L,-1,-3);lua_pop(L,2);
   if(!same)return no("string.metatable","string lookup redirected");lua_pop(L,1);
  }
  // Pinned bodies also use primitive method syntax (name:sub/find/match),
  // which performs no GETTABUP string lookup. Seal those leaves regardless
  // of whether another source expression references the string global.
  if(count!=1)return false;
  AIRound45Inspect::globals(L);raw(L,-1,"string");const int strings=lua_absindex(L,-1);
  if(!plain(L,strings))return no("string.methods","method library redirected");
  for(const char *key:{"sub","find","match"}){raw(L,strings,key);
   bool ok=nativeBinding(-1,(QByteArray("string.")+key).constData(),"string.methods");lua_pop(L,1);if(!ok)return false;}
  return true;
 }
 bool function(int index,const QString &where,int depth){
  index=lua_absindex(L,index);const Proto *p=proto(L,index);
  if(!p||!matchAIRound45PinnedFunctionFamily(c.room,L,index,QString::fromLatin1(modeSource.path),modeAnchors))return no(where,"Lua callback is outside pinned mode closure union");
  const void *identity=lua_topointer(L,index);if(functions.contains(identity))return true;functions.insert(identity);
  if(!childEnvironments(p,where))return false;
  for(int n=1;;++n){
   const char *name=lua_getupvalue(L,index,n);if(!name)break;
   bool ok;
   if(!std::strcmp(name,"_ENV")){
    AIRound45Inspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);
    ok=ok&&environment(p,n-1,where+"._ENV");
   }else ok=value(-1,where+"."+QString::fromUtf8(name),depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};
bool exactEnvironment(lua_State *L,int fn){
 fn=lua_absindex(L,fn);Stack stack(L);
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;
  if(std::strcmp(name,"_ENV"))return false;
  AIRound45Inspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
 }return true;
}
bool certifyPatterns(Certificate &c,int fn,int count,QString *reason){
 auto fail=[&](const char *why){if(reason)*reason=QString::fromLatin1(why);return false;};
 lua_State *L=c.L;Stack stack(L);Check check(c);
 if(count!=1||!check.value(fn+1,"patterns.argument",0,false))return fail("patterns argument is not a primitive native value");
 // The fixed native callers use true/string/nil. A table argument would be
 // truthy but cannot introduce an arbitrary aliased object into the proof.
 const int kind=lua_type(L,fn+1);
 if(kind!=LUA_TNIL&&kind!=LUA_TSTRING&&kind!=LUA_TBOOLEAN)return fail("patterns argument ABI changed");
 AIRound45Inspect::globals(L);const int globals=lua_absindex(L,-1);
 if(!plain(L,globals))return fail("patterns global assignment could invoke a metamethod");
 for(const char *name:{"type","ipairs"}){raw(L,globals,name);
  bool ok=matchAIRound45NativeBinding(c.room,L,-1,QString::fromLatin1(name));lua_pop(L,1);if(!ok)return fail("patterns native primitive binding changed");}
 raw(L,globals,"string");const int strings=lua_absindex(L,-1);if(!plain(L,strings))return fail("patterns string library redirected");
 raw(L,strings,"sub");if(!matchAIRound45NativeBinding(c.room,L,-1,"string.sub"))return fail("patterns string.sub changed");lua_pop(L,2);
 raw(L,globals,"table");const int tables=lua_absindex(L,-1);if(!plain(L,tables))return fail("patterns table library redirected");
 raw(L,tables,"insert");if(!matchAIRound45NativeBinding(c.room,L,-1,"table.insert"))return fail("patterns table.insert changed");lua_pop(L,1);
 raw(L,tables,"contains");
 if(!matchAIRound45PinnedFunction(c.room,L,-1,"lua/utilities.lua",95,109)||!exactEnvironment(L,-1))
  return fail("patterns table.contains fixed code/environment changed");
 // The complete pinned patterns body always supplies exactly two arguments to
 // contains. The compare_objectName userdata branch is unreachable by syntax,
 // not by sampling a private value. Both table arguments below contain only
 // immutable primitive values; ipairs and equality cannot dispatch user code.
 lua_pop(L,2);
 if(!matchAIRound45NativeNamespaceProperty(c.room,L,"sgs","Sanguosha"))return fail("patterns engine namespace property changed");
 for(const char *method:{"getBanPackages","getCardCount","getEngineCard"})
  if(!matchAIRound45NativeClassMethod(c.room,L,"Engine",method))return fail("patterns Engine method changed");
 if(!matchAIRound45NativeDerivedMethods(c.room,L,"Card",{"objectName","getClassName","getPackage","getTypeId"}))
  return fail("patterns engine-card metadata method union changed");
 check.tables.clear();
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;bool ok=false;
  if(!std::strcmp(name,"_ENV"))ok=lua_rawequal(L,-1,globals);
  else if(!std::strcmp(name,"cached_ban_packages")||!std::strcmp(name,"card_names")||!std::strcmp(name,"class_to_name"))
   ok=plain(L,-1)&&check.value(-1,QString::fromLatin1(name),0,false);
  lua_pop(L,1);if(!ok)return fail("patterns private cache is not its finite primitive graph");
 }
 if(reason)reason->clear();return true;
 }
bool certifyBoss(Certificate &c,int fn,int count,QString *reason){
 auto fail=[&](const char *why){if(reason)*reason=QString::fromLatin1(why);return false;};
 lua_State *L=c.L;Stack stack(L);Check check(c);
 if(count!=2||!matchAIRound45NativeObjectMethods(c.room,L,fn+1,"LuaMaxCardsSkill",{"objectName"})
    ||!matchAIRound45NativeObjectMethods(c.room,L,fn+2,"Player",{"isLord","getMark","getGeneralName"}))
  return fail("BossMaxCards arguments/native getter bindings are not authentic");
 // The exact wrapper only reads spec.fixed_func and forwards its two arguments.
 // No other spec key, private role or mark outcome selects a different proof.
 bool found=false;
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;
  if(std::strcmp(name,"spec")||found||!plain(L,-1))return fail("BossMaxCards wrapper upvalues changed");
  found=true;raw(L,-1,"fixed_func");
  if(!matchAIRound45PinnedFunction(c.room,L,-1,"lua/sanguosha.lua",171,176)||!exactEnvironment(L,-1))
   return fail("BossMaxCards fixed_func provider/environment changed");lua_pop(L,2);
 }
 if(!found||!check.primitiveMetatables())return fail("BossMaxCards primitive metamethod graph changed");
 AIRound45Inspect::globals(L);raw(L,-1,"string");const int strings=lua_absindex(L,-1);
 if(!plain(L,strings))return fail("BossMaxCards string library redirected");
 raw(L,strings,"startsWith");
 if(!matchAIRound45PinnedFunction(c.room,L,-1,"lua/utilities.lua",164,168)||!exactEnvironment(L,-1))
  return fail("BossMaxCards startsWith source/environment changed");lua_pop(L,1);
 raw(L,strings,"find");if(!matchAIRound45NativeBinding(c.room,L,-1,"string.find"))return fail("BossMaxCards string.find changed");
 // Player::isLord's BOTH identity/hegemony branches read only native role,
 // shown bits and General name/lord metadata; getMark's AIMarkQueryScope is
 // a native thread-local scalar overlay. None enters Lua or touches an AI.
 if(reason)reason->clear();return true;
}
}

void initializeAIRound45ContinuityCertificate(Room &r){
 auto *c=ledger(r,true);if(c->initialized)return;c->initialized=true;
 auto *runtime=r.roomRuntime();if(!runtime)return;
 LuaRuntime::Binding binding(runtime->lua(),false);c->L=runtime->lua().state();c->generation=runtime->lua().generation();
 if(!c->L||!lua_checkstack(c->L,512))return;Stack stack(c->L);
 if(!registerAIRound45PinnedSource(r,c->L,QString::fromLatin1(modeSource.path),QByteArray(modeSource.sha)))return;
 AIRound45Inspect::globals(c->L);raw(c->L,-1,"sgs");if(!lua_istable(c->L,-1))return;raw(c->L,-1,"evaluateModeAI");
 c->modeReady=matchAIRound45PinnedFunction(r,c->L,-1,QString::fromLatin1(modeSource.path),1364,1530);
 if(!registerAIRound45PinnedSource(r,c->L,smartSource.path,QByteArray(smartSource.sha))
    ||!registerAIRound45PinnedSource(r,c->L,utilitiesSource.path,QByteArray(utilitiesSource.sha)))return;
 AIRound45Inspect::globals(c->L);raw(c->L,-1,"patterns");
 c->patternsReady=matchAIRound45PinnedFunction(r,c->L,-1,smartSource.path,33,61);
 c->bossReady=registerAIRound45PinnedSource(r,c->L,sanguoshaSource.path,QByteArray(sanguoshaSource.sha))
  &&registerAIRound45PinnedSource(r,c->L,sgsExSource.path,QByteArray(sgsExSource.sha));
 c->naturalReady=registerAIRound45NaturalFilterSources(r,c->L);
 c->correctionReady=registerAIRound45GetterCorrectionSources(r,c->L);
 registerAIRound45XufaBodySources(r,c->L);
 registerAIRound47XufaGetterSources(r,c->L);
 registerAIRound47HeqiaSources(r,c->L);
 registerAIRound47BLeiluanSources(r,c->L);
 registerAIRound46JianmanSources(r,c->L);
 registerAIRound45XufaSlotSources(r,c->L);
 registerAIRound46PlanningSlotSources(r,c->L);
 registerAIRound47OlClanSlotSources(r,c->L);
}
void clearAIRound45ContinuityCertificate(Room &r){
 // This ledger owns only native bytes/identity metadata, never Lua references.
 // Teardown must not bind/inspect a closing gameplay VM.
 delete ledger(r);
 clearAIRound45PinnedSources(r);
 clearAIRound45NativeBindings(r);
}
bool certifyAIRound45ContinuityEntry(Room &r,lua_State *L,int count,QString *reason){
 auto refuse=[&](const QString &why){if(reason)*reason=why;return false;};
 auto *c=ledger(r);auto *runtime=r.roomRuntime();
 if(!Sanguosha || Sanguosha->currentRoom()!=&r)
  return refuse("certificate Engine runtime Room is absent or foreign");
 if(!c||!runtime||!L||c->L!=L||runtime->lua().state()!=L||runtime->lua().generation()!=c->generation)
  return refuse("certificate runtime/generation is absent or foreign");
 if(count<0||lua_gettop(L)<count+1)return refuse("protected call frame is malformed");
 if(!runtime->lua().hasProtectedEntryObserver())return refuse("asynchronous pre-entry observer absent");
 if(!AIRound45Inspect::capacity(L,4096))return refuse("pre-reserved inspection stack unavailable");
 Stack stack(L);const int fn=lua_gettop(L)-count;
 if(c->naturalReady&&matchAIRound45PinnedFunction(r,L,fn,smartSource.path,344,375))
  return certifyAIRound45NaturalFilterGraph(r,L,count,reason);
 if(c->bossReady&&matchAIRound45PinnedFunction(r,L,fn,sgsExSource.path,387,389))
  return certifyBoss(*c,fn,count,reason);
 if(c->patternsReady&&matchAIRound45PinnedFunction(r,L,fn,smartSource.path,33,61))
  return certifyPatterns(*c,fn,count,reason);
 if(!c->modeReady)return refuse("pinned mode root is unavailable");
 if(!matchAIRound45PinnedFunction(r,L,fn,QString::fromLatin1(modeSource.path),1364,1530))
  return refuse("protected entry is outside the implemented finite root set");
 if(count<1||count>3)return refuse("mode argument count is outside native query ABI");
 Check check(*c);if(!check.primitiveMetatables())return refuse(check.failure);
 // Separate traversals prevent a callback-table visit from accidentally
// authorizing the same table when supplied as a primitive root argument.
 for(int i=1;i<=count;++i){check.tables.clear();if(!check.value(fn+i,"argument"+QString::number(i),0,false))return refuse(check.failure);}
 check.tables.clear();
 if(!check.function(fn,"evaluateModeAI",0))return refuse(check.failure);
 if(reason)reason->clear();return true;
}

void observeAIRound45UnprovedNativeQuery(Room *room,bool preserveAiNoTo) noexcept {
 if(!room)return;
 // Only the separate complete R54 native public-definition proof can exempt aiNoTo.
 if(!preserveAiNoTo)retireAIRound53AiNoTo(*room);
 // A privately skipped callback is not a proof of noninterference. These
 // existing-ledger epoch sinks also cover same-value and cached query paths.
 // A future complete-union verifier may replace this unconditional refusal;
 // an entered-callback subtype alone must never do so.
 interruptAIChoiceContinuityAsync(*room);
 interruptAIRound41FaqiPlansAsync(*room);
}
