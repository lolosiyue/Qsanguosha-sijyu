#ifndef AI_STRATEGY_ROUND41_RESPONSE_H
#define AI_STRATEGY_ROUND41_RESPONSE_H
#include "ai-strategy-readonly-card-store.h"
#include "structs.h"
#include <QStringList>
#include <QJsonObject>
#include <memory>
#include <functional>
class Room; class ServerPlayer; struct AIRequest; struct AICardSpec;
// The capture selector is authenticated by the coordinator's active readonly
// store (or exact disclosed native identity). No wire, face or ticket is trusted.
QString captureAIRound41FaqiPlan(const AIRequest &,ServerPlayer *,int offeredId,
    AIReadonlyCardLease, const QStringList &targets,QString *error=nullptr,
    const QJsonObject &sourceFields=QJsonObject());
enum class AIRound41FaqiOutcome { Unsupported, NativeRejected, Ready, SourcePass, SourceError };
const Card *restoreAIRound41FaqiPlan(const AIRequest &,ServerPlayer *,const QString &token,
    QStringList *targets,QString *error=nullptr,AIRound41FaqiOutcome*sourceStatus=nullptr);
void clearAIRound41FaqiPlans(Room &);
void interruptAIRound41FaqiPlansAsync(Room &) noexcept;
// No issuer/payment lives here. This reconstructs ONLY the exact saved source
// string in the current native question and calls a guarded native admission
// supplied by the coordinator. A failed native rule is distinct from missing
// observation/provenance. Never dispatches a legacy AI callback.
struct AIRound41FaqiSelection {
    AIRound41FaqiOutcome outcome=AIRound41FaqiOutcome::Unsupported;
    QString error,sourceWire;
    QStringList targets;
    CardUseStruct use;
};
AIRound41FaqiSelection prepareAIRound41FaqiSelection(const AIRequest &,ServerPlayer *,
    const QString &,const std::function<bool(CardUseStruct &)> &guardedNativeAdmission);
// Finite readonly source constructors; nil is an actual missing prototype.
std::unique_ptr<Card> constructAIRound41FaqiRevise(const AIRequest &,ServerPlayer *,
 const QString &kind,const QList<int> &,QString *error);
// Selected receipts reserve a normal ledger ID but are validated exclusively by
// this finite helper. They do not impersonate a conversion skill instance.
QJsonObject issueAIRound41FaqiReceipt(const AIRequest &,ServerPlayer *,const QString &token,
 const std::function<bool(CardUseStruct &)> &admit);
bool hasAIRound41FaqiReceipt(const AIRequest &,int id);
bool applyAIRound41FaqiReceipt(const AIRequest &,ServerPlayer *,const AICardSpec &,
 const QStringList &targets,const std::function<bool(CardUseStruct &)> &admit,CardUseStruct &);
#endif
