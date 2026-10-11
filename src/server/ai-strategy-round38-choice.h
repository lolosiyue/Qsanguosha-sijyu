#ifndef AI_STRATEGY_ROUND38_CHOICE_H
#define AI_STRATEGY_ROUND38_CHOICE_H
#include <QJsonObject>
#include <QString>
#include <memory>
class Card;class Room;class ServerPlayer;struct AIRequest;
// Fixed htms-ai.lua6346: same disclosed physical policy identity, no receipt.
std::unique_ptr<Card> aiRound38ZhongerhxEvaluation(const AIRequest &,Room &,ServerPlayer *,int,
    QJsonObject *disclosedFace,QString *error);
#endif
