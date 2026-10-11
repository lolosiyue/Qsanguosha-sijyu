#ifndef AI_STRATEGY_ROUND27_RESPONSE_H
#define AI_STRATEGY_ROUND27_RESPONSE_H
#include <QJsonObject>
#include <QList>
#include <QString>
struct AIRequest;
class ServerPlayer;
// Audited readonly source constructors; never grants payment or a receipt.
bool aiRound27ResponseConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
// Zhangwu's native non-recast branches select no cards and generate materials.
// The second function requires exact original order, not merely a set match.
bool aiRound27ZhangwuGeneratedBranch(const AIRequest &,const ServerPlayer *,const QString &skill);
bool aiRound27ZhangwuGeneratedPayment(const AIRequest &,const ServerPlayer *,const QString &skill,const QList<int> &costs);
#endif
