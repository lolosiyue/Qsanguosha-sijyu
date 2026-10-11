#ifndef AI_STRATEGY_PREVIEW_AUTHORITY_H
#define AI_STRATEGY_PREVIEW_AUTHORITY_H
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
struct AIRequest;
struct SkillInstanceRef;
class Room;
class ServerPlayer;
// These checks grant only a material/disclosed-face domain. They NEVER issue a
// ticket or replace native canSelect/create/target/limitation/payment checks.
QString aiStrategyPreviewSkill(const AIRequest &request);
bool authorizeAIStrategyPreviewEvaluation(const AIRequest &, Room &, ServerPlayer *,
    int cardId, QJsonObject *disclosedFace = nullptr, QString *error = nullptr);
bool authorizeAIStrategyPreviewPayment(const AIRequest &, Room &, ServerPlayer *,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &orderedCosts, QJsonArray *disclosedFaces = nullptr, QString *error = nullptr);
// Caoduo donor submits the ORIGINAL borrowed physical card; its human V1
// viewAs clone is a different path, not an interchangeable authority product.
bool authorizeAIStrategyPreviewPhysicalProduct(const AIRequest &, Room &, ServerPlayer *,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    int physicalId, QJsonObject *disclosedFace = nullptr, QString *error = nullptr);
#endif
