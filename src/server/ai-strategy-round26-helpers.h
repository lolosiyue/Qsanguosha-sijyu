#ifndef AI_STRATEGY_ROUND26_HELPERS_H
#define AI_STRATEGY_ROUND26_HELPERS_H
#include <QJsonObject>
#include "ai-strategy-readonly-card-store.h"
#include <QString>
#include <QVariant>
#include <functional>
class Card;
class Player;
class Room;
class ServerPlayer;
struct AIRequest;
struct CardUseStruct;
bool recordAIRound28LihuoDelegateUse(const AIRequest &,ServerPlayer *,const CardUseStruct &,const AIReadonlyCardLease &,QString *error=nullptr);
using AIRound26UseProfile = std::function<QJsonObject(const Card *)>;
void captureAIRound26DelegateMetadata(Room &);
int aiRound26DelegatePrototype(const AIRequest &, const ServerPlayer *, const QString &skill, const QString &prototype);
// Exact current-question observation; no payment or native tag mutation.
void projectAIRound26Delegates(AIRequest &, Room &, ServerPlayer *, const AIRound26UseProfile &, const QVariant &incoming = QVariant());
bool recordAIRound26DelegateUse(const AIRequest &, ServerPlayer *, const QString &sourceKey, QString *error = nullptr);
// present=false means no AI write; native reader remains authoritative. A
// present-but-unknown shadow must NOT fall back to an older native tag value.
QJsonObject restoreAIRound26DelegateUse(const AIRequest &, ServerPlayer *, const QString &destination, const AIRound26UseProfile &, const AIRound26UseProfile &constructedLihuoProfile = AIRound26UseProfile());
void noteAIRound26DelegateTagWrite(const Player *, const QString &key);
void noteAIRound26DelegateTagsClear(const Player *);
void clearAIRound26DelegateObservations(Room &);
#endif
