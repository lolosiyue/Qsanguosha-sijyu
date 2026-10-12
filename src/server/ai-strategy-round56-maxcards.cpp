#include "maneuvering.h"
#include "tenyear.h"
#include "wrapped-card.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-native-types.h"
#include "ai-strategy-round56-factory-source.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "room-definition-registry.h"
#include "serverplayer.h"
#include "player-initial-source-origin.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "package.h"
#include "lua-wrapper.h"
#include "settings.h"
#include "server-info.h"
#include <QPointer>
#include <QHash>
#include <QSet>
#include <cstdio>
#include <typeinfo>
namespace {
thread_local AIRound56MaxCardsScope *activeMaxCards=nullptr;
struct Stack { lua_State *L;int top;~Stack(){lua_settop(L,top);} };
void traceCorrection(int root,const char *phase,const QString &reason=QString()) {
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
  std::fprintf(stderr,"%s phase=%s reason=%s\n",root==18?"R56_ZUNWEI_SEED_SCOPE":root==17?"R56_OWNER_EQUIPMENT_QUERY_SCOPE":root==16?"R56_PUBLIC_ATTACK_RANGE_MATRIX_SCOPE":root==15?"R56_OWNER_PINDIAN_SCOPE":root==14?"R56_OWNER_JILEI_SCOPE":root==13?"R56_WORLD_SKILL_QUERY_SCOPE":root==12?"R56_OWNER_CARD_STATE_SCOPE":root==11?"R56_PUBLIC_SKILL_QUERY_SCOPE":root==10?"R56_SKILL_VALIDITY_SCOPE":root==9?"R56_CONNECTION_QUERY_SCOPE":root==8?"R56_OWNER_CANDIDATES_SCOPE":root==7?"R56_OWNER_EVALUATIONS_SCOPE":root==6?"R56_FIXED_SLASH_PROBES_SCOPE":root==5?"R56_OWNER_ANALEPTIC_SCOPE":root==4?"R56_OWNER_SLASH_SCOPE":root==3?"R56_DISTANCE_SCOPE":root==2?"R56_OWNER_CARD_RULES_SCOPE":root==1?"R56_ATTACK_RANGE_SCOPE":"R56_MAXCARDS_SCOPE",phase,qPrintable(reason));std::fflush(stderr);
 }
}
}
struct AIRound56MaxCardsScope::Data {
 Room &room;lua_State *L;AIRound56MaxCardsScope *previous;
 QPointer<ServerPlayer> target;QList<QPointer<ServerPlayer>> roster;
 quint64 generation=0,mapGeneration=0,weaponRevision=0;
 bool ready=false;Root root;
 bool cardRulesRoot()const noexcept{return root==OwnerCardRules||root==OwnerSlashWithoutCrossbow||root==OwnerAnaleptic||root==FixedSlashProbes||root==FixedOwnerEvaluations||root==OwnerPhysicalCandidates||root==OwnerCardState||root==OwnerJilei||root==ZunweiSeed;}
 QHash<const void *,QSet<int>> entries;
 QHash<const Card *,QPointer<const Card>> policyCards;
 QHash<const WrappedCard *,QPointer<const Card>> physicalFaces;
 QSet<QString> literalFactories;
 std::unique_ptr<CardLifetimeScope> lifetime;
 std::unique_ptr<AIRound56AsyncScope> async;
 Data(Room&r,ServerPlayer*p,lua_State*l,Root root):room(r),L(l),previous(activeMaxCards),target(p),root(root){}
 bool receiverOrigins() const noexcept {
  if(!room.roomState()->aiRound56PhysicalIdMapSourceOrigin())return false;
  for(const auto &p:roster)if(!p||p->getRoom()!=&room||p->parent()!=&room
   ||!hasAIRound56PlayerCanonicalHandOrigin(*p)
   ||!hasAIRound56PlayerEmptyPublicCardsOrigin(*p)
   ||!hasAIRound56PlayerEmptyLimitationsOrigin(*p))return false;
  return true;
 }
};
AIRound56MaxCardsScope::AIRound56MaxCardsScope(Room&r,ServerPlayer*p,lua_State*L)
 :AIRound56MaxCardsScope(r,p,L,MaxCardsFixedExtra){}
