#ifndef AI_STRATEGY_RESPONSE_OBSERVER_H
#define AI_STRATEGY_RESPONSE_OBSERVER_H
#include <QJsonObject>
#include <QList>
#include <QString>
#include <memory>
class Card;
class Room;
class ServerPlayer;
struct AIWorldView;
struct SkillContext;
// Surround the actual V2 effect callback, including unaudited nested callbacks.
// Only commit after a normal callback return; destruction never infers success.
class AIResponseObservationScope {
public:
    AIResponseObservationScope(Room &, const QString &skill, ServerPlayer *actor,
                               int originalEvent, const SkillContext &);
    ~AIResponseObservationScope();
    void commit();
    AIResponseObservationScope(const AIResponseObservationScope &) = delete;
    AIResponseObservationScope &operator=(const AIResponseObservationScope &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
};
// Call at successful native notification boundaries, with its actual audience.
// Empty audience denotes the actual all-player broadcast, not an unknown list.
void recordAIResponseObservedMark(Room &, ServerPlayer *, const QString &, int,
                                 const QList<ServerPlayer *> &audience = {});
void recordAIResponseObservedPlayerFlag(Room &, ServerPlayer *, const QString &,
                                       const QList<ServerPlayer *> &audience = {});
// Only a normally observed fresh-game GameReady may establish initial negatives.
void initializeAIResponseObservations(Room &, ServerPlayer *observer, bool freshGame);
void projectAIResponseObservations(AIWorldView &, Room &, ServerPlayer *observer);
// Caller already holds a lawful card descriptor; this grants no card face/ID.
// Unknown returns an empty object. The opaque key is never an effective card ID.
QJsonObject readAIResponseCardObservation(Room &, ServerPlayer *observer, const Card *);
#endif
