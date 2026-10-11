#ifndef AI_STRATEGY_ROUND25_DISTANCE_H
#define AI_STRATEGY_ROUND25_DISTANCE_H
#include "engine.h"
class Room;
class ServerPlayer;
struct AIRequest;
// Finite OLQimou hypothesis; never mutates marks, dispatches events or grants payment.
class AIRound25OwnerDistanceScope final {
public:
    AIRound25OwnerDistanceScope(const ServerPlayer *owner, int delta);
    ~AIRound25OwnerDistanceScope();
    static bool active(const ServerPlayer *owner = nullptr);
    AIRound25OwnerDistanceScope(const AIRound25OwnerDistanceScope &) = delete;
    AIRound25OwnerDistanceScope &operator=(const AIRound25OwnerDistanceScope &) = delete;
private:
    const ServerPlayer *m_owner;
    AIMarkQueryScope m_mark;
    AIRound25OwnerDistanceScope *m_previous;
    static thread_local AIRound25OwnerDistanceScope *s_current;
};
bool validateAIRound25OwnerDistance(Room &, ServerPlayer *, const AIRequest &, const QString &, int);
QJsonObject projectAIRound25OwnerDistance(Room &, ServerPlayer *, const QString &, int);
#endif
