#ifndef AI_STRATEGY_ROUND33_CHOICE_H
#define AI_STRATEGY_ROUND33_CHOICE_H

#include <QJsonArray>
#include <QString>
#include <memory>

class Card;
class Room;
class ServerPlayer;
struct AIRequest;

// Fixed smart-ai.lua use_revises: V1 policy objects only, never V2 receipts.
bool aiRound33ChoiceEvaluationSource(const QString &sourceKey);
std::unique_ptr<Card> aiRound33ChoiceEvaluation(const AIRequest &request, Room &room,
    const ServerPlayer *viewer, const QString &sourceKey, const QString &className,
    const QJsonArray &costs, QString *error);

#endif
