#ifndef AI_STRATEGY_ROUND45_POXI_EFFECTS_H
#define AI_STRATEGY_ROUND45_POXI_EFFECTS_H
#include <QString>
#include <QStringList>
#include <functional>
class Room; class Skill; struct lua_State;
struct AIRound45NaturalFilterProof;
// Native-only effect proof services. No service may execute the supplied Lua
// closure or use a private branch outcome to decide whether to prove it.
struct AIRound45PoxiEffectProof {
 std::function<bool(lua_State*,int,const QString&,QString*)> luaFunction;
 std::function<bool(const Skill*,const QString&,QString*)> nativeSkill;
 std::function<bool(const QString&,QString*)> dependency;
};
// Exhaustive runtime definition union, independent of ownership, validity,
// holder, correction result, or the actual spjili branch outcome.
bool proveAIRound45PoxiCorrectionUnion(Room&,lua_State*,const AIRound45PoxiEffectProof&,QString*reason=nullptr);
// Proves the reached shouldSuppressIntention callee, preserving every branch.
// methodIndex must be resolved by the shared exact SmartAI instance/class seal.
// namespace index must be the shared captured sgs namespace, not a lookalike.
bool proveAIRound45PoxiSuppressionBindings(Room&,lua_State*,int methodIndex,int namespaceIndex,
 const AIRound45PoxiEffectProof&,QString*reason=nullptr);
// Exact event-specific body obligations. Shared caller must still prove the
// full filterEvent prelude, registries, and every earlier native event root.
// The five-argument entry has no implicit positive effect service.
bool certifyAIRound45PoxiFilterBody(Room&,lua_State*,int selfIndex,const AIRound45NaturalFilterProof&,QString*reason=nullptr);
bool certifyAIRound45PoxiFilterBody(Room&,lua_State*,int selfIndex,const AIRound45NaturalFilterProof&,
 const AIRound45PoxiEffectProof&,QString*reason=nullptr);
#endif
