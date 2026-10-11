#ifndef AI_STRATEGY_NEWSTAR_H
#define AI_STRATEGY_NEWSTAR_H
#include <QJsonObject>
struct AIRequest;
class Room;
class ServerPlayer;
// Only the named active inquiries receive these public participant names.
void projectAINewstarContext(AIRequest &, Room &, const ServerPlayer *);
// Readonly owner preview: no subcards, skill name, candidate or action receipt.
QJsonObject evaluateAINengchenPreview(Room &, ServerPlayer *, const AIRequest &, int materialId);
#endif
