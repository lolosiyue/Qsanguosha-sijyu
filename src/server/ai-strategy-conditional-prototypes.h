#ifndef AI_STRATEGY_CONDITIONAL_PROTOTYPES_H
#define AI_STRATEGY_CONDITIONAL_PROTOTYPES_H

#include <QList>
#include <QString>

struct AIRequest;
class ServerPlayer;

// Fixed-source constructor catalog only. This does not issue a scope or authorize
// payment: the caller must still rebuild the exact current skill instance and run
// can_activate/can_select/feasible/create_card and final product validation.
// Same flags as aiAuditedSourcePrototype: -1 absent, bit 0 source supplies costs,
// bit 1 source supplies userString. Reads only this request's live owner state.
int aiAuditedConditionalSourcePrototype(const AIRequest &request, const QString &skill,
                                       const QString &prototype, const ServerPlayer *viewer);

// Exact ordinary-card constructor branch corresponding to the same providers.
// Raw printed skill, suit, number and ordered costs are part of the predicate;
// accepting a product name alone would merge different native branches.
bool aiAuditedConditionalOrdinaryProduct(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &orderedCosts,
                                        const ServerPlayer *viewer);

// Readonly-only superset: Aoyi's unskilled red Slash hypothesis is not the
// eventual paid native product. Never use this predicate for receipt issuance.
bool aiAuditedConditionalOrdinaryPreview(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &orderedCosts,
                                        const ServerPlayer *viewer);

#endif
