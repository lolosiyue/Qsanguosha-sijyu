#ifndef AI_STRATEGY_ROUND21_READONLY_H
#define AI_STRATEGY_ROUND21_READONLY_H
#include <QString>
#include <QJsonArray>
struct AIRequest;
// Finite fixed-source prototypes only; these functions grant no action scope.
int aiRound21ReadonlyPrototype(const QString &skill, const QString &prototype);
bool aiRound21FenyueEvaluation(const AIRequest &, const QString &skill,
                              const QString &name, const QJsonArray &costs);
#endif
