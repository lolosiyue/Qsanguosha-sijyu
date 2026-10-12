#ifndef AI_STRATEGY_ROUND47_XIEDOU_H
#define AI_STRATEGY_ROUND47_XIEDOU_H
class AIRequest;
class ServerPlayer;
class QJsonObject;
bool aiRound47XiedouConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
class Card;
class Room;
// Sole production issuer: the real fresh Duel constructor in Xiedou.effectOnTarget.
// No isolated binding, face lookup, mutation authority or gameplay-state change.
void recordAIRound47FreshXiedouDuel(Room &, const Card *);
// Sole issuer: current Xufa valuePlan's fresh Card::Parse product, before native
// admission callbacks. Shares the existing literal-flag mutation epoch only.
void recordAIRound53FreshXufaValueDuel(Room &, const Card *);
void invalidateAIRound47XiedouFlags(const Card *) noexcept;
// Only merge into an already lawfully disclosed actual-card descriptor.
QJsonObject readAIRound47XiedouFlags(Room &, ServerPlayer *, const Card *);
#endif
