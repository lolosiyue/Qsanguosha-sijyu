#ifndef AI_STRATEGY_ROUND50_YURI_EVENTS_H
#define AI_STRATEGY_ROUND50_YURI_EVENTS_H
#include <QJsonObject>
#include <functional>
class Room; class ServerPlayer; class Card; class TriggerSkillV2; class QVariant;
struct lua_State;
bool registerAIRound50YuriSource(Room&,lua_State*);
struct SkillContext; struct AIRequest; struct AIWorldView; struct AIEventView;
// Every selected native V2 entry shadows the outer entry, including unrelated skills.
// This lexical capability holds no cross-event saved history and grants no payment.
class AIRound50YuriScope {
public:
 AIRound50YuriScope(Room &,const TriggerSkillV2 *,SkillContext *,int originalEvent);
 ~AIRound50YuriScope();
 AIRound50YuriScope(const AIRound50YuriScope&)=delete;
 AIRound50YuriScope&operator=(const AIRound50YuriScope&)=delete;
 struct State;
 const State *state()const{return state_;}
private:
 State *state_=nullptr;AIRound50YuriScope *previous_=nullptr;
};
void projectAIRound50YuriQuestion(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)>&);
void projectAIRound50YuriEvent(AIWorldView &,Room &,ServerPlayer *viewer,ServerPlayer *actor,const AIEventView &,const QVariant &,const std::function<QJsonObject(const Card *)>&);
#endif
