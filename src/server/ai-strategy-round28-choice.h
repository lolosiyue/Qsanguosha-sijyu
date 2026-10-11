#ifndef AI_STRATEGY_ROUND28_CHOICE_H
#define AI_STRATEGY_ROUND28_CHOICE_H
#include <QJsonObject>
#include <QVariant>
#include <memory>
struct AIRequest; struct CardUseStruct; class Room; class ServerPlayer; class Card;
bool aiRound28ChoicePlanningQuestion(const AIRequest &, Room &, const ServerPlayer *);
QJsonObject aiRound28ChoicePlanningContext(const AIRequest &, Room &, const ServerPlayer *);
bool aiRound28NiwoResponseQuestion(const AIRequest &, Room &, const ServerPlayer *);
// Pure constructor of the exact reached owner ChangeSlash hypothesis. No payment.
std::unique_ptr<Card> createAIRound28LihuoHypothesis(const AIRequest &, Room &, ServerPlayer *, const QVariant &);
void projectAIRound28LihuoQuestion(AIRequest &, Room &, ServerPlayer *, const QVariant &);
std::unique_ptr<Card> createAIRound28CapturedLihuoHypothesis(const AIRequest &, Room &, ServerPlayer *, CardUseStruct *, QString *error = nullptr);
void clearAIRound28ChoiceState(Room &);
#endif
