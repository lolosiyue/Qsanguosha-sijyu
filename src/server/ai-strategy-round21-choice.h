#ifndef AI_STRATEGY_ROUND21_CHOICE_H
#define AI_STRATEGY_ROUND21_CHOICE_H
#include <QString>
#include <QJsonObject>
#include <functional>
struct AIRequest;
class ServerPlayer;
class Room;
class Card;
// Fixed-source constructor vocabulary only. -1: absent; bit0: ordered payment
// materials; bit1: declared userString. Neither this mask nor a readonly clone
// grants action authority: current instance, selection and factory stay native.
int aiRound21ChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype);
// Source sgs.Analeptic_IsAvailable(player) is the static predicate with a null
// card, not Analeptic::isAvailable (which additionally checks BasicCard rules).
// Caller supplies the active Room/Engine context and a live owner viewer.
bool aiRound21ChoiceAnalepticAvailable(const ServerPlayer *viewer);
void projectAIRound21ChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard);
#endif
