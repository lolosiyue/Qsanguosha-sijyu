#pragma once
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;class QVariant;struct AIRequest;struct SkillContext;struct lua_State;
// Native compiled CardAsked cost/effect only. No policy-facing scope constructor.
class AIRound54JijiangScope {
public:
 AIRound54JijiangScope(Room&,const SkillContext&,int event,bool effect);
 ~AIRound54JijiangScope();
 AIRound54JijiangScope(const AIRound54JijiangScope&)=delete;
 AIRound54JijiangScope&operator=(const AIRound54JijiangScope&)=delete;
 struct Data;Data*d;
};
class AIRound54JijiangLiege {
public:AIRound54JijiangLiege(Room&,ServerPlayer*);~AIRound54JijiangLiege();
 struct Data;Data*d;
};
// Frame over the real event record/filter pair, after preselection invalidation.
class AIRound54JijiangEvent {
public:AIRound54JijiangEvent(Room&,int,ServerPlayer*,const QVariant&);~AIRound54JijiangEvent();
 struct Data;Data*d;
};
void enterAIRound54JijiangEvent(Room&,int,ServerPlayer*);
void enterAIRound54JijiangFilter(Room&,int,ServerPlayer*,const QVariant&);
void clearAIRound54Jijiang(Room&);
bool initializeAIRound54Jijiang(Room&,lua_State*);
QJsonObject queryAIRound54Jijiang(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&);
