#ifndef AI_STRATEGY_ROUND49_RECORD_CONSTRUCTORS_H
#define AI_STRATEGY_ROUND49_RECORD_CONSTRUCTORS_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
bool aiRound49RecordConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif
