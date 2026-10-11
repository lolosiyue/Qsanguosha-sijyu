#ifndef AI_STRATEGY_ROUND48_RESPONSE_H
#define AI_STRATEGY_ROUND48_RESPONSE_H
#include <QJsonObject>
struct AIRequest;
class ServerPlayer;
class Card;
// Fixed ol_sp Choulie source constructor only. Neither function issues payment.
bool aiRound48ChoulieEvaluation(const AIRequest &, const ServerPlayer *, const QJsonObject &);
bool configureAIRound48ChoulieEvaluation(Card &);
#endif
