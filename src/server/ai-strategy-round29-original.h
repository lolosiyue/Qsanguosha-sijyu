#ifndef AI_STRATEGY_ROUND29_ORIGINAL_H
#define AI_STRATEGY_ROUND29_ORIGINAL_H
#include <QJsonObject>
#include <QList>
#include <QString>
struct AIRequest;
struct AIWorldView;
struct CardsMoveStruct;
class Room;
class ServerPlayer;
class Card;
int aiRound29ExtraPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
// Call only after the real recipient-redacted notification was sent.
void recordAIRound29ShicaiPreview(Room &, ServerPlayer *, const QList<CardsMoveStruct> &, bool isLostPhase);
void invalidateAIRound29ShicaiPreview(Room &, int cardId);
void clearAIRound29ShicaiPreview(Room &);
void projectAIRound29ExtraWorld(AIWorldView &, Room &, ServerPlayer *);
// No caller-supplied card ID; returns only the same active, witnessed top card.
// It grants readonly evaluation, never permission to select/pay the preview.
const Card *aiRound29ShicaiPreview(const AIRequest &, Room &, ServerPlayer *);
#endif
