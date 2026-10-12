#ifndef AI_STRATEGY_ROUND56_MAXCARDS_H
#define AI_STRATEGY_ROUND56_MAXCARDS_H
#include <memory>
#include <functional>
struct JudgeStruct;struct AICardView;class QVariant;
struct AIRequest;class QJsonObject;struct AIWorldView;class Analeptic;class Room;class Player;class ServerPlayer;class Card;class Engine;class QString;struct lua_State;
// Separate exact MaxCards and AttackRange lexical roots share their complete
// transitive correction dependency preflight. Neither grants full Card policy
// or full Activate. Each begins before its root's private applicability.
class AIRound56MaxCardsScope final {
public:
 AIRound56MaxCardsScope(Room &,ServerPlayer *,lua_State *);
 ~AIRound56MaxCardsScope();
 bool accepted() const noexcept;
 AIRound56MaxCardsScope(const AIRound56MaxCardsScope &)=delete;
 AIRound56MaxCardsScope &operator=(const AIRound56MaxCardsScope &)=delete;
private:
 enum Root { MaxCardsFixedExtra, AttackRangeFixedExtra, OwnerCardRules, PublicDistances, OwnerSlashWithoutCrossbow, OwnerAnaleptic, FixedSlashProbes, FixedOwnerEvaluations, OwnerPhysicalCandidates, ConnectionQuery, PlayerSkillValidity, PublicSkillQueries, OwnerCardState, WorldSkillQueries, OwnerJilei, OwnerPindian, PublicAttackRangeMatrix, OwnerEquipmentQueries, ZunweiSeed };
 AIRound56MaxCardsScope(Room &,ServerPlayer *,lua_State *,Root);
 bool prepareLiteralFactory(const QString &) noexcept;
 friend class AIRound56AttackRangeScope;
 friend class AIRound56OwnerCardRulesScope;
 friend class AIRound56DistanceScope;
 friend class AIRound56CardRulesCloneBirth;
 friend class AIRound56OwnerSlashScope;
 friend class AIRound56PlayerSlashBirth;
 friend class AIRound56OwnerAnalepticScope;
 friend class AIRound56OwnerAnalepticBirth;
 friend class AIRound56FixedSlashProbesScope;
 friend class AIRound56OwnerEvaluationsScope;
 friend class AIRound56OwnerCandidatesScope;
 friend class AIRound56ConnectionQueryScope;
 friend class AIRound56SkillValidityScope;
 friend class AIRound56PublicSkillQueriesScope;
 friend class AIRound56OwnerCardStateScope;
 friend class AIRound56WorldSkillQueriesScope;
 friend class AIRound56OwnerJileiScope;
 friend class AIRound56OwnerPindianScope;
 friend class AIRound56PublicAttackRangeMatrixScope;
 friend class AIRound56OwnerEquipmentQueriesScope;
 friend class AIRound56ZunweiSeedScope;
 friend class AIRound56ZunweiSkillCloneBirth;
 friend bool inAIRound56ConnectionQueryScope(const Room &,lua_State *) noexcept;
 friend class AIRound56AnalepticNullBirth;
 struct Data;std::unique_ptr<Data>d;
 friend bool acceptAIRound56PlayerCorrectionGetterEntry(Room &,lua_State *,int) noexcept;
 friend bool preserveAIRound56PlayerCorrectionNativeQuery(Room &,const Card *,bool) noexcept;
};
bool acceptAIRound56PlayerCorrectionGetterEntry(Room &,lua_State *,int) noexcept;
bool preserveAIRound56PlayerCorrectionNativeQuery(Room &,const Card *,bool hasCard) noexcept;
// Exact native validity queries in UI and action/conversion projection, with
// complete public metadata and transitive correction graph. Each query ends
// before usage/activation providers, publication, Card policy or its consumer.
class AIRound56SkillValidityScope final {
 friend class PlayerUIStateBuilder;
 friend class AiDecisionCoordinator;
 AIRound56SkillValidityScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::PlayerSkillValidity){}
 AIRound56MaxCardsScope scope;
};
// Original public-board armor blocking and visible Skill metadata loops only.
// Complete definition+correction graph; no Card policy or full board grant.
class AIRound56PublicSkillQueriesScope final {
 friend class AiDecisionCoordinator;
 AIRound56PublicSkillQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::PublicSkillQueries){}
 AIRound56MaxCardsScope scope;
};
// Original owner physical-state projection only. The full correction graph and
// canonical physical custody precede each original physical-state loop; no
// factory product is issued. Each consumer has its own lexical lifetime.
class AIRound56OwnerCardStateScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIRound56OwnerCardStateScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerCardState){}
 AIRound56MaxCardsScope scope;
};
// Original world Skill presence queries only; independent complete metadata
// and correction preflight, ending before providers and conditional bodies.
class AIRound56WorldSkillQueriesScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 friend void projectAIStrategyAnimecardCatalog(AIWorldView &,Room &,const ServerPlayer *);
 friend void projectAIStrategyYjcm2012(AIWorldView &,Room &,ServerPlayer *);
 friend QJsonObject projectAIOwnerFilteredCard(Room &,ServerPlayer *,int,const JudgeStruct *,int,const AICardView *);
 friend void projectAIStrategyResponseContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIRound22ChoiceContext(AIRequest &,Room &,ServerPlayer *,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIRound26ChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIRound27ChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIRound45Newgenerals(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIRound45PlanningFacts(AIWorldView &,Room &,const ServerPlayer *);
 friend void projectAIRound26PlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend bool aiRound28NiwoResponseQuestion(const AIRequest &,Room &,const ServerPlayer *);
 friend void projectAIRound24ChoiceContext(AIRequest &,Room &,ServerPlayer *);
 friend void projectAIRound51Jijiao(AIRequest &,Room &,ServerPlayer *);
 friend void projectAIRound29ExtraWorld(AIWorldView &,Room &,ServerPlayer *);
 friend bool aiRound28ChoicePlanningQuestion(const AIRequest &,Room &,const ServerPlayer *);
 friend void projectAIRound28PlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &,const std::function<QJsonObject(const Card *,int,const QString &)> &);
 AIRound56WorldSkillQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::WorldSkillQueries){}
 AIRound56MaxCardsScope scope;
};
// Only the existing issued Zunwei seed evaluator creates this lexical root.
// Complete CardRules/correction/factory proof precedes clone and card queries.
class AIRound56ZunweiSeedScope final {
 friend class AiDecisionCoordinator;
 AIRound56ZunweiSeedScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::ZunweiSeed){}
 AIRound56MaxCardsScope scope;
};
// Original SP Jilei literal ExNihilo clone and isCardLimited(hand=true).
// Separate full CardRules dependency/custody root; no Skill query grants it.
class AIRound56OwnerJileiScope final {
public:
 AIRound56OwnerJileiScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerJilei){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
// Original four owner canPindian projections; full public Pindian prohibit
// type family precedes any target/private-hand branch. No Card policy grant.
class AIRound56OwnerPindianScope final {
public:
 AIRound56OwnerPindianScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerPindian){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
// Original Yjcm2015 public inMyAttackRange matrix only. The full public
// metadata and correction family precedes all applicability. No Card policy.
class AIRound56PublicAttackRangeMatrixScope final {
 friend void projectAIYjcm2015(AIWorldView &,Room &,const ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
 AIRound56PublicAttackRangeMatrixScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::PublicAttackRangeMatrix){}
 AIRound56MaxCardsScope scope;
};
// Original owner hasDefensiveHorse and view-as-equipment declaration loop.
// Complete metadata/correction/provider/recipe family before applicability;
// each call site has an independent lifetime and no Card policy custody.
class AIRound56OwnerEquipmentQueriesScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIRound56OwnerEquipmentQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerEquipmentQueries){}
 AIRound56MaxCardsScope scope;
};
// A distinct root lifetime: never prolongs the preceding MaxCards scope.
class AIRound56AttackRangeScope final {
public:
 AIRound56AttackRangeScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::AttackRangeFixedExtra){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
class AIRound56DistanceScope final {
public:
 AIRound56DistanceScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::PublicDistances){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
class AIRound56OwnerCardRulesScope final {
public:
 AIRound56OwnerCardRulesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerCardRules){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
class AIRound56OwnerSlashScope final {
public:
 AIRound56OwnerSlashScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerSlashWithoutCrossbow){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
// Only Player's original no-argument Slash allocation can issue this receipt.
class AIRound56PlayerSlashBirth final {
 friend class Player;
 AIRound56PlayerSlashBirth(Room &,const Player *,const Card *) noexcept;
};
class AIRound56OwnerAnalepticScope final {
public:
 AIRound56OwnerAnalepticScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerAnaleptic){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
class AIRound56OwnerAnalepticBirth final {
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIRound56OwnerAnalepticBirth(Room &,const ServerPlayer *,const Analeptic *) noexcept;
};
class AIRound56FixedSlashProbesScope final {
public:
 AIRound56FixedSlashProbesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::FixedSlashProbes){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
class AIRound56OwnerEvaluationsScope final {
public:
 AIRound56OwnerEvaluationsScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::FixedOwnerEvaluations){}
 bool accepted()const noexcept{return scope.accepted();}
 // Validates the input BEFORE the factory runs; never certifies its result.
 bool prepareFactory(const QString &name)noexcept{return scope.prepareLiteralFactory(name);}
private:
 AIRound56MaxCardsScope scope;
};
// Only the two original hand/equip candidate loops use this root. No factory
// product receives a policy receipt here; custody comes from the physical map.
class AIRound56OwnerCandidatesScope final {
public:
 AIRound56OwnerCandidatesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::OwnerPhysicalCandidates){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
// Created only by the actual source-cache bridge, before its requested-player
// selection or cache access. This root grants no Card policy or factory custody.
class AIRound56ConnectionQueryScope final {
 friend QJsonObject queryAIRound56ConnectionCache(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIRound56ConnectionQueryScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIRound56MaxCardsScope::ConnectionQuery){}
public:
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIRound56MaxCardsScope scope;
};
bool inAIRound56ConnectionQueryScope(const Room &,lua_State *) noexcept;
class AIRound56AnalepticNullBirth final {
 friend class Analeptic;
 AIRound56AnalepticNullBirth(Room &,const Player *,const Card *) noexcept;
};
// Only the original Engine named factory can issue this product receipt. No
// public function accepts a later pointer/name/value to mint clone authority.
class AIRound56CardRulesCloneBirth final {
 friend class Engine;
 AIRound56CardRulesCloneBirth(const Engine *,const Card *,const QString &) noexcept;
};
// Only the actual native Engine cloneSkillCard return site can issue custody.
class AIRound56ZunweiSkillCloneBirth final {
 friend class Engine;
 AIRound56ZunweiSkillCloneBirth(const Engine *,const Card *,const QString &) noexcept;
};
#endif
