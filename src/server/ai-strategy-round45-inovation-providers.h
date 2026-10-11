#ifndef AI_STRATEGY_ROUND45_INOVATION_PROVIDERS_H
#define AI_STRATEGY_ROUND45_INOVATION_PROVIDERS_H
#include <QString>
class QJsonObject; struct AIRequest;
class Room; class ServerPlayer; class AI; struct AIWorldView; struct lua_State;
// Installed/captured only at native bootstrap and successful real CloneAI.
void initializeAIRound45InovationProviders(Room&);
void captureAIRound45InovationProviders(Room&,ServerPlayer*,AI*);
void projectAIRound45InovationProviders(AIWorldView&,Room&,const ServerPlayer*);
// Generated SWIG implementation: native pointer conversion only; no invocation.
bool aiRound45InovationPlayerMatches(lua_State*,int,const ServerPlayer*);
bool aiRound45InovationRoomMatches(lua_State*,int,const Room*);
bool aiRound45InovationProviderKnown(Room&,const ServerPlayer*,const QString&);
bool aiRound45InovationPhaseConstruction(const AIRequest&,const ServerPlayer*,const QJsonObject&);
// Success pushes one exact native-owner self table, no effects or invocation.
bool pushAIRound45ActualAISelf(Room&,const ServerPlayer*,lua_State*);
#endif
