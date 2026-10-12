#ifndef AI_STRATEGY_OVERSEAS_H
#define AI_STRATEGY_OVERSEAS_H

#include <QJsonObject>
#include <QVariant>
#include <memory>
#include <functional>
#include <QList>

struct AIRequest;
struct AIWorldView;
class Card;
struct CardUseStruct;
class Room;
class ServerPlayer;
struct SkillInstanceRef;

bool aiNiwoShelieChoicePlanningQuestion(const AIRequest &, Room &, const ServerPlayer *);
QJsonObject aiNiwoShelieChoicePlanningContext(const AIRequest &, Room &, const ServerPlayer *);
bool aiNiwoResponseQuestion(const AIRequest &, Room &, const ServerPlayer *);
// Pure constructor of the exact reached owner ChangeSlash hypothesis. No payment.
std::unique_ptr<Card> createAILihuoHypothesis(const AIRequest &, Room &, ServerPlayer *, const QVariant &);
void projectAILihuoQuestion(AIRequest &, Room &, ServerPlayer *, const QVariant &);
std::unique_ptr<Card> createAICapturedLihuoHypothesis(const AIRequest &, Room &, ServerPlayer *, CardUseStruct *, QString *error = nullptr);
void clearAILihuoChoiceState(Room &);

bool aiXiafengChoicePlanningQuestion(const AIRequest &,Room &,const ServerPlayer *);
QJsonObject aiXiafengChoicePlanningContext(const AIRequest &,Room &,const ServerPlayer *);
int aiOverseasChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiOverseasChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAIOverseasChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);

// Seven named reached source mutations only. No retained value, read permit,
// observer exemption, copied callback, action receipt or generic registry setter.
QJsonObject writeAIOverseasPrefix(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

// Immutable load-time metadata only; never executes the original callback.
void captureAIGuoChoiceRegistry(Room &room);
void projectAIGuoChoiceRegistry(AIWorldView &world, Room &room);
void projectAIGuoChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer);
// Readonly finite constructors. No selected cost, quota or action receipt.
bool aiGuoChoiceConstruction(const AIRequest &request, const ServerPlayer *viewer, const QJsonObject &spec);
bool aiChumingPublicMaterials(const AIRequest &request, const ServerPlayer *viewer, const QJsonObject &spec);
// Call only with the request-issued refs, then retain all normal V2 admission.
// This permits generated materials only; selectedCardIds must remain empty.
bool authorizeAIChumingPayment(const AIRequest &request, const ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QJsonObject &spec, QList<int> *publicMaterials = nullptr);

#endif
