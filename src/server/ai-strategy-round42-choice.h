#ifndef AI_STRATEGY_ROUND42_CHOICE_H
#define AI_STRATEGY_ROUND42_CHOICE_H
#include <QString>
class ServerPlayer;
struct AIRequest;
// Fixed ol_qifu1111-1124. This is a policy-class admission only; the existing
// V2 issuer and final reconstruction remain the sole payment authority.
int aiRound42ChoicePrototype(const AIRequest &,const ServerPlayer *,
                             const QString &skill,const QString &prototype);
#endif
