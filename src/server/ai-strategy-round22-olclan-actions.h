#ifndef AI_STRATEGY_ROUND22_OLCLAN_ACTIONS_H
#define AI_STRATEGY_ROUND22_OLCLAN_ACTIONS_H
#include <QJsonObject>
#include <QString>
struct AIRequest;
class ServerPlayer;
class Card;
// Finite source hypotheses only; never payment or an activation-scope issuer.
bool aiRound22OlClanActionConstruction(const AIRequest &, const ServerPlayer *,
                                const QJsonObject &, QString *error = nullptr);
// Call only on the newly constructed detached hypothesis, before rule queries
// and readonly recipe issuance. This cannot mutate a physical room card.
bool applyAIRound22OlClanActionConstruction(const AIRequest &, const ServerPlayer *,
                                     const QJsonObject &, Card &);
// -1 absent; bit0 source material permission. Existing live skill scope and
// native canSelect/feasible/create remain mandatory when selected.
int aiRound22OlClanActionPrototype(const QString &skill, const QString &prototype);
#endif
