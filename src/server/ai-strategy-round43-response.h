#ifndef AI_STRATEGY_ROUND43_RESPONSE_H
#define AI_STRATEGY_ROUND43_RESPONSE_H
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>
class Room;class ServerPlayer;class Card;class QVariant;class LuaTriggerSkillV2;struct SkillContext;struct AIRequest;
// Stack-only exact producer context. Every nested V2 callback shadows its parent.
class AIRound43GoddessScope final {
public:
 AIRound43GoddessScope(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool cost);
 ~AIRound43GoddessScope();
 AIRound43GoddessScope(const AIRound43GoddessScope&)=delete;
 AIRound43GoddessScope&operator=(const AIRound43GoddessScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
void projectAIRound43GoddessContext(AIRequest&,Room&,ServerPlayer*,const QVariant&,QJsonObject&,
 const std::function<QJsonObject(const Card*)>&);
#endif
