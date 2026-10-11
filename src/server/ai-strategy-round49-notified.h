#ifndef AI_STRATEGY_ROUND49_NOTIFIED_H
#define AI_STRATEGY_ROUND49_NOTIFIED_H
#include <QJsonObject>
#include <QString>
struct AIRequest;struct SkillInstanceRef;class Room;class ServerPlayer;
QString aiRound49NotifiedSkill(const AIRequest&);
void projectAIRound49Notified(AIRequest&,Room&,ServerPlayer*);
bool authorizeAIRound49NotifiedEvaluation(const AIRequest&,Room&,ServerPlayer*,int,QJsonObject* = nullptr);
bool authorizeAIRound49NotifiedPhysical(const AIRequest&,Room&,ServerPlayer*,const SkillInstanceRef&,const SkillInstanceRef&,int);
#endif
