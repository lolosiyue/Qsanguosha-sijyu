#ifndef AI_STRATEGY_PROJECTION_H
#define AI_STRATEGY_PROJECTION_H
#include "ai.h"
class Room;
class ServerPlayer;
QJsonObject projectAIOwnerFilteredCard(Room &, ServerPlayer *, int id, const JudgeStruct *judge = nullptr, int hypotheticalPlace = int(Player::PlaceJudge),
                                       const AICardView *disclosed = nullptr);
void recordAIStrategyNotifiedPile(Room &, ServerPlayer *, const QString &pile, const QJsonObject &locations, bool active,
                                   const QList<int> &orderedIds = {}, bool orderKnown = false);
QJsonObject readAIStrategyNotifiedPile(Room &, ServerPlayer *, const QString &pile);
QMap<QString, bool> projectAICanUseTargets(Room &, ServerPlayer *, const Card *);
void projectAIStrategyWorld(AIWorldView &, Room &, ServerPlayer *, bool eventOnly);
// Owner hypothetical filtering of descriptors the caller has already disclosed.
// Candidate IDs alone never authorize this provider.
void projectAIStrategyDisclosedFilters(AIWorldView &, Room &, ServerPlayer *,
                                       const QList<AICardView> &disclosed);
void projectAIStrategyFanProbe(AIWorldView &, Room &, ServerPlayer *, const Card *source);
void projectAIStrategyRequest(AIRequest &, Room &, ServerPlayer *);
void projectAIStrategyEvent(AIEventView &, const AIWorldView &, Room &, ServerPlayer *observer,
                           ServerPlayer *target, const QVariant &data);
bool resolveAIStrategyRandomChoice(Room &, ServerPlayer *, const AIRequest &, AIResult &);
AICardCandidateView projectAIStrategyCandidate(Room &, ServerPlayer *, const Card *, Card::HandlingMethod,
                                              AIRequest::DecisionKind, const QString &, int &);
#endif
