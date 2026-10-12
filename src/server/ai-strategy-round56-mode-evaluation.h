#ifndef AI_STRATEGY_ROUND56_MODE_EVALUATION_H
#define AI_STRATEGY_ROUND56_MODE_EVALUATION_H
#include <QString>
#include "lua-runtime.h"
#include <memory>
class Room;class AIRound56BootstrapSourceProof;struct lua_State;
// Native namespace birth only: registers empty-table observations, no compile.
bool beginAIRound56ModeEvaluationLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
// Original post-Binding/native-seal pre-config boundary only; once, no reseed.
bool prepareAIRound56ModeEvaluationSources(Room&,lua_State*) noexcept;
// Called at the original bootstrap completion before consuming its token.
// The issuer additionally proves no mode evaluate/prepare/commit call during
// bootstrap, and that only pinned register/install functions can reach captures.
bool completeAIRound56ModeEvaluationLineage(Room&,lua_State*,const AIRound56BootstrapSourceProof&) noexcept;
bool hasAIRound56ModeEvaluationLineage(const Room&,lua_State*) noexcept;
bool proveAIRound56ModeEvaluationUnion(Room&,lua_State*,QString*why=nullptr) noexcept;
void retireAIRound56ModeEvaluationLineage(Room&,lua_State*) noexcept;
void clearAIRound56ModeEvaluationLineage(Room&) noexcept;
// Lexical native evaluateModePolicy or ordinary queryStandardMode, BEFORE registry guard
// and pushAIWorldView allocations. publicMode is AIWorldView.modeId. The fixed
// producer builds a fresh plain primitive graph: world,bool for evaluation;
// world,target-string for ordinary objective prepare; world,nil/nonnegative
// integer for the independent Activate relations refresh; then its actual token.
class AIRound56ModeEvaluationScope final {
public:
 enum Entry { Evaluation, StandardObjective, StandardRelations };
 AIRound56ModeEvaluationScope(Room&,lua_State*,const QString&publicMode,Entry=Evaluation);
 // Original prepare return only; records actual born result/token identities.
 // Refusal retires optional proof without changing native schema/error behavior.
 void preparedObjective(int resultIndex,int tokenIndex) noexcept;
 ~AIRound56ModeEvaluationScope();bool accepted()const noexcept;
 AIRound56ModeEvaluationScope(const AIRound56ModeEvaluationScope&)=delete;
 AIRound56ModeEvaluationScope&operator=(const AIRound56ModeEvaluationScope&)=delete;
private:
 friend class AIRound56ModeWorldSource;
 friend bool acceptAIRound56ModeEvaluationEntry(Room&,lua_State*,int) noexcept;
 struct Data;std::unique_ptr<Data>d;
};
// Lexical evaluateModePolicy/queryStandardMode pushAIWorldView call only. This
// native producer scope ends before protectedCall; frame/depth must stay exact.
class AIRound56ModeWorldSource final {
public:
 AIRound56ModeWorldSource(Room&,lua_State*);~AIRound56ModeWorldSource();
 AIRound56ModeWorldSource(const AIRound56ModeWorldSource&)=delete;
 AIRound56ModeWorldSource&operator=(const AIRound56ModeWorldSource&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
bool acceptAIRound56ModeEvaluationEntry(Room&,lua_State*,int argumentCount) noexcept;
#endif
