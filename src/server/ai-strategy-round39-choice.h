#ifndef AI_STRATEGY_ROUND39_CHOICE_H
#define AI_STRATEGY_ROUND39_CHOICE_H
#include <QJsonObject>
#include <QString>
#include <memory>
class Card;class Room;class ServerPlayer;struct AIRequest;
// Fixed olddiy-ai.lua819: same disclosed physical policy identity, no receipt.
std::unique_ptr<Card> aiRound39GuicaiEvaluation(const AIRequest &,Room &,ServerPlayer *,int,
    QJsonObject *disclosedFace,QString *error);
#endif
