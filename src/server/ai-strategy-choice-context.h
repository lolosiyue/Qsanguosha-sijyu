#ifndef AI_STRATEGY_CHOICE_CONTEXT_H
#define AI_STRATEGY_CHOICE_CONTEXT_H
#include <QJsonObject>
#include <QVariant>
#include <QList>
#include <functional>
struct AIRequest;
class Room;
class ServerPlayer;
class Card;
// Called only after an actual fillAG notification to this recipient. clearAG
// does not revoke remembered face knowledge; every real mapping write does.
void recordAIStrategyChoiceAGDisclosure(Room &, ServerPlayer *, const QList<int> &);
void invalidateAIStrategyChoiceAGDisclosure(Room &, int cardId);
QJsonObject readAIStrategyChoiceAGDisclosure(Room &, ServerPlayer *, int cardId);
void projectAIStrategyChoiceContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);
#endif
