#ifndef AI_STRATEGY_ROUND30_ORIGINAL_H
#define AI_STRATEGY_ROUND30_ORIGINAL_H
#include <QJsonObject>
#include <functional>
struct AIRequest; struct JudgeStruct;
class Room; class ServerPlayer; class Card;
// Caller supplies only its authorized current JudgeStruct. Readonly printed
// candidates are limited to faces already disclosed in this request.
void projectAIRound30OriginalJudge(const AIRequest &, Room &, ServerPlayer *,
    const JudgeStruct *, QJsonObject &,
    const std::function<QJsonObject(const Card *)> &publicCard);
#endif
