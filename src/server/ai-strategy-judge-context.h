#ifndef AI_STRATEGY_JUDGE_CONTEXT_H
#define AI_STRATEGY_JUDGE_CONTEXT_H
#include <QJsonObject>
#include <functional>
struct AIRequest; struct JudgeStruct;
class Room; class ServerPlayer; class Card;
// Existing normal judge projection, shared by audited named current questions.
// Caller must prove this judge belongs to the current authorized inquiry.
QJsonObject projectAIStrategyJudgeContext(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge,
    const std::function<QJsonObject(const Card *)> &publicCard);
#endif
