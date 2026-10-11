#ifndef AI_STRATEGY_ROUND53_PEIQI_H
#define AI_STRATEGY_ROUND53_PEIQI_H
#include <QJsonObject>
#include <QString>
class Room;
class ServerPlayer;
struct AIRequest;
struct AIEventView;
// event must be the runtime's native-only currently delivered event, never Lua data.
QJsonObject queryAIRound53PeiqiRecord(Room&, const AIRequest&, ServerPlayer*,
 const AIEventView* event,const QString& op,const QString& field,const QJsonObject&);
#endif
