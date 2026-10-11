#ifndef AI_STRATEGY_ROUND26_PLANNING_H
#define AI_STRATEGY_ROUND26_PLANNING_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest;struct CardUseStruct;class Room;class ServerPlayer;class Card;
void projectAIRound26PlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,
 const std::function<QJsonObject(const Card *)> &);
// Actual public-use event witness; never a Lua-provided observation.
void clearAIRound26PlanningPublicUses(Room &);
void recordAIRound26PlanningPublicUse(Room &,int triggerEvent,const QVariant &);
// A current native use, including its actor and ordered targets, must match a
// live public event. A disclosed card face alone is not proof of a CardUse.
bool aiRound26PlanningPublicUseWitness(const AIRequest &,Room &,const ServerPlayer *,const CardUseStruct &);
// An observer can read the card face only after the public use stage; this
// grants neither a foreign actor's private rules nor payment authority.
bool aiRound26PlanningPublicCardWitness(const AIRequest &,Room &,const ServerPlayer *,const Card *);
// Borrowed actual publicly disclosed card; never cloned, owned, or payment authority.
const Card *aiRound26PlanningEvaluation(const AIRequest &,Room &,const ServerPlayer *,const QString &sourceKey);
#endif
