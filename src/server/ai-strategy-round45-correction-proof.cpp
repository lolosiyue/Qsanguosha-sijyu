#include "ai-strategy-round45-correction-proof.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "lua.hpp"
#include <QStringList>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
extern "C" {
#include "lobject.h"
#include "lopcodes.h"
}
namespace {
struct Row{const char*path;const char*sha;int first,last;AIRound45CorrectionArguments arguments;const char*methods;};
const Row rows[]={
#include "ai-strategy-round45-correction-rows.inc"
};
struct Stack{lua_State*L;int n;~Stack(){lua_settop(L,n);}};
bool fail(QString*r,const QString&s){if(r)*r=s;return false;}
bool bindings(Room&r,lua_State*L,const Row&row,QString*why){
 // These bodies use only ordinary primitive conditions/arithmetic/concatenation
 // and listed native getters. No Lua global lookup or table/object assignment.
 for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER})if(AIRound45Inspect::primitiveMetatable(L,kind))return fail(why,"correction primitive metamethod replaced");
 for(const auto&name:QString::fromLatin1(row.methods).split('|',Qt::SkipEmptyParts)){
  if(name=="getPrimary"||name=="getSecondary"||name=="getHolder"){
   if(!matchAIRound45NativeClassMethod(r,L,"CorrectSkillContext",name))return fail(why,"correction context getter changed: "+name);
  }else if(name=="getMark"){
   if(!matchAIRound45NativeDerivedMethods(r,L,"Player",{name}))return fail(why,"correction Player.getMark changed");
  }else if(name=="hasFlag"||name=="faceUp"||name=="isKongcheng"||name=="getGeneralName"||name=="getGeneral2Name"){
   // Whole native bodies read fixed fields/containers only. isKongcheng calls
   // the sealed native getHandcardNum; actual V2 caller supplies ServerPlayer.
   if(!matchAIRound45NativeDerivedMethods(r,L,"Player",{name}))return fail(why,"correction pure native getter changed: "+name);
   if(name=="isKongcheng"&&!matchAIRound45NativeDerivedMethods(r,L,"Player",{"getHandcardNum"}))return fail(why,"correction hand-count getter changed");
  }else if(name=="objectName"){
   if(!matchAIRound45NativeDerivedMethods(r,L,"Player",{name})||!matchAIRound45NativeDerivedMethods(r,L,"Skill",{name}))return fail(why,"correction native objectName changed");
  }else return fail(why,"correction unaudited getter");
 }
 return true;
}
}
bool registerAIRound45GetterCorrectionSources(Room&r,lua_State*L,QString*why){
 QStringList done;for(const auto&row:rows){const auto path=QString::fromLatin1(row.path);if(done.contains(path))continue;
  if(!registerAIRound45PinnedSource(r,L,path,row.sha,why))return false;done<<path;
 }return true;
}
bool proveAIRound45GetterCorrection(Room&r,lua_State*L,int index,AIRound45CorrectionArguments arguments,QString*why){
 if(!L||!AIRound45Inspect::capacity(L,128))return fail(why,"correction inspection stack unavailable");
 Stack stack{L,lua_gettop(L)};index=lua_absindex(L,index);
 if(!lua_isfunction(L,index)||lua_iscfunction(L,index))return fail(why,"correction not a Lua function");
 const Row*row=nullptr;for(const auto&candidate:rows)if(candidate.arguments==arguments&&matchAIRound45PinnedFunction(r,L,index,candidate.path,candidate.first,candidate.last,nullptr)){row=&candidate;break;}
 if(!row)return fail(why,"correction outside complete pinned getter-only subtype");
 const auto*closure=static_cast<const LClosure*>(lua_topointer(L,index));const Proto*p=closure->p;
 if(p->sizep||p->is_vararg||p->numparams!=(arguments==AIRound45CorrectionArguments::Invalidity?3:2))return fail(why,"correction argument/nested closure shape changed");
 for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
  if(std::strcmp(name,"_ENV"))return fail(why,"correction captured non-environment state");
  AIRound45Inspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);
  if(!same)return fail(why,"correction environment replaced");
 }
 for(int i=0;i<p->sizecode;++i){const auto op=GET_OPCODE(p->code[i]);
  if(op==OP_GETTABUP||op==OP_GETUPVAL||op==OP_SETUPVAL||op==OP_SETTABUP
     ||op==OP_SETTABLE||op==OP_SETI||op==OP_SETFIELD||op==OP_CLOSURE)
   return fail(why,"correction contains external read/write or nested closure");
 }
 return bindings(r,L,*row,why);
}

bool certifyAIRound45GetterCorrectionEntry(Room&r,lua_State*L,int nargs,QString*why){
 // This is the exact callable-plus-arguments tuple immediately before
 // LuaRuntime::protectedCall. Extra lower stack values belong to its caller.
 // Do not infer an event, skill name or saved-slot identity from this tuple.
 if(!L||!AIRound45Inspect::capacity(L,2048)||(nargs!=2&&nargs!=3))
  return fail(why,"correction entry signature/inspection capacity unavailable");
 const int top=lua_gettop(L),fn=top-nargs;
 if(fn<1)return fail(why,"correction entry callable missing");
 Stack stack{L,top};
 const auto object=[&](int i,const QString&base,const QStringList&methods){
  return matchAIRound45NativeObjectMethods(r,L,i,base,methods);
 };
 if(nargs==3){
  // swig/luaskills.i LuaInvaliditySkill::isSkillValid supplies these
  // exact three objects. Null Player/Skill pointers become nil; the pinned
  // podai body explicitly accepts both and has the same whole-effect proof.
  if(!object(fn+1,"LuaInvaliditySkill",{"objectName"})
     ||(!lua_isnil(L,fn+2)&&!object(fn+2,"Player",{"getMark"}))
     ||(!lua_isnil(L,fn+3)&&!object(fn+3,"Skill",{"objectName"})))
   return fail(why,"correction invalidity argument binding changed");
  return proveAIRound45GetterCorrection(r,L,fn,AIRound45CorrectionArguments::Invalidity,why);
 }
 // The V2 native wrapper passes the context by borrowed userdata. Validate
 // its captured class/metatable/getter routes, without fetching primary,
 // secondary or holder; the complete body proof seals their Player methods.
 if(!object(fn+1,"LuaDistanceSkillV2",{"objectName"})
    ||!object(fn+2,"CorrectSkillContext",{"getPrimary","getSecondary","getHolder"}))
  return fail(why,"correction distance argument binding changed");
 return proveAIRound45GetterCorrection(r,L,fn,AIRound45CorrectionArguments::DistanceV2,why);
}
