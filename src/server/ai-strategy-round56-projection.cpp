#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-native-types.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round45-continuity.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round53-ainoto.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "wrapped-card.h"
#include "standard.h"
#include "standard-cards.h"
#include "maneuvering.h"
#include "ol.h"
#include "settings.h"
#include "server-info.h"
#include <QPointer>
#include <QSet>
#include <QHash>
#include <typeinfo>
#include <cstdio>
#ifdef Q_OS_LINUX
#include <execinfo.h>
#endif
namespace {
thread_local AIRound56ProjectionScope*active=nullptr;
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool ordinaryPolicy(const Card*c){
 if(!c)return false;
 // No virtual call on an unknown outer type. All permitted wrappers and
 // concrete policies have independently audited full virtual descendants.
 if(typeid(*c)==typeid(WrappedCard))c=static_cast<const WrappedCard*>(c)->getRealCard();
 if(!c)return false;const auto&t=typeid(*c);
 return t==typeid(XufaCard)||t==typeid(Slash)||t==typeid(FireSlash)||t==typeid(ThunderSlash)
  ||t==typeid(Jink)||t==typeid(Peach)||t==typeid(Analeptic)||t==typeid(Duel)
  ||t==typeid(Snatch)||t==typeid(Dismantlement)||t==typeid(Nullification)||t==typeid(ExNihilo)
  ||t==typeid(Collateral)||t==typeid(AmazingGrace)||t==typeid(GodSalvation)
  ||t==typeid(SavageAssault)||t==typeid(ArcheryAttack)||t==typeid(FireAttack)
  ||t==typeid(IronChain)||t==typeid(Indulgence)||t==typeid(SupplyShortage)||t==typeid(Lightning);
}
bool ownerPolicy(const Card*c){
 if(ordinaryPolicy(c))return true;
 // Owner-only extension: Crossbow has no policy override. Its complete
 // Weapon/EquipCard/Card readonly dispatch, metadata, correction and
 // allocation union is audited in equipment-source-v1. Exact native dispatch
 // below restricts that preproved family; no card value supplies the proof.
 if(!c)return false;
 if(typeid(*c)==typeid(WrappedCard))c=static_cast<const WrappedCard*>(c)->getRealCard();
 return c&&typeid(*c)==typeid(Crossbow);
}
bool literalPolicy(const Card*c,const QString&name){
 if(!ordinaryPolicy(c)||typeid(*c)==typeid(WrappedCard))return false;
 // These methods now dispatch only inside the exact admitted source family.
 return c->objectName()==name&&c->getId()<0;
}
}
struct AIRound56ProjectionScope::Data{
 Room&r;QPointer<ServerPlayer>viewer;lua_State*L;AIRound56ProjectionScope*previous;
 bool ready=false,invalid=false;quint64 generation=0,transport=0;
 quint64 weaponRangeRevision=Weapon::rangeRevision();
 QSet<const void*>getters;QHash<const Card*,QPointer<const Card>>policyCards;
 std::unique_ptr<AIRound56AsyncScope>async;
 Data(Room&room,ServerPlayer*p,lua_State*l):r(room),viewer(p),L(l),previous(active){}
};
AIRound56ProjectionScope::AIRound56ProjectionScope(Room&r,ServerPlayer*p,lua_State*L,QString*why):d(new Data(r,p,L)){
 active=this;auto fail=[&](const QString&s){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   // Fixed proof guard/source-binding labels only; no holder, card, mark or
   // callback result is printed and this diagnostic grants no authority.
   std::fprintf(stderr,"R56_PROJECTION_ENTRY_REFUSAL reason=%s\n",s.toUtf8().constData());std::fflush(stderr);
  }
  if(why)*why=s;d->invalid=true;retireAIRound56Projection(r,L);
 };
 try{
  auto*rt=r.roomRuntime();
  if(!p||p->getRoom()!=&r||!rt||!L||rt->lua().rawState()!=L||rt->isClosing()
   ||r.getMode()!="02p"||Config.EnableHegemony||ServerInfo.EnableHegemony||r.getScenario()
   ||!lua_checkstack(L,8192)){fail("projection public runtime domain");return;}
  if(!hasAIRound56CloneInputLineage(r,L)||!hasAIRound56ProjectionLineage(r,L)||!hasAIRound56PatternsLineage(r,L)||!hasAIRound56AsyncUniverse(r,L)
   ||!hasAIRound56ModeEvaluationLineage(r,L)||!proveAIRound56ServerLogEffects(r)){
   fail("projection complete bootstrap/constructor/mode/host source lineage unavailable");return;}
  // All public definitions, before private skill-instance applicability. The
  // compiled per-TU type map proves the exact native source implementation;
  // this does not call a virtual getter or examine a holder/card/mark result.
  for(const QString&name:Sanguosha->getSkillNames()){
   const Skill*s=Sanguosha->getSkill(name);if(!s||(aiRound56NativeEffectClass(s)&2u)==0){fail("unreviewed native skill metadata implementation");return;}
   if(auto*x=dynamic_cast<const LuaTriggerSkillV2*>(s))if(x->get_usage_ref!=0){fail("authored usage-reference provider outside projection source union");return;}
   if(auto*x=dynamic_cast<const LuaViewAsSkillV2*>(s))if(x->get_usage_ref!=0){fail("authored view-as usage-reference provider outside projection source union");return;}
  }
  const auto nativeFamily=[&](const auto&definitions){for(const auto*s:definitions)if(!s||(aiRound56NativeEffectClass(s)&1u)==0)return false;return true;};
  if(!nativeFamily(Sanguosha->getDistanceSkills())||!nativeFamily(Sanguosha->getMaxCardsSkills())
   ||!nativeFamily(Sanguosha->getTargetModSkills())||!nativeFamily(Sanguosha->getAttackRangeSkills())
   ||!nativeFamily(Sanguosha->getInvaliditySkills())||!nativeFamily(Sanguosha->getViewAsEquipSkills())
   ||!nativeFamily(Sanguosha->getProhibitSkills())||!nativeFamily(Sanguosha->getCardLimitSkills())){
   fail("unreviewed correction implementation");return;}
  QString error;if(!inspectAIRound47XufaCorrectionBindings(r,L,&error)
   ||!proveAIRound56ModeEvaluationUnion(r,L,&error)){fail(error);return;}
  // A detached QPointer records actual native source custody, not a face/value
  // cache. Only this viewer's original hand/equipment enters full policy calls.
  for(const Card*c:p->getCards("he")){
   if(!ownerPolicy(c)){fail("own full-policy card outside finite standard source family");return;}
   d->policyCards.insert(c,QPointer<const Card>(c));
  }
  for(const QString&name:{"jink","slash","peach","fire_slash","thunder_slash","analeptic","dismantlement"})
   if(!Sanguosha->aiRound56StandardCloneFactory(name)){fail("public named projection factory differs");return;}
  const auto capture=[&](const LuaFunction&f){if(f==0)return;Stack z(L);f.push(L);d->getters.insert(lua_topointer(L,-1));};
  for(const auto*s:Sanguosha->getDistanceSkills()){
   if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s)){capture(x->correct_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getMaxCardsSkills()){
   if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s)){capture(x->extra_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getTargetModSkills()){
   if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))capture(x->correct_func);
   else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s)){capture(x->residue_func);capture(x->distance_limit_func);capture(x->extra_target_func);}}
  for(const auto*s:Sanguosha->getAttackRangeSkills()){
   if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s)){capture(x->extra_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s))capture(x->skill_valid);
  for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s))capture(x->view_as_equip);
  for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s))capture(x->is_prohibited);
  for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s)){capture(x->limit_list);capture(x->limit_pattern);capture(x->limit_reason);}
  // Both physical catalogue and virtual-horse prototype lookups are public
  // registered domains. They are checked without selecting equipped players.
  QSet<const Card*>horses;for(int id=0;id<Sanguosha->getCardCount();++id)horses.insert(Sanguosha->getEngineCard(id));
  for(const auto*c:Sanguosha->findChildren<OffensiveHorse*>())horses.insert(c);
  for(const auto*c:Sanguosha->findChildren<DefensiveHorse*>())horses.insert(c);
  for(const Card*c:horses){
   const LuaFunction*f=nullptr;
   if(auto*x=dynamic_cast<const LuaHorse*>(c))f=&x->correct_func;
   else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(c))f=&x->correct_func;
   else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(c))f=&x->correct_func;
   if(f&&*f!=0){Stack z(L);f->push(L);if(!proveAIRound47XufaGetterFunction(r,L,-1,&error)){fail(error);return;}d->getters.insert(lua_topointer(L,-1));}
  }
  d->generation=rt->lua().generation();d->transport=rt->xufaTransportEpoch();
  d->async.reset(new AIRound56AsyncScope(r,L,AIRound56AsyncScope::GameplayVariants));
  d->ready=d->transport&&d->async->accepted();if(!d->ready)fail("projection source allocation union unavailable");
 }catch(...){fail("projection native proof allocation failure");}
}
AIRound56ProjectionScope::~AIRound56ProjectionScope(){
 if(d->ready&&!Weapon::rangeRevisionCurrent(d->weaponRangeRevision))
  retireAIRound56Projection(d->r,d->L);
 active=d->previous;
}
bool AIRound56ProjectionScope::accepted()const noexcept{
 auto*rt=d->r.roomRuntime();return d->ready&&!d->invalid&&d->viewer&&rt&&!rt->isClosing()
  &&Weapon::rangeRevisionCurrent(d->weaponRangeRevision)
  &&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&hasAIRound56CloneInputLineage(d->r,d->L)&&hasAIRound56AsyncUniverse(d->r,d->L)&&hasAIRound56ProjectionLineage(d->r,d->L)&&hasAIRound56ModeEvaluationLineage(d->r,d->L)&&hasAIRound56PatternsLineage(d->r,d->L);
}
bool inAIRound56ProjectionScope(const Room&r,lua_State*L)noexcept{
 return active&&&active->d->r==&r&&active->d->L==L&&active->accepted();
}
void retireAIRound56Projection(Room&r,lua_State*L)noexcept{
 for(auto*x=active;x;x=x->d->previous)if(&x->d->r==&r&&x->d->L==L)x->d->invalid=true;
 retireAIRound56ProjectionLineage(r,L);retireAIRound56CloneInputLineage(r,L);
 retireAIRound56ModeEvaluationLineage(r,L);
 retireAIRound56PatternsLineage(r,L);
 retireAIRound56AsyncUniverse(L);retireAIRound53AiNoTo(r);
 if(auto*rt=r.roomRuntime()){rt->revokePrioritySource();rt->revokeXufaTransportContinuity();}
 observeAIRound45UnprovedNativeQuery(&r);
}
bool acceptAIRound56ProjectionGetterEntry(Room&r,lua_State*L,int n)noexcept{
 if(!inAIRound56ProjectionScope(r,L)||n<1||n>5||lua_gettop(L)<n+1)return false;
 // Permission was established for the entire callback union before any private
 // eligibility. This is only exact dispatch membership, not a new proof/grant.
 return active->d->getters.contains(lua_topointer(L,-n-1));
}
bool preserveAIRound56ProjectionNativeQuery(Room&r,const Card*c,bool hasCard,bool scalarOnly)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIRound56ProjectionScope(r,rt->lua().rawState()))return false;
 if(!hasCard||scalarOnly)return true;
 const auto i=active->d->policyCards.constFind(c);
 const bool ok=i!=active->d->policyCards.cend()&&i.value().data()==c;
 if(!ok){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   std::fprintf(stderr,"R56_QUERY_RETIRE site=native-card-policy-custody\n");std::fflush(stderr);
#ifdef Q_OS_LINUX
   // Code locations only, after refusal; no card data or source authority.
   std::fprintf(stderr,"R56_CUSTODY_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
   void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
   std::fprintf(stderr,"R56_CUSTODY_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
  }
  retireAIRound56Projection(r,rt->lua().rawState());
 }
 return ok;
}
void recordAIRound56ProjectionSourceCard(Room&r,const Card*c,const QString&name)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIRound56ProjectionScope(r,rt->lua().rawState()))return;
 try{
  if(!literalPolicy(c,name)){retireAIRound56Projection(r,rt->lua().rawState());return;}
  active->d->policyCards.insert(c,QPointer<const Card>(c));
 }catch(...){retireAIRound56Projection(r,rt->lua().rawState());}
}

void recordAIRound56ProjectionOwnerSourceCard(Room&r,const Player*owner,const Card*c,const QString&name)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIRound56ProjectionScope(r,rt->lua().rawState())
  ||active->d->viewer.data()!=owner)return;
 recordAIRound56ProjectionSourceCard(r,c,name);
}

bool aiRound56OrdinaryCardPolicyType(const Card *card) noexcept { return ordinaryPolicy(card); }
