#ifndef AI_STRATEGY_ROUND22_OLCLAN_PLAN_H
#define AI_STRATEGY_ROUND22_OLCLAN_PLAN_H
#include <QJsonObject>
#include <QString>
#include "ai-strategy-readonly-card-store.h"
struct AIRequest; class ServerPlayer; class Room;
// Readonly only. Complete source tuple/current question/material disclosure
// validation; this never authorizes selection, a receipt, or a final payment.
bool aiRound22OlClanConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &, QString *error=nullptr);
// Call only after the full construction predicate returned true. These two
// exceptions belong solely to the fixed source mutation/explicit shown-card.
bool aiRound22OlClanRepeatedMaterials(const QJsonObject &);
bool aiRound22OlClanForeignMaterial(const QJsonObject &);
int aiRound22OlClanExtraTarget(const QJsonObject &);
int aiRound22OlClanPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
void projectAIRound22OlClanPlanContext(AIRequest &, Room &, const ServerPlayer *);
// Native-only cross-question observation. Custody must come from the active
// runtime readonly store, never from a Lua-provided pointer or reconstructed wire.
QString captureAIRound23OlClanObservation(const AIRequest &, ServerPlayer *,
    const QString &slot, AIReadonlyCardLease, QString *error=nullptr);
const Card *restoreAIRound23OlClanObservation(const AIRequest &, ServerPlayer *,
    const QString &slot, const QString &token, QString *error=nullptr);
void clearAIRound23OlClanObservations(Room &);
#endif
