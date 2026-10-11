#ifndef AI_STRATEGY_USE_DISPATCH_H
#define AI_STRATEGY_USE_DISPATCH_H
#include <QStringList>
struct AIRequest;
class Room;
class ServerPlayer;
void projectAIUseCardDispatch(AIRequest &, Room &, ServerPlayer *);
void projectAIAocaiInvokeDispatch(AIRequest &, Room &, const QStringList &);
void projectAILianliSlashInvokeDispatch(AIRequest &, Room &, const QStringList &);
#endif
