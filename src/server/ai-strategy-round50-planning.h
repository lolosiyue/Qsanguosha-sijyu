#ifndef AI_STRATEGY_ROUND50_PLANNING_H
#define AI_STRATEGY_ROUND50_PLANNING_H
#include <QJsonObject>
#include <memory>
struct lua_State;
class Room;class ServerPlayer;class Card;class LuaTriggerSkillV2;struct SkillContext;struct AIRequest;
bool aiRound50PlanningEvaluation(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiRound50PlanningRecipe(const AIRequest&,Room&,const ServerPlayer*,const QString&,const Card*);
int aiRound50PlanningPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);
bool registerAIRound50PlanningSources(Room&,lua_State*,QString*reason=nullptr);
void projectAIRound50Planning(AIRequest&,Room&,const ServerPlayer*);
class AIRound50YuxinScope final{
public:AIRound50YuxinScope(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool);~AIRound50YuxinScope();
 AIRound50YuxinScope(const AIRound50YuxinScope&)=delete;AIRound50YuxinScope&operator=(const AIRound50YuxinScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
#endif
