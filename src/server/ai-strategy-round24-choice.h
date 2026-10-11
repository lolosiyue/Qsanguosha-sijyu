#ifndef AI_STRATEGY_ROUND24_CHOICE_H
#define AI_STRATEGY_ROUND24_CHOICE_H
#include <QJsonObject>
#include <QList>
class Room;
class ServerPlayer;
struct AIRequest;
struct AIWorldView;
struct SkillInstanceRef;
// Immutable load-time metadata only; never executes the original callback.
void captureAIRound24ChoiceRegistry(Room &room);
void projectAIRound24ChoiceRegistry(AIWorldView &world, Room &room);
void projectAIRound24ChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer);
// Readonly finite constructors. No selected cost, quota or action receipt.
bool aiRound24ChoiceConstruction(const AIRequest &request, const ServerPlayer *viewer, const QJsonObject &spec);
bool aiRound24ChumingPublicMaterials(const AIRequest &request, const ServerPlayer *viewer, const QJsonObject &spec);
// Call only with the request-issued refs, then retain all normal V2 admission.
// This permits generated materials only; selectedCardIds must remain empty.
bool authorizeAIRound24ChumingPayment(const AIRequest &request, const ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QJsonObject &spec, QList<int> *publicMaterials = nullptr);
#endif
