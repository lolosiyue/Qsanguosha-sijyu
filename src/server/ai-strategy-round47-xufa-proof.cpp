#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-native-types.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "room.h"
#include "serverplayer.h"
#include "card.h"
#include "wrapped-card.h"
#include "standard.h"
#include "maneuvering.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-round45-continuity.h"
#include "ai-strategy-round41-response.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include <QSet>
#include <QHash>
#include <QStringList>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
#include "ai-strategy-round56-projection-lineage.h"
extern "C" {
#include "lopcodes.h"
}
namespace {
using namespace AIRound45Inspect;
struct Row {const char*path;const char*sha;int first,last;const char*methods;const char*sgsFields;};
const Row rows[]={
#include "ai-strategy-round47-xufa-getter-rows.inc"
};
// Separate source domain: existing correction callers never consult these.
const Row activateRows[]={
#include "ai-strategy-round56-activate-getter-rows.inc"
};
struct ActivateSelectionRow { Row body; const char *role; };
const ActivateSelectionRow activateSelectionRows[]={
#include "ai-strategy-round56-activate-selection-rows.inc"
};
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool plain(lua_State*L,int i){const auto*t=table(L,i);return t&&!t->metatable;}
bool scalar(lua_State*L,int i){int t=lua_type(L,i);return t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER||t==LUA_TSTRING;}
// Resolve ordinary module(...,package.seeall) table chains WITHOUT calling an
// authored __index. A function/metatable cycle is unsupported, not executed.
bool lookup(lua_State*L,int index,const char*key){
 index=lua_absindex(L,index);QSet<const void*> seen;
 for(int n=0;n<16;++n){
  auto*t=table(L,index);if(!t||seen.contains(t))return false;seen.insert(t);
  field(L,index,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!t->metatable){push(L,nullptr);return true;}
  if(!lua_getmetatable(L,index))return false;
  if(!plain(L,-1))return false;field(L,-1,"__index");
  if(!lua_istable(L,-1))return false;index=lua_absindex(L,-1);
 }return false;
}
struct Check {
 Room&r;lua_State*L;QString why;int budget=100000;
 QSet<const void*> functions,tables,protectedRoots;QSet<QString> sealedMethods;bool rootsReady=false,requireCaptureOrigin=false,includeActivateRows=false,includeActivateSelectionRows=false;
 bool no(const QString&s){if(why.isEmpty())why=s;return false;}
 bool native(int i,const QString&p){return matchAIRound45NativeBinding(r,L,i,p)||no("native binding: "+p);}
 bool nativeField(int lib,const char*k,const QString&path){Stack s(L);field(L,lib,k);return native(-1,path);}
 bool primitiveMeta(){
  Stack s(L);for(int t:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})if(primitiveMetatable(L,t))return no("primitive metatable changed");
  // Preserve the actual trusted Lua 5.4 compatibility metatable. It has
  // native arithmetic entries as well as __index; no names-only exception.
  // Nonstring operand delegation is closed by the finite getter argument,
  // captured-plain-table and exact SWIG value-metamethod proofs.
  return matchAIRound47NativeStringMetatable(r,L)||no("string bootstrap metamethod bindings changed");
 }
 bool protectRoots(){
  if(rootsReady)return true;Stack st(L);globals(L);protectedRoots.insert(lua_topointer(L,-1));
  field(L,-1,"sgs");if(!table(L,-1))return no("getter protected namespace absent");
  protectedRoots.insert(lua_topointer(L,-1));int ns=lua_absindex(L,-1);
  // These are actual registry identities, not table-shape guesses. value()
  // checks every nested capture before its cycle shortcut. Full pinned source
  // separately proves fresh local mutation sinks and removeOne caller origin.
  for(const char*k:{"ai_skill_choice","ai_use_priority","ai_use_value","ai_role","role_evaluation"}){
   field(L,ns,k);if(lua_istable(L,-1))protectedRoots.insert(lua_topointer(L,-1));lua_pop(L,1);
  }
  rootsReady=true;return true;
 }
 bool value(int index,int depth=0){
  if(--budget<0||depth>48)return no("getter capture graph bound");index=lua_absindex(L,index);
  if(scalar(L,index))return true;if(lua_isfunction(L,index))return function(index,depth+1);
  if(requireCaptureOrigin&&!isAIRound56ProjectionCaptureOrigin(r,L,lua_topointer(L,index)))
   return no("getter capture lacks exact source allocation receipt");
  if(!plain(L,index))return no("getter captured capability/metatable");
  const void*p=lua_topointer(L,index);if(hasAIRound56AsyncUniverse(r,L)&&isAIRound56AsyncProtectedTable(r,L,index))return no("getter capture aliases asynchronous capability");if(!protectRoots()||protectedRoots.contains(p))return no("getter capture aliases protected registry root");if(tables.contains(p))return true;tables.insert(p);
  lua_pushnil(L);while(next(L,index)){if(!scalar(L,-2)||!value(-1,depth+1))return false;lua_pop(L,1);}return true;
 }
 bool library(int env,const QString&name){
  Stack s(L);if(!lookup(L,env,name.toUtf8().constData())||!plain(L,-1))return no("getter library redirected: "+name);int lib=lua_absindex(L,-1);
  static const QHash<QString,QStringList> leaves={
   {"math",{"abs","min","max","floor","ceil","fmod"}},
   {"string",{"find","sub","gmatch","gsub","format","len"}},
   {"table",{"insert","remove","concat"}}, {"debug",{"traceback"}}};
  for(const auto&k:leaves.value(name))if(!nativeField(lib,k.toUtf8().constData(),name+'.'+k))return false;
  if(name=="math"){
   field(L,lib,"mod");
   if(!lua_isnil(L,-1)){
    if(lua_iscfunction(L,-1)){if(!native(-1,"math.mod"))return false;}
    else {
     // Original utilities replaces the compatibility slot with this exact
     // Lua wrapper. Authenticate its body/environment; never adopt a current
     // function as a new native seal or accept another row in this role.
     if(!matchAIRound45PinnedFunction(r,L,-1,"lua/utilities.lua",176,178))
      return no("math.mod outside fixed utilities wrapper");
     if(!function(-1,1))return false;
    }
   }
   lua_pop(L,1);
  }
  for(const auto&k:name=="table"?QStringList{"contains","removeOne"}:name=="string"?QStringList{"contains","split","startsWith","endsWith"}:QStringList{}){
   field(L,lib,k.toUtf8().constData());bool ok=function(-1,1);lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool global(int env,const QString&key,const Row&row){
  Stack s(L);if(!lookup(L,env,key.toUtf8().constData()))return no("indirect getter environment: "+key);int v=lua_absindex(L,-1);
  static const QSet<QString> prim={"type","tonumber","tostring","ipairs","pairs","next","error","assert","select"};
  if(prim.contains(key))return native(v,key);
  if(key=="table"||key=="string"||key=="math"||key=="debug")return library(env,key);
  if(key=="sgs"){
   if(!lua_istable(L,v))return no("sgs namespace missing");{Stack sg(L);globals(L);field(L,-1,"sgs");if(!lua_rawequal(L,v,-1))return no("getter sgs namespace redirected");}
   for(const auto&f:QString::fromLatin1(row.sgsFields).split('|',Qt::SkipEmptyParts)){
    if(f=="Sanguosha"){if(!matchAIRound45NativeNamespaceProperty(r,L,"sgs",f))return no("engine namespace getter changed");continue;}
    field(L,v,f.toUtf8().constData());bool ok=false;
    if(includeActivateRows&&(f=="Slash_IsAvailable"||f=="Analeptic_IsAvailable")){
     // Every admitted row calls these with exactly one Player argument.
     // Their fixed native defaults construct fresh Slash/Analeptic products;
     // caller still owes complete projection/correction/allocation scope.
     ok=native(-1,"sgs."+f);
    }
    else if(f=="qlist"||f=="list")ok=function(-1,1);
    else if(f=="QVariant"||f=="IntList"||f=="PlayerList"||f=="SPlayerList")ok=matchAIRound47NativeScratchConstructor(r,L,-1,f);
    else if(f=="CorrectSkillResult"){
     // Fixed callers use only this raw static C factory; do not call an
     // arbitrary class table or resolve its metamethods during inspection.
     if(lua_istable(L,-1)){field(L,-1,"unlimitedResidue");ok=native(-1,"sgs.CorrectSkillResult_unlimitedResidue");lua_pop(L,1);}
    }
    else ok=matchAIRound45NativeConstant(r,L,-1,"sgs."+f)||(lua_isnil(L,-1)&&matchAIRound45NativeNamespaceRead(r,L,"sgs",f));
    lua_pop(L,1);if(!ok)return no("getter namespace binding: "+f);
   }return true;
  }
  // The finite manifests name the actual fixed helper roots. Nil/scalar is
  // also nonwriter: attempting to call it raises the source ordinary error.
  if(scalar(L,v))return true;
  return function(v,1);
 }
 bool environment(const Proto*p,int up,int env,const Row&row){
  for(int i=0;i<p->sizecode;++i){auto in=p->code[i];auto op=GET_OPCODE(in);
   if((op==OP_GETUPVAL&&GETARG_B(in)==up)||(op==OP_SETUPVAL&&GETARG_B(in)==up)||(op==OP_SETTABUP&&GETARG_A(in)==up))return no("getter environment escape/write");
   if(op==OP_GETTABUP&&GETARG_B(in)==up){int k=GETARG_C(in);if(k<0||k>=p->sizek||!ttisstring(&p->k[k]))return no("nonliteral getter global");
    auto*t=tsvalue(&p->k[k]);if(!global(env,QString::fromUtf8(getstr(t),qsizetype(tsslen(t))),row))return false;
   }
  }
  for(int c=0;c<p->sizep;++c)for(int u=0;u<p->p[c]->sizeupvalues;++u)
   if(p->p[c]->upvalues[u].name&&!std::strcmp(getstr(p->p[c]->upvalues[u].name),"_ENV")&&!environment(p->p[c],u,env,row))return false;
  return true;
 }
 bool noCaptureWrite(const Proto*p){
  for(int pc=0;pc<p->sizecode;++pc)if(GET_OPCODE(p->code[pc])==OP_SETUPVAL)return no("getter writes captured state");
  for(int c=0;c<p->sizep;++c)if(!noCaptureWrite(p->p[c]))return false;return true;
 }
 bool methods(const Row&row); // finite audited native leaves, defined below
 bool function(int i,int depth=0){
  if(--budget<0||depth>48||!lua_isfunction(L,i)||lua_iscfunction(L,i))return no("getter function outside pinned Lua graph");i=lua_absindex(L,i);
  const auto*actual=static_cast<const LClosure*>(lua_topointer(L,i));const Row*row=nullptr;
  for(const auto&v:rows)if(actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last
    &&matchAIRound45PinnedFunction(r,L,i,v.path,v.first,v.last)){row=&v;break;}
  if(!row&&includeActivateRows)for(const auto&v:activateRows)
   if(actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last
    &&matchAIRound45PinnedFunction(r,L,i,v.path,v.first,v.last)){row=&v;break;}
  if(!row&&includeActivateSelectionRows)for(const auto&v:activateSelectionRows)
   if(actual->p->linedefined==v.body.first&&actual->p->lastlinedefined==v.body.last
    &&matchAIRound45PinnedFunction(r,L,i,v.body.path,v.body.first,v.body.last)){row=&v.body;break;}
  if(!row){
   // Route diagnostic labels only through the finite public source rows. Never
   // print an arbitrary Proto source (which can contain a load-string body).
   QString descriptor=QStringLiteral("unknown-fixed-descriptor"),candidateReason;
   if(actual->p->source){
    QByteArray hint(getstr(actual->p->source),qsizetype(tsslen(actual->p->source)));
    if(hint.startsWith('@'))hint.remove(0,1);
    while(hint.startsWith("./"))hint.remove(0,2);
    for(const auto&v:rows)if(hint==v.path){descriptor=QString::fromLatin1(v.path);break;}
   }
   for(const auto&v:rows)if(descriptor==QString::fromLatin1(v.path)
     &&actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last){
    matchAIRound45PinnedFunction(r,L,i,v.path,v.first,v.last,&candidateReason);break;
   }
   return no(QStringLiteral("getter full Proto outside fixed source union: descriptor=%1 first=%2 last=%3 seal=%4")
    .arg(descriptor).arg(actual->p->linedefined).arg(actual->p->lastlinedefined)
    .arg(candidateReason.isEmpty()?QStringLiteral("no-exact-row"):candidateReason));
  }
  const void*id=lua_topointer(L,i);if(functions.contains(id))return true;functions.insert(id);
  const Proto*p=static_cast<const LClosure*>(id)->p;
  if(!methods(*row))return false;
  // Captured state replacement is not part of these getter bodies. Scratch
  // assignments in child functions remain in their actual pinned body.
  if(!noCaptureWrite(p))return false;
  for(const char*k:{"ownerObjectName","key"})if(!matchAIRound45NativeClassProperty(r,L,"SkillInstanceRef",k))return no("SkillInstanceRef property changed");
  for(const char*k:{"skillName","instanceID"})if(!matchAIRound45NativeClassProperty(r,L,"SkillInstanceKey",k))return no("SkillInstanceKey property changed");
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;
   bool ok=!std::strcmp(k,"_ENV")?environment(p,u-1,lua_absindex(L,-1),*row):value(-1,depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};
bool Check::methods(const Row&row){
 const QStringList ms=QString::fromLatin1(row.methods).split('|',Qt::SkipEmptyParts);
 static const QSet<QString> context={"getPrimary","getSecondary","getHolder","getModType","includesWeapon","getCurrentAmount","getStateValue","getInstanceRef"};
 static const QSet<QString> card={"getActivationSkillName","getClassName","getEffectiveId","getId","getType","targetFixed","isAvailable","getNumber","getRealCard","getSkillName","getSkillNames","getSubcards","getSuit","getSuitString","getSubtype","getTypeId","hasSuit","hasTip","isBlack","isDamageCard","isKindOf","isNDTrick","isRed","isVirtualCard","sameColorWith","subcardsLength","toWeapon"};
 static const QSet<QString> player={"aliveCount","containsTrick","handCards","getEquipsId","isSkillInstanceEffectAvailable","canSlash","isProhibited","distanceTo","faceUp","getAliveSiblings","getArmor","getAttackRange","getChangeSkillState","getDefensiveHorse","getEquip","getEquips","getGeneral2Name","getGeneralName","getHandcardNum","getHandcards","getHp","getJudgingArea","getKingdom","getLostHp","getMark","getMarkNames","getMaxHp","getOffensiveHorse","getPhase","getPile","getRole","getSeat","getSiblings","getSkillInstanceCorrectStateValue","getSkillInstanceIds","getSkillInstanceParentRef","getSkillInstanceStateValue","getTreasure","getValidSkillInstanceIds","getVisibleSkillList","getWeapon","hasArmorEffect","hasDefensiveHorse","hasEquip","hasEquipArea","hasEquipSkill","hasLordSkill","hasOffensiveHorse","hasSkill","hasTreasure","hasUsed","hasWeapon","inMyAttackRange","isAlive","isChained","isDead","isFemale","isKongcheng","isLastHandCard","isLord","isMale","isWounded","isYourFriend"};
 static const QSet<QString> skill={"getFrequency","isAttachedLordSkill","isLordSkill","isChangeSkill"};
 for(const auto&m:ms){
  QStringList classes;bool derived=false;
  if(includeActivateSelectionRows&&m=="getSelectedCardIds"){
   // ActiveSkillRequest::getSelectedCardIds returns its value-list copy.
   // SWIG scratch allocation/lifetime and receiver custody remain separate.
   classes<<"ActiveSkillRequest";
  }else if(includeActivateSelectionRows&&m=="isEquipped"){
   // The canonical QSAN_ENGINE_BUILD Card body is false, but exact virtual
   // receiver/product provenance remains an independent native obligation.
   classes<<"Card";derived=true;
  }else if(includeActivateRows&&(m=="getInitiator"||m=="getReason"||m=="getPattern"||m=="getActivationInstanceId"))classes<<"ActiveSkillRequest";
  else if(includeActivateRows&&(m=="isNude"||m=="isAllNude")){classes<<"Player";derived=true;}
  else if(includeActivateRows&&(m=="usedTimes"||m=="hasTurn")){classes<<"Player";derived=true;}
  else if(includeActivateRows&&m=="getCardCount"){
   // Both same-named public native leaf alternatives are readonly. Seal both
   // rather than infer the class from the spelling of a private receiver.
   if(!matchAIRound45NativeClassMethod(r,L,"Engine",m))return no("Activate Engine card count binding changed");
   classes<<"Player";derived=true;
  }
  else if(context.contains(m))classes<<"CorrectSkillContext";
  else if(card.contains(m)){classes<<"Card";derived=true;}
  else if(player.contains(m)){classes<<"Player";derived=true;}
  else if(skill.contains(m)){classes<<"Skill";derived=true;}
  else if(m=="getCard")classes<<"Engine"<<"CorrectSkillContext";
  else if(m=="getRoleList"||m=="getEngineCard"||m=="correctCardTarget"||m=="matchExpPattern"||m=="getBanPackages"||m=="isPackageBanned")classes<<"Engine";
  else if(m=="getOtherPlayers")classes<<"Room";
  else if(m=="getGameMode"){classes<<"Player";derived=true;}
  else if(m=="getRange")classes<<"Weapon";
  else if(m=="isValid")classes<<"SkillInstanceRef";
  else if(m=="toInt"||m=="toBool"||m=="toStringList")classes<<"QVariant";
  else if(m=="toString"){classes<<"QVariant"<<"Card";derived=true;}
  else if(m=="objectName"){classes<<"Player"<<"Card"<<"Skill";derived=true;}
  else if(m=="property"){classes<<"Player"<<"Card";derived=true;}
  else if(m=="hasFlag"){classes<<"Player"<<"Card";derived=true;}
  else if(m=="setObjectName"){classes<<"Skill";derived=true;}
  else if(m=="length"||m=="at"||m=="first"||m=="contains"||m=="isEmpty"||m=="append"||m=="prepend"||m=="removeOne"){
   // QList values are copies; append/prepend affect the callback's scratch.
   if(m=="append"){
    // All six manifest append receivers are actual fresh IntList or borrowed
    // Player/SPlayer list copies (Loong234, excard2014:385, hezongkangqin89,
    // htms5349, kearjsrg7867, luaoldenemy_lib27). No SkillList append occurs;
    // its real sgs_ex wrapper is a different provider graph, not this leaf.
    classes<<"IntList"<<"PlayerList"<<"SPlayerList";
   }else classes<<"IntList"<<"PlayerList"<<"SPlayerList"<<"CardList"<<"SkillList"<<"EquipCardList"<<"SkillInstanceKeyList";
  }else if(m=="split"||m=="startsWith"||m=="endsWith")continue; // primitive string library sealed separately
  else return no("unproved native leaf: "+m);
  for(const auto&c:classes){const QString seal=c+"."+m+(derived?".derived":"");if(sealedMethods.contains(seal))continue;bool ok=derived?matchAIRound45NativeDerivedMethods(r,L,c,{m}):matchAIRound45NativeClassMethod(r,L,c,m);
   if(!ok)return no("native getter method changed: "+c+'.'+m);sealedMethods.insert(seal);
  }
 }
 // Method-syntax string lookups bypass _ENV. Always seal the primitive
 // string table/helper graph, including contains (also used on QList).
 Stack st(L);globals(L);return library(lua_absindex(L,-1),"string");
}
} // namespace
bool registerAIRound47XufaGetterSources(Room&r,lua_State*L,QString*why){
 QSet<QString> done;
 const auto registerRows=[&](const auto&domain){for(const auto&row:domain){
  QString p=QString::fromLatin1(row.path);if(done.contains(p))continue;
  if(!registerAIRound45PinnedSource(r,L,p,row.sha,why))return false;done.insert(p);
 }return true;};
 if(!registerRows(rows)||!registerRows(activateRows))return false;
 for(const auto&entry:activateSelectionRows){const auto&row=entry.body;
  QString p=QString::fromLatin1(row.path);if(done.contains(p))continue;
  if(!registerAIRound45PinnedSource(r,L,p,row.sha,why))return false;done.insert(p);
 }return true;
}
bool proveAIRound47XufaGetterFunction(Room&r,lua_State*L,int i,QString*why){
 if(!L||!capacity(L,4096)){if(why)*why="Xufa getter inspection stack unavailable";return false;}
 Stack s(L);Check c{r,L};bool ok=c.primitiveMeta()&&c.function(i);if(why)*why=c.why;return ok;
}
bool proveAIRound56ActivateGetterFunction(Room&r,lua_State*L,int i,QString*why){
 if(!L||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate getter inspection frame unavailable";return false;
 }
 Stack stack(L);Check c{r,L};c.includeActivateRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 const bool ok=c.primitiveMeta()&&matchAIRound47NativeValueMetamethods(r,L,&metaWhy)&&c.function(i);
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
bool proveAIRound56ActivateGetterFamily(Room&r,lua_State*L,const QList<LuaCallbackRef>&family,QString*why){
 if(!L||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate getter family inspection frame unavailable";return false;
 }
 // Same complete Check as the single-root API and original bootstrap union.
 // Its graph budget is unchanged (100000 for this whole family), with no
 // outcome memoization and no visitor surviving this one preflight call.
 Stack stack(L);Check c{r,L};c.includeActivateRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 bool ok=c.primitiveMeta()&&matchAIRound47NativeValueMetamethods(r,L,&metaWhy);
 const auto generation=r.roomRuntime()->lua().generation();
 if(ok)for(const LuaCallbackRef &fn:family){
  if(fn==0)continue;
  if(fn.state()!=L||fn.generation()!=generation){ok=c.no("getter family callback VM changed");break;}
  Stack entry(L);AIRound45Inspect::rawGeti(L,LUA_REGISTRYINDEX,fn.registryRef());
  if(!c.function(-1)){ok=false;break;}
 }
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
bool proveAIRound56ActivateSelectionFunction(Room&r,lua_State*L,int i,const char*role,QString*why){
 if(!L||!role||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate selection source inspection frame unavailable";return false;
 }
 Stack stack(L);bool root=false;
 // A selected field cannot borrow a descriptor from an unrelated callback
 // role. This remains source inspection only, without dispatch admission.
 for(const auto&entry:activateSelectionRows)if(!std::strcmp(role,entry.role)
  &&matchAIRound45PinnedFunction(r,L,i,entry.body.path,entry.body.first,entry.body.last)){root=true;break;}
 if(!root){if(why)*why="selection callback role/full Proto outside finite nonwriting source union";return false;}
 Check c{r,L};c.includeActivateRows=true;c.includeActivateSelectionRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 const bool ok=c.primitiveMeta()&&matchAIRound47NativeValueMetamethods(r,L,&metaWhy)&&c.function(i);
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
static bool inspectAIRound56CorrectionBindings(Room&r,lua_State*L,QSet<const void*>*captures,QString*why){
 if(!L||r.roomRuntime()->lua().state()!=L||!capacity(L,4096)){if(why)*why="Xufa union inspection frame unavailable";return false;}
 Stack st(L);Check c{r,L};c.requireCaptureOrigin=captures!=nullptr;QString metaWhy;
 if(!c.primitiveMeta()||!matchAIRound47NativeValueMetamethods(r,L,&metaWhy)){
  if(why)*why=c.why.isEmpty()?metaWhy:c.why;return false;
 }
 auto f=[&](const LuaFunction&fn){if(fn==0)return true;fn.push(L);bool ok=c.function(-1);lua_pop(L,1);return ok;};
 bool ok=true;
 for(const auto*s:Sanguosha->getDistanceSkills()){
  if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getMaxCardsSkills()){
  if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s))ok=f(x->extra_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getTargetModSkills()){
  if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s))ok=f(x->residue_func)&&f(x->distance_limit_func)&&f(x->extra_target_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getAttackRangeSkills()){
  if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s))ok=f(x->extra_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s)){if(!(ok=f(x->skill_valid)))break;}
 if(ok)for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s)){if(!(ok=f(x->view_as_equip)))break;}
 if(ok)for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s)){if(!(ok=f(x->is_prohibited)))break;}
 if(ok)for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s)){
  if(!(ok=f(x->limit_list)&&f(x->limit_pattern)&&f(x->limit_reason)))break;
 }
 // Public engine definition domain, not the privately equipped horse. This
 // union is proved before any ownership/effect filter and never calls a horse.
 if(ok)for(int id=0;id<Sanguosha->getCardCount();++id){const Card*card=Sanguosha->getEngineCard(id);
  if(auto*x=dynamic_cast<const LuaHorse*>(card))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(card))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(card))ok=f(x->correct_func);
  if(!ok)break;
 }
 // No current player/holder, role/skill ownership, mark, flag or query result
 // was read. Native Card virtual dispatch is separately guarded by custody.
 if(ok&&captures)*captures=c.tables;
 if(why)*why=c.why;return ok;
}
bool inspectAIRound47XufaCorrectionBindings(Room&r,lua_State*L,QString*why){return inspectAIRound56CorrectionBindings(r,L,nullptr,why);}
bool collectAIRound56CorrectionCaptureTables(Room&r,lua_State*L,QSet<const void*>&out,QString*why){return inspectAIRound56CorrectionBindings(r,L,&out,why);}
bool certifyAIRound47XufaPatternsEntry(Room&r,lua_State*L,int nargs,QString*why){
 const auto fail=[&](const QString&s){if(why)*why=s;return false;};
 if(!currentAIRound47XufaContinuityWriter(r)||!L||r.roomRuntime()->lua().state()!=L
  ||nargs<0||nargs>1||!AIRound45Inspect::capacity(L,4096))return fail("Xufa effective patterns entry unavailable");
 Stack stack(L);const int fn=lua_gettop(L)-nargs;
 if(fn<1||!matchAIRound45PinnedFunction(r,L,fn,"extensions/addFunction.lua",1809,1838))return fail("Xufa effective patterns full source differs");
 if(nargs==1&&lua_type(L,fn+1)!=LUA_TNIL&&lua_type(L,fn+1)!=LUA_TBOOLEAN&&lua_type(L,fn+1)!=LUA_TSTRING)return fail("patterns authored argument outside fixed source");
 Check check{r,L};if(!check.primitiveMeta()||!matchAIRound47NativeValueMetamethods(r,L))return fail("patterns scalar dispatch changed");
 globals(L);field(L,-1,"sgs");if(!table(L,-1))return fail("patterns native namespace absent");int ns=lua_absindex(L,-1);
 field(L,ns,"ai_skill_choice");const void*choice=lua_topointer(L,-1);lua_pop(L,1);
 const void*globalsIdentity=lua_topointer(L,-2),*namespaceIdentity=lua_topointer(L,ns);
 auto cache=[&](int at){
  // Neither captured primitive cache may alias any retained Xufa root. Other
  // priority/role ledgers are unconditionally invalidated by this entry path.
  if(!plain(L,at))return false;const void*p=lua_topointer(L,at);
  if(p==choice||p==namespaceIdentity||p==globalsIdentity)return false;
  at=lua_absindex(L,at);lua_pushnil(L);int n=0;
  while(next(L,at)){if(++n>65536||!scalar(L,-2)||!scalar(L,-1))return false;lua_pop(L,1);}return true;
 };
 int env=0,seen=0;
 for(int u=1;;++u){const char*key=lua_getupvalue(L,fn,u);if(!key)break;
  if(!std::strcmp(key,"_ENV")){
   if(env||!table(L,-1))return fail("patterns environment absent/duplicate");env=lua_absindex(L,-1);
   // Lua ordinary assignment writes equip_patterns to this environment, never
   // a namespace setter or an authored __newindex. __index is separately
   // resolved as a native table chain by the exact binding checks below.
   if(lua_getmetatable(L,env)){
    if(!plain(L,-1))return fail("patterns environment metatable redirected");
    field(L,-1,"__newindex");bool ok=lua_isnil(L,-1);lua_pop(L,2);if(!ok)return fail("patterns environment write callback");
   }
  }else{
   if(std::strcmp(key,"banPs")&&std::strcmp(key,"cardNames")&&std::strcmp(key,"class2Names"))return fail("patterns extra capture");
   if(!cache(-1))return fail("patterns primitive cache aliases a retained root or capability");++seen;lua_pop(L,1);
  }
 }
 if(!env||seen!=3)return fail("patterns exact cache capture set differs");
 for(const char*k:{"type","ipairs"}){
  if(!lookup(L,env,k)||!check.native(-1,QString::fromLatin1(k)))return fail(check.why);lua_pop(L,1);
 }
 for(const char*lib:{"table","string"})if(!lookup(L,env,lib)||!plain(L,-1))return fail("patterns library indirection");else {
  int at=lua_absindex(L,-1);const char*k=!std::strcmp(lib,"table")?"insert":"sub";
  if(!check.nativeField(at,k,QString::fromLatin1(lib)+'.'+k))return fail(check.why);
  if(!std::strcmp(lib,"table")){
   field(L,at,"contains");if(!check.function(-1))return fail(check.why);lua_pop(L,1);
  }lua_pop(L,1);
 }
 if(!lookup(L,env,"sgs")||!lua_rawequal(L,ns,-1))return fail("patterns engine namespace redirected");lua_pop(L,1);
 if(!matchAIRound45NativeNamespaceProperty(r,L,"sgs","Sanguosha"))return fail("patterns engine property changed");
 for(const char*k:{"getBanPackages","getCardCount","getEngineCard"})if(!matchAIRound45NativeClassMethod(r,L,"Engine",k))return fail("patterns native Engine leaf changed");
 if(!matchAIRound45NativeDerivedMethods(r,L,"Card",{"objectName","getClassName","getPackage","getTypeId"}))return fail("patterns public definition metadata leaf changed");
 return currentAIRound47XufaContinuityWriter(r)!=nullptr;
}

namespace {
thread_local AIRound56XufaCorrectionScope *xufaCorrection=nullptr;
bool xufaNotificationMetadata(Room&r,lua_State*L,QString*why){
 const auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 auto*rt=r.roomRuntime();
 if(!rt||rt->isClosing()||!L||rt->lua().rawState()!=L
  ||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
  ||!Sanguosha||Sanguosha->currentRoom()!=&r)
  return fail("Xufa notification current Game VM unavailable");
 // Complete public metadata domain BEFORE a controller, holder, mark or UI
 // visibility branch. Bit2 closes the fixed frequency/equipment/quota bodies;
 // the independently compiler-bound amount row closes describeSkillUsage.
 for(const QString&name:Sanguosha->getSkillNames()){
  const Skill*s=Sanguosha->getSkill(name);
  if(!s||(aiRound56NativeEffectClass(s)&2u)==0)
   return fail("Xufa notification native metadata body unreviewed");
  if(dynamic_cast<const AmountSkillV2*>(s)){
   const auto*source=aiRound56ActivateNativeTypeSource(s);bool amount=false;
   if(source)for(unsigned i=0;i<source->methodCount;++i)
    if(!std::strcmp(source->methods[i].method,"getBaseAmount"))amount=source->methods[i].scalarBodyProved||source->methods[i].notificationBodyProved;
   if(!amount)return fail("Xufa notification amount body unreviewed");
  }
  if(const auto*x=dynamic_cast<const LuaTriggerSkillV2*>(s))if(x->get_usage_ref!=0)
   return fail("Xufa notification trigger usage-ref callback unproved");
  if(const auto*x=dynamic_cast<const LuaViewAsSkillV2*>(s))if(x->get_usage_ref!=0)
   return fail("Xufa notification active usage-ref callback unproved");
 }
 return true;
}
}
struct AIRound56XufaCorrectionScope::Data {
 Room&room;lua_State*L;ServerPlayer*writer;AIRound56XufaCorrectionScope*previous;
 quint64 generation=0,transport=0,serial=0;QString why;
 std::unique_ptr<AIRound56OwnerCardRulesScope>corrections;
 Data(Room&r,ServerPlayer*p,lua_State*l):room(r),L(l),writer(p),previous(xufaCorrection){}
};
AIRound56XufaCorrectionScope::AIRound56XufaCorrectionScope(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 xufaCorrection=this;
 try{
  if(!p||currentAIRound47XufaContinuityWriter(r)!=p||!xufaNotificationMetadata(r,L,&d->why))return;
  auto*rt=r.roomRuntime();d->generation=rt->lua().generation();
  d->transport=rt->xufaTransportEpoch();d->serial=currentAIRound47XufaWriteSerial(r);
  // Reuse R56's complete correction/native-type/capture-origin/factory and
  // canonical receiver proof, plus its full lexical Card lifetime and async
  // scope. A momentary metadata boolean must not outlive these capabilities.
  d->corrections.reset(new AIRound56OwnerCardRulesScope(r,p,L));
  if(!accepted())d->why=QStringLiteral("Xufa complete correction source lifetime unavailable");
 }catch(...){d->why=QStringLiteral("Xufa correction preflight allocation failure");}
}
AIRound56XufaCorrectionScope::~AIRound56XufaCorrectionScope(){
 if(d->serial&&!accepted())interruptAIRound47XufaContinuity(d->room);
 d->corrections.reset();xufaCorrection=d->previous;
}
bool AIRound56XufaCorrectionScope::accepted()const noexcept{
 auto*rt=d->room.roomRuntime();
 return xufaCorrection==this&&d->corrections&&d->corrections->accepted()
  &&rt&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&d->serial&&currentAIRound47XufaWriteSerial(d->room)==d->serial
  &&currentAIRound47XufaContinuityWriter(d->room)==d->writer;
}
// The fixed correction setters only publish seven named dynamic properties
// and fixed distance. Their native notification descendants reach controller
// cleanup, whose constructed marks always contain sys_; setPlayerMark therefore
// cannot dispatch MarkChange/MarkChanged. Player::removeTag is not Room::setTag
// and cannot call Scenario. UI refresh rejoins the complete correction and
// quota graph above. Disconnection revokes the atomic transport generation
// before controller/socket selection. None of these facts uses a current tag,
// flag, mark, socket or private applicability result as proof authority.
bool proveAIRound47XufaCorrectionUnion(Room&r,lua_State*L,QString*why){
 const auto*scope=xufaCorrection;
 if(!scope||&scope->d->room!=&r||scope->d->L!=L||!scope->accepted()){
  if(why)*why=scope&&!scope->d->why.isEmpty()?scope->d->why:
   QStringLiteral("Xufa exact correction source scope absent or retired");
  return false;
 }
 if(why)why->clear();return true;
}
