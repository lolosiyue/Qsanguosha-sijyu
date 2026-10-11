#pragma once
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct SkillContext;struct AIRequest;struct lua_State;
// Exact compiled OLShebian cost/effect calls only; no policy constructor.
class AIRound54ShebianScope {
public:
 AIRound54ShebianScope(Room&,const SkillContext&,bool effect);
 ~AIRound54ShebianScope();
 AIRound54ShebianScope(const AIRound54ShebianScope&)=delete;
 AIRound54ShebianScope&operator=(const AIRound54ShebianScope&)=delete;
 struct Data;Data*d;
};
void invalidateAIRound54Shebian(Room&);
void enterAIRound54ShebianInquiry(Room&,ServerPlayer*,const QString&,const QString&);
void clearAIRound54Shebian(Room&);
bool initializeAIRound54Shebian(Room&,lua_State*);
QJsonObject queryAIRound54Shebian(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

QJsonObject refreshAIRound54OrdinaryCardChoice(Room&,ServerPlayer*,const AIRequest&);
