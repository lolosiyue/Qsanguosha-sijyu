#ifndef AI_STRATEGY_ROUND49_BOZHI_H
#define AI_STRATEGY_ROUND49_BOZHI_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
void projectAIRound49Bozhi(AIRequest&,Room&,ServerPlayer*);
bool aiRound49BozhiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif
