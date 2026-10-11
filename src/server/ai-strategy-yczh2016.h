#ifndef AI_STRATEGY_YCZH2016_H
#define AI_STRATEGY_YCZH2016_H
#include "ai.h"
class Room;
class ServerPlayer;
class Card;
struct SkillContext;

void projectAIStrategyYCZH2016(AIRequest &, Room &, ServerPlayer *viewer);

// Stage only after final acceptance. These hints are invisible until the same
// accepted decision receives its exact public skill-use/player-choice witness.
bool stageAIStrategyPlayerHints(Room &, const AIRequest &, const QJsonArray &, QString *error = nullptr, bool commit = true);
void witnessAIStrategyPlayerHints(Room &, quint64 decisionId, const ServerPlayer *source,
                                  const QString &skill, const QStringList &targets);
// The native card identity stays Room-local. Only this exact accepted product's
// later public CardUsed witness can publish its hints. Unrelated same-skill
// replacements and destroyed/rejected products cannot borrow a pending decision.
// Native admission may explicitly transfer a verified rebuild below.
bool stageAcceptedAIStrategyCardHints(Room &, const AIRequest &, const QJsonArray &,
                                     const Card *acceptedCard, QString *error = nullptr, bool commit = true);
// Only native successful V2 rebuilding may transfer an already accepted exact
// pointer. Both complete provenance refs and the immutable product must match.
void transferAcceptedAIStrategyCardHints(Room &, const Card *acceptedCard,
    const Card *rebuiltCard, const ServerPlayer *source,
    const SkillInstanceRef &activationRef, const SkillInstanceRef &sourceRef);
void witnessActualAIStrategyCardHints(Room &, const Card *actualCard,
                                      const ServerPlayer *source, const QStringList &targets);
// Accepted Jiaozhao PlayerChosen is tied to the live native skill execution and
// original use-card identity. ChoiceMade itself is not a public disclosure.
bool stageAcceptedAIStrategyChoiceHints(Room &, const AIRequest &, const QJsonArray &,
                                       const ServerPlayer *choice, QString *error = nullptr, bool commit = true);
// Call after target interception/cancellation, immediately before the native
// Jiaozhao declaration effect opens AG. No opaque execution ID is exported.
void witnessAIStrategyJiaozhaoDeclaration(Room &, const SkillContext &, const ServerPlayer *target);
// Call only at the original reader point, with this active request's viewer.
// Does not read the donor's unrestricted Player tags or change Room revision.
QJsonObject consumeAIStrategyPlayerHint(Room &, const AIRequest &, ServerPlayer *, const QString &name);
#endif
