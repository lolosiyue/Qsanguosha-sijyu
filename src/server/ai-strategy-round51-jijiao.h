#ifndef AI_STRATEGY_ROUND51_JIJIAO_H
#define AI_STRATEGY_ROUND51_JIJIAO_H
#include <QString>
struct AIRequest;
class Room;
class ServerPlayer;
// Current owner Play inquiry only. No retained history, payment, or hidden faces.
void projectAIRound51Jijiao(AIRequest &, Room &, ServerPlayer *);
// Zero-cost, no-declaration fixed V1 prototype. Final native validation remains required.
int aiRound51JijiaoPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
#endif
