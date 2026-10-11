#include "ai-strategy-round45-poxi-effects.h"
#include "ai-strategy-round45-natural-event.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "lua-wrapper.h"
#include "structs.h"
#include "lua.hpp"
#include <cstring>
#include <typeinfo>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;~Stack(){lua_settop(L,n);}};
bool no(QString*r,const QString&s){if(r)*r=s;return false;}
bool plain(lua_State*L,int i){auto*t=AIRound45Inspect::table(L,i);return t&&!t->metatable;}
bool require(const AIRound45PoxiEffectProof&p,const QString&key,QString*r){return p.dependency?p.dependency(key,r):no(r,"Poxi unproved dependency: "+key);}
bool callback(lua_State*L,const LuaFunction&ref,const QString&where,const AIRound45PoxiEffectProof&p,QString*r){
 // Fixed generated wrappers use zero for an absent function. Do not treat an
 // invalid nonzero reference as a native default or silently skip it.
 if(ref==0)return true;
 Stack s{L,lua_gettop(L)};
 if(ref.state()!=L||ref.generation()!=LuaRuntime::fromState(L)->generation())return no(r,where+": callback generation/state mismatch");
 AIRound45Inspect::rawGeti(L,LUA_REGISTRYINDEX,ref.registryRef());
 if(!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return no(r,where+": missing/non-Lua correction callback");
 return p.luaFunction?p.luaFunction(L,lua_absindex(L,-1),where,r):no(r,where+": complete Lua effect proof unavailable");
}
bool env(lua_State*L,int index){
 index=lua_absindex(L,index);Stack s{L,lua_gettop(L)};
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  if(std::strcmp(n,"_ENV"))return false;
  AIRound45Inspect::globals(L);bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
 }return true;
}
}
bool proveAIRound45PoxiCorrectionUnion(Room&r,lua_State*L,const AIRound45PoxiEffectProof&p,QString*why){
 if(!L||r.getLuaState()!=L||!AIRound45Inspect::capacity(L,64))return no(why,"Poxi correction union outside native gameplay state");
 EngineRuntimeContextScope scope(*Sanguosha,&r);
 // Prove all invalidity definitions BEFORE any caller can branch on hasSkill.
 // Include even definitions whose current native mayReject prefilter is false.
 for(const InvaliditySkill*skill:Sanguosha->getInvaliditySkills()){
  if(!skill)return no(why,"null invalidity definition");
  if(const auto*q=dynamic_cast<const LuaInvaliditySkill*>(skill)){
   if(!callback(L,q->skill_valid,"invalidity:"+skill->objectName()+".skill_valid",p,why))return false;
  }else if(typeid(*skill)!=typeid(InvaliditySkill)){
   if(!p.nativeSkill||!p.nativeSkill(skill,"InvaliditySkill.isSkillValid",why))return no(why,"unproved native invalidity provider: "+skill->objectName());
  }
 }
 // Both getFixed and getCorrect are in the source branch union. Enumerate all
 // V2 definitions even when no current hidden holder/instance would select it.
 for(const DistanceSkill*skill:Sanguosha->getDistanceSkills()){
  if(!skill)return no(why,"null distance definition");
  if(const auto*q=dynamic_cast<const LuaDistanceSkillV2*>(skill)){
   if(!callback(L,q->correct_func,"distance-v2:"+skill->objectName()+".correct_func",p,why)
      ||!callback(L,q->fixed_func,"distance-v2:"+skill->objectName()+".fixed_func",p,why))return false;
  }else if(const auto*q=dynamic_cast<const LuaDistanceSkill*>(skill)){
   if(!callback(L,q->correct_func,"distance:"+skill->objectName()+".correct_func",p,why)
      ||!callback(L,q->fixed_func,"distance:"+skill->objectName()+".fixed_func",p,why))return false;
  }else if(typeid(*skill)!=typeid(DistanceSkill)&&typeid(*skill)!=typeid(DistanceSkillV2)){
   if(!p.nativeSkill||!p.nativeSkill(skill,"DistanceSkill.fixed+correct",why))return no(why,"unproved native distance provider: "+skill->objectName());
  }
 }
 // V2 holder expansion itself calls instance availability/invalidity. Its
 // fixed native body must be checked as well, not merely the final Lua refs.
 return require(p,"Engine.evaluateCorrectSkill+correctSkillHolders+Player.isSkillInstanceEffectAvailable.native_union",why);
}
bool proveAIRound45PoxiSuppressionBindings(Room&r,lua_State*L,int method,int ns,const AIRound45PoxiEffectProof&p,QString*why){
 if(!L||!AIRound45Inspect::capacity(L,128))return no(why,"Poxi suppression inspection unavailable");
 Stack stack{L,lua_gettop(L)};method=lua_absindex(L,method);ns=lua_absindex(L,ns);
 if(!matchAIRound45PinnedFunction(r,L,method,"lua/ai/smart-ai.lua",2366,2431,why)||!env(L,method))return no(why,"Poxi shouldSuppressIntention exact provider/environment changed");
 if(!lua_istable(L,ns))return no(why,"Poxi sgs namespace absent");
 // Native SWIG sgs carries its captured __index/__newindex table. Authenticate
 // exact read/write keys; do not falsely require the whole namespace be plain.
 for(const auto&name:QStringList{"ai_liuli_effect","ai_lijian_effect"})
  if(!matchAIRound45NativeNamespaceWrite(r,L,"sgs",name))return no(why,"Poxi namespace write route changed: "+name);
 // Shared service proves this is the original namespace with unmodified
 // binding routes; merely being a plain attacker-supplied table is insufficient.
 if(!require(p,"captured_sgs_namespace_identity+raw_required_keys_or_exact_native_getters",why)
    ||!require(p,"sgs.QList2Table+sgs.list.full_pinned_union",why))return false;
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"ipairs");
 if(!matchAIRound45NativeBinding(r,L,-1,"ipairs"))return no(why,"Poxi ipairs native binding changed");lua_pop(L,2);
 AIRound45Inspect::field(L,ns,"ai_suppress_intention");
 if(!plain(L,-1))return no(why,"Poxi intention lookup has unproved __index");lua_pop(L,1);
 // Truthiness of suppression-table values and the two global booleans is never
 // used here. All private outcomes have the same proof result.
 for(const auto&name:QStringList{"hasFlag","isKindOf","toString","getSkillNames"})
  if(!matchAIRound45NativeDerivedMethods(r,L,"Card",{name}))return no(why,"Poxi card native binding changed: "+name);
 for(const auto&name:QStringList{"hasSkill","distanceTo","hasFlag","getMark","inMyAttackRange"})
  if(!matchAIRound45NativeDerivedMethods(r,L,"Player",{name}))return no(why,"Poxi player native binding changed: "+name);
 if(!require(p,"Card.flags+kind+serialization+change_cards.native_lifetime_union",why))return false;
 // Keep even the Slash/lolita alternative until an independently proved
 // immutable construction/effect graph allows its exclusion. Mutable current
 // isDamageCard/isKindOf observations cannot establish continuity.
 if(!require(p,"Player.inMyAttackRange.full_native_targetmod_attackrange_union",why))return false;
 return proveAIRound45PoxiCorrectionUnion(r,L,p,why);
}
bool certifyAIRound45PoxiFilterBody(Room&r,lua_State*L,int self,const AIRound45NaturalFilterProof&proof,const AIRound45PoxiEffectProof&p,QString*why){
 if(!L||!AIRound45Inspect::capacity(L,128))return no(why,"Poxi body metadata stack unavailable");
 Stack stack{L,lua_gettop(L)};self=lua_absindex(L,self);
 bool actual=inspectAIRound45NaturalFilterEntry(r,L,4,[&](int event,ServerPlayer*,const QVariant&){return event==proof.event;});
 if(!actual)return no(why,"Poxi body has no authenticated actual filterEvent tuple");
 if(!require(p,"actual_self_is_captured_native_AI_instance",why))return false;
 if(!require(p,"Poxi.native_factory_to_current_event_frame.complete_effect_union",why))return false;
 // These exact events have no event-specific body in fixed SmartAI:filterEvent;
 // the common prelude, complete event callbacks, and trailing common effects
 // are still obligations of the central verifier. No claim about private data.
 switch(proof.event){
 case TargetSpecifying:case TargetConfirming:case TargetConfirmed:
 case EventSkillWillInvoke:case EventSkillPay:case EventSkillTargetConfirming:
 case EventSkillInvoking:case EventSkillEffect:case EventSkillEffectTarget:
 case CardAsked:case MarkChanged:case EventAcquireSkill:return true;
 case PreCardUsed:return require(p,"Poxi.PreCardUsed.full_getLord+conversion+whocard+choicemade_union",why);
 case TargetSpecified:return require(p,"Poxi.TargetSpecified.actual_suppress_binding+complete_intention+damage_union",why);
 case CardEffect:return require(p,"Poxi.CardEffect.clone+prior_cardEffect_deletion+native_lifetime_union",why);
 default:return no(why,"Poxi event outside pre-consumer finite body union");
 }
}
bool certifyAIRound45PoxiFilterBody(Room&r,lua_State*L,int self,const AIRound45NaturalFilterProof&p,QString*why){
 return certifyAIRound45PoxiFilterBody(r,L,self,p,AIRound45PoxiEffectProof{},why);
}
