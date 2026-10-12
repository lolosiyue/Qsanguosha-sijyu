#ifndef AI_STRATEGY_ROUND56_BOOTSTRAP_SOURCE_H
#define AI_STRATEGY_ROUND56_BOOTSTRAP_SOURCE_H
#include <QJsonObject>
class Room;
class RoomDefinitionRegistry;
class AIRound56BootstrapSourceProof;
struct lua_State;
// Original RoomRuntime::initialize boundaries only. These functions never
// replace authored execution or make a later snapshot into source authority.
bool installAIRound56BootstrapSourceBoundary(Room &) noexcept;
bool prepareAIRound56BootstrapSourceBoundary(Room &,lua_State *) noexcept;
bool completeAIRound56BootstrapSourceBoundary(Room &,lua_State *) noexcept;
void retireAIRound56BootstrapSourceBoundary(Room &) noexcept;
void clearAIRound56BootstrapSourceBoundary(Room &) noexcept;
// Exact original bootstrap scope only; no post-bootstrap async seed or grant.
bool acceptAIRound56BootstrapAsyncEntry(lua_State *,int asyncKind) noexcept;
bool acceptAIRound56BootstrapProtectedEntry(Room &,lua_State *,int arguments) noexcept;
// Diagnostic fixed labels/public source identity only. No authority capability
// or account/cache/mind/player value is returned.
QJsonObject inspectAIRound56BootstrapSourceBoundary(const Room &);
// Original issuer continuity for recorded native metadata transitions. During
// Completing the one-shot source token is issued but async is not yet seeded;
// after Completed the ordinary Clone/async lineage must still be live.
bool hasAIRound56BootstrapMetadataContinuity(Room&,lua_State*) noexcept;
// Original native definition producer boundary; registry writer observer only.
bool isAIRound56OriginalDefinitionProducer(Room &,const RoomDefinitionRegistry *) noexcept;
bool completeAIRound56FactoryMapOrigin(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
#endif
