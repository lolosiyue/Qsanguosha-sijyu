#ifndef AI_STRATEGY_ROUND28_RESPONSE_H
#define AI_STRATEGY_ROUND28_RESPONSE_H
#include <QJsonObject>
struct AIRequest;
class ServerPlayer;
// Fixed ol_qifu Cihuai/Chixin source hypotheses; never issues payment authority.
bool aiRound28ResponseConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
bool aiRound28XdResponseConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
#endif
