#ifndef AI_STRATEGY_ROUND49_NAMES_H
#define AI_STRATEGY_ROUND49_NAMES_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
void projectAIRound49OwnerNames(AIRequest&,Room&,ServerPlayer*);
bool aiRound49JilieConstruction(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);
#endif
