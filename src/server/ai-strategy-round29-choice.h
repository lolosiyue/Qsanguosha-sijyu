#ifndef AI_STRATEGY_ROUND29_CHOICE_H
#define AI_STRATEGY_ROUND29_CHOICE_H
#include <QJsonObject>
class Room; class ServerPlayer; struct AIRequest;
// Call once after all configured donor scripts/Original overrides and before any
// policy invocation. Never recapture on an isolated VM rebuild.
void captureAIRound29ChoicePriorities(Room &);
// Every policy entry, including events, must pass this public viewer boundary.
// Legacy/fallback or uninstrumented execution invalidates all continuity.
void beginAIRound29ChoicePolicy(Room &,const QString &viewer);
void invalidateAIRound29ChoicePriorities(Room &,bool invalidateChoices=true);
QJsonObject queryAIRound29ChoicePriority(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &);
QJsonObject writeAIRound29XiafengFlag(Room &,const AIRequest &,ServerPlayer *,int cardId,bool enabled);
#endif
