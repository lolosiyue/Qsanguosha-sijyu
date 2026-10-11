#ifndef AI_STRATEGY_ROUND44_RANGE_H
#define AI_STRATEGY_ROUND44_RANGE_H
#include <QJsonObject>
#include <QString>
struct AIRequest;class Room;class ServerPlayer;
bool aiRound44RangeConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
int aiRound44RangeExtra(const QString& source);
QJsonObject writeAIRound44RangeQinggang(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);
#endif
