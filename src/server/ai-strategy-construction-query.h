#ifndef AI_STRATEGY_CONSTRUCTION_QUERY_H
#define AI_STRATEGY_CONSTRUCTION_QUERY_H

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
struct AIRequest;
struct AIWorldView;
class ServerPlayer;

// Fixed-source readonly constructors, not native conversion products. The
// caller owns cloning/evaluation and must never use this match to issue a
// payment ticket. Exact request, raw printing and constructor fields matter.
bool aiAuditedConstructionQuery(const AIRequest &request, const ServerPlayer *viewer,
                               const QJsonObject &spec, QString *error = nullptr);
// Existing skill-action resolver still performs canSelect/createCard validation.
// -1 means not admitted; bit 0 means the source supplies material costs.
int aiAuditedConstructionSourcePrototype(const AIRequest &request, const ServerPlayer *viewer,
                                        const QString &skill, const QString &prototype);
// MR_zhizhe selects one owner hand material but emits a zero-subcard product.
// The resolver must still run the live V2 selection/factory and persist these
// selection IDs separately from product costs in its native-only receipt.
bool aiAuditedNonconsumingSelection(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QJsonObject &product, const QJsonArray &selectedMaterials,
    QString *error = nullptr);
// Public engine definition metadata; never queries an opponent's skill state.
void projectAIConstructionFacts(AIWorldView &world);

#endif