AIRound56MaxCardsScope::AIRound56MaxCardsScope(Room&r,ServerPlayer*p,lua_State*L,Root root):d(new Data(r,p,L,root)) {
 activeMaxCards=this;
 const auto fail=[&](const QString &why){d->ready=false;traceCorrection(d->root,"refused",why);retireAIRound56Projection(r,L);};
 try {
  auto *rt=r.roomRuntime();
  if(!rt||rt->isClosing()||!p||typeid(*p)!=typeid(ServerPlayer)||p->getRoom()!=&r||p->parent()!=&r
   ||!L||rt->lua().rawState()!=L||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
   ||!Sanguosha->currentRoomContext()||Sanguosha->currentRoomContext()->roomRuntime()!=rt
   ||(r.getMode()!="02p"&&r.getMode()!="04p")||Config.EnableHegemony||ServerInfo.EnableHegemony||r.getScenario()) {
   fail("original normal Game VM receiver domain");return;
  }
  d->lifetime.reset(new CardLifetimeScope(globalCardLifetimeManager(),root==ZunweiSeed?"R56ZunweiSeed":root==OwnerEquipmentQueries?"R56OwnerEquipmentQueries":root==PublicAttackRangeMatrix?"R56PublicAttackRangeMatrix":root==OwnerPindian?"R56OwnerPindian":root==OwnerJilei?"R56OwnerJilei":root==WorldSkillQueries?"R56WorldSkillQueries":root==OwnerCardState?"R56OwnerCardState":root==PublicSkillQueries?"R56PublicSkillQueries":root==PlayerSkillValidity?"R56SkillValidity":root==ConnectionQuery?"R56ConnectionQuery":root==OwnerPhysicalCandidates?"R56OwnerCandidates":root==FixedOwnerEvaluations?"R56OwnerEvaluations":root==FixedSlashProbes?"R56FixedSlashProbes":root==OwnerAnaleptic?"R56OwnerAnaleptic":root==OwnerSlashWithoutCrossbow?"R56OwnerSlash":root==PublicDistances?"R56PublicDistances":root==OwnerCardRules?"R56OwnerCardRules":root==AttackRangeFixedExtra?"R56AttackRangeFixedExtra":"R56MaxCardsFixedExtra"));
  if(!hasAIRound56CloneInputLineage(r,L)||!hasAIRound56ProjectionLineage(r,L)
   ||!hasAIRound56AsyncUniverse(r,L)||!proveAIRound56ServerLogEffects(r)
   ||!rt->definitions().hasFactorySourceOrigin(r,L)||!hasAIRound56GlobalFactoryMapOrigin(Sanguosha)
   ||!lua_checkstack(L,8192)) {fail("complete bootstrap/native map/async source absent");return;}
  // The 04p source domain has its own bootstrap-issued mode lineage. Prove
  // its complete callback union independently of the selected skill/holder.
  if(r.getMode()=="04p"){
   QString why;if(!hasAIRound56ModeEvaluationLineage(r,L)||!proveAIRound56ModeEvaluationUnion(r,L,&why)){
    fail(why.isEmpty()?QStringLiteral("ordinary identity mode source union"):why);return;
   }
  }
  d->generation=rt->lua().generation();d->mapGeneration=r.roomState()->aiRound56PhysicalIdMapGeneration();
  d->weaponRevision=Weapon::rangeRevision();
  // All public definitions and all necessary callback roles are proved before
  // any holder/instance/mark branch. No selected skill list shrinks this union.
  const auto family=[&](const auto&skills){for(const auto*s:skills)if(!s||(aiRound56NativeEffectClass(s)&1u)==0)return false;return true;};
  if(!family(Sanguosha->getMaxCardsSkills())||!family(Sanguosha->getDistanceSkills())
   ||!family(Sanguosha->getAttackRangeSkills())||!family(Sanguosha->getInvaliditySkills())
   ||!family(Sanguosha->getViewAsEquipSkills())) {fail("unreviewed complete correction family");return;}
  if(root==OwnerPindian) {
   // Complete public native family, before canPindian examines either hand or
   // except-self branch. Only the audited Qianjie body (bit4) is currently
   // closed; a new Lua/native Pindian provider fails the whole root.
   for(const auto *skill:Sanguosha->getProhibitPindianSkills())
    if(!skill||(aiRound56NativeEffectClass(skill)&4u)==0) {
     fail("unreviewed complete Pindian prohibit family");return;
    }
  }
  if(d->cardRulesRoot()&&(!family(Sanguosha->getProhibitSkills())
   ||!family(Sanguosha->getTargetModSkills())||!family(Sanguosha->getCardLimitSkills())
   ||!hasAIRound56PatternsLineage(r,L))) {fail("complete owner-card native correction/pattern family");return;}
  if(root==ConnectionQuery||root==PlayerSkillValidity||root==PublicSkillQueries||root==WorldSkillQueries||root==OwnerPindian||root==PublicAttackRangeMatrix||root==OwnerEquipmentQueries||root==ZunweiSeed) {
   // Cache export and validity dispatch use registered public metadata,
   // including getFrequency/isEquipSkill. Close every implementation
   // independently of the selected skill, holder or applicability branch.
   // No getUsageRef/getAmountRef/activation provider is invoked by this graph.
   for(const QString &name:Sanguosha->getSkillNames()) {
    const Skill *skill=Sanguosha->getSkill(name);
    if(!skill||(aiRound56NativeEffectClass(skill)&2u)==0) {
     fail("complete public connection metadata family");return;
    }
   }
  }
  QString why;QList<LuaFunction> getterFamily;
  const auto capture=[&](const LuaFunction &field,int arguments){
   const LuaFunction fn(field);if(fn==0)return true;
   if(fn.state()!=L||fn.generation()!=d->generation)return false;
   Stack stack{L,lua_gettop(L)};AIRound45Inspect::rawGeti(L,LUA_REGISTRYINDEX,fn.registryRef());
   if(!lua_isfunction(L,-1))return false;
   getterFamily.append(fn);
   // Membership is inert until the COMPLETE source family and all remaining
   // native/custody/async checks succeed below (d->ready is still false).
   d->entries[lua_topointer(L,-1)].insert(arguments);return true;
  };
  bool callbacks=true;
  for(const auto*s:Sanguosha->getMaxCardsSkills()) {
   if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s))callbacks=capture(x->extra_func,2)&&capture(x->fixed_func,2)&&callbacks;
  }
  for(const auto*s:Sanguosha->getDistanceSkills()) {
   if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s))callbacks=capture(x->correct_func,3)&&capture(x->fixed_func,3)&&callbacks;
  }
  for(const auto*s:Sanguosha->getAttackRangeSkills()) {
   if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s))callbacks=capture(x->extra_func,3)&&capture(x->fixed_func,3)&&callbacks;
  }
  for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s))callbacks=capture(x->skill_valid,3)&&callbacks;
  for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s))callbacks=capture(x->view_as_equip,2)&&callbacks;
  if(d->cardRulesRoot()) {
   for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s))callbacks=capture(x->is_prohibited,5)&&callbacks;
   for(const auto*s:Sanguosha->getTargetModSkills()) {
    if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))callbacks=capture(x->correct_func,2)&&callbacks;
    else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s))callbacks=capture(x->residue_func,4)&&capture(x->distance_limit_func,4)&&capture(x->extra_target_func,3)&&callbacks;
   }
   for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s))
    callbacks=capture(x->limit_list,3)&&capture(x->limit_pattern,3)&&capture(x->limit_reason,3)&&callbacks;
  }
  // Full Room-owned horse definition family, including clone getCorrect.
  // Global HorseSkill uses nonvirtual Horse::getCorrect through native base
  // pointers: private correction is constructor-only, no Lua field dispatch.
  for(const auto*package:rt->packages())for(const Card*c:package->findChildren<Card*>()) {
   if(auto*x=dynamic_cast<const LuaHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
  }
  if(callbacks)callbacks=proveAIRound56ActivateGetterFamily(r,L,getterFamily,&why);
  if(!callbacks){fail(why.isEmpty()?QStringLiteral("complete correction closure/VM family"):why);return;}
  const auto factories=inspectAIRound56PublicFactorySources(r);
  if(!factories.enumerationComplete||!factories.equipmentRecipeFamilyComplete) {fail("equipment resolver source incomplete");return;}
  if(root==ZunweiSeed&&!factories.zunweiSkillRecipeComplete) {
   fail("fixed native ZunweiCard factory alternatives differ");return;
  }
  const auto players=r.getAllPlayers(true);
  if(!players.contains(p)){fail("target outside source Room roster");return;}
  for(auto*receiver:players) {
   if(!receiver||typeid(*receiver)!=typeid(ServerPlayer)){fail("unreviewed native Player receiver");return;}
   d->roster.append(receiver);
  }
  // These are constructor/writer histories, not current empty-card/limit tests.
  // Canonical physical ID excludes virtual-material hand suit dispatch; all
  // other receiver reads are confined by the Game VM and Card lifetime scope.
  if(!d->receiverOrigins()){fail("complete canonical hand/public-card/limitation receiver origins");return;}
  if(d->cardRulesRoot()) {
   // Prove the entire fixed public factory domain before looking at owner
   // material. Runtime maps already have original, monotonic source history.
   QStringList factoryNames{"jink","slash","peach","fire_slash","thunder_slash"};
   if(root==FixedOwnerEvaluations)factoryNames<<"analeptic"<<"dismantlement";
   if(root==OwnerPhysicalCandidates||root==OwnerCardState||root==ZunweiSeed)factoryNames.clear();
   if(root==OwnerJilei)factoryNames=QStringList{QStringLiteral("ex_nihilo")};
   for(const QString&name:factoryNames) {
    if(!Sanguosha->aiRound56StandardCloneFactory(name)){fail("original fixed owner-rule factory recipe differs");return;}
    d->literalFactories.insert(name);
   }
   // Canonical hand source + Room-owned physical map is the custody issuer;
   // native type membership only closes virtual dispatch, never issues custody.
   if(root!=ZunweiSeed)for(const Card *card:p->getCards("he")) {
    if(!r.roomState()->aiRound56PhysicalIdSourceContains(card)
     ||!aiRound56OrdinaryCardPolicyType(card)){fail("owner policy receiver outside canonical native family");return;}
    d->policyCards.insert(card,QPointer<const Card>(card));
    if(typeid(*card)!=typeid(WrappedCard)){fail("physical policy root is not the original wrapper");return;}
    const auto *wrapper=static_cast<const WrappedCard *>(card);
    const Card *face=wrapper->aiRound56PhysicalCloneOwnedFaceOrigin();
    if(!face||!aiRound56OrdinaryCardPolicyType(face)){fail("owned physical policy face source unavailable");return;}
    d->physicalFaces.insert(wrapper,QPointer<const Card>(face));
    d->policyCards.insert(face,QPointer<const Card>(face));
   }
  }
  // Constructors are exact equipment recipes, IDs atomic; LuaFunction copies
  // only snapshot scalar tuples. Fresh products have no parent/connections;
  // deleteLater and approved userdata cleanup enqueue domain retirement. The
  // active CardLifetimeScope prevents reclamation, and this graph pumps no events.
  d->async.reset(new AIRound56AsyncScope(r,L,AIRound56AsyncScope::GameplayVariants));
  d->ready=d->async->accepted();
  if(!accepted()){fail("entry continuity invalidated");return;}
  traceCorrection(d->root,"entered");
 }catch(...){fail("native preflight allocation failure");}
}
AIRound56MaxCardsScope::~AIRound56MaxCardsScope() {
 const bool wasReady=d->ready;
 if(wasReady&&(!accepted()||!d->receiverOrigins()))retireAIRound56Projection(d->room,d->L);
 if(wasReady)traceCorrection(d->root,accepted()?"completed":"retired");
 activeMaxCards=d->previous;
}
bool AIRound56MaxCardsScope::accepted() const noexcept {
 // Recheck the original custody receipt, not a sampled getRealCard equality.
 // QPointers fail before dereferencing a retired wrapper or inner object.
 for(auto it=d->physicalFaces.cbegin();it!=d->physicalFaces.cend();++it)
  if(d->policyCards.value(it.key()).data()!=it.key()||!it.value()
    ||it.key()->aiRound56PhysicalCloneOwnedFaceOrigin()!=it.value().data())return false;
 auto *rt=d->room.roomRuntime();return d->ready&&d->target&&rt&&!rt->isClosing()
  &&LuaRuntime::current()==&rt->lua()&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->mapGeneration==d->room.roomState()->aiRound56PhysicalIdMapGeneration()
  &&Weapon::rangeRevisionCurrent(d->weaponRevision)
  &&rt->definitions().hasFactorySourceOrigin(d->room,d->L)
  &&hasAIRound56GlobalFactoryMapOrigin(Sanguosha)
  &&hasAIRound56ProjectionLineage(d->room,d->L)&&hasAIRound56CloneInputLineage(d->room,d->L)
  &&hasAIRound56AsyncUniverse(d->room,d->L)
  &&(!d->cardRulesRoot()||hasAIRound56PatternsLineage(d->room,d->L));
}
bool acceptAIRound56PlayerCorrectionGetterEntry(Room&r,lua_State*L,int n) noexcept {
 if(!activeMaxCards||&activeMaxCards->d->room!=&r||activeMaxCards->d->L!=L
  ||n<1||lua_gettop(L)<n+1||!activeMaxCards->accepted())return false;
 const auto i=activeMaxCards->d->entries.constFind(lua_topointer(L,-n-1));
 return i!=activeMaxCards->d->entries.cend()&&i->contains(n);
}
bool preserveAIRound56PlayerCorrectionNativeQuery(Room&r,const Card*card,bool hasCard) noexcept {
 if(!activeMaxCards||&activeMaxCards->d->room!=&r||!activeMaxCards->accepted())return false;
 if(!hasCard)return true;
 if(!activeMaxCards->d->cardRulesRoot())return false;
 const auto it=activeMaxCards->d->policyCards.constFind(card);
 return it!=activeMaxCards->d->policyCards.cend()&&it.value().data()==card;
}
AIRound56CardRulesCloneBirth::AIRound56CardRulesCloneBirth(const Engine *engine,const Card *card,const QString &name) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||!scope->d->cardRulesRoot()||!card
  ||engine!=Sanguosha||!scope->accepted()||!scope->d->literalFactories.contains(name))return;
 try {
  // This constructor can only be called by Engine itself, immediately after
  // original clone completion. No current ID/name equality supplies authority.
  if(!aiRound56OrdinaryCardPolicyType(card)){
   retireAIRound56Projection(scope->d->room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIRound56Projection(scope->d->room,scope->d->L);}
}

AIRound56PlayerSlashBirth::AIRound56PlayerSlashBirth(Room &room,const Player *owner,const Card *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIRound56MaxCardsScope::OwnerSlashWithoutCrossbow
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Issued before the original source copies getEquips(0). The whole roster's
  // original empty-public-card writer histories exclude material recursion.
  // The private constructor is called only at the intrinsic new Slash site.
  if(!aiRound56OrdinaryCardPolicyType(card)){
   retireAIRound56Projection(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIRound56Projection(room,scope->d->L);}
}

AIRound56OwnerAnalepticBirth::AIRound56OwnerAnalepticBirth(Room &room,const ServerPlayer *owner,const Analeptic *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIRound56MaxCardsScope::OwnerAnaleptic
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Only the original owner's constant native stack constructor issues this.
  // Keep its original function-long lifetime; the getter grant ends sooner.
  if(typeid(*card)!=typeid(Analeptic)){
   retireAIRound56Projection(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIRound56Projection(room,scope->d->L);}
}

bool AIRound56MaxCardsScope::prepareLiteralFactory(const QString &name) noexcept {
 if(activeMaxCards==this&&d->root==FixedOwnerEvaluations&&accepted()
  &&d->literalFactories.contains(name))return true;
 // Complete finite recipes were proved before selecting any DTO key. A key
 // outside them retires BEFORE constructor dispatch, preserving legacy behavior.
 retireAIRound56Projection(d->room,d->L);return false;
}
AIRound56AnalepticNullBirth::AIRound56AnalepticNullBirth(Room &room,const Player *owner,const Card *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIRound56MaxCardsScope::FixedOwnerEvaluations
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Private source callsite is the original IsAvailable(nullptr) new expression.
  if(typeid(*card)!=typeid(Analeptic)){
   retireAIRound56Projection(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIRound56Projection(room,scope->d->L);}
}

bool inAIRound56ConnectionQueryScope(const Room &room,lua_State *L) noexcept {
 return activeMaxCards&&activeMaxCards->d->root==AIRound56MaxCardsScope::ConnectionQuery
  &&&activeMaxCards->d->room==&room&&activeMaxCards->d->L==L&&activeMaxCards->accepted();
}

AIRound56ZunweiSkillCloneBirth::AIRound56ZunweiSkillCloneBirth(const Engine *engine,const Card *card,const QString &name) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIRound56MaxCardsScope::ZunweiSeed
  ||engine!=Sanguosha||!card||name!=QStringLiteral("ZunweiCard")||!scope->accepted())return;
 try {
  // The Engine's actual completed native allocation issues this receipt.
  // A type/name match is only a dispatch check, never an adoption API.
  if(typeid(*card)!=typeid(ZunweiCard)) {
   retireAIRound56Projection(scope->d->room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIRound56Projection(scope->d->room,scope->d->L);}
}
