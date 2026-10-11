#ifndef AI_STRATEGY_ROUND37_RESPONSE_INIT_H
#define AI_STRATEGY_ROUND37_RESPONSE_INIT_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;class AI;struct AIRequest;struct lua_State;
class AIRound37OriginalConstructor final {
public:
 AIRound37OriginalConstructor(Room&,ServerPlayer*,lua_State*);
 ~AIRound37OriginalConstructor();
 void complete(AI*);
 AIRound37OriginalConstructor(const AIRound37OriginalConstructor&)=delete;
 AIRound37OriginalConstructor&operator=(const AIRound37OriginalConstructor&)=delete;
private:struct Data;Data*d;
};
void initializeAIRound37OriginalTurnUse(Room&);
void initializeAIRound41ChoiceTables(Room&,lua_State*,bool isolated);
void captureAIRound41ChoiceProxies(Room&,lua_State*);
bool validateAIRound41ChoiceProxies(Room&);
void invalidateAIRound37OriginalTurnUse(Room&);
QJsonObject queryAIRound37OriginalTurnUse(Room&,const AIRequest&,const ServerPlayer*,const QString&write=QString());
// Exact already-installed native constructor hook, valid only in its live lexical capture.
bool matchesAIRound56OriginalConstructorHook(Room&,lua_State*) noexcept;
#endif

// Exact capture-owned room lookup for async hook inspection.
bool matchesAIRound56OriginalConstructorHook(lua_State*) noexcept;
