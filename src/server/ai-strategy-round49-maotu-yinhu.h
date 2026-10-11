#ifndef AI_STRATEGY_ROUND49_MAOTU_YINHU_H
#define AI_STRATEGY_ROUND49_MAOTU_YINHU_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
void projectAIRound49MaotuYinhu(AIRequest&,Room&,ServerPlayer*);
bool aiRound49MaotuYinhuConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiRound49MaotuYinhuPublicMaterial(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif
