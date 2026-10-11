#ifndef AI_STRATEGY_ROUND47_XUFA_EVENT_SCOPE_H
#define AI_STRATEGY_ROUND47_XUFA_EVENT_SCOPE_H
#include <memory>
class Room;class ServerPlayer;class QVariant;struct lua_State;
// Only instantiated at the compiled recordAiEvent -> actual filterEvent pair.
// The row is not readable while pending; destruction never restores a row.
class AIRound47XufaEventScope {
public:
 AIRound47XufaEventScope(Room&,int,ServerPlayer*,const QVariant&);
 ~AIRound47XufaEventScope();
 AIRound47XufaEventScope(const AIRound47XufaEventScope&)=delete;
 AIRound47XufaEventScope&operator=(const AIRound47XufaEventScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound47XufaEventAccess;
};
bool pendingAIRound47XufaEvent(Room&);
bool prepareAIRound47XufaFilterCallback(Room&,const char*method);
void completeAIRound47XufaEventProof(Room&,lua_State*,int);
#endif
