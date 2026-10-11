#ifndef AI_STRATEGY_RETRIAL_CONTEXT_H
#define AI_STRATEGY_RETRIAL_CONTEXT_H
#include <QJsonObject>
#include <QVariant>
#include <QList>
#include <functional>
struct AIRequest;
struct AIWorldView;
struct AIEventView;
struct CardsMoveStruct;
class Room;
class ServerPlayer;
class Card;
// Fixed package questions only. Reads no arbitrary tag selected by Lua and
// never reveals card faces beyond the host's public-card serialization policy.
void projectAIStrategyRetrialContext(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
// ChoiceMade is a private request-result observation, not a public broadcast.
void projectAIStrategyRetrialEventContext(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &, const std::function<QJsonObject(const Card *)> &publicCard);
// Called by Room only after the actual notification, with recipient-redacted
// moves. A loss starts the two-phase PREVIEW; only matching gain commits faces.
void recordAIStrategyRetrialPreview(Room &, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &recipientMoves, bool isLostPhase,
    const std::function<QJsonObject(const Card *)> &publicCard);
// Room notification hook overload; serializes intrinsic disclosed face only.
void recordAIStrategyRetrialPreview(Room &, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &recipientMoves, bool isLostPhase);
// Call for every authoritative card-location mapping change, including hidden
// moves. Invalidates existing knowledge only; never exposes the new location.
void invalidateAIStrategyRetrialPreview(Room &, int cardId);
// Pure current preview; no arbitrary tag or hidden card lookup from Lua.
QJsonObject readAIStrategyRetrialPreview(Room &, ServerPlayer *viewer, const QString &skill);
#endif
